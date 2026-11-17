// Background analysis queue. One worker thread, bounded history, artefacts on
// disk. Jobs survive in memory for the session; the outputs are written so they
// can be inspected or committed afterwards.
#pragma once

#include <atomic>
#include <condition_variable>
#include <deque>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

#include "json.h"
#include "parser.h"
#include "types.h"

namespace pgr {

struct HttpPart;

class JobStore {
public:
    explicit JobStore(std::string root);
    ~JobStore();

    void start();
    void stop();

    // Saves uploads, queues a job and returns its id. `options` may be null.
    std::string submit(const std::vector<HttpPart>& files, const Json& options, std::string* error);

    Json list() const;
    Json get(const std::string& id) const;
    bool exists(const std::string& id) const;

    std::string dir_for(const std::string& id) const;
    bool artifact(const std::string& id, const std::string& name, std::string* bytes, std::string* mime) const;

    void set_agent(const std::string& id, const Json& agent);
    Json agent(const std::string& id) const;
    std::string primary_image(const std::string& id) const;
    std::string code_path(const std::string& id) const;

    size_t count() const;
    int queued() const;

private:
    struct Job {
        std::string id;
        std::string name;
        std::string status = "queued";
        std::string error;
        std::string created;
        std::string finished;
        int progress = 0;
        int eta_seconds = 0;
        double elapsed_ms = 0;
        size_t bytes = 0;
        std::vector<std::string> inputs;
        AnalysisOptions options;
        Json report = Json::object();
        Json agent_state = Json::object();
        std::vector<std::string> warnings;
    };

    void worker();
    void process(Job& job);
    Job* find(const std::string& id);
    const Job* find(const std::string& id) const;
    void persist(const Job& job);

    std::string root_;
    mutable std::mutex mutex_;
    std::condition_variable cv_;
    std::deque<std::string> queue_;
    std::vector<Job> jobs_;
    std::atomic<bool> running_{false};
    std::thread worker_;
};

}  // namespace pgr
