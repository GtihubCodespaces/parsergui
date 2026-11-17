#include "api.h"

#include <chrono>
#include <filesystem>
#include <sstream>
#include <vector>

#include "emit.h"
#include "web_ui.h"

namespace pgr {
namespace {

double now_ms() {
    using clock = std::chrono::steady_clock;
    return std::chrono::duration<double, std::milli>(clock::now().time_since_epoch()).count();
}

HttpResponse json_response(Json body, int status = 200) {
    HttpResponse res;
    res.status = status;
    res.body = body.dump(2);
    res.headers["Content-Type"] = "application/json; charset=utf-8";
    return res;
}

HttpResponse error_response(int status, const std::string& message) {
    Json j = Json::object();
    j["error"] = message;
    return json_response(j, status);
}

std::vector<std::string> split_path(const std::string& path) {
    std::vector<std::string> out;
    std::string current;
    for (char c : path) {
        if (c == '/') {
            if (!current.empty()) out.push_back(current);
            current.clear();
        } else {
            current.push_back(c);
        }
    }
    if (!current.empty()) out.push_back(current);
    return out;
}

bool parse_body_json(const HttpRequest& request, Json* out, std::string* error) {
    if (request.body.empty()) {
        *out = Json::object();
        return true;
    }
    return Json::parse(request.body, out, error);
}

}  // namespace

Api::Api(JobStore& jobs, ProviderRegistry& providers, AgentBridge& agents, std::string version)
    : jobs_(jobs), providers_(providers), agents_(agents), version_(std::move(version)), started_ms_(now_ms()) {}

HttpResponse Api::handle(const HttpRequest& request) {
    const std::vector<std::string> segments = split_path(request.path);

    if (request.method == "OPTIONS") {
        HttpResponse res;
        res.status = 204;
        res.headers["Allow"] = "GET, POST, OPTIONS";
        return res;
    }

    if (segments.empty() || (segments.size() == 1 && segments[0] == "index.html")) {
        HttpResponse res;
        res.status = 200;
        res.body = kWebUiHtml;
        res.headers["Content-Type"] = "text/html; charset=utf-8";
        return res;
    }
    if (segments.size() == 1 && segments[0] == "favicon.ico") {
        HttpResponse res;
        res.status = 204;
        return res;
    }

    if (segments[0] != "api") {
        return error_response(404, "not found");
    }

    if (segments.size() == 2 && segments[1] == "health") {
        Json j = Json::object();
        j["ok"] = true;
        j["version"] = version_;
        j["uptime_seconds"] = (now_ms() - started_ms_) / 1000.0;
        j["jobs"] = static_cast<int>(jobs_.count());
        j["queued"] = jobs_.queued();
        j["providers"] = static_cast<int>(providers_.size());
        j["skills"] = static_cast<int>(agents_.skills().size());
        int cli = 0;
        const Json available = agents_.available();
        if (const Json* list = available.find("cli"))
            for (const auto& item : list->items())
                if (item.find("installed") && item.find("installed")->as_bool()) ++cli;
        j["cli"] = cli;
        return json_response(j);
    }

    // ---- jobs ----
    if (segments.size() >= 2 && segments[1] == "jobs") {
        if (segments.size() == 2) {
            if (request.method == "GET") return json_response(jobs_.list());
            if (request.method == "POST") return job_post(request);
            return error_response(405, "GET or POST only");
        }
        const std::string id = segments[2];
        if (segments.size() == 3 && request.method == "GET") {
            Json job = jobs_.get(id);
            if (job.is_null()) return error_response(404, "unknown job");
            // Merge live agent state when a session is attached.
            Json agent = jobs_.agent(id);
            if (agent.is_object() && agent.has("session_id")) {
                Json live = agents_.status(agent.find("session_id")->as_string());
                if (!live.is_null()) {
                    agent["status"] = live.find("status") ? live.find("status")->as_string() : "running";
                    agent["edits"] = live.find("edits") ? live.find("edits")->as_int() : 0;
                    agent["changed_lines"] = live.find("changed_lines") ? live.find("changed_lines")->as_int() : 0;
                    job["agent"] = agent;
                }
            }
            return json_response(job);
        }
        if (segments.size() == 5 && segments[3] == "artifact" && request.method == "GET") {
            std::string bytes, mime;
            if (!jobs_.artifact(id, segments[4], &bytes, &mime)) return error_response(404, "artifact not found");
            HttpResponse res;
            res.status = 200;
            res.body = std::move(bytes);
            res.headers["Content-Type"] = mime;
            return res;
        }
        return error_response(404, "unknown job route");
    }

    // ---- providers ----
    if (segments.size() >= 2 && segments[1] == "providers") {
        if (segments.size() == 2 && request.method == "GET") {
            const bool free_only = request.params.count("free") && request.params.at("free") != "0";
            const std::string query = request.params.count("q") ? request.params.at("q") : "";
            return json_response(providers_.to_json(free_only, query, true));
        }
        if (segments.size() == 4 && segments[3] == "test" && request.method == "POST") {
            return json_response(providers_.test(url_decode(segments[2])));
        }
        if (segments.size() == 4 && segments[3] == "key" && request.method == "POST") {
            Json body;
            std::string parse_error;
            if (!parse_body_json(request, &body, &parse_error)) return error_response(400, parse_error);
            const std::string key = body.find("key") ? body.find("key")->as_string() : "";
            if (!providers_.set_key(url_decode(segments[2]), key)) return error_response(400, "unknown provider or empty key");
            Json ok = Json::object();
            ok["ok"] = true;
            ok["note"] = "key kept in memory for this process only";
            return json_response(ok);
        }
        return error_response(404, "unknown provider route");
    }

    // ---- agents ----
    if (segments.size() >= 2 && segments[1] == "agents") {
        if (segments.size() == 2 && request.method == "GET") return json_response(agents_.available());
        if (segments.size() == 3 && segments[2] == "session" && request.method == "POST") return agent_post(request);
        if (segments.size() >= 4 && segments[2] == "session" && request.method == "GET") {
            Json status = agents_.status(segments[3]);
            if (status.is_null()) return error_response(404, "unknown session");
            return json_response(status);
        }
        return error_response(404, "unknown agent route");
    }

    return error_response(404, "unknown route");
}

HttpResponse Api::job_post(const HttpRequest& request) {
    Json options = Json::object();
    if (!request.parts.empty()) {
        for (const auto& part : request.parts) {
            if (part.name == "options" && !part.data.empty()) {
                std::string parse_error;
                Json parsed;
                if (Json::parse(part.data, &parsed, &parse_error)) options = parsed;
            }
        }
    } else if (!request.body.empty() &&
               request.headers.count("content-type") &&
               request.headers.at("content-type").find("application/json") != std::string::npos) {
        std::string parse_error;
        if (!Json::parse(request.body, &options, &parse_error)) return error_response(400, parse_error);
    }
    std::string error;
    const std::string id = jobs_.submit(request.parts, options, &error);
    if (id.empty()) return error_response(400, error);
    Json j = Json::object();
    j["id"] = id;
    j["eta_seconds"] = 2 + static_cast<int>(request.parts.size() * 2);
    j["status"] = "queued";
    return json_response(j, 201);
}

HttpResponse Api::agent_post(const HttpRequest& request) {
    Json body;
    std::string parse_error;
    if (!parse_body_json(request, &body, &parse_error)) return error_response(400, parse_error);
    const std::string job_id = body.find("job_id") ? body.find("job_id")->as_string() : "";
    if (job_id.empty()) return error_response(400, "job_id required");
    if (!jobs_.exists(job_id)) return error_response(404, "unknown job");
    Json job = jobs_.get(job_id);
    if (job.find("status")->as_string() != "done") return error_response(409, "job is not finished");

    AgentConfig config;
    config.agent = body.find("agent") ? body.find("agent")->as_string("claude") : "claude";
    config.provider_id = body.find("provider_id") ? body.find("provider_id")->as_string() : "";
    config.model = body.find("model") ? body.find("model")->as_string() : "";
    config.instructions = body.find("instructions") ? body.find("instructions")->as_string() : "";
    config.edit_budget = body.find("budget") ? body.find("budget")->as_int(150) : 150;
    config.dry_run = body.find("dry_run") ? body.find("dry_run")->as_bool(false) : false;
    if (config.edit_budget < 1) config.edit_budget = 1;
    if (config.edit_budget > 500) config.edit_budget = 500;

    std::string error;
    const std::string session = agents_.start(job_id, jobs_.dir_for(job_id), config, &error);
    if (session.empty()) return error_response(400, error);

    Json agent = Json::object();
    agent["session_id"] = session;
    agent["kind"] = config.agent;
    agent["status"] = config.agent == "plan" || config.dry_run ? "planned" : "running";
    agent["budget"] = config.edit_budget;
    agent["edits"] = 0;
    const int elements = job.find("report") && job.find("report")->find("report") &&
                                 job.find("report")->find("report")->find("elements")
                             ? job.find("report")->find("report")->find("elements")->as_int()
                             : 40;
    agent["eta_seconds"] = estimate_agent_seconds(elements, config.edit_budget);
    jobs_.set_agent(job_id, agent);

    Json out = Json::object();
    out["id"] = session;
    out["eta_seconds"] = agent.find("eta_seconds")->as_int();
    out["kind"] = config.agent;
    return json_response(out, 201);
}

}  // namespace pgr
