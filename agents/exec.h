// Subprocess execution with a timeout. Used to drive Claude Code, Codex and
// OpenCode, and by the HTTP client (curl) so the project needs no TLS library.
#pragma once

#include <string>
#include <vector>

namespace pgr {

struct ProcessResult {
    bool spawned = false;
    bool timed_out = false;
    int exit_code = -1;
    std::string out;
    std::string err;
    double elapsed_ms = 0.0;
};

ProcessResult run_process(const std::vector<std::string>& argv, const std::string& cwd, int timeout_ms,
                          const std::string& stdin_data = std::string());

bool which(const std::string& binary, std::string* resolved = nullptr);
std::string shell_quote(const std::string& s);

}  // namespace pgr
