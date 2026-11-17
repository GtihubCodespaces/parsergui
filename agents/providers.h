// Provider registry. One entry per gateway an agent can call. The registry is
// data-driven so a user can add private gateways without a rebuild; the bundled
// catalogue is the seed, not a hard limit. Capacity is capped at 500.
#pragma once

#include <mutex>
#include <string>
#include <vector>

#include "json.h"

namespace pgr {

struct Provider {
    std::string id;
    std::string name;
    std::string kind = "openai";  // openai | anthropic | cli | local
    std::string base_url;
    std::string env_key;
    std::string category = "aggregator";
    bool free_tier = false;
    bool local = false;
    bool requires_key = true;
    int priority = 50;            // lower is shown first among equals
    std::string notes;
    std::vector<std::string> models;
    std::string docs_url;

    // Runtime state, never persisted.
    bool key_present = false;
    bool reachable = false;
    int last_status = 0;
    std::string last_test;
    double last_latency_ms = 0.0;
};

class ProviderRegistry {
public:
    static constexpr int kCapacity = 500;

    // Loads the bundled catalogue; `extra_path` is merged on top when present.
    bool load(const std::string& bundled_path, const std::string& extra_path, std::string* error);

    bool merge_file(const std::string& path, std::string* error);
    bool merge_json(const Json& root, std::string* error);

    // Registers a session-only key. Never written to disk.
    bool set_key(const std::string& id, const std::string& key);

    std::vector<Provider> sorted(bool free_only, const std::string& query) const;

    Json to_json(bool free_only, const std::string& query, bool redact) const;

    // GET /models against the provider with the configured key.
    Json test(const std::string& id);

    const Provider* find(const std::string& id) const;
    bool get(const std::string& id, Provider* out) const;
    // Session key first, then the environment variable. Never persisted.
    std::string key_for(const std::string& id) const;
    size_t size() const;
    bool full() const { return providers_.size() >= static_cast<size_t>(kCapacity); }

private:
    void refresh_keys();
    void sort_in_place();

    mutable std::mutex mutex_;
    std::vector<Provider> providers_;
    std::vector<std::pair<std::string, std::string>> session_keys_;
};

std::string env_value(const std::string& name);

}  // namespace pgr
