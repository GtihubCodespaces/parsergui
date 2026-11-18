// Automatic GUI detection: a dark ImGui-style overlay floating over a bright,
// textured game frame must be found and parsed on its own, without cropping by
// hand. Also verifies that a normal full-window UI is not mistaken for an overlay.
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <filesystem>
#include <string>
#include <vector>

#include "color.h"
#include "detect.h"
#include "image.h"
#include "parser.h"
#include "raster.h"

using namespace pgr;
namespace fs = std::filesystem;

static int g_checks = 0;
static int g_failures = 0;

static void check(bool ok, const std::string& what) {
    ++g_checks;
    if (!ok) ++g_failures;
    std::printf("%s  %s\n", ok ? "ok  " : "FAIL", what.c_str());
}

static const RGBA PANEL(12, 13, 21);
static const RGBA PANEL_TOP(17, 18, 28);
static const RGBA CARD(20, 21, 32);
static const RGBA FIELD(23, 25, 38);
static const RGBA MUTED(150, 156, 174);
static const RGBA TEXT(226, 230, 240);
static const RGBA ACCENT(96, 132, 246);
static const RGBA TRACK(52, 54, 74);

// The overlay rectangle used by every check below.
static RectI overlay_rect() { return RectI{1000, 100, 560, 460}; }

static RGBA lerp(RGBA a, RGBA b, float t) {
    auto mix = [t](int x, int y) { return static_cast<uint8_t>(x + (y - x) * t); };
    return RGBA(mix(a.r, b.r), mix(a.g, b.g), mix(a.b, b.b));
}

// Small glyph-like blocks: the detector needs real text runs to accept a panel.
static void word(Canvas& c, int x, int y, int count, RGBA color, int gh = 8) {
    static const int widths[6] = {4, 6, 5, 7, 5, 6};
    for (int i = 0; i < count; ++i) c.fill_rect(RectI{x + i * 8, y, std::min(widths[i % 6], 6), gh}, color);
}

static Image make_game_scene() {
    const int W = 1600, H = 900;
    Canvas c(W, H, RGBA(150, 200, 235));
    for (int y = 0; y < H; ++y) {
        const float t = static_cast<float>(y) / H;
        RGBA sky = t < 0.55f ? lerp(RGBA(96, 164, 226), RGBA(196, 226, 244), t / 0.55f)
                             : lerp(RGBA(196, 226, 244), RGBA(226, 206, 160), (t - 0.55f) / 0.45f);
        c.fill_rect(RectI{0, y, W, 1}, sky);
    }
    uint32_t seed = 0x51f3a7u;
    auto rnd = [&seed]() {
        seed = seed * 1664525u + 1013904223u;
        return (seed >> 8) & 0xFFFFu;
    };
    // Dense foliage: saturated greens, exactly what the neutral-panel rule rejects.
    for (int i = 0; i < 460; ++i) {
        const int x = static_cast<int>(rnd() % W);
        const int y = static_cast<int>(rnd() % 470);
        const int r = 18 + static_cast<int>(rnd() % 74);
        const RGBA g(static_cast<uint8_t>(24 + rnd() % 70), static_cast<uint8_t>(104 + rnd() % 86),
                     static_cast<uint8_t>(20 + rnd() % 54));
        c.fill_circle(static_cast<float>(x), static_cast<float>(y), static_cast<float>(r), g);
    }
    c.fill_rect(RectI{0, 520, W, H - 520}, RGBA(214, 194, 140));
    for (int i = 0; i < 2400; ++i) {
        const int x = static_cast<int>(rnd() % W), y = 520 + static_cast<int>(rnd() % (H - 520));
        c.fill_rect(RectI{x, y, 2, 2}, RGBA(186, 166, 116));
    }
    // A house, HUD and inventory: busy geometry that must be ignored.
    c.fill_rect(RectI{740, 380, 230, 160}, RGBA(226, 210, 186));
    c.fill_rect(RectI{728, 356, 254, 28}, RGBA(120, 90, 70));
    c.fill_rect(RectI{790, 430, 44, 60}, RGBA(70, 60, 52));
    c.add_noise(11, 0.035f);
    for (int i = 0; i < 6; ++i) word(c, 28, 22 + i * 18, 5 + i % 3, RGBA(250, 250, 250), 7);
    c.fill_rect(RectI{24, 250, 210, 10}, RGBA(60, 200, 110));
    c.fill_rect(RectI{24, 266, 150, 10}, RGBA(230, 200, 60));
    for (int i = 0; i < 6; ++i) {
        c.fill_rect(RectI{1260 + i * 52, 806, 44, 56}, RGBA(36, 38, 46));
        c.fill_rect(RectI{1268 + i * 52, 814, 28, 40}, RGBA(96, 98, 110));
    }
    return c.to_image();
}

