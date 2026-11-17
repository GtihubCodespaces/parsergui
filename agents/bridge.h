// Agent bridge. Two transports:
//   cli      - spawns Claude Code, Codex or OpenCode in the job directory
//   provider - OpenAI/Anthropic compatible chat loop with file tools
// Both run asynchronously and report a log, an edit count and an estimate.
#pragma once

#include <map>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

#include "json.h"
#include "providers.h"

namespace pgr {

struct AgentConfig {
    std::string agent = "claude";  // claude | codex | opencode | provider | plan
    std::string provider_id;
    std::string model;
    std::string instructions;
    int edit_budget = 150;
    int max_steps = 12;
    int timeout_seconds = 900;
    bool dry_run = false;
};

class AgentBridge {
public:
    AgentBridge(std::string skills_dir, ProviderRegistry* registry);

    Json available() const;
    Json skills() const;

    std::string start(const std::string& job_id, const std::string& job_dir, const AgentConfig& config,
                      std::string* error);
    Json status(const std::string& session_id) const;
    void stop();

private:
    struct Session {
        std::string id;
        std::string job_id;
        std::string job_dir;
        std::string kind;
        std::string status = "running";
        std::string error;
        std::string started;
        std::string finished;
        int edits = 0;
        int budget = 150;
        int changed_lines = 0;
        int eta_seconds = 0;
        std::vector<std::string> log;
        Json result = Json::object();
    };

    void run(Session& session, AgentConfig config);
    void run_cli(Session& session, const AgentConfig& config, const std::string& prompt);
    void run_provider(Session& session, const AgentConfig& config, const std::string& prompt);
    void append(Session& session, const std::string& line);
    std::string load_skills() const;
    std::string build_prompt(const Session& session, const AgentConfig& config) const;
    std::string read_file(const std::string& path) const;
    bool write_job_file(const std::string& job_dir, const std::string& relative, const std::string& content,
                        std::string* error);

    std::string skills_dir_;
    ProviderRegistry* registry_;
    mutable std::mutex mutex_;
    std::map<std::string, Session> sessions_;
    std::vector<std::thread> threads_;
};

int estimate_agent_seconds(int element_count, int budget);

}  // namespace pgr
