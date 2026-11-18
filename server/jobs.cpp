#include "jobs.h"

#include <algorithm>
#include <chrono>
#include <cstdio>
#include <ctime>
#include <filesystem>
#include <fstream>
#include <random>

#include "detect.h"
#include "emit.h"
#include "http.h"
#include "image.h"
#include "render.h"

namespace fs = std::filesystem;

namespace pgr {
namespace {

std::string timestamp() {
    const std::time_t now = std::time(nullptr);
    std::tm tm{};
#ifdef _WIN32
    localtime_s(&tm, &now);
#else
    localtime_r(&now, &tm);
#endif
    char buf[32];
    std::strftime(buf, sizeof(buf), "%Y-%m-%d %H:%M:%S", &tm);
    return buf;
}

std::string make_id() {
    const std::time_t now = std::time(nullptr);
    std::tm tm{};
#ifdef _WIN32
    localtime_s(&tm, &now);
#else
    localtime_r(&now, &tm);
#endif
    char buf[32];
    std::strftime(buf, sizeof(buf), "%Y%m%d_%H%M%S", &tm);
    std::random_device rd;
    char suffix[8];
    std::snprintf(suffix, sizeof(suffix), "%04x", rd() & 0xFFFF);
    return std::string("j_") + buf + "_" + suffix;
}

bool safe_name(const std::string& name) {
    if (name.empty() || name.size() > 64) return false;
    for (char c : name)
        if (!(std::isalnum(static_cast<unsigned char>(c)) || c == '.' || c == '-' || c == '_')) return false;
    return name.find("..") == std::string::npos;
}

}  // namespace

JobStore::JobStore(std::string root) : root_(std::move(root)) {
    std::error_code ec;
    fs::create_directories(root_, ec);
}

JobStore::~JobStore() { stop(); }

void JobStore::start() {
    if (running_.exchange(true)) return;
    worker_ = std::thread([this] { worker(); });
}

void JobStore::stop() {
    if (!running_.exchange(false)) return;
    cv_.notify_all();
    if (worker_.joinable()) worker_.join();
}

std::string JobStore::submit(const std::vector<HttpPart>& files, const Json& options, std::string* error) {
    std::vector<const HttpPart*> images;
    size_t total = 0;
    for (const auto& part : files) {
        if (part.data.empty()) continue;
        if (part.name != "files" && part.name != "file" && part.name != "screenshots") continue;
        images.push_back(&part);
        total += part.data.size();
    }
    if (images.empty()) {
        if (error) *error = "no image uploaded (field name must be 'files')";
        return {};
    }
    if (total > 200ull * 1024 * 1024) {
        if (error) *error = "uploads exceed 200 MB";
        return {};
    }
    // Decode before queueing so a corrupt upload fails immediately with a clear
    // message instead of sitting in the queue.
    for (const auto* part : images) {
        std::string decode_error;
        Image probe = Image::load_memory(reinterpret_cast<const uint8_t*>(part->data.data()), part->data.size(),
                                         &decode_error);
        if (probe.empty()) {
            if (error) *error = (part->filename.empty() ? std::string("upload") : part->filename) + ": " + decode_error;
            return {};
        }
    }

    Job job;
    job.id = make_id();
    job.created = timestamp();
    job.name = images.size() == 1 ? (images.front()->filename.empty() ? "screenshot.png" : images.front()->filename)
                                  : std::to_string(images.size()) + " frames";
    job.bytes = total;
    job.eta_seconds = 2 + static_cast<int>(images.size() * 2);
    job.options.auto_gui = true;  // overlays are cropped out of game frames by default
    if (options.is_object()) {
        if (options.has("max_colors")) job.options.max_colors = std::clamp(options.find("max_colors")->as_int(24), 4, 64);
        if (options.has("auto_gui")) job.options.auto_gui = options.find("auto_gui")->as_bool(true);
        if (const Json* crop = options.find("crop"); crop && crop->is_array() && crop->size() == 4) {
            job.options.crop = RectI{crop->at(0).as_int(), crop->at(1).as_int(), crop->at(2).as_int(),
                                     crop->at(3).as_int()};
        }
        if (options.has("icons")) job.options.detect_icons = options.find("icons")->as_bool(true);
        if (options.has("text")) job.options.detect_text = options.find("text")->as_bool(true);
        if (options.has("sequence")) job.options.sequence = options.find("sequence")->as_bool(true);
        if (options.has("target")) job.options.target = options.find("target")->as_string("imgui");
    }

    const fs::path dir = fs::path(root_) / job.id;
    std::error_code ec;
    fs::create_directories(dir, ec);
    int index = 0;
    for (const auto* part : images) {
        const std::string ext = part->filename.size() > 4 ? part->filename.substr(part->filename.size() - 4) : ".png";
        const std::string name = "input_" + std::to_string(index) + ext;
        const fs::path path = dir / name;
        std::ofstream out(path, std::ios::binary);
        if (!out) {
            if (error) *error = "cannot write upload";
            return {};
        }
        out.write(part->data.data(), static_cast<std::streamsize>(part->data.size()));
        out.close();
        job.inputs.push_back(path.string());
        ++index;
    }

    std::lock_guard<std::mutex> lock(mutex_);
    jobs_.push_back(std::move(job));
    const std::string id = jobs_.back().id;
    queue_.push_back(id);
    if (jobs_.size() > 200) jobs_.erase(jobs_.begin(), jobs_.begin() + 50);
    cv_.notify_one();
    return id;
}

void JobStore::worker() {
    while (running_) {
        std::string id;
        {
            std::unique_lock<std::mutex> lock(mutex_);
            cv_.wait(lock, [this] { return !running_ || !queue_.empty(); });
            if (!running_) break;
            if (queue_.empty()) continue;
            id = queue_.front();
            queue_.pop_front();
        }
        Job* job = nullptr;
        {
            std::lock_guard<std::mutex> lock(mutex_);
            job = find(id);
            if (job) {
                job->status = "decoding";
                job->progress = 5;
            }
        }
        if (!job) continue;
        try {
            process(*job);
        } catch (const std::exception& e) {
            std::lock_guard<std::mutex> lock(mutex_);
            job->status = "error";
            job->error = e.what();
            job->progress = 100;
            persist(*job);
        } catch (...) {
            std::lock_guard<std::mutex> lock(mutex_);
            job->status = "error";
            job->error = "unhandled exception";
            job->progress = 100;
            persist(*job);
        }
    }
}

void JobStore::process(Job& job) {
    const auto started = std::chrono::steady_clock::now();
    const std::string dir = dir_for(job.id);
    // Copy the inputs out under the lock: the job record can move in the vector.
    std::vector<std::string> inputs;
    AnalysisOptions options;
    {
        std::lock_guard<std::mutex> lock(mutex_);
        inputs = job.inputs;
        options = job.options;
    }

    std::vector<Image> frames;
    std::vector<std::string> names;
    for (const auto& path : inputs) {
        std::string decode_error;
        Image img = Image::load(path, &decode_error);
        if (img.empty()) {
            std::lock_guard<std::mutex> lock(mutex_);
            job.status = "error";
            job.error = fs::path(path).filename().string() + ": " + decode_error;
            job.progress = 100;
            job.finished = timestamp();
            persist(job);
            return;
        }
        names.push_back(fs::path(path).filename().string());
        frames.push_back(std::move(img));
    }
    {
        std::lock_guard<std::mutex> lock(mutex_);
        job.status = "analyzing";
        job.progress = 30;
    }

    ParseResult result = parse_frames(frames, names, options);
    if (!result.error.empty()) {
        std::lock_guard<std::mutex> lock(mutex_);
        job.status = "error";
        job.error = result.error;
        job.progress = 100;
        job.finished = timestamp();
        persist(job);
        return;
    }
    {
        std::lock_guard<std::mutex> lock(mutex_);
        job.status = "rendering";
        job.progress = 70;
    }

    if (!frames.empty()) frames.front().save_png(dir + "/source.png");
    std::string write_error;
    if (!write_artifacts(result, dir, &write_error)) {
        std::lock_guard<std::mutex> lock(mutex_);
        job.status = "error";
        job.error = write_error;
        job.progress = 100;
        job.finished = timestamp();
        persist(job);
        return;
    }
    {
        std::lock_guard<std::mutex> lock(mutex_);
        job.status = "done";
        job.progress = 100;
        job.report = screen_json(result.screen);
        job.warnings = result.screen.report.warnings;
        job.elapsed_ms = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - started).count();
        job.finished = timestamp();
        job.eta_seconds = 0;
        // default agent state so the UI can render the panel before a session runs
        if (job.agent_state.size() == 0) {
            job.agent_state = Json::object();
            job.agent_state["status"] = "idle";
            job.agent_state["edits"] = 0;
            job.agent_state["budget"] = 150;
        }
        persist(job);
    }
}