static void draw_overlay(Canvas& c) {
    const RectI r = overlay_rect();
    c.drop_shadow(RectF(r.x, r.y, static_cast<float>(r.w), static_cast<float>(r.h)), 14.f, 18.f, 10.f,
                  RGBA(0, 0, 0), 0.45f);
    c.fill_rect_rounded(RectF(r), 12.f, PANEL);
    c.fill_rect_rounded(RectF(r.x, r.y, r.w, 38), 12.f, PANEL_TOP);
    c.fill_rect(RectI{r.x, r.y + 30, r.w, 8}, PANEL_TOP);

    const char* labels[] = {"Aimbot", "Triggerbot", "ESP", "Exploits", "Config"};
    int tx = r.x + 22;
    for (int i = 0; i < 5; ++i) {
        word(c, tx, r.y + 15, (i == 0 ? 5 : (i == 2 ? 3 : 6)), i == 2 ? TEXT : MUTED, 7);
        if (i == 2) c.fill_rect(RectI{tx - 4, r.y + 32, 46, 2}, ACCENT);
        tx += 74;
    }
    c.fill_rect(RectI{r.x + 420, r.y + 14, 8, 8}, ACCENT);
    word(c, r.x + 434, r.y + 15, 6, MUTED, 6);

    const RectI search{r.x + 16, r.y + 50, r.w - 32, 28};
    c.fill_rect_rounded(RectF(search), 7.f, FIELD);
    c.stroke_circle(static_cast<float>(search.x + 18), static_cast<float>(search.y + 14), 6.f, 1.6f, MUTED);
    c.line(static_cast<float>(search.x + 23), static_cast<float>(search.y + 19),
           static_cast<float>(search.x + 27), static_cast<float>(search.y + 23), 1.6f, MUTED);
    word(c, search.x + 38, search.y + 9, 12, MUTED, 8);

    const RectI left{r.x + 16, r.y + 92, 266, 300};
    const RectI right{r.x + 296, r.y + 92, 248, 300};
    for (const RectI& card : {left, right}) {
        c.fill_rect_rounded(RectF(card), 9.f, CARD);
        c.fill_rect(RectI{card.x + 14, card.y + 16, 3, 12}, ACCENT);
        word(c, card.x + 24, card.y + 18, 7, TEXT, 7);
        c.fill_rect(RectI{card.x + 14, card.y + 38, card.w - 28, 1}, RGBA(40, 42, 58));
    }
    for (int i = 0; i < 6; ++i) {
        const int y = left.y + 58 + i * 38;
        c.stroke_circle(static_cast<float>(left.x + 26), static_cast<float>(y + 7), 7.f, 1.4f, MUTED);
        if (i == 1) c.fill_circle(static_cast<float>(left.x + 26), static_cast<float>(y + 7), 4.f, ACCENT);
        word(c, left.x + 44, y, 8, i == 1 ? TEXT : MUTED, 8);
        if (i < 4) {
            const RectI toggle{left.right() - 56, y - 1, 30, 16};
            c.fill_rect_rounded(RectF(toggle), 8.f, i == 0 ? ACCENT : RGBA(44, 46, 64));
            c.fill_circle(static_cast<float>(i == 0 ? toggle.right() - 8 : toggle.x + 8),
                          static_cast<float>(toggle.cy()), 6.f, RGBA(240, 242, 250));
        } else {
            word(c, left.right() - 84, y, 7, MUTED, 7);
        }
    }
    for (int i = 0; i < 5; ++i) {
        const int y = right.y + 58 + i * 34;
        const RectI box{right.x + 18, y - 1, 14, 14};
        if (i < 2)
            c.fill_rect_rounded(RectF(box), 3.f, ACCENT);
        else
            c.stroke_rect_rounded(RectF(box), 3.f, 1.2f, RGBA(78, 82, 104));
        if (i < 2) c.line(static_cast<float>(box.x + 3), static_cast<float>(box.y + 7),
                          static_cast<float>(box.x + 6), static_cast<float>(box.y + 10), 1.6f, RGBA(255, 255, 255));
        word(c, right.x + 42, y, 7 + i % 3, i < 2 ? TEXT : MUTED, 7);
    }
    word(c, right.x + 18, right.y + 244, 10, MUTED, 7);
    c.fill_rect(RectI{right.x + 18, right.y + 262, right.w - 36, 3}, TRACK);
    c.fill_rect(RectI{right.x + 18, right.y + 262, 118, 3}, ACCENT);
    c.fill_circle(static_cast<float>(right.x + 136), static_cast<float>(right.y + 263), 6.f, ACCENT);
    word(c, right.x + 218, right.y + 258, 3, MUTED, 7);

    c.fill_rect(RectI{r.x, r.bottom() - 34, r.w, 1}, RGBA(34, 36, 50));
    word(c, r.x + 18, r.bottom() - 24, 5, MUTED, 6);
    word(c, r.right() - 96, r.bottom() - 24, 8, MUTED, 6);
}

static Image make_overlay_scene() {
    Image img = make_game_scene();
    Canvas c(img.width(), img.height());
    c.blit(img, 0, 0, img.bounds());
    draw_overlay(c);
    return c.to_image();
}

