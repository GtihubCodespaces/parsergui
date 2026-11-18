// Dense desktop scene: menu bar with an open dropdown, toolbar, sidebar, tabs,
// KPI cards, data table, settings form and status bar. Every label is drawn on an
// 8px rhythm so the engine has to tell menus from buttons from rows.
#include <algorithm>
#include <cstdio>
#include <map>
#include <string>
#include <vector>

#include "color.h"
#include "emit.h"
#include "font.h"
#include "image.h"
#include "parser.h"
#include "raster.h"

using namespace pgr;

static int g_checks = 0;
static int g_failures = 0;

static void check(bool ok, const std::string& what) {
    ++g_checks;
    if (!ok) ++g_failures;
    std::printf("%s  %s\n", ok ? "ok  " : "FAIL", what.c_str());
}

static const RGBA BG(15, 18, 22);
static const RGBA BAR(18, 22, 27);
static const RGBA SURFACE(23, 27, 35);
static const RGBA SURFACE2(29, 34, 43);
static const RGBA FIELD(15, 19, 25);
static const RGBA LINE(42, 49, 64);
static const RGBA TEXT(230, 234, 242);
static const RGBA MUTED(153, 163, 181);
static const RGBA ACCENT(94, 129, 244);
static const RGBA GREEN(52, 211, 153);

static void box(Canvas& c, const RectI& r, RGBA fill, RGBA border = RGBA(0, 0, 0, 0), int radius = 0) {
    if (radius > 0) {
        c.fill_rect_rounded(RectF(r), static_cast<float>(radius), fill);
        if (border.a) c.stroke_rect_rounded(RectF(r), static_cast<float>(radius), 1.f, border);
    } else {
        c.fill_rect(r, fill);
        if (border.a) {
            c.fill_rect(RectI{r.x, r.y, r.w, 1}, border);
            c.fill_rect(RectI{r.x, r.bottom() - 1, r.w, 1}, border);
            c.fill_rect(RectI{r.x, r.y, 1, r.h}, border);
            c.fill_rect(RectI{r.right() - 1, r.y, 1, r.h}, border);
        }
    }
}

// Variable-width glyph boxes, 3px gaps, so a run looks like a word and not a bar.
static void word(Canvas& c, int x, int y, int count, RGBA color, int gh = 9, int pitch = 8) {
    static const int widths[6] = {4, 6, 5, 7, 5, 6};
    for (int i = 0; i < count; ++i) {
        const int w = std::min(widths[i % 6], pitch - 3);
        c.fill_rect(RectI{x + i * pitch, y + (9 - gh) / 2, w, gh}, color);
    }
}

static std::vector<std::string> rows_of(const std::string& art) {
    std::vector<std::string> rows;
    std::string cur;
    for (char ch : art) {
        if (ch == '|') {
            rows.push_back(cur);
            cur.clear();
        } else {
            cur += ch;
        }
    }
    rows.push_back(cur);
    return rows;
}

static void art(Canvas& c, const char* name, int x, int y, RGBA color, int scale = 1) {
    for (const auto& t : icon_templates()) {
        if (std::string(t.name) != name) continue;
        const auto rows = rows_of(t.art);
        for (int ry = 0; ry < static_cast<int>(rows.size()); ++ry)
            for (int rx = 0; rx < static_cast<int>(rows[ry].size()); ++rx)
                if (rows[ry][rx] == '#')
                    c.fill_rect(RectI{x + rx * scale, y + ry * scale, scale, scale}, color);
        return;
    }
}

