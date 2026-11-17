#include "font.h"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <map>

#include "color.h"

namespace pgr {
namespace {

struct Bitmap {
    int w = 0, h = 0;
    std::vector<uint8_t> v;
    uint8_t at(int x, int y) const {
        if (x < 0 || y < 0 || x >= w || y >= h) return 0;
        return v[static_cast<size_t>(y) * w + x];
    }
};

Bitmap ink_bitmap(const Image& img, const RectI& box, RGBA ink, float tolerance) {
    Bitmap b;
    RectI r = box.clip(img.width(), img.height());
    if (r.empty()) return b;
    b.w = r.w;
    b.h = r.h;
    b.v.assign(static_cast<size_t>(r.w) * r.h, 0);
    const Lab ink_lab = to_lab(ink);
    for (int y = 0; y < r.h; ++y) {
        for (int x = 0; x < r.w; ++x) {
            RGBA c = img.at(r.x + x, r.y + y);
            const float d = delta_e_lab(to_lab(c), ink_lab);
            float cov = 1.f;
            if (d > tolerance) cov = std::clamp(1.f - (d - tolerance) / 8.f, 0.f, 1.f);
            b.v[static_cast<size_t>(y) * r.w + x] = static_cast<uint8_t>(cov * 255.f);
        }
    }
    return b;
}

Bitmap downsample(const Bitmap& src, int nw, int nh) {
    Bitmap out;
    out.w = nw;
    out.h = nh;
    out.v.assign(static_cast<size_t>(nw) * nh, 0);
    if (src.w <= 0 || src.h <= 0) return out;
    for (int y = 0; y < nh; ++y) {
        for (int x = 0; x < nw; ++x) {
            const int sx0 = x * src.w / nw, sx1 = std::max(sx0 + 1, (x + 1) * src.w / nw);
            const int sy0 = y * src.h / nh, sy1 = std::max(sy0 + 1, (y + 1) * src.h / nh);
            int sum = 0, n = 0;
            for (int sy = sy0; sy < sy1; ++sy)
                for (int sx = sx0; sx < sx1; ++sx, ++n) sum += src.at(sx, sy);
            out.v[static_cast<size_t>(y) * nw + x] = n > 0 ? static_cast<uint8_t>(sum / n) : 0;
        }
    }
    return out;
}

Bitmap parse_template(const char* art) {
    Bitmap b;
    std::string s(art);
    std::vector<std::string> rows;
    size_t start = 0;
    while (start <= s.size()) {
        const size_t bar = s.find('|', start);
        rows.push_back(s.substr(start, bar == std::string::npos ? std::string::npos : bar - start));
        if (bar == std::string::npos) break;
        start = bar + 1;
    }
    b.h = static_cast<int>(rows.size());
    for (const auto& r : rows) b.w = std::max(b.w, static_cast<int>(r.size()));
    b.v.assign(static_cast<size_t>(b.w) * b.h, 0);
    for (int y = 0; y < b.h; ++y)
        for (int x = 0; x < b.w; ++x)
            b.v[static_cast<size_t>(y) * b.w + x] =
                x < static_cast<int>(rows[y].size()) && rows[y][x] == '#' ? 255 : 0;
    return b;
}

float iou(const Bitmap& a, const Bitmap& b, bool invert_b) {
    int inter = 0, uni = 0;
    for (int y = 0; y < a.h; ++y) {
        for (int x = 0; x < a.w; ++x) {
            const bool pa = a.at(x, y) >= 110;
            const uint8_t vb = b.at(x, y);
            const bool pb = invert_b ? vb < 110 : vb >= 110;
            if (pa && pb) ++inter;
            if (pa || pb) ++uni;
        }
    }
    return uni > 0 ? static_cast<float>(inter) / uni : 0.f;
}

double median_of(std::vector<int> v) {
    if (v.empty()) return 0.0;
    std::sort(v.begin(), v.end());
    return v[v.size() / 2];
}

}  // namespace

int measure_stroke_px(const Image& img, const RectI& box, RGBA ink) {
    RectI r = box.clip(img.width(), img.height());
    if (r.empty()) return 1;
    const Lab ink_lab = to_lab(ink);
    std::vector<int> runs;
    for (int y = r.y; y < r.bottom(); ++y) {
        int run = 0;
        for (int x = r.x; x < r.right(); ++x) {
            const float d = delta_e_lab(to_lab(img.at(x, y)), ink_lab);
            if (d < 24.f) {
                ++run;
            } else if (run > 0) {
                runs.push_back(run);
                run = 0;
            }
        }
        if (run > 0) runs.push_back(run);
    }
    if (runs.empty()) return 1;
    std::sort(runs.begin(), runs.end());
    // Horizontal scanlines cross stems and bowls. The 20th..50th percentile is a
    // stable estimate of the stem width without counting long horizontal bars.
    const size_t lo = runs.size() / 5;
    const size_t hi = std::max(lo + 1, runs.size() / 2);
    int sum = 0, n = 0;
    for (size_t i = lo; i < hi && i < runs.size(); ++i) {
        sum += runs[i];
        ++n;
    }
    if (n == 0) return 1;
    return std::clamp(static_cast<int>(std::lround(static_cast<double>(sum) / n)), 1, 12);
}

FontEstimate estimate_font(const Image& img, const TextRun& run) {
    FontEstimate e;
    RectI box = run.box.clip(img.width(), img.height());
    if (box.empty()) return e;

    const Bitmap bmp = ink_bitmap(img, box, run.color, 24.f);
    const int stroke = measure_stroke_px(img, box, run.color);

    // Cap height: the median glyph height is the best available proxy for the
    // distance from cap line to baseline on a mixed-case label.
    e.cap_height = std::max(3.f, static_cast<float>(run.median_glyph_height));
    e.size_px = std::clamp(e.cap_height / 0.71f, 7.f, 96.f);
    e.letter_spacing = 0;

    // Advance-width consistency is a direct monospace signal.
    float mono = run.monospaced ? 0.85f : 0.15f;
    if (run.glyphs >= 4) {
        mono += run.monospaced ? 0.1f : 0.f;
    }
    e.mono_score = std::clamp(mono, 0.f, 1.f);

    // Stroke contrast: serif faces modulate stem width far more than sans faces.
    std::vector<int> stem_widths;
    for (int x = 0; x < bmp.w; ++x) {
        int run_len = 0;
        for (int y = 0; y < bmp.h; ++y) {
            if (bmp.at(x, y) >= 110) {
                ++run_len;
            } else if (run_len > 0) {
                if (run_len >= 2) stem_widths.push_back(run_len);
                run_len = 0;
            }
        }
        if (run_len >= 2) stem_widths.push_back(run_len);
    }
    double cv = 0.0;
    if (stem_widths.size() >= 6) {
        double mean = 0;
        for (int s : stem_widths) mean += s;
        mean /= stem_widths.size();
        double var = 0;
        for (int s : stem_widths) var += (s - mean) * (s - mean);
        var /= stem_widths.size();
        cv = mean > 0 ? std::sqrt(var) / mean : 0.0;
    }
    e.serif_score = static_cast<float>(std::clamp((cv - 0.18) / 0.30, 0.0, 1.0));

    // Rounded faces keep more ink in the corners of their glyph boxes.
    double corner = 0, total = 1e-6;
    for (int y = 0; y < bmp.h; ++y)
        for (int x = 0; x < bmp.w; ++x) total += bmp.at(x, y);
    const int cw = std::max(1, bmp.w / 4), ch = std::max(1, bmp.h / 4);
    for (int y = 0; y < ch; ++y)
        for (int x = 0; x < cw; ++x) {
            corner += bmp.at(x, y) + bmp.at(bmp.w - 1 - x, y) + bmp.at(x, bmp.h - 1 - y) +
                      bmp.at(bmp.w - 1 - x, bmp.h - 1 - y);
        }
    const double corner_ratio = corner / (total * 4.0);
    e.round_score = static_cast<float>(std::clamp((corner_ratio - 0.25) / 0.45, 0.0, 1.0));

    // Slant: an italic face shifts the top centroid to the right of the bottom one.
    double top_x = 0, top_n = 0, bottom_x = 0, bottom_n = 0;
    for (int y = 0; y < bmp.h / 2; ++y)
        for (int x = 0; x < bmp.w; ++x)
            if (bmp.at(x, y) >= 110) {
                top_x += x;
                top_n += 1;
            }
    for (int y = bmp.h / 2; y < bmp.h; ++y)
        for (int x = 0; x < bmp.w; ++x)
            if (bmp.at(x, y) >= 110) {
                bottom_x += x;
                bottom_n += 1;
            }
    const double slant = (top_n > 0 && bottom_n > 0)
                             ? (top_x / top_n - bottom_x / bottom_n) / std::max(1.0, static_cast<double>(bmp.h))
                             : 0.0;
    e.italic = slant > 0.10;

    // Width ratio separates condensed from rounded or display cuts.
    const float width_ratio =
        run.median_glyph_height > 0 ? static_cast<float>(run.median_glyph_width) / run.median_glyph_height : 0.6f;

    FontFamily family = FontFamily::Sans;
    float best = 0.35f;
    if (e.mono_score > 0.6f) {
        family = FontFamily::Mono;
        best = e.mono_score;
    } else if (e.serif_score > 0.55f) {
        family = FontFamily::Serif;
        best = e.serif_score;
    } else if (width_ratio < 0.44f) {
        family = FontFamily::Condensed;
        best = 0.55f;
    } else if (e.size_px >= 26.f && run.glyphs <= 12) {
        family = FontFamily::Display;
        best = 0.6f;
    } else if (e.round_score > 0.62f && e.size_px >= 13.f) {
        family = FontFamily::Rounded;
        best = e.round_score * 0.8f;
    }
    e.family = family;

    const float ratio = stroke / std::max(1.f, e.cap_height);
    if (ratio < 0.072f) e.weight = 300;
    else if (ratio < 0.098f) e.weight = 400;
    else if (ratio < 0.125f) e.weight = 500;
    else if (ratio < 0.16f) e.weight = 600;
    else if (ratio < 0.21f) e.weight = 700;
    else e.weight = 800;

    const float clarity = std::min(1.f, run.glyphs / 8.f);
    e.confidence = std::clamp(0.35f + 0.45f * clarity + 0.2f * best, 0.f, 0.98f);

    char rationale[256];
    std::snprintf(rationale, sizeof(rationale),
                  "cap %.1fpx, stroke %dpx, stem CV %.2f, mono %.2f, corner %.2f, slant %.2f, %d glyphs",
                  e.cap_height, stroke, cv, e.mono_score, corner_ratio, slant, run.glyphs);
    e.rationale = rationale;
    return e;
}

std::vector<std::string> font_candidates(FontFamily family, int weight) {
    switch (family) {
        case FontFamily::Mono:
            return {"JetBrains Mono", "IBM Plex Mono", "Roboto Mono", "SF Mono", "Cascadia Mono", "Consolas"};
        case FontFamily::Serif:
            return {"Source Serif 4", "IBM Plex Serif", "Lora", "Georgia", "Times New Roman"};
        case FontFamily::Display:
            return {"Inter Display", "Satoshi", "General Sans", "Manrope", "SF Pro Display"};
        case FontFamily::Rounded:
            return {"Nunito", "Quicksand", "SF Pro Rounded", "M PLUS Rounded 1c"};
        case FontFamily::Condensed:
            return {"Roboto Condensed", "Barlow Condensed", "Archivo Narrow", "Oswald"};
        default:
            return weight >= 600
                       ? std::vector<std::string>{"Inter SemiBold", "SF Pro Text Semibold", "Segoe UI Semibold",
                                                  "Roboto Medium"}
                       : std::vector<std::string>{"Inter", "SF Pro Text", "Segoe UI", "Roboto", "Helvetica Neue"};
    }
}

const std::vector<IconTemplate>& icon_templates() {
    static const std::vector<IconTemplate> kTemplates = {
        {"search",
         "....####...|..##....##.|..#......#.|..#......#.|..#......#.|..##....##.|....####...|.......##..|........##.|.........##|..........."},
        {"close",
         "...........|..##...##..|...##.##...|....###....|....###....|...##.##...|..##...##..|...........|...........|...........|..........."},
        {"check",
         "...........|.........##|........##.|.......##..|..##..##...|..####.....|...##......|...........|...........|...........|..........."},
        {"chevron_down",
         "...........|...........|..#######..|...#####...|....###....|.....#.....|...........|...........|...........|...........|..........."},
        {"chevron_right",
         "...........|...........|....##.....|.....##....|......##...|.....##....|....##.....|...........|...........|...........|..........."},
        {"chevron_left",
         "...........|...........|.....##....|....##.....|...##......|....##.....|.....##....|...........|...........|...........|..........."},
        {"plus",
         "...........|.....#.....|.....#.....|.....#.....|..#######..|.....#.....|.....#.....|.....#.....|...........|...........|..........."},
        {"minus",
         "...........|...........|...........|...........|..#######..|...........|...........|...........|...........|...........|..........."},
        {"menu",
         "...........|...........|..#######..|...........|..#######..|...........|..#######..|...........|...........|...........|..........."},
        {"dots_vertical",
         "...........|....##.....|....##.....|...........|....##.....|....##.....|...........|....##.....|....##.....|...........|..........."},
        {"dots_horizontal",
         "...........|...........|...........|...........|.##..##..##|.##..##..##|...........|...........|...........|...........|..........."},
        {"user",
         "...........|....###....|...#####...|....###....|...........|..#######..|.#########.|.#########.|.#########.|...........|..........."},
        {"bell",
         "...........|....###....|...#####...|..#######..|..#######..|..#######..|.#########.|.....#.....|....###....|...........|..........."},
        {"home",
         "...........|.....#.....|....###....|...#####...|..#######..|.#########.|...#####...|...#...#...|...#...#...|...........|..........."},
        {"settings",
         ".....#.....|.##..#..##.|..#######..|...#####...|##.#####.##|...#####...|..#######..|.##..#..##.|.....#.....|...........|..........."},
        {"star",
         ".....#.....|....###....|....###....|###########|.#########.|..#######..|..#######..|.###...###.|..#.....#..|...........|..........."},
        {"heart",
         "...........|.##...##...|#########..|#########..|.#######...|..#####....|...###.....|....#......|...........|...........|..........."},
        {"play",
         "...........|..##.......|..####.....|..######...|..########.|..######...|..####.....|..##.......|...........|...........|..........."},
        {"arrow_right",
         "...........|...........|.....#.....|......#....|..#########|......#....|.....#.....|...........|...........|...........|..........."},
        {"arrow_left",
         "...........|...........|.....#.....|....#......|#########..|....#......|.....#.....|...........|...........|...........|..........."},
        {"trash",
         "...........|..#######..|.....#.....|..#######..|..#..#..#..|..#..#..#..|..#..#..#..|..#######..|...........|...........|..........."},
        {"filter",
         "...........|..#######..|...#####...|....###....|....###....|.....#.....|...........|...........|...........|...........|..........."},
        {"calendar",
         "...........|..##..##...|.#########.|.#########.|.#.#.#.#.#.|.#.#.#.#.#.|.#########.|...........|...........|...........|..........."},
        {"download",
         "...........|.....#.....|.....#.....|.....#.....|..#######..|...#####...|....###....|.....#.....|...........|...........|..........."},
        {"upload",
         "...........|.....#.....|....###....|...#####...|..#######..|.....#.....|.....#.....|.....#.....|...........|...........|..........."},
        {"edit",
         ".........##|........##.|.......##..|......##...|..##.##....|..####.....|...##......|...........|...........|...........|..........."},
        {"lock",
         "....###....|...#####...|....###....|..#######..|..#######..|..###.###..|..#######..|..#######..|...........|...........|..........."},
        {"info",
         "....###....|.....#.....|...........|...#####...|.....#.....|.....#.....|...#####...|...........|...........|...........|..........."},
    };
    return kTemplates;
}

IconMatch match_icon(const Image& img, const RectI& box, RGBA ink) {
    IconMatch best;
    if (box.empty()) return best;
    const int N = 12;
    const Bitmap bmp = downsample(ink_bitmap(img, box, ink, 26.f), N, N);
    int inked = 0;
    for (uint8_t v : bmp.v)
        if (v >= 110) ++inked;
    if (inked < 6) return best;

    for (const auto& t : icon_templates()) {
        const Bitmap temp = downsample(parse_template(t.art), N, N);
        for (int inv = 0; inv < 2; ++inv) {
            const float s = iou(bmp, temp, inv == 1);
            if (s > best.score) {
                best.score = s;
                best.name = t.name;
                best.inverted = inv == 1;
            }
        }
    }
    return best;
}

std::vector<RectI> icon_candidates(const std::vector<Component>& comps, int max_size) {
    std::vector<RectI> out;
    std::vector<RectI> small;
    for (const auto& c : comps) {
        if (c.bbox.w > max_size || c.bbox.h > max_size) continue;
        if (c.bbox.w < 4 || c.bbox.h < 4) continue;
        small.push_back(c.bbox);
    }
    std::vector<int> used(small.size(), 0);
    for (size_t i = 0; i < small.size(); ++i) {
        if (used[i]) continue;
        RectI box = small[i];
        used[i] = 1;
        bool grew = true;
        while (grew) {
            grew = false;
            for (size_t j = 0; j < small.size(); ++j) {
                if (used[j]) continue;
                const RectI& o = small[j];
                const int dx = std::max(0, std::max(box.x - o.right(), o.x - box.right()));
                const int dy = std::max(0, std::max(box.y - o.bottom(), o.y - box.bottom()));
                if (dx <= 2 && dy <= 2) {
                    const RectI merged{std::min(box.x, o.x), std::min(box.y, o.y),
                                       std::max(box.right(), o.right()) - std::min(box.x, o.x),
                                       std::max(box.bottom(), o.bottom()) - std::min(box.y, o.y)};
                    if (merged.w <= max_size && merged.h <= max_size) {
                        box = merged;
                        used[j] = 1;
                        grew = true;
                    }
                }
            }
        }
        if (box.w >= 6 && box.h >= 6) out.push_back(box);
    }
    return out;
}

}  // namespace pgr
