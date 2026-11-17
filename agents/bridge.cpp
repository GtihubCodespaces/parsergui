#include "bridge.h"

#include <algorithm>
#include <chrono>
#include <ctime>
#include <filesystem>
#include <fstream>
#include <random>
#include <sstream>

#include "exec.h"
#include "http.h"

namespace fs = std::filesystem;

namespace pgr {
namespace {

std::string now_string() {
    const std::time_t now = std::time(nullptr);
    std::tm tm{};
#ifdef _WIN32
    localtime_s(&tm, &now);
#else
    localtime_r(&now, &tm);
#endif
    char buf[32];
    std::strftime(buf, sizeof(buf), "%H:%M:%S", &tm);
    return buf;
}

std::string make_session_id() {
    std::random_device rd;
    char buf[32];
    std::snprintf(buf, sizeof(buf), "s_%08x%04x", rd(), rd() & 0xFFFF);
    return buf;
}

std::string truncate(const std::string& s, size_t n) {
    if (s.size() <= n) return s;
    return s.substr(0, n) + "\n... (" + std::to_string(s.size() - n) + " bytes truncated)";
}

int count_changed_lines(const std::string& before, const std::string& after) {
    auto split = [](const std::string& s) {
        std::vector<std::string> lines;
        std::istringstream in(s);
        std::string line;
        while (std::getline(in, line)) lines.push_back(line);
        return lines;
    };
    const auto a = split(before);
    const auto b = split(after);
    std::map<std::string, int> counts;
    for (const auto& l : a) ++counts[l];
    int changed = 0;
    for (const auto& l : b) {
        auto it = counts.find(l);
        if (it != counts.end() && it->second > 0)
            --it->second;
        else
            ++changed;
    }
    for (const auto& kv : counts) changed += std::max(0, kv.second);
    return changed;
}

std::string json_str(const Json& j, const char* key, const std::string& def = "") {
    const Json* v = j.find(key);
    return v ? v->as_string(def) : def;
}

}  // namespace

int estimate_agent_seconds(int element_count, int budget) {
    // Rough, network-bound estimate. Shown to the user as an estimate, never as a
    // promise: 6s of context building, 0.05s per measured element, 0.9s per edit.
    const double seconds = 6.0 + element_count * 0.05 + budget * 0.9;
    return static_cast<int>(std::clamp(seconds, 15.0, 3600.0));
}

AgentBridge::AgentBridge(std::string skills_dir, ProviderRegistry* registry)
    : skills_dir_(std::move(skills_dir)), registry_(registry) {}

Json AgentBridge::available() const {
    Json root = Json::object();
    Json clis = Json::array();
    struct Kind {
        const char* id;
        const char* binary;
        const char* label;
    };
    const Kind kinds[] = {{"claude", "claude", "Claude Code"},
                          {"codex", "codex", "Codex CLI"},
                          {"opencode", "opencode", "OpenCode"}};
    for (const auto& k : kinds) {
        Json entry = Json::object();
        std::string resolved;
        const bool found = which(k.binary, &resolved);
        entry["id"] = k.id;
        entry["label"] = k.label;
        entry["installed"] = found;
        entry["path"] = resolved;
        clis.push_back(entry);
    }
    root["cli"] = clis;
    root["provider_count"] = static_cast<int>(registry_ ? registry_->size() : 0);
    root["skills"] = skills();
    root["edit_budget_max"] = 500;
    return root;
}

Json AgentBridge::skills() const {
    Json out = Json::array();
    std::error_code ec;
    if (!fs::exists(skills_dir_, ec)) return out;
    for (const auto& entry : fs::directory_iterator(skills_dir_, ec)) {
        if (!entry.is_regular_file() || entry.path().extension() != ".md") continue;
        Json s = Json::object();
        s["name"] = entry.path().stem().string();
        s["bytes"] = static_cast<double>(entry.file_size(ec));
        std::ifstream in(entry.path(), std::ios::binary);
        std::ostringstream ss;
        ss << in.rdbuf();
        s["preview"] = truncate(ss.str(), 400);
        out.push_back(s);
    }
    return out;
}

std::string AgentBridge::load_skills() const {
    std::string combined;
    std::error_code ec;
    if (!fs::exists(skills_dir_, ec)) return combined;
    std::vector<fs::path> files;
    for (const auto& entry : fs::directory_iterator(skills_dir_, ec))
        if (entry.is_regular_file() && entry.path().extension() == ".md") files.push_back(entry.path());
    std::sort(files.begin(), files.end());
    for (const auto& path : files) {
        std::ifstream in(path, std::ios::binary);
        std::ostringstream ss;
        ss << in.rdbuf();
        combined += "\n\n<!-- skill: " + path.filename().string() + " -->\n" + ss.str();
        if (combined.size() > 60000) break;
    }
    return combined;
}

std::string AgentBridge::build_prompt(const Session& session, const AgentConfig& config) const {
    std::ostringstream o;
    o << "# parsergui reconstruction task\n\n";
    o << "Working directory: " << session.job_dir << "\n";
    o << "Screenshot: source.png  Measured report: report.json  Layout dump: layout.txt\n";
    o << "Generated code: parsed_ui.cpp (the only file you may edit)\n\n";
    o << "## Objective\n";
    o << "Turn the generated ImGui reconstruction into code that builds and matches the screenshot. "
         "Keep every measured value that is already correct. Fix what the parser could not: text content, "
         "icon choice, gradient stops, spacing that looks wrong, missing widgets.\n\n";
    o << "## Hard rules\n";
    o << "- Edit only parsed_ui.cpp. Do not create build systems, tests or documentation.\n";
    o << "- Keep the public shape: namespace ParsedUI, Draw(ImDrawList*, ImVec2, float), Build().\n";
    o << "- Every constant you keep must be traceable to report.json. If you change a number, say why in a comment.\n";
    o << "- Do not invent text that is not visible in the screenshot. Leave a TODO(text) marker instead.\n";
    o << "- Budget: " << config.edit_budget << " line edits. Stop before the budget is exhausted.\n";
    if (!config.instructions.empty()) o << "- Extra instruction from the user: " << config.instructions << "\n";
    o << "\n## Measured report\n```json\n" << truncate(read_file(session.job_dir + "/report.json"), 12000) << "\n```\n";
    o << "\n## Skills\n" << load_skills() << "\n";
    return o.str();
}

std::string AgentBridge::read_file(const std::string& path) const {
    std::ifstream in(path, std::ios::binary);
    if (!in) return {};
    std::ostringstream ss;
    ss << in.rdbuf();
    return ss.str();
}

bool AgentBridge::write_job_file(const std::string& job_dir, const std::string& relative, const std::string& content,
                                 std::string* error) {
    std::error_code ec;
    const fs::path root = fs::weakly_canonical(job_dir, ec);
    const fs::path target = fs::weakly_canonical(fs::path(job_dir) / relative, ec);
    if (ec) {
        if (error) *error = "invalid path";
        return false;
    }
    const std::string root_str = root.string();
    const std::string target_str = target.string();
    if (target_str.rfind(root_str, 0) != 0) {
        if (error) *error = "path escapes the job directory";
        return false;
    }
    std::ofstream out(target, std::ios::binary);
    if (!out) {
        if (error) *error = "cannot write " + relative;
        return false;
    }
    out << content;
    return true;
}

void AgentBridge::append(Session& session, const std::string& line) {
    session.log.push_back("[" + now_string() + "] " + line);
    if (session.log.size() > 400) session.log.erase(session.log.begin(), session.log.begin() + 100);
}

std::string AgentBridge::start(const std::string& job_id, const std::string& job_dir, const AgentConfig& config,
                               std::string* error) {
    const bool cli_agent = config.agent == "claude" || config.agent == "codex" || config.agent == "opencode";
    const bool dry = config.agent == "plan" || config.dry_run;
    if (!cli_agent && !dry && config.agent != "provider") {
        if (error) *error = "unknown agent: " + config.agent;
        return {};
    }
    if (!dry && cli_agent) {
        std::string resolved;
        if (!which(config.agent, &resolved)) {
            if (error) *error = config.agent + " is not installed or not on PATH";
            return {};
        }
    }
    if (!dry && config.agent == "provider") {
        if (!registry_ || !registry_->find(config.provider_id)) {
            if (error) *error = "unknown provider: " + config.provider_id;
            return {};
        }
    }
    if (!fs::exists(job_dir + "/parsed_ui.cpp")) {
        if (error) *error = "job has no generated code yet";
        return {};
    }

    Session session;
    session.id = make_session_id();
    session.job_id = job_id;
    session.job_dir = job_dir;
    session.kind = dry ? "plan" : (cli_agent ? config.agent : "provider");
    session.started = now_string();
    session.budget = config.edit_budget;
    session.eta_seconds = estimate_agent_seconds(40, config.edit_budget);
    {
        std::lock_guard<std::mutex> lock(mutex_);
        sessions_[session.id] = session;
    }

    AgentConfig cfg = config;
    const std::string id = session.id;
    threads_.emplace_back([this, id, cfg]() {
        Session copy;
        {
            std::lock_guard<std::mutex> lock(mutex_);
            copy = sessions_[id];
        }
        run(copy, cfg);
        std::lock_guard<std::mutex> lock(mutex_);
        sessions_[id] = copy;  // final state, including status and log
    });
    return id;
}

void AgentBridge::run(Session& session, AgentConfig config) {
    const std::string prompt = build_prompt(session, config);
    const std::string before = read_file(session.job_dir + "/parsed_ui.cpp");
    {
        std::ofstream out(session.job_dir + "/agent_prompt.md", std::ios::binary);
        if (out) out << prompt;
    }
    append(session, "session " + session.id + " (" + session.kind + "), budget " + std::to_string(config.edit_budget));
    append(session, "prompt written to agent_prompt.md (" + std::to_string(prompt.size()) + " bytes)");

    if (session.kind == "plan") {
        append(session, "plan mode: no provider called, no file changed");
        session.status = "planned";
        session.finished = now_string();
        session.result["plan"] = "see agent_prompt.md";
        std::lock_guard<std::mutex> lock(mutex_);
        sessions_[session.id] = session;
        return;
    }

    if (session.kind == "provider") {
        run_provider(session, config, prompt);
    } else {
        run_cli(session, config, prompt);
    }

    const std::string after = read_file(session.job_dir + "/parsed_ui.cpp");
    session.changed_lines = count_changed_lines(before, after);
    // CLI agents edit files directly, so the changed-line count is the best
    // available edit signal. Provider sessions count tool calls instead.
    if (session.kind != "provider") session.edits = session.changed_lines;
    session.finished = now_string();
    session.result["changed_lines"] = session.changed_lines;
    session.result["before_bytes"] = static_cast<double>(before.size());
    session.result["after_bytes"] = static_cast<double>(after.size());
    if (session.status == "running") session.status = "done";
    // Persist a small sidecar so the work is inspectable later.
    Json side = Json::object();
    side["id"] = session.id;
    side["kind"] = session.kind;
    side["status"] = session.status;
    side["edits"] = session.edits;
    side["budget"] = session.budget;
    side["changed_lines"] = session.changed_lines;
    side["started"] = session.started;
    side["finished"] = session.finished;
    side["eta_seconds"] = session.eta_seconds;
    side["result"] = session.result;
    Json log = Json::array();
    for (const auto& line : session.log) log.push_back(line);
    side["log"] = log;
    std::ofstream out(session.job_dir + "/agent_session.json", std::ios::binary);
    if (out) out << side.dump(2);
    std::lock_guard<std::mutex> lock(mutex_);
    sessions_[session.id] = session;
}

void AgentBridge::run_cli(Session& session, const AgentConfig& config, const std::string& prompt) {
    std::vector<std::string> argv;
    if (session.kind == "claude")
        argv = {"claude", "-p", prompt};
    else if (session.kind == "codex")
        argv = {"codex", "exec", prompt};
    else
        argv = {"opencode", "run", prompt};
    append(session, "spawning " + argv.front() + " in " + session.job_dir);
    {
        std::lock_guard<std::mutex> lock(mutex_);
        sessions_[session.id] = session;
    }
    const ProcessResult proc = run_process(argv, session.job_dir, config.timeout_seconds * 1000);
    if (!proc.spawned) {
        session.status = "error";
        session.error = "cannot spawn " + argv.front();
        append(session, session.error);
        return;
    }
    if (!proc.out.empty()) {
        std::istringstream in(proc.out);
        std::string line;
        while (std::getline(in, line)) {
            if (!line.empty()) append(session, line);
        }
    }
    if (!proc.err.empty()) {
        std::istringstream in(proc.err);
        std::string line;
        while (std::getline(in, line)) {
            if (!line.empty()) append(session, "stderr: " + line);
        }
    }
    if (proc.timed_out) {
        session.status = "timeout";
        session.error = "agent exceeded " + std::to_string(config.timeout_seconds) + "s";
        append(session, session.error);
    } else if (proc.exit_code != 0) {
        session.status = "error";
        session.error = argv.front() + " exited with " + std::to_string(proc.exit_code);
        append(session, session.error);
    } else {
        session.status = "done";
    }
    session.result["exit_code"] = proc.exit_code;
    session.result["agent_ms"] = proc.elapsed_ms;
    append(session, "agent finished in " + std::to_string(static_cast<int>(proc.elapsed_ms / 1000)) + "s");
}

namespace {

Json tool_schema_openai() {
    Json tools = Json::array();
    {
        Json t = Json::object();
        t["type"] = "function";
        Json f = Json::object();
        f["name"] = "write_file";
        f["description"] = "Replace a file inside the job directory. Only parsed_ui.cpp is writable.";
        Json params = Json::object();
        params["type"] = "object";
        Json props = Json::object();
        Json path = Json::object();
        path["type"] = "string";
        props["path"] = path;
        Json content = Json::object();
        content["type"] = "string";
        props["content"] = content;
        params["properties"] = props;
        Json required = Json::array();
        required.push_back("path");
        required.push_back("content");
        params["required"] = required;
        f["parameters"] = params;
        t["function"] = f;
        tools.push_back(t);
    }
    {
        Json t = Json::object();
        t["type"] = "function";
        Json f = Json::object();
        f["name"] = "read_file";
        f["description"] = "Read a file inside the job directory.";
        Json params = Json::object();
        params["type"] = "object";
        Json props = Json::object();
        Json path = Json::object();
        path["type"] = "string";
        props["path"] = path;
        params["properties"] = props;
        Json required = Json::array();
        required.push_back("path");
        params["required"] = required;
        f["parameters"] = params;
        t["function"] = f;
        tools.push_back(t);
    }
    return tools;
}

Json tool_schema_anthropic() {
    Json tools = Json::array();
    auto add = [&](const char* name, const char* description, bool with_content) {
        Json t = Json::object();
        t["name"] = name;
        t["description"] = description;
        Json schema = Json::object();
        schema["type"] = "object";
        Json props = Json::object();
        Json path = Json::object();
        path["type"] = "string";
        props["path"] = path;
        if (with_content) {
            Json content = Json::object();
            content["type"] = "string";
            props["content"] = content;
        }
        schema["properties"] = props;
        Json required = Json::array();
        required.push_back("path");
        if (with_content) required.push_back("content");
        schema["required"] = required;
        t["input_schema"] = schema;
        tools.push_back(t);
    };
    add("write_file", "Replace a file inside the job directory. Only parsed_ui.cpp is writable.", true);
    add("read_file", "Read a file inside the job directory.", false);
    return tools;
}

}  // namespace

void AgentBridge::run_provider(Session& session, const AgentConfig& config, const std::string& prompt) {
    if (!registry_) {
        session.status = "error";
        session.error = "no provider registry";
        return;
    }
    Provider provider;
    if (!registry_->get(config.provider_id, &provider)) {
        session.status = "error";
        session.error = "unknown provider: " + config.provider_id;
        return;
    }
    const std::string key = registry_->key_for(provider.id);
    if (provider.requires_key && key.empty()) {
        session.status = "error";
        session.error = "missing key: set " + provider.env_key + " or provide it in the UI";
        append(session, session.error);
        return;
    }
    const std::string model = config.model.empty() ? (provider.models.empty() ? "gpt-4.1-mini" : provider.models.front())
                                                   : config.model;
    append(session, "provider " + provider.name + " model " + model);

    Json messages = Json::array();
    {
        Json sys = Json::object();
        sys["role"] = "system";
        sys["content"] = "You are a precise UI reconstruction engineer. You only use the provided tools. "
                         "Never invent measurements.";
        messages.push_back(sys);
        Json user = Json::object();
        user["role"] = "user";
        user["content"] = prompt;
        messages.push_back(user);
    }

    for (int step = 0; step < config.max_steps && session.edits < config.edit_budget; ++step) {
        Json payload = Json::object();
        payload["model"] = model;
        payload["messages"] = messages;
        payload["temperature"] = 0.2;
        HttpRequest req;
        req.method = "POST";
        std::string assistant_text;
        Json tool_calls = Json::array();
        bool anthropic = provider.kind == "anthropic";
        if (anthropic) {
            payload["system"] = prompt;
            payload["max_tokens"] = 8192;
            payload["tools"] = tool_schema_anthropic();
            // Anthropic expects the system prompt separately, not in messages.
            Json trimmed = Json::array();
            for (size_t i = 1; i < messages.size(); ++i) trimmed.push_back(messages.at(i));
            payload["messages"] = trimmed;
            req.url = provider.base_url + "/v1/messages";
            req.headers["x-api-key"] = key;
            req.headers["anthropic-version"] = "2023-06-01";
        } else {
            payload["tools"] = tool_schema_openai();
            req.url = provider.base_url + "/chat/completions";
            req.headers["Authorization"] = "Bearer " + key;
        }
        req.headers["Content-Type"] = "application/json";
        req.body = payload.dump(0);
        req.timeout_ms = 120000;
        const HttpResponse res = http_request(req);
        if (res.status < 200 || res.status >= 300) {
            session.status = "error";
            session.error = "provider HTTP " + std::to_string(res.status) +
                            (res.error.empty() ? "" : (": " + res.error));
            append(session, session.error);
            append(session, truncate(res.body, 800));
            return;
        }
        Json reply;
        std::string parse_error;
        if (!Json::parse(res.body, &reply, &parse_error)) {
            session.status = "error";
            session.error = "provider returned invalid JSON: " + parse_error;
            append(session, session.error);
            return;
        }
        if (anthropic) {
            Json assistant = Json::object();
            assistant["role"] = "assistant";
            assistant["content"] = reply.find("content") ? *reply.find("content") : Json::array();
            messages.push_back(assistant);
            if (const Json* content = reply.find("content")) {
                for (const auto& block : content->items()) {
                    const std::string type = json_str(block, "type");
                    if (type == "text") {
                        const std::string text = json_str(block, "text");
                        if (!text.empty()) {
                            append(session, "agent: " + truncate(text, 400));
                            assistant_text += text;
                        }
                    } else if (type == "tool_use") {
                        Json call = Json::object();
                        call["id"] = json_str(block, "id");
                        call["name"] = json_str(block, "name");
                        call["input"] = block.find("input") ? *block.find("input") : Json::object();
                        tool_calls.push_back(call);
                    }
                }
            }
        } else {
            const Json* choices = reply.find("choices");
            if (!choices || choices->size() == 0) {
                session.status = "error";
                session.error = "provider returned no choices";
                return;
            }
            const Json& message = *choices->at(0).find("message");
            if (const Json* content = message.find("content")) {
                assistant_text = content->as_string();
                if (!assistant_text.empty()) append(session, "agent: " + truncate(assistant_text, 400));
            }
            messages.push_back(message);
            if (const Json* calls = message.find("tool_calls")) {
                for (const auto& c : calls->items()) {
                    Json call = Json::object();
                    call["id"] = json_str(c, "id");
                    const Json* fn = c.find("function");
                    call["name"] = fn ? json_str(*fn, "name") : "";
                    Json input;
                    const Json* args = fn ? fn->find("arguments") : nullptr;
                    std::string parse_err;
                    if (args && Json::parse(args->as_string(), &input, &parse_err)) {
                        call["input"] = input;
                    } else {
                        call["input"] = Json::object();
                    }
                    tool_calls.push_back(call);
                }
            }
        }

        if (tool_calls.size() == 0) {
            append(session, "agent stopped calling tools");
            break;
        }
        Json tool_results = Json::array();
        for (const auto& call : tool_calls.items()) {
            const std::string name = json_str(call, "name");
            const Json* input = call.find("input");
            const std::string path = input ? input->find("path") ? input->find("path")->as_string() : "" : "";
            std::string output;
            if (name == "read_file") {
                output = truncate(read_file(session.job_dir + "/" + path), 24000);
                append(session, "read " + path + " (" + std::to_string(output.size()) + " bytes)");
            } else if (name == "write_file") {
                const std::string content = input && input->find("content") ? input->find("content")->as_string() : "";
                std::string write_error;
                if (write_job_file(session.job_dir, path, content, &write_error)) {
                    ++session.edits;
                    output = "written " + path + " (" + std::to_string(content.size()) + " bytes)";
                    append(session, output + " [" + std::to_string(session.edits) + "/" +
                                      std::to_string(config.edit_budget) + "]");
                } else {
                    output = "error: " + write_error;
                    append(session, output);
                }
            } else {
                output = "error: unknown tool " + name;
            }
            Json result = Json::object();
            if (anthropic) {
                result["type"] = "tool_result";
                result["tool_use_id"] = json_str(call, "id");
                result["content"] = output;
            } else {
                result["role"] = "tool";
                result["tool_call_id"] = json_str(call, "id");
                result["content"] = output;
            }
            tool_results.push_back(result);
        }
        if (anthropic) {
            Json user = Json::object();
            user["role"] = "user";
            user["content"] = tool_results;
            messages.push_back(user);
        } else {
            for (const auto& r : tool_results.items()) messages.push_back(r);
        }
        {
            std::lock_guard<std::mutex> lock(mutex_);
            sessions_[session.id] = session;
        }
    }
    if (session.edits >= config.edit_budget) {
        session.status = "budget_exhausted";
        append(session, "edit budget reached, stopping");
    } else {
        session.status = "done";
    }
}

Json AgentBridge::status(const std::string& session_id) const {
    std::lock_guard<std::mutex> lock(mutex_);
    auto it = sessions_.find(session_id);
    if (it == sessions_.end()) return Json();
    const Session& s = it->second;
    Json j = Json::object();
    j["id"] = s.id;
    j["job_id"] = s.job_id;
    j["kind"] = s.kind;
    j["status"] = s.status;
    j["error"] = s.error;
    j["started"] = s.started;
    j["finished"] = s.finished;
    j["edits"] = s.edits;
    j["budget"] = s.budget;
    j["changed_lines"] = s.changed_lines;
    j["eta_seconds"] = s.eta_seconds;
    j["result"] = s.result;
    Json log = Json::array();
    for (const auto& line : s.log) log.push_back(line);
    j["log"] = log;
    return j;
}

void AgentBridge::stop() {
    for (auto& t : threads_)
        if (t.joinable()) t.join();
    threads_.clear();
}

}  // namespace pgr
