#include "parser.h"

#include <chrono>
#include <cmath>
#include <cstdio>
#include <fstream>
#include <sstream>

#include "detect.h"
#include "emit.h"
#include "render.h"

namespace pgr {
namespace {

double now_ms() {
    using clock = std::chrono::steady_clock;
    return std::chrono::duration<double, std::milli>(clock::now().time_since_epoch()).count();
}

void score_typography(ParseResult& result) {
    double weighted = 0;
    double total = 0;
    for (const auto& e : result.screen.elements) {
        if (e.text.size_px <= 0.f) continue;
        const double w = std::max(1.0, static_cast<double>(e.text.box.area()));
        weighted += e.text.confidence * w;
        total += w;
    }
    result.typography = total > 0 ? static_cast<float>(weighted / total * 100.0) : 0.f;
    result.screen.report.typography = result.typography;
}

}  // namespace

ParseResult parse_screenshot(const Image& image, const AnalysisOptions& options, const std::string& name) {
    ParseResult result;
    const double started = now_ms();
    if (image.empty()) {
        result.error = "empty or undecodable image";
        return result;
    }
    // A screenshot is not always the GUI: it can be a game frame with a floating
    // overlay. Locate that rectangle first and measure only it, so trees and
    // characters never become elements.
    Image working = image;
    RectI region;
    const int full_area = image.width() * image.height();
    if (!options.crop.empty()) {
        region = options.crop.clip(image.width(), image.height());
        if (region.area() < 64 || region.area() >= full_area * 0.94) region = RectI{};
    } else if (options.auto_gui) {
        const GuiRegion gui = find_gui_region(image);
        if (gui.found() && gui.rect.area() < full_area * 0.94) {
            region = gui.rect.clip(image.width(), image.height());
        }
    }
    if (!region.empty()) working = image.crop(region);

    result.screen = detect_screen(working, options, name);
    result.screen.region = region;
    result.screen.auto_gui = !region.empty() && options.crop.empty();

    RenderOptions ro;
    ro.carry_text_pixels = true;
    result.reconstruction = render_reconstruction(result.screen, &working, ro);
    const Similarity sim = compare_images(working, result.reconstruction);
    result.diff = make_diff_image(working, result.reconstruction);
    result.fidelity = sim.fidelity;

    result.screen.report.fidelity = sim.fidelity;
    result.screen.report.elapsed_ms = now_ms() - started;
    score_typography(result);

    std::ostringstream summary;
    summary << result.screen.report.elements << " elements, " << result.screen.report.cards << " panels, "
            << result.screen.report.buttons << " buttons, " << result.screen.report.inputs << " inputs, "
            << result.screen.report.icons << " icons; grid " << result.screen.metrics.base_unit << "px; "
            << "fidelity " << static_cast<int>(sim.fidelity) << "/100 (" << fidelity_summary(sim) << ")";
    if (!region.empty())
        summary << "; gui " << region.w << "x" << region.h << " at " << region.x << "," << region.y;
    result.screen.report.summary = summary.str();
    if (sim.fidelity < 70.f)
        result.screen.report.warnings.push_back(
            "geometry score below 70: regions were not modelled (photos, charts or heavy effects)");
    return result;
}

ParseResult parse_frames(const std::vector<Image>& frames, const std::vector<std::string>& names,
                         const AnalysisOptions& options) {
    ParseResult result;
    if (frames.empty()) {
        result.error = "no frames";
        return result;
    }
    const std::string name = names.empty() ? "screenshot" : names.front();
    result = parse_screenshot(frames.front(), options, name);
    if (result.error.empty() && options.sequence && frames.size() > 1) {
        result.screen.sequence = analyze_sequence(frames, names);
        for (const auto& w : result.screen.sequence.warnings) result.screen.report.warnings.push_back(w);
    }
    return result;
}

bool write_artifacts(const ParseResult& result, const std::string& directory, std::string* error) {
    auto fail = [&](const std::string& msg) {
        if (error) *error = msg;
        return false;
    };
    if (result.error.empty()) {
        if (!result.reconstruction.save_png(directory + "/reconstruction.png"))
            return fail("cannot write reconstruction.png");
        if (!result.diff.empty() && !result.diff.save_png(directory + "/diff.png"))
            return fail("cannot write diff.png");
    }
    EmitOptions eo;
    eo.source_name = result.screen.name;
    {
        std::ofstream out(directory + "/parsed_ui.cpp", std::ios::binary);
        if (!out) return fail("cannot write parsed_ui.cpp");
        out << emit_imgui(result.screen, eo);
    }
    {
        std::ofstream out(directory + "/parsed_ui_main.cpp", std::ios::binary);
        if (!out) return fail("cannot write parsed_ui_main.cpp");
        out << emit_stub_main(result.screen, eo);
    }
    {
        std::ofstream out(directory + "/report.json", std::ios::binary);
        if (!out) return fail("cannot write report.json");
        out << emit_json(result.screen);
    }
    {
        std::ofstream out(directory + "/report.md", std::ios::binary);
        if (!out) return fail("cannot write report.md");
        out << emit_markdown(result.screen);
    }
    {
        std::ofstream out(directory + "/layout.txt", std::ios::binary);
        if (!out) return fail("cannot write layout.txt");
        out << emit_layout_txt(result.screen);
    }
    return true;
}

}  // namespace pgr