static Image make_desktop() {
    Canvas c(1280, 800, BG);

    // Menu bar.
    box(c, RectI{0, 0, 1280, 40}, BAR);
    c.fill_rect(RectI{0, 39, 1280, 1}, LINE);
    box(c, RectI{16, 10, 20, 20}, ACCENT, RGBA(0, 0, 0, 0), 5);
    word(c, 46, 15, 6, TEXT);
    word(c, 120, 15, 7, TEXT);
    word(c, 190, 15, 7, MUTED);
    word(c, 270, 15, 8, TEXT);
    word(c, 360, 15, 6, TEXT);
    word(c, 430, 15, 5, TEXT);
    const char* items[] = {"plus", "upload", "download", "lock", "settings", "trash"};

    // Toolbar.
    box(c, RectI{0, 40, 1280, 52}, RGBA(19, 24, 32));
    c.fill_rect(RectI{0, 91, 1280, 1}, LINE);
    for (int i = 0; i < 4; ++i) {
        box(c, RectI{16 + i * 40, 50, 32, 32}, SURFACE2, LINE, 6);
        art(c, items[i], 24 + i * 40, 58, MUTED);
    }
    box(c, RectI{200, 50, 360, 32}, FIELD, LINE, 8);
    art(c, "search", 212, 58, MUTED);
    word(c, 234, 61, 22, MUTED);

    // Sidebar.
    box(c, RectI{0, 92, 224, 684}, BAR);
    c.fill_rect(RectI{223, 92, 1, 684}, LINE);
    const char* nav[] = {"home", "search", "calendar", "user", "settings", "bell", "info"};
    for (int i = 0; i < 7; ++i) {
        const int y = 112 + i * 48;
        if (i == 0) {
            box(c, RectI{12, y, 200, 40}, RGBA(27, 37, 54), RGBA(0, 0, 0, 0), 8);
            c.fill_rect(RectI{12, y, 3, 40}, ACCENT);
        }
        art(c, nav[i], 24, y + 12, i == 0 ? TEXT : MUTED);
        word(c, 52, y + 15, 6 + i, i == 0 ? TEXT : MUTED);
    }

    // Content header and tabs.
    word(c, 256, 110, 14, TEXT, 14, 12);
    word(c, 256, 136, 18, MUTED);
    word(c, 700, 172, 6, TEXT);
    word(c, 784, 172, 4, MUTED);
    word(c, 872, 172, 7, MUTED);
    c.fill_rect(RectI{700, 188, 52, 2}, ACCENT);

    // KPI cards.
    for (int i = 0; i < 3; ++i) {
        const int x = 256 + i * 264;
        box(c, RectI{x, 300, 240, 120}, SURFACE, LINE, 10);
        word(c, x + 20, 320, 4, TEXT, 16, 14);
        word(c, x + 20, 348, 10, MUTED);
        box(c, RectI{x + 20, 376, 64, 20}, RGBA(18, 51, 38), RGBA(30, 91, 62), 10);
        word(c, x + 32, 382, 4, GREEN);
    }

    // Data table.
    box(c, RectI{256, 440, 752, 300}, SURFACE, LINE, 10);
    c.fill_rect(RectI{256, 440, 752, 34}, RGBA(26, 31, 40));
    const int cols[] = {276, 420, 560, 700};
    for (int x : cols) word(c, x, 453, 6, MUTED);
    word(c, 900, 453, 5, MUTED);
    for (int r = 0; r < 6; ++r) {
        const int y = 474 + r * 40;
        c.fill_rect(RectI{256, y + 39, 752, 1}, RGBA(35, 41, 54));
        for (int x : cols) word(c, x, y + 14, 7 + (x % 3), TEXT);
        box(c, RectI{900, y + 11, 56, 18}, RGBA(18, 51, 38), RGBA(30, 91, 62), 9);
        word(c, 910, y + 16, 4, GREEN);
    }

    // Settings form.
    box(c, RectI{1024, 208, 232, 460}, SURFACE, LINE, 10);
    word(c, 1044, 228, 8, TEXT, 11, 9);
    word(c, 1044, 262, 7, MUTED, 8);
    box(c, RectI{1044, 276, 192, 34}, FIELD, LINE, 8);
    word(c, 1056, 288, 12, TEXT);
    word(c, 1044, 322, 6, MUTED, 8);
    box(c, RectI{1044, 336, 192, 34}, FIELD, LINE, 8);
    word(c, 1056, 348, 10, TEXT);
    // Checked checkbox + label.
    box(c, RectI{1044, 386, 16, 16}, ACCENT, RGBA(0, 0, 0, 0), 4);
    art(c, "check", 1046, 388, RGBA(255, 255, 255));
    word(c, 1068, 390, 9, TEXT);
    // Unchecked checkbox + label.
    box(c, RectI{1044, 414, 16, 16}, FIELD, LINE, 4);
    word(c, 1068, 418, 7, TEXT);
    // Switch.
    box(c, RectI{1044, 444, 40, 22}, ACCENT, RGBA(0, 0, 0, 0), 11);
    c.fill_circle(1073, 455, 8, RGBA(255, 255, 255));
    word(c, 1094, 450, 6, TEXT);
    // Slider.
    box(c, RectI{1044, 484, 192, 6}, SURFACE2, RGBA(0, 0, 0, 0), 3);
    box(c, RectI{1044, 484, 120, 6}, ACCENT, RGBA(0, 0, 0, 0), 3);
    c.fill_circle(1164, 487, 7, RGBA(255, 255, 255));
    // Footer buttons.
    box(c, RectI{1044, 536, 90, 34}, SURFACE2, LINE, 8);
    word(c, 1060, 548, 7, TEXT);
    box(c, RectI{1146, 536, 90, 34}, ACCENT, ACCENT, 8);
    word(c, 1162, 548, 8, RGBA(255, 255, 255));

    // Overlay: the open "Fichier" dropdown sits above toolbar and sidebar.
    box(c, RectI{424, 44, 264, 248}, RGBA(34, 40, 52), LINE, 8);
    const int item_y[] = {54, 86, 118, 158, 190, 222};
    for (int i = 0; i < 6; ++i) {
        art(c, items[i], 440, item_y[i] + 3, MUTED);
        word(c, 464, item_y[i], i == 0 ? 7 : 9, TEXT);
        word(c, 632, item_y[i], 4, MUTED);
    }
    c.fill_rect(RectI{440, 150, 232, 1}, LINE);

    // Status bar.
    box(c, RectI{0, 776, 1280, 24}, RGBA(12, 15, 19));
    c.fill_rect(RectI{0, 776, 1280, 1}, LINE);
    word(c, 16, 783, 20, MUTED, 8);
    return c.to_image();
}