JobStore::Job* JobStore::find(const std::string& id) {
    for (auto& j : jobs_)
        if (j.id == id) return &j;
    return nullptr;
}

const JobStore::Job* JobStore::find(const std::string& id) const {
    for (const auto& j : jobs_)
        if (j.id == id) return &j;
    return nullptr;
}

void JobStore::persist(const Job& job) {
    Json j = Json::object();
    j["id"] = job.id;
    j["name"] = job.name;
    j["status"] = job.status;
    j["error"] = job.error;
    j["created"] = job.created;
    j["finished"] = job.finished;
    j["elapsed_ms"] = job.elapsed_ms;
    j["options"] = Json::object();
    j["options"]["max_colors"] = job.options.max_colors;
    j["options"]["icons"] = job.options.detect_icons;
    j["options"]["text"] = job.options.detect_text;
    j["options"]["sequence"] = job.options.sequence;
    j["report"] = job.report;
    j["agent"] = job.agent_state;
    Json warnings = Json::array();
    for (const auto& w : job.warnings) warnings.push_back(w);
    j["warnings"] = warnings;
    std::ofstream out(dir_for(job.id) + "/job.json", std::ios::binary);
    if (out) out << j.dump(2);
}

Json JobStore::list() const {
    std::lock_guard<std::mutex> lock(mutex_);
    Json out = Json::array();
    for (auto it = jobs_.rbegin(); it != jobs_.rend(); ++it) {
        Json j = Json::object();
        j["id"] = it->id;
        j["name"] = it->name;
        j["status"] = it->status;
        j["progress"] = it->progress;
        j["eta_seconds"] = it->eta_seconds;
        j["created"] = it->created;
        j["frames"] = static_cast<int>(it->inputs.size());
        if (it->report.has("report")) j["summary"] = it->report.find("report")->find("summary")->as_string();
        j["fidelity"] = it->report.has("report") ? it->report.find("report")->find("fidelity")->as_number() : 0.0;
        j["agent"] = it->agent_state;
        out.push_back(j);
    }
    Json root = Json::object();
    root["jobs"] = out;
    root["count"] = static_cast<int>(jobs_.size());
    return root;
}

