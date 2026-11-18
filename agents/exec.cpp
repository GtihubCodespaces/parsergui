#include "exec.h"

#include <chrono>
#include <cstring>

#ifdef _WIN32
#define NOMINMAX
#include <windows.h>
#else
#include <fcntl.h>
#include <signal.h>
#include <sys/select.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>
#include <cerrno>
#include <cstdlib>
#endif

namespace pgr {
namespace {

double now_ms() {
    using clock = std::chrono::steady_clock;
    return std::chrono::duration<double, std::milli>(clock::now().time_since_epoch()).count();
}

}  // namespace

std::string shell_quote(const std::string& s) {
    std::string out = "'";
    for (char c : s) {
        if (c == '\'')
            out += "'\\''";
        else
            out.push_back(c);
    }
    out += "'";
    return out;
}

#ifndef _WIN32

ProcessResult run_process(const std::vector<std::string>& argv, const std::string& cwd, int timeout_ms,
                          const std::string& stdin_data) {
    ProcessResult result;
    if (argv.empty()) return result;
    const double started = now_ms();

    int out_pipe[2], err_pipe[2], in_pipe[2];
    if (pipe(out_pipe) != 0) return result;
    if (pipe(err_pipe) != 0) {
        close(out_pipe[0]);
        close(out_pipe[1]);
        return result;
    }
    if (pipe(in_pipe) != 0) {
        close(out_pipe[0]);
        close(out_pipe[1]);
        close(err_pipe[0]);
        close(err_pipe[1]);
        return result;
    }

    const pid_t pid = fork();
    if (pid < 0) {
        close(out_pipe[0]);
        close(out_pipe[1]);
        close(err_pipe[0]);
        close(err_pipe[1]);
        close(in_pipe[0]);
        close(in_pipe[1]);
        return result;
    }
    if (pid == 0) {
        dup2(in_pipe[0], STDIN_FILENO);
        dup2(out_pipe[1], STDOUT_FILENO);
        dup2(err_pipe[1], STDERR_FILENO);
        close(in_pipe[0]);
        close(in_pipe[1]);
        close(out_pipe[0]);
        close(out_pipe[1]);
        close(err_pipe[0]);
        close(err_pipe[1]);
        if (!cwd.empty() && chdir(cwd.c_str()) != 0) _exit(126);
        std::vector<char*> args;
        args.reserve(argv.size() + 1);
        for (const auto& a : argv) args.push_back(const_cast<char*>(a.c_str()));
        args.push_back(nullptr);
        execvp(args[0], args.data());
        _exit(127);
    }

    result.spawned = true;
    close(in_pipe[0]);
    close(out_pipe[1]);
    close(err_pipe[1]);

    if (!stdin_data.empty()) {
        size_t written = 0;
        while (written < stdin_data.size()) {
            const ssize_t n = write(in_pipe[1], stdin_data.data() + written, stdin_data.size() - written);
            if (n <= 0) break;
            written += static_cast<size_t>(n);
        }
    }
    close(in_pipe[1]);

    fcntl(out_pipe[0], F_SETFL, O_NONBLOCK);
    fcntl(err_pipe[0], F_SETFL, O_NONBLOCK);

    bool out_open = true, err_open = true;
    char buffer[8192];
    const double deadline = started + (timeout_ms > 0 ? timeout_ms : 600000);
    while (out_open || err_open) {
        if (now_ms() > deadline) {
            result.timed_out = true;
            kill(pid, SIGKILL);
            break;
        }
        fd_set readfds;
        FD_ZERO(&readfds);
        int maxfd = 0;
        if (out_open) {
            FD_SET(out_pipe[0], &readfds);
            maxfd = std::max(maxfd, out_pipe[0]);
        }
        if (err_open) {
            FD_SET(err_pipe[0], &readfds);
            maxfd = std::max(maxfd, err_pipe[0]);
        }
        timeval tv{};
        tv.tv_usec = 100 * 1000;
        const int ready = select(maxfd + 1, &readfds, nullptr, nullptr, &tv);
        if (ready > 0) {
            if (out_open && FD_ISSET(out_pipe[0], &readfds)) {
                const ssize_t n = read(out_pipe[0], buffer, sizeof(buffer));
                if (n > 0)
                    result.out.append(buffer, static_cast<size_t>(n));
                else if (n == 0)
                    out_open = false;
            }
            if (err_open && FD_ISSET(err_pipe[0], &readfds)) {
                const ssize_t n = read(err_pipe[0], buffer, sizeof(buffer));
                if (n > 0)
                    result.err.append(buffer, static_cast<size_t>(n));
                else if (n == 0)
                    err_open = false;
            }
        }
        if (!out_open && !err_open) break;
    }
    close(out_pipe[0]);
    close(err_pipe[0]);

    int status = 0;
    if (waitpid(pid, &status, 0) > 0) {
        if (WIFEXITED(status)) result.exit_code = WEXITSTATUS(status);
        else if (WIFSIGNALED(status)) result.exit_code = 128 + WTERMSIG(status);
    }
    result.elapsed_ms = now_ms() - started;
    return result;
}

bool which(const std::string& binary, std::string* resolved) {
    if (binary.empty()) return false;
    if (binary.find('/') != std::string::npos) {
        if (access(binary.c_str(), X_OK) == 0) {
            if (resolved) *resolved = binary;
            return true;
        }
        return false;
    }
    const char* path_env = getenv("PATH");
    if (!path_env) return false;
    std::string path(path_env);
    size_t start = 0;
    while (start <= path.size()) {
        const size_t colon = path.find(':', start);
        const std::string dir = path.substr(start, colon == std::string::npos ? std::string::npos : colon - start);
        if (!dir.empty()) {
            const std::string full = dir + "/" + binary;
            if (access(full.c_str(), X_OK) == 0) {
                if (resolved) *resolved = full;
                return true;
            }
        }
        if (colon == std::string::npos) break;
        start = colon + 1;
    }
    return false;
}

#else  // _WIN32

ProcessResult run_process(const std::vector<std::string>& argv, const std::string& cwd, int timeout_ms,
                          const std::string& stdin_data) {
    ProcessResult result;
    if (argv.empty()) return result;
    const double started = now_ms();

    SECURITY_ATTRIBUTES sa{};
    sa.nLength = sizeof(sa);
    sa.bInheritHandle = TRUE;
    HANDLE out_read = nullptr, out_write = nullptr, err_read = nullptr, err_write = nullptr;
    HANDLE in_read = nullptr, in_write = nullptr;
    if (!CreatePipe(&out_read, &out_write, &sa, 0) || !CreatePipe(&err_read, &err_write, &sa, 0) ||
        !CreatePipe(&in_read, &in_write, &sa, 0))
        return result;
    SetHandleInformation(out_read, HANDLE_FLAG_INHERIT, 0);
    SetHandleInformation(err_read, HANDLE_FLAG_INHERIT, 0);
    SetHandleInformation(in_write, HANDLE_FLAG_INHERIT, 0);

    std::string command;
    for (size_t i = 0; i < argv.size(); ++i) {
        if (i) command += " ";
        command += "\"" + argv[i] + "\"";
    }

    STARTUPINFOA si{};
    si.cb = sizeof(si);
    si.dwFlags = STARTF_USESTDHANDLES;
    si.hStdInput = in_read;
    si.hStdOutput = out_write;
    si.hStdError = err_write;
    PROCESS_INFORMATION pi{};
    std::vector<char> cmd(command.begin(), command.end());
    cmd.push_back('\0');
    const BOOL ok = CreateProcessA(nullptr, cmd.data(), nullptr, nullptr, TRUE, CREATE_NO_WINDOW, nullptr,
                                   cwd.empty() ? nullptr : cwd.c_str(), &si, &pi);
    CloseHandle(out_write);
    CloseHandle(err_write);
    CloseHandle(in_read);
    if (!ok) {
        CloseHandle(out_read);
        CloseHandle(err_read);
        CloseHandle(in_write);
        return result;
    }
    result.spawned = true;
    if (!stdin_data.empty()) {
        DWORD written = 0;
        WriteFile(in_write, stdin_data.data(), static_cast<DWORD>(stdin_data.size()), &written, nullptr);
    }
    CloseHandle(in_write);

    auto drain = [](HANDLE h, std::string& sink) {
        char buffer[8192];
        DWORD available = 0;
        while (PeekNamedPipe(h, nullptr, 0, nullptr, &available, nullptr) && available > 0) {
            DWORD got = 0;
            if (!ReadFile(h, buffer, std::min<DWORD>(available, sizeof(buffer)), &got, nullptr) || got == 0) break;
            sink.append(buffer, got);
        }
    };

    const DWORD timeout = timeout_ms > 0 ? static_cast<DWORD>(timeout_ms) : 600000;
    const DWORD wait = WaitForSingleObject(pi.hProcess, timeout);
    if (wait == WAIT_TIMEOUT) {
        result.timed_out = true;
        TerminateProcess(pi.hProcess, 1);
    }
    for (int i = 0; i < 4; ++i) {
        drain(out_read, result.out);
        drain(err_read, result.err);
        Sleep(20);
    }
    DWORD code = 0;
    GetExitCodeProcess(pi.hProcess, &code);
    result.exit_code = static_cast<int>(code);
    CloseHandle(out_read);
    CloseHandle(err_read);
    CloseHandle(pi.hProcess);
    CloseHandle(pi.hThread);
    result.elapsed_ms = now_ms() - started;
    return result;
}

bool which(const std::string& binary, std::string* resolved) {
    const char* path_env = getenv("PATH");
    if (!path_env) return false;
    std::string path(path_env);
    std::vector<std::string> names{binary};
    if (binary.find('.') == std::string::npos) {
        names.push_back(binary + ".exe");
        names.push_back(binary + ".cmd");
        names.push_back(binary + ".bat");
    }
    size_t start = 0;
    while (start <= path.size()) {
        const size_t semi = path.find(';', start);
        const std::string dir = path.substr(start, semi == std::string::npos ? std::string::npos : semi - start);
        for (const auto& name : names) {
            const std::string full = dir + "\\" + name;
            if (GetFileAttributesA(full.c_str()) != INVALID_FILE_ATTRIBUTES) {
                if (resolved) *resolved = full;
                return true;
            }
        }
        if (semi == std::string::npos) break;
        start = semi + 1;
    }
    return false;
}

#endif

}  // namespace pgr