// A normal application window: the whole frame is the UI, so no crop is allowed.
static Image make_full_window() {
    Canvas c(900, 600, RGBA(16, 19, 25));
    c.fill_rect(RectI{0, 0, 900, 40}, RGBA(22, 26, 34));
    for (int i = 0; i < 3; ++i) {
        const RectI card{24 + i * 284, 70, 260, 210};
        c.fill_rect_rounded(RectF(card), 10.f, RGBA(25, 30, 39));
        word(c, card.x + 16, card.y + 18, 7, TEXT, 8);
        c.fill_rect(RectI{card.x + 16, card.y + 44, card.w - 32, 1}, RGBA(44, 50, 64));
        for (int row = 0; row < 5; ++row) word(c, card.x + 16, card.y + 62 + row * 26, 6 + row % 3, MUTED, 7);
    }
    for (int i = 0; i < 7; ++i) word(c, 28, 300 + i * 26, 8 + i % 4, MUTED, 8);
    return c.to_image();
}

static bool near(const RectI& a, const RectI& b, int tolerance) {
    return std::abs(a.x - b.x) <= tolerance && std::abs(a.y - b.y) <= tolerance &&
           std::abs(a.w - b.w) <= tolerance * 2 && std::abs(a.h - b.h) <= tolerance * 2;
}

int main(int argc, char** argv) {
    const fs::path out = argc > 1 ? fs::path(argv[1]) : fs::path("overlay-out");
    std::error_code ec;
    fs::create_directories(out, ec);

    const Image scene = make_overlay_scene();
    check(scene.save_png((out / "game_with_overlay.png").string()), "game scene rendered");

    const GuiRegion found = find_gui_region(scene);
    const RectI want = overlay_rect();
    check(found.found(), "overlay region found");
    if (found.found()) {
        std::printf("  region %d,%d %dx%d confidence %.2f (want %d,%d %dx%d)\n", found.rect.x, found.rect.y,
                    found.rect.w, found.rect.h, found.confidence, want.x, want.y, want.w, want.h);
    }
    check(found.found() && near(found.rect, want, 10), "overlay rectangle within 10px");
    check(found.found() && found.rect.iou(want) > 0.88, "overlay IoU above 0.88");
    check(found.found() && found.confidence >= 0.45f, "overlay confidence above 0.45");

    AnalysisOptions auto_opts;
    auto_opts.auto_gui = true;
    ParseResult parsed = parse_screenshot(scene, auto_opts, "game_with_overlay.png");
    check(parsed.error.empty(), "auto-cropped parse succeeds");
    check(parsed.screen.region.iou(want) > 0.88, "parse used the detected region");
    check(parsed.screen.width < 620 && parsed.screen.height < 520, "parsed frame is the overlay, not the game");
    check(parsed.screen.auto_gui, "report marks the region as automatic");
    check(parsed.screen.report.elements >= 15, "overlay elements measured");
    check(parsed.screen.report.cards >= 2, "overlay cards measured");
    check(parsed.fidelity >= 85.f, "overlay fidelity above 85");
    std::printf("  overlay %s\n", parsed.screen.report.summary.c_str());
    check(write_artifacts(parsed, out.string(), nullptr), "overlay artefacts written");

    AnalysisOptions crop_opts;
    crop_opts.crop = want;
    ParseResult cropped = parse_screenshot(scene, crop_opts, "crop");
    check(near(cropped.screen.region, want, 2), "explicit crop honoured");
    check(!cropped.screen.auto_gui, "explicit crop is not reported as automatic");

    const Image window = make_full_window();
    const GuiRegion none = find_gui_region(window);
    check(!none.found(), "full-window UI is not cropped");
    AnalysisOptions window_opts;
    window_opts.auto_gui = true;
    ParseResult full = parse_screenshot(window, window_opts, "window.png");
    check(full.screen.region.empty() && full.screen.width == 900, "full window keeps its frame");
    check(full.screen.report.elements >= 10, "full window still parses");

    // Drop the two reference screenshots into assets/demo/overlay/ and they will be
    // checked too; the suite stays green when they are absent.
    int reference = 0;
    const fs::path refs = fs::path("assets/demo/overlay");
    if (fs::is_directory(refs, ec)) {
        for (const auto& entry : fs::directory_iterator(refs, ec)) {
            if (!entry.is_regular_file() || entry.path().extension() != ".png") continue;
            std::string error;
            Image img = Image::load(entry.path().string(), &error);
            if (img.empty()) continue;
            const GuiRegion region = find_gui_region(img);
            std::printf("  reference %s: %d,%d %dx%d confidence %.2f\n", entry.path().filename().string().c_str(),
                        region.rect.x, region.rect.y, region.rect.w, region.rect.h, region.confidence);
            check(region.found(), "reference screenshot GUI found: " + entry.path().filename().string());
            ++reference;
        }
    }
    if (reference == 0) std::printf("  (no reference screenshots in assets/demo/overlay/)\n");

    std::printf("\n%d checks, %d failures\n", g_checks, g_failures);
    return g_failures == 0 ? 0 : 1;
}