Json JobStore::get(const std::string& id) const {
    std::lock_guard<std::mutex> lock(mutex_);
    const Job* job = find(id);
    if (!job) return Json();
    Json j = Json::object();
    j["id"] = job->id;
    j["name"] = job->name;
    j["status"] = job->status;
    j["error"] = job->error;
    j["progress"] = job->progress;
    j["eta_seconds"] = job->eta_seconds;
    j["elapsed_ms"] = job->elapsed_ms;
    j["bytes"] = static_cast<double>(job->bytes);
    j["created"] = job->created;
    j["finished"] = job->finished;
    j["report"] = job->report;
    j["agent"] = job->agent_state;
    j["artifacts"] = Json::object();
    if (job->status == "done") {
        j["artifacts"]["source"] = "/api/jobs/" + job->id + "/artifact/source.png";
        j["artifacts"]["reconstruction"] = "/api/jobs/" + job->id + "/artifact/reconstruction.png";
        j["artifacts"]["diff"] = "/api/jobs/" + job->id + "/artifact/diff.png";
        j["artifacts"]["code"] = "/api/jobs/" + job->id + "/artifact/parsed_ui.cpp";
        j["artifacts"]["main"] = "/api/jobs/" + job->id + "/artifact/parsed_ui_main.cpp";
        j["artifacts"]["report_json"] = "/api/jobs/" + job->id + "/artifact/report.json";
        j["artifacts"]["report_md"] = "/api/jobs/" + job->id + "/artifact/report.md";
        j["artifacts"]["layout"] = "/api/jobs/" + job->id + "/artifact/layout.txt";
    }
    Json warnings = Json::array();
    for (const auto& w : job->warnings) warnings.push_back(w);
    j["warnings"] = warnings;
    return j;
}

bool JobStore::exists(const std::string& id) const {
    std::lock_guard<std::mutex> lock(mutex_);
    return find(id) != nullptr;
}

std::string JobStore::dir_for(const std::string& id) const { return root_ + "/" + id; }

bool JobStore::artifact(const std::string& id, const std::string& name, std::string* bytes, std::string* mime) const {
    if (!safe_name(name)) return false;
    const fs::path path = fs::path(dir_for(id)) / name;
    std::ifstream in(path, std::ios::binary);
    if (!in) return false;
    std::string data((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
    if (bytes) *bytes = std::move(data);
    if (mime) *mime = mime_for(name);
    return true;
}

void JobStore::set_agent(const std::string& id, const Json& agent) {
    std::lock_guard<std::mutex> lock(mutex_);
    Job* job = find(id);
    if (!job) return;
    job->agent_state = agent;
    persist(*job);
}

Json JobStore::agent(const std::string& id) const {
    std::lock_guard<std::mutex> lock(mutex_);
    const Job* job = find(id);
    return job ? job->agent_state : Json();
}

std::string JobStore::primary_image(const std::string& id) const {
    std::lock_guard<std::mutex> lock(mutex_);
    const Job* job = find(id);
    return job && !job->inputs.empty() ? job->inputs.front() : std::string();
}

std::string JobStore::code_path(const std::string& id) const { return dir_for(id) + "/parsed_ui.cpp"; }

size_t JobStore::count() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return jobs_.size();
}

int JobStore::queued() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return static_cast<int>(queue_.size());
}

}  // namespace pgr
