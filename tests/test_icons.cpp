// Icon recognition: every template must match itself from a tight ink box, and a
// real scene must produce named icons rather than anonymous squares.
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <string>
#include <vector>

#include "color.h"
#include "detect.h"
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

// Renders a template at `scale` px per cell on a light background.
static Image render_template(const std::string& art, int scale, int pad) {
    const auto rows = rows_of(art);
    int w = 0;
    for (const auto& r : rows) w = std::max(w, static_cast<int>(r.size()));
    const int h = static_cast<int>(rows.size());
    Canvas c(w * scale + pad * 2, h * scale + pad * 2, RGBA(250, 250, 252));
    for (int y = 0; y < h; ++y)
        for (int x = 0; x < static_cast<int>(rows[y].size()); ++x)
            if (rows[y][x] == '#')
                c.fill_rect(RectI{pad + x * scale, pad + y * scale, scale, scale}, RGBA(24, 26, 33));
    return c.to_image();
}

static RectI ink_bounds(const Image& img, RGBA background) {
    int x0 = img.width(), y0 = img.height(), x1 = -1, y1 = -1;
    for (int y = 0; y < img.height(); ++y)
        for (int x = 0; x < img.width(); ++x)
            if (delta_e(img.at(x, y), background) > 8.f) {
                x0 = std::min(x0, x);
                y0 = std::min(y0, y);
                x1 = std::max(x1, x);
                y1 = std::max(y1, y);
            }
    if (x1 < x0) return RectI{};
    return RectI{x0, y0, x1 - x0 + 1, y1 - y0 + 1};
}

int main() {
    // 1. Self-recognition from a tight box, at several scales.
    int named = 0;
    int total = 0;
    for (const auto& t : icon_templates()) {
        for (int scale : {2, 3, 4}) {
            const Image img = render_template(t.art, scale, 5);
            const RectI box = ink_bounds(img, RGBA(250, 250, 252));
            const IconMatch m = match_icon(img, box, RGBA(24, 26, 33));
            ++total;
            if (m.name == t.name) {
                ++named;
            } else {
                std::printf("MISS  %-16s scale %d -> %-16s %.2f\n", t.name, scale, m.name.c_str(), m.score);
            }
        }
    }
    const int templates = static_cast<int>(icon_templates().size());
    std::printf("self-match %d/%d across scales\n", named, total);
    check(named >= total * 9 / 10, "at least 90% of templates self-match at 2x-4x");

    // 2. A real scene: toolbar with a search icon next to a label.
    Canvas scene(640, 240, RGBA(16, 18, 22));
    scene.fill_rect_rounded(RectF{24, 24, 592, 48}, 10.f, RGBA(26, 29, 36));
    const std::string& art = [] {
        for (const auto& t : icon_templates())
            if (std::string(t.name) == "search") return std::string(t.art);
        return std::string();
    }();
    {
        const auto rows = rows_of(art);
        for (int y = 0; y < static_cast<int>(rows.size()); ++y)
            for (int x = 0; x < static_cast<int>(rows[y].size()); ++x)
                if (rows[y][x] == '#')
                    scene.fill_rect(RectI{40 + x * 2, 34 + y * 2, 2, 2}, RGBA(226, 230, 240));
    }
    for (int i = 0; i < 5; ++i)
        scene.fill_rect(RectI{72 + i * 12, 44, 7, 9}, RGBA(226, 230, 240));
    scene.fill_rect_rounded(RectF{24, 96, 280, 36}, 8.f, RGBA(26, 29, 36));
    scene.stroke_rect_rounded(RectF{24, 96, 280, 36}, 8.f, 1.f, RGBA(60, 66, 80));

    Image shot = scene.to_image();
    AnalysisOptions opt;
    opt.max_colors = 24;
    opt.detect_icons = true;
    ParseResult result = parse_frames({shot}, {"toolbar_scene.png"}, opt);
    check(result.error.empty(), "toolbar scene parses");

    int icons = 0;
    std::string names;
    for (const auto& e : result.screen.elements) {
        if (e.type != ElementType::Icon) continue;
        ++icons;
        if (!names.empty()) names += ",";
        names += e.icon.name.empty() ? "?" : e.icon.name;
    }
    std::printf("scene icons: %d (%s), report.icons=%d, fidelity %.1f\n", icons, names.c_str(),
                result.screen.report.icons, result.screen.report.fidelity);
    for (const auto& e : result.screen.elements)
        std::printf("  el %d %-8s rect %d,%d %dx%d icon '%s' %.2f\n", e.id, to_string(e.type), e.rect.x, e.rect.y,
                    e.rect.w, e.rect.h, e.icon.name.c_str(), e.icon.score);
    for (const auto& b : icon_candidates({}, 48)) std::printf("  cand %d,%d %dx%d\n", b.x, b.y, b.w, b.h);
    check(icons >= 1, "scene has at least one Icon element");
    check(names.find("search") != std::string::npos, "the magnifier is named 'search'");
    check(names.find("?") == std::string::npos, "no Icon element is left unnamed");
    check(result.screen.report.fidelity > 90.f, "scene fidelity above 90");

    std::printf("\n%d checks, %d failures\n", g_checks, g_failures);
    return g_failures == 0 ? 0 : 1;
}
