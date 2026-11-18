// parsergui command line.
//   parsergui parse <image...> [--out DIR] [--colors N] [--no-icons]
//   parsergui serve [--port 8787] [--data DIR]
//   parsergui providers [--free] [--q TEXT] [--test ID]
//   parsergui agent --job DIR --agent claude|codex|opencode|provider|plan [--provider ID]
//   parsergui version
#include <algorithm>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <iostream>
#include <string>
#include <thread>
#include <vector>

#include "agents/bridge.h"
#include "agents/providers.h"
#include "api.h"
#include "emit.h"
#include "http.h"
#include "image.h"
#include "jobs.h"
#include "parser.h"

using namespace pgr;
namespace fs = std::filesystem;

namespace {

std::string version() { return "1.0.0"; }

std::string exe_dir(const char* argv0) {
    std::error_code ec;
    const fs::path p = fs::weakly_canonical(fs::path(argv0 ? argv0 : "."), ec);
    return p.parent_path().string();
}

std::string find_asset(const std::string& relative, const char* argv0) {
    std::error_code ec;
    if (fs::exists(relative, ec)) return relative;
    const std::string near_exe = exe_dir(argv0) + "/" + relative;
    if (fs::exists(near_exe, ec)) return near_exe;
    const std::string parent = exe_dir(argv0) + "/../" + relative;
    if (fs::exists(parent, ec)) return parent;
    return relative;
}

std::string data_dir(const std::string& override_dir) {
    if (!override_dir.empty()) return override_dir;
    if (const char* env = getenv("PARSERGUI_DATA")) return env;
    std::error_code ec;
    const fs::path p = fs::temp_directory_path(ec) / "parsergui-data";
    return p.string();
}

void usage() {
    std::printf(
        "parsergui %s - screenshot to ImGui reconstruction\n\n"
        "  parsergui parse <image...> [--out DIR] [--colors N] [--no-icons] [--json]\n"
        "                    [--no-auto-gui] [--crop X,Y,W,H]\n"
        "  parsergui serve [--port 8787] [--data DIR]\n"
        "  parsergui providers [--free] [--q TEXT] [--test ID]\n"
        "  parsergui agent --job DIR --agent claude|codex|opencode|provider|plan [--provider ID] [--budget N]\n"
        "  parsergui version\n",
        version().c_str());
}

int cmd_parse(const std::vector<std::string>& args, const char* argv0) {
    std::vector<std::string> inputs;
    std::string out = "parsergui-out";
    AnalysisOptions options;
    options.auto_gui = true;  // a game frame with an overlay is the common case
    bool json_only = false;
    for (size_t i = 0; i < args.size(); ++i) {
        if (args[i] == "--out" && i + 1 < args.size()) out = args[++i];
        else if (args[i] == "--colors" && i + 1 < args.size()) options.max_colors = std::atoi(args[++i].c_str());
        else if (args[i] == "--no-icons") options.detect_icons = false;
        else if (args[i] == "--no-auto-gui") options.auto_gui = false;
        else if (args[i] == "--auto-gui") options.auto_gui = true;
        else if (args[i] == "--crop" && i + 1 < args.size()) {
            int x = 0, y = 0, w = 0, h = 0;
            if (std::sscanf(args[++i].c_str(), "%d,%d,%d,%d", &x, &y, &w, &h) == 4)
                options.crop = RectI{x, y, w, h};
            else {
                std::fprintf(stderr, "--crop expects X,Y,W,H\n");
                return 2;
            }
        }
        else if (args[i] == "--json") json_only = true;
        else if (args[i].rfind("--", 0) == 0) {
            std::fprintf(stderr, "unknown flag %s\n", args[i].c_str());
            return 2;
        } else {
            inputs.push_back(args[i]);
        }
    }
    if (inputs.empty()) {
        usage();
        return 2;
    }
    std::vector<Image> frames;
    std::vector<std::string> names;
    for (const auto& path : inputs) {
        std::string error;
        Image img = Image::load(path, &error);
        if (img.empty()) {
            std::fprintf(stderr, "%s: %s\n", path.c_str(), error.c_str());
            return 1;
        }
        names.push_back(fs::path(path).filename().string());
        frames.push_back(std::move(img));
    }
    ParseResult result = parse_frames(frames, names, options);
    if (!result.error.empty()) {
        std::fprintf(stderr, "%s\n", result.error.c_str());
        return 1;
    }
    std::error_code ec;
    fs::create_directories(out, ec);
    std::string error;
    if (!write_artifacts(result, out, &error)) {
        std::fprintf(stderr, "%s\n", error.c_str());
        return 1;
    }
    if (json_only) {
        std::printf("%s\n", emit_json(result.screen).c_str());
    } else {
        std::printf("%s\n", result.screen.report.summary.c_str());
        std::printf("artefacts: %s\n", fs::absolute(out, ec).string().c_str());
    }
    (void)argv0;
    return 0;
}

int cmd_serve(const std::vector<std::string>& args, const char* argv0) {
    int port = 8787;
    std::string data;
    for (size_t i = 0; i < args.size(); ++i) {
        if (args[i] == "--port" && i + 1 < args.size()) port = std::atoi(args[++i].c_str());
        else if (args[i] == "--data" && i + 1 < args.size()) data = args[++i];
    }
    const std::string root = data_dir(data);
    std::error_code ec;
    fs::create_directories(root, ec);

    JobStore jobs(root);
    jobs.start();
    ProviderRegistry providers;
    std::string load_error;
    const std::string bundled = find_asset("providers/providers.json", argv0);
    const std::string extra = find_asset("providers/user.json", argv0);
    providers.load(bundled, extra, &load_error);
    const std::string skills = find_asset("skills", argv0);
    AgentBridge bridge(skills, &providers);
    Api api(jobs, providers, bridge, version());

    HttpServer server(port, [&api](const HttpRequest& request) { return api.handle(request); });
    std::string error;
    if (!server.start(&error)) {
        std::fprintf(stderr, "cannot start server: %s\n", error.c_str());
        return 1;
    }
    std::printf("parsergui %s\n", version().c_str());
    std::printf("  ui        http://localhost:%d\n", port);
    std::printf("  api       http://localhost:%d/api/health\n", port);
    std::printf("  data      %s\n", fs::absolute(root, ec).string().c_str());
    std::printf("  providers %zu loaded from %s%s\n", providers.size(), bundled.c_str(),
                load_error.empty() ? "" : (" (" + load_error + ")").c_str());
    std::printf("  skills    %s\n", fs::absolute(skills, ec).string().c_str());
    std::fflush(stdout);
    // Run until the process is signalled. The HTTP server owns its accept thread.
    while (server.running()) std::this_thread::sleep_for(std::chrono::milliseconds(400));
    server.stop();
    jobs.stop();
    bridge.stop();
    return 0;
}

int cmd_providers(const std::vector<std::string>& args, const char* argv0) {
    ProviderRegistry registry;
    std::string error;
    const std::string bundled = find_asset("providers/providers.json", argv0);
    registry.load(bundled, "", &error);
    bool free_only = false;
    std::string query;
    std::string test_id;
    for (size_t i = 0; i < args.size(); ++i) {
        if (args[i] == "--free") free_only = true;
        else if (args[i] == "--q" && i + 1 < args.size()) query = args[++i];
        else if (args[i] == "--test" && i + 1 < args.size()) test_id = args[++i];
    }
    if (!test_id.empty()) {
        std::printf("%s\n", registry.test(test_id).dump(2).c_str());
        return 0;
    }
    const auto list = registry.sorted(free_only, query);
    std::printf("%-22s %-10s %-5s %-6s %s\n", "id", "category", "free", "key", "base_url");
    for (const auto& p : list) {
        std::printf("%-22s %-10s %-5s %-6s %s\n", p.id.c_str(), p.category.c_str(), p.free_tier ? "yes" : "",
                    p.key_present ? "yes" : "", p.base_url.c_str());
    }
    std::printf("\n%zu providers (capacity 500)\n", list.size());
    return 0;
}

int cmd_agent(const std::vector<std::string>& args, const char* argv0) {
    std::string job_dir;
    std::string agent = "plan";
    std::string provider;
    std::string model;
    int budget = 150;
    for (size_t i = 0; i < args.size(); ++i) {
        if (args[i] == "--job" && i + 1 < args.size()) job_dir = args[++i];
        else if (args[i] == "--agent" && i + 1 < args.size()) agent = args[++i];
        else if (args[i] == "--provider" && i + 1 < args.size()) provider = args[++i];
        else if (args[i] == "--model" && i + 1 < args.size()) model = args[++i];
        else if (args[i] == "--budget" && i + 1 < args.size()) budget = std::atoi(args[++i].c_str());
    }
    if (job_dir.empty()) {
        std::fprintf(stderr, "--job is required\n");
        return 2;
    }
    ProviderRegistry registry;
    std::string error;
    registry.load(find_asset("providers/providers.json", argv0), "", &error);
    AgentBridge bridge(find_asset("skills", argv0), &registry);
    AgentConfig config;
    config.agent = agent;
    config.provider_id = provider;
    config.model = model;
    config.edit_budget = budget;
    const std::string session = bridge.start("cli", job_dir, config, &error);
    if (session.empty()) {
        std::fprintf(stderr, "%s\n", error.c_str());
        return 1;
    }
    std::printf("session %s started\n", session.c_str());
    // Wait for completion so the command is useful in a script.
    for (int i = 0; i < 7200; ++i) {
        const Json status = bridge.status(session);
        const std::string state = status.find("status") ? status.find("status")->as_string() : "";
        if (state != "running") {
            std::printf("%s\n", status.dump(2).c_str());
            bridge.stop();
            return state == "error" ? 1 : 0;
        }
        std::this_thread::sleep_for(std::chrono::seconds(1));
    }
    std::fprintf(stderr, "timeout\n");
    return 1;
}

}  // namespace

int main(int argc, char** argv) {
    std::vector<std::string> args(argv + 1, argv + argc);
    if (args.empty()) {
        usage();
        return 0;
    }
    const std::string command = args.front();
    args.erase(args.begin());
    if (command == "parse") return cmd_parse(args, argv[0]);
    if (command == "serve") return cmd_serve(args, argv[0]);
    if (command == "providers") return cmd_providers(args, argv[0]);
    if (command == "agent") return cmd_agent(args, argv[0]);
    if (command == "version" || command == "--version") {
        std::printf("parsergui %s\n", version().c_str());
        return 0;
    }
    if (command == "help" || command == "--help" || command == "-h") {
        usage();
        return 0;
    }
    std::fprintf(stderr, "unknown command: %s\n", command.c_str());
    usage();
    return 2;
}
