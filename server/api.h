#pragma once

#include <string>

#include "agents/bridge.h"
#include "agents/providers.h"
#include "http.h"
#include "jobs.h"

namespace pgr {

class Api {
public:
    Api(JobStore& jobs, ProviderRegistry& providers, AgentBridge& agents, std::string version);

    HttpResponse handle(const HttpRequest& request);

private:
    HttpResponse job_post(const HttpRequest& request);
    HttpResponse agent_post(const HttpRequest& request);

    JobStore& jobs_;
    ProviderRegistry& providers_;
    AgentBridge& agents_;
    std::string version_;
    double started_ms_ = 0.0;
};

extern const char* kWebUiHtml;

}  // namespace pgr
