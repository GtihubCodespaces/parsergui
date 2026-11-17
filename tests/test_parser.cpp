// End-to-end check: draw a UI with known measurements, encode it, decode it, parse
// it, and assert the recovered values. Also exercises JSON, emission and the
// reconstruction diff.
#include <cmath>
#include <cstdio>
#include <string>
#include <vector>

#include "color.h"
#include "detect.h"
#include "emit.h"
#include "image.h"
#include "json.h"
#include "parser.h"
#include "raster.h"
#include "render.h"

using namespace pgr;

static int g_failures = 0;
static int g_checks = 0;

static void check(bool ok, const std::string& what) {
    ++g_checks;
    if (!ok) {
        ++g_failures;
        std::printf("FAIL  %s\n", what.c_str());
    } else {
        std::printf("ok    %s\n", what.c_str());
    }
}

// Draws a crisp 1px border the way a browser or a design tool renders it:
// outer rounded rectangle in the border colour, inner one in the fill colour.
static void bordered(Canvas& c, const RectF& r, float radius, RGBA border, RGBA fill) {
    c.fill_rect_rounded(r, radius, border);
    c.fill_rect_rounded(RectF(r.x + 1, r.y + 1, r.w - 2, r.h - 2), std::max(0.f, radius - 1), fill);
}

static void glyphs(Canvas& c, int x, int y, int count, int gw, int gh, int pitch, RGBA color) {
    for (int i = 0; i < count; ++i) c.fill_rect(RectI{x + i * pitch, y, gw, gh}, color);
}

static Image make_scene() {
    const int W = 900, H = 600;
    Canvas c(W, H, RGBA(16, 18, 22));
    c.fill_rect(RectI{0, 0, 200, H}, RGBA(22, 25, 31));
    bordered(c, RectF(40, 60, 420, 240), 12, RGBA(44, 49, 59), RGBA(26, 29, 36));
    bordered(c, RectF(64, 240, 132, 36), 8, RGBA(64, 92, 190), RGBA(94, 129, 244));
    bordered(c, RectF(236, 240, 132, 36), 8, RGBA(60, 66, 80), RGBA(38, 42, 51));
    c.fill_rect(RectI{40, 320, 420, 1}, RGBA(40, 44, 52));
    bordered(c, RectF(480, 60, 380, 160), 8, RGBA(44, 49, 59), RGBA(26, 29, 36));
    c.fill_rect_rounded(RectF(500, 84, 180, 9), 4, RGBA(226, 230, 240));
    c.fill_rect_rounded(RectF(500, 108, 300, 6), 3, RGBA(120, 126, 140));
    c.fill_rect_rounded(RectF(500, 124, 260, 6), 3, RGBA(120, 126, 140));
    // A fake label: seven 7x11 glyph boxes on a 10px pitch, cap height 11.
    glyphs(c, 64, 88, 7, 7, 11, 10, RGBA(226, 230, 240));
    // Button label: four glyph boxes centred against the 132px button.
    glyphs(c, 100, 254, 4, 6, 9, 9, RGBA(255, 255, 255));
    return c.to_image();
}

int main(int argc, char** argv) {
    const std::string outdir = argc > 1 ? argv[1] : ".";
    const Image scene = make_scene();
    scene.save_png(outdir + "/source.png");

    // Encode/decode round trip.
    std::vector<uint8_t> png = scene.encode_png();
    check(!png.empty(), "PNG encoder produces bytes");
    std::string error;
    Image decoded = Image::load_memory(png.data(), png.size(), &error);
    check(!decoded.empty() && decoded.width() == scene.width(), "PNG decodes back: " + error);
    if (!decoded.empty()) {
        const Similarity rt = compare_images(scene, decoded);
        check(rt.ssim > 0.999f, "lossless PNG round trip (SSIM " + std::to_string(rt.ssim) + ")");
    }

    AnalysisOptions opt;
    ParseResult result = parse_screenshot(scene, opt, "test_scene");
    const Screen& screen = result.screen;
    check(result.error.empty(), "parse completes");
    check(screen.elements.size() >= 6, "found at least 6 elements, got " + std::to_string(screen.elements.size()));

    // The card at 40,60 420x240 must be recovered closely.
    int best_iou_id = -1;
    double best_iou = 0;
    const RectI card{40, 60, 420, 240};
    for (const auto& e : screen.elements) {
        const double iou = e.rect.iou(card);
        if (iou > best_iou) {
            best_iou = iou;
            best_iou_id = e.id;
        }
    }
    check(best_iou > 0.90, "card recovered with IoU 0.90, got " + std::to_string(best_iou));
    if (best_iou_id >= 0) {
        const Element& e = screen.elements[best_iou_id];
        check(std::abs(e.radius - 12) <= 1, "card radius ~12, got " + std::to_string(e.radius));
        check(e.border.present(), "card border detected");
        check(delta_e(e.fill.top, RGBA(26, 29, 36)) < 4.f, "card fill colour exact");
    }

    // Accent colour appears in the palette.
    bool accent = false;
    for (const auto& p : screen.palette)
        if (delta_e(p.color, RGBA(94, 129, 244)) < 3.f) accent = true;
    check(accent, "accent colour present in palette");

    check(screen.metrics.base_unit == 4 || screen.metrics.base_unit == 8 || screen.metrics.base_unit == 6,
          "base unit is a plausible grid, got " + std::to_string(screen.metrics.base_unit));
    check(result.fidelity > 55.f, "fidelity above 55, got " + std::to_string(result.fidelity));

    check(result.screen.report.text_runs >= 1, "text lines detected, got " +
                                                       std::to_string(result.screen.report.text_runs));
    bool mono_label = false;
    float label_size = 0;
    for (const auto& e : screen.elements) {
        if (e.text.size_px <= 0.f) continue;
        if (e.text.family == FontFamily::Mono) mono_label = true;
        label_size = std::max(label_size, e.text.size_px);
    }
    check(mono_label, "equal-width glyphs classified as monospace");
    check(label_size > 11.f && label_size < 19.f, "estimated font size 11..19px, got " + std::to_string(label_size));
    check(result.typography > 30.f, "typography confidence above 30, got " + std::to_string(result.typography));

    bool button_found = false;
    for (const auto& e : screen.elements)
        if (e.type == ElementType::Button && std::abs(e.rect.w - 132) <= 2) button_found = true;
    check(button_found, "accent button classified as Button");

    // JSON export must round trip.
    const std::string js = emit_json(screen);
    Json parsed;
    std::string jerr;
    check(Json::parse(js, &parsed, &jerr), "report JSON parses: " + jerr);
    check(parsed.has("elements") && parsed["elements"].size() == screen.elements.size(),
          "JSON element count matches");

    // Code generation must be non-trivial and contain the palette + icons.
    EmitOptions eo;
    eo.source_name = "test_scene.png";
    const std::string code = emit_imgui(screen, eo);
    check(code.find("namespace ParsedUI") != std::string::npos, "emitted namespace present");
    check(code.find("void Draw(") != std::string::npos, "emitted Draw() present");
    check(code.find("static void Icon(") != std::string::npos, "emitted icon helper present");
    check(code.size() > 2000, "emitted code is substantial");

    // Writing artefacts.
    if (!write_artifacts(result, outdir, &error)) {
        std::printf("FAIL  write_artifacts: %s\n", error.c_str());
        ++g_failures;
    } else {
        std::printf("ok    artefacts written to %s\n", outdir.c_str());
    }

    std::printf("\n%d checks, %d failures\n", g_checks, g_failures);
    std::printf("%s\n", result.screen.report.summary.c_str());
    return g_failures == 0 ? 0 : 1;
}