// Context menu over a panel: checked row, separator, submenu arrow. A second,
// independent layout so the menu rules are not tuned to one fixture.
static Image make_context_menu() {
    Canvas c(640, 420, RGBA(16, 19, 24));
    box(c, RectI{20, 20, 600, 380}, RGBA(23, 27, 35), LINE, 12);
    word(c, 44, 44, 10, TEXT, 11, 9);
    word(c, 44, 66, 22, MUTED);
    box(c, RectI{300, 80, 220, 200}, RGBA(34, 40, 52), LINE, 8);
    const char* icons[] = {"check", "edit", "download", "lock", "trash"};
    const int ys[] = {94, 126, 158, 198, 230, 262};
    for (int i = 0; i < 4; ++i) {
        art(c, icons[i], 316, ys[i], i == 0 ? ACCENT : MUTED, 2);
        word(c, 344, ys[i], i == 0 ? 8 : 9, TEXT);
        word(c, 470, ys[i], 4, MUTED);
    }
    c.fill_rect(RectI{316, 190, 188, 1}, LINE);
    art(c, icons[4], 316, ys[4], MUTED, 2);
    word(c, 344, ys[4], 9, TEXT);
    art(c, "chevron_right", 488, ys[4], MUTED, 2);
    art(c, "settings", 316, ys[5], MUTED, 2);
    word(c, 344, ys[5], 7, TEXT);
    return c.to_image();
}

int main(int argc, char** argv) {
    const std::string outdir = argc > 1 ? argv[1] : ".";
    const Image scene = make_desktop();
    scene.save_png(outdir + "/complex_source.png");

    AnalysisOptions opt;
    opt.max_colors = 32;
    opt.detect_icons = true;
    ParseResult result = parse_frames({scene}, {"complex.png"}, opt);
    check(result.error.empty(), "complex scene parses");

    std::map<std::string, int> counts;
    for (const auto& e : result.screen.elements) counts[to_string(e.type)]++;
    std::printf("elements=%d  ", result.screen.report.elements);
    for (const auto& [k, v] : counts) std::printf("%s=%d ", k.c_str(), v);
    std::printf("\nfidelity=%.1f typography=%.1f grid=%d icons=%d menu=%d tabs=%d inputs=%d toggles=%d buttons=%d\n",
                result.screen.report.fidelity, result.screen.report.typography,
                result.screen.metrics.base_unit, counts["Icon"], counts["MenuItem"], counts["Tab"], counts["Input"],
                counts["Toggle"], counts["Button"]);

    for (const auto& e : result.screen.elements) {
        std::printf("  %-8s %4d,%-4d %3dx%-3d r%-2d icon=%-8s text=%2d\n", to_string(e.type), e.rect.x, e.rect.y,
                    e.rect.w, e.rect.h, e.radius, e.icon.name.c_str(), static_cast<int>(e.text.text.size()));
    }

    check(counts["MenuItem"] >= 6, "menu bar items and dropdown rows found");
    check(counts["Tab"] >= 2, "tabs found");
    check(counts["Input"] >= 2, "text fields found");
    check(counts["Toggle"] >= 2, "checkboxes and switch found");
    check(counts["Button"] >= 2, "buttons found");
    check(counts["Panel"] >= 5, "panels found");
    check(counts["Icon"] >= 6, "icons found");
    check(result.screen.report.fidelity > 95.f, "fidelity above 95");
    check(result.screen.report.typography > 60.f, "typography above 60");

    EmitOptions eo;
    eo.source_name = "complex.png";
    const std::string code = emit_imgui(result.screen, eo);
    check(code.find("namespace ParsedUI") != std::string::npos, "code emitted");

    std::string error;
    check(write_artifacts(result, outdir, &error), "artefacts written: " + error);

    // Second scene: context menu with a checked item and a submenu arrow.
    {
        const Image ctx = make_context_menu();
        ctx.save_png(outdir + "/context_source.png");
        ParseResult r2 = parse_frames({ctx}, {"context_menu.png"}, opt);
        std::map<std::string, int> c2;
        for (const auto& e : r2.screen.elements) c2[to_string(e.type)]++;
        std::printf("context: elements=%d menu=%d icons=%d fidelity=%.1f\n", r2.screen.report.elements,
                    c2["MenuItem"], c2["Icon"], r2.screen.report.fidelity);
        check(r2.error.empty(), "context menu scene parses");
        check(c2["MenuItem"] >= 6, "context menu rows found");
        check(c2["Icon"] >= 4, "context menu icons found");
        check(r2.screen.report.fidelity > 90.f, "context menu fidelity above 90");
        check(write_artifacts(r2, outdir, &error), "context artefacts written");
    }

    std::printf("\n%d checks, %d failures\n", g_checks, g_failures);
    return g_failures == 0 ? 0 : 1;
}
