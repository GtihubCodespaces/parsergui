#include "providers.h"

#include <algorithm>
#include <cstdlib>
#include <fstream>
#include <sstream>

#include "http.h"

namespace pgr {
namespace {

bool is_free_model(const std::string& name) {
    return name.find(":free") != std::string::npos || name.find("-free") != std::string::npos;
}

std::string query_host(const std::string& url) {
    const size_t start = url.find("://");
    const size_t begin = start == std::string::npos ? 0 : start + 3;
    const size_t end = url.find('/', begin);
    return url.substr(begin, end == std::string::npos ? std::string::npos : end - begin);
}

}  // namespace

std::string env_value(const std::string& name) {
    if (name.empty()) return {};
    const char* v = getenv(name.c_str());
    return v ? std::string(v) : std::string();
}

bool ProviderRegistry::load(const std::string& bundled_path, const std::string& extra_path, std::string* error) {
    {
        std::lock_guard<std::mutex> lock(mutex_);
        providers_.clear();
    }
    if (!merge_file(bundled_path, error)) return false;
    if (!extra_path.empty()) {
        std::string ignored;
        merge_file(extra_path, &ignored);
    }
    return true;
}

bool ProviderRegistry::merge_file(const std::string& path, std::string* error) {
    std::ifstream in(path, std::ios::binary);
    if (!in) {
        if (error) *error = "cannot open " + path;
        return false;
    }
    std::ostringstream ss;
    ss << in.rdbuf();
    Json root;
    std::string parse_error;
    if (!Json::parse(ss.str(), &root, &parse_error)) {
        if (error) *error = path + ": " + parse_error;
        return false;
    }
    return merge_json(root, error);
}

bool ProviderRegistry::merge_json(const Json& root, std::string* error) {
    const Json* list = root.is_array() ? &root : root.find("providers");
    if (!list || !list->is_array()) {
        if (error) *error = "providers: expected an array or {\"providers\": [...]}";
        return false;
    }
    std::lock_guard<std::mutex> lock(mutex_);
    std::string skipped;
    for (const auto& item : list->items()) {
        if (!item.is_object()) continue;
        Provider p;
        p.id = item.find("id") ? item.find("id")->as_string() : "";
        p.name = item.find("name") ? item.find("name")->as_string() : p.id;
        if (p.id.empty()) continue;
        if (item.find("kind")) p.kind = item.find("kind")->as_string();
        if (item.find("base_url")) p.base_url = item.find("base_url")->as_string();
        if (item.find("env_key")) p.env_key = item.find("env_key")->as_string();
        if (item.find("category")) p.category = item.find("category")->as_string();
        if (item.find("free_tier")) p.free_tier = item.find("free_tier")->as_bool();
        if (item.find("local")) p.local = item.find("local")->as_bool();
        if (item.find("requires_key")) p.requires_key = item.find("requires_key")->as_bool();
        if (item.find("priority")) p.priority = item.find("priority")->as_int(50);
        if (item.find("notes")) p.notes = item.find("notes")->as_string();
        if (item.find("docs_url")) p.docs_url = item.find("docs_url")->as_string();
        if (const Json* models = item.find("models")) {
            if (models->is_array())
                for (const auto& m : models->items()) p.models.push_back(m.as_string());
        }
        // A provider that only advertises :free routes is a free-tier provider.
        for (const auto& m : p.models)
            if (is_free_model(m)) p.free_tier = true;

        auto existing = std::find_if(providers_.begin(), providers_.end(),
                                     [&](const Provider& e) { return e.id == p.id; });
        if (existing != providers_.end()) {
            const std::string old_env = existing->env_key;
            const bool old_free = existing->free_tier;
            *existing = p;
            if (p.env_key.empty()) existing->env_key = old_env;
            existing->free_tier = p.free_tier || old_free;
        } else if (static_cast<int>(providers_.size()) < kCapacity) {
            providers_.push_back(p);
        } else {
            skipped += " " + p.id;
        }
    }
    refresh_keys();
    sort_in_place();
    if (!skipped.empty() && error) *error = "capacity reached, skipped:" + skipped;
    return true;
}

bool ProviderRegistry::set_key(const std::string& id, const std::string& key) {
    std::lock_guard<std::mutex> lock(mutex_);
    auto it = std::find_if(providers_.begin(), providers_.end(), [&](const Provider& p) { return p.id == id; });
    if (it == providers_.end() || key.empty()) return false;
    auto existing = std::find_if(session_keys_.begin(), session_keys_.end(),
                                 [&](const std::pair<std::string, std::string>& kv) { return kv.first == id; });
    if (existing != session_keys_.end())
        existing->second = key;
    else
        session_keys_.emplace_back(id, key);
    it->key_present = true;
    it->last_test = "key set for this session";
    return true;
}

void ProviderRegistry::refresh_keys() {
    for (auto& p : providers_) {
        if (p.local || !p.requires_key) {
            p.key_present = true;
            continue;
        }
        bool session = false;
        for (const auto& kv : session_keys_)
            if (kv.first == p.id && !kv.second.empty()) session = true;
        p.key_present = session || (!p.env_key.empty() && !env_value(p.env_key).empty());
    }
}

void ProviderRegistry::sort_in_place() {
    std::stable_sort(providers_.begin(), providers_.end(), [](const Provider& a, const Provider& b) {
        if (a.free_tier != b.free_tier) return a.free_tier;
        if (a.local != b.local) return a.local;
        if (a.key_present != b.key_present) return a.key_present;
        if (a.priority != b.priority) return a.priority < b.priority;
        return a.name < b.name;
    });
}

std::vector<Provider> ProviderRegistry::sorted(bool free_only, const std::string& query) const {
    std::lock_guard<std::mutex> lock(mutex_);
    std::vector<Provider> out;
    std::string q = query;
    std::transform(q.begin(), q.end(), q.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    for (const auto& p : providers_) {
        if (free_only && !p.free_tier) continue;
        if (!q.empty()) {
            std::string hay = p.id + " " + p.name + " " + p.category + " " + p.notes + " ";
            for (const auto& m : p.models) hay += m + " ";
            std::transform(hay.begin(), hay.end(), hay.begin(),
                           [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
            if (hay.find(q) == std::string::npos) continue;
        }
        Provider copy = p;
        for (const auto& kv : session_keys_)
            if (kv.first == p.id) copy.key_present = true;
        out.push_back(copy);
    }
    return out;
}

Json ProviderRegistry::to_json(bool free_only, const std::string& query, bool redact) const {
    Json root = Json::object();
    const auto list = sorted(free_only, query);
    root["count"] = static_cast<int>(list.size());
    root["capacity"] = kCapacity;
    Json array = Json::array();
    for (const auto& p : list) {
        Json j = Json::object();
        j["id"] = p.id;
        j["name"] = p.name;
        j["kind"] = p.kind;
        j["base_url"] = p.base_url;
        j["env_key"] = p.env_key;
        j["category"] = p.category;
        j["free_tier"] = p.free_tier;
        j["local"] = p.local;
        j["requires_key"] = p.requires_key;
        j["key_present"] = p.key_present;
        j["reachable"] = p.reachable;
        j["last_status"] = p.last_status;
        j["last_test"] = p.last_test;
        j["notes"] = p.notes;
        j["docs_url"] = p.docs_url;
        j["priority"] = p.priority;
        Json models = Json::array();
        for (const auto& m : p.models) models.push_back(m);
        j["models"] = models;
        array.push_back(j);
    }
    root["providers"] = array;
    if (redact) root["keys"] = "stored in the process, never written to disk";
    return root;
}

const Provider* ProviderRegistry::find(const std::string& id) const {
    std::lock_guard<std::mutex> lock(mutex_);
    for (const auto& p : providers_)
        if (p.id == id) return &p;
    return nullptr;
}

bool ProviderRegistry::get(const std::string& id, Provider* out) const {
    std::lock_guard<std::mutex> lock(mutex_);
    for (const auto& p : providers_)
        if (p.id == id) {
            *out = p;
            return true;
        }
    return false;
}

std::string ProviderRegistry::key_for(const std::string& id) const {
    std::lock_guard<std::mutex> lock(mutex_);
    for (const auto& kv : session_keys_)
        if (kv.first == id && !kv.second.empty()) return kv.second;
    for (const auto& p : providers_)
        if (p.id == id) return env_value(p.env_key);
    return {};
}

size_t ProviderRegistry::size() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return providers_.size();
}

Json ProviderRegistry::test(const std::string& id) {
    std::string base, key, kind;
    {
        std::lock_guard<std::mutex> lock(mutex_);
        const Provider* p = nullptr;
        for (const auto& candidate : providers_)
            if (candidate.id == id) p = &candidate;
        if (!p) {
            Json err = Json::object();
            err["ok"] = false;
            err["error"] = "unknown provider";
            return err;
        }
        base = p->base_url;
        kind = p->kind;
        key = p->requires_key ? env_value(p->env_key) : std::string();
        for (const auto& kv : session_keys_)
            if (kv.first == id) key = kv.second;
    }
    Json result = Json::object();
    result["id"] = id;
    if (base.empty()) {
        result["ok"] = false;
        result["error"] = "no base URL";
        return result;
    }
    if (kind == "cli") {
        result["ok"] = true;
        result["note"] = "CLI providers are probed by the agent bridge, not over HTTP";
        return result;
    }
    const std::string url = base + (base.find('?') == std::string::npos ? "/models" : "");
    HttpRequest req;
    req.method = "GET";
    req.url = url;
    if (!key.empty()) {
        req.headers["Authorization"] = "Bearer " + key;
        req.headers["x-api-key"] = key;
    }
    req.headers["Accept"] = "application/json";
    req.timeout_ms = 8000;
    const HttpResponse res = http_request(req);
    std::lock_guard<std::mutex> lock(mutex_);
    for (auto& p : providers_) {
        if (p.id != id) continue;
        p.reachable = res.status >= 200 && res.status < 500;
        p.last_status = res.status;
        p.last_latency_ms = res.elapsed_ms;
        p.last_test = res.error.empty() ? ("HTTP " + std::to_string(res.status)) : res.error;
    }
    result["ok"] = res.status >= 200 && res.status < 300;
    result["status"] = res.status;
    result["latency_ms"] = res.elapsed_ms;
    result["host"] = query_host(base);
    if (!res.error.empty()) result["error"] = res.error;
    if (res.body.size() < 4000) result["body"] = res.body;
    return result;
}

}  // namespace pgr
