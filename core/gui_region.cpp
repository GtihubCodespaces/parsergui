// Automatic GUI region detection.
//
// A game screenshot with an injected ImGui overlay is mostly textured, saturated
// imagery; the overlay itself is a large, dark, neutral, locally flat rectangle
// full of small high-contrast glyphs. This finds that rectangle so the parser can
// work on the GUI instead of trying to model trees and characters.
#include "detect.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <vector>

namespace pgr {
namespace {

bool flat_dark(const Image& img, int x, int y) {
    const RGBA c = img.at(x, y);
    const int mx = std::max({c.r, c.g, c.b});
    const int mn = std::min({c.r, c.g, c.b});
    const int luma = (c.r * 2126 + c.g * 7152 + c.b * 722) / 10000;
    if (mx - mn > 34 || luma > 96) return false;
    // Flatness: a UI panel does not change colour over a few pixels. Sample two
    // neighbours: anti-aliased glyph edges are thin, so 4px away is panel again.
    const RGBA a = img.at_clamped(x + 4, y);
    const RGBA b = img.at_clamped(x, y + 4);
    const int da = std::abs(static_cast<int>(a.r) - c.r) + std::abs(static_cast<int>(a.g) - c.g) +
                   std::abs(static_cast<int>(a.b) - c.b);
    const int db = std::abs(static_cast<int>(b.r) - c.r) + std::abs(static_cast<int>(b.g) - c.g) +
                   std::abs(static_cast<int>(b.b) - c.b);
    return da <= 54 && db <= 54;
}

struct Box {
    RectI rect;
    int area = 0;
};

}  // namespace

GuiRegion find_gui_region(const Image& source) {
    GuiRegion result;
    if (source.empty() || source.width() < 160 || source.height() < 120) return result;
    Image img = source;
    if (img.at(0, 0).a < 250) img = img.with_alpha_over(RGBA(255, 255, 255));

    const int W = img.width();
    const int H = img.height();
    const size_t n = static_cast<size_t>(W) * H;

    // Upscale-invariant trick: work on at most 480px wide so a 4K screenshot costs
    // the same as a 1080p one. Box coordinates are scaled back at the end.
    const int step = std::max(1, W / 480);
    const int sw = (W + step - 1) / step;
    const int sh = (H + step - 1) / step;

    std::vector<uint8_t> mask(static_cast<size_t>(sw) * sh, 0);
    for (int sy = 0; sy < sh; ++sy)
        for (int sx = 0; sx < sw; ++sx)
            if (flat_dark(img, std::min(W - 1, sx * step), std::min(H - 1, sy * step)))
                mask[static_cast<size_t>(sy) * sw + sx] = 1;

    // Connected components on the mask (4-neighbour, iterative).
    std::vector<int> label(static_cast<size_t>(sw) * sh, -1);
    std::vector<Box> boxes;
    std::vector<RectI> all_rects;  // full-resolution boxes, for merging split panels
    std::vector<int> stack;
    for (int sy = 0; sy < sh; ++sy) {
        for (int sx = 0; sx < sw; ++sx) {
            const size_t start = static_cast<size_t>(sy) * sw + sx;
            if (!mask[start] || label[start] >= 0) continue;
            const int id = static_cast<int>(boxes.size());
            Box box{RectI{sx, sy, 1, 1}, 0};
            stack.clear();
            stack.push_back(static_cast<int>(start));
            label[start] = id;
            while (!stack.empty()) {
                const int p = stack.back();
                stack.pop_back();
                const int x = p % sw;
                const int y = p / sw;
                ++box.area;
                box.rect.x = std::min(box.rect.x, x);
                box.rect.y = std::min(box.rect.y, y);
                box.rect.w = std::max(box.rect.w, x + 1 - box.rect.x);
                box.rect.h = std::max(box.rect.h, y + 1 - box.rect.y);
                const int nx[4] = {x - 1, x + 1, x, x};
                const int ny[4] = {y, y, y - 1, y + 1};
                for (int k = 0; k < 4; ++k) {
                    if (nx[k] < 0 || ny[k] < 0 || nx[k] >= sw || ny[k] >= sh) continue;
                    const size_t q = static_cast<size_t>(ny[k]) * sw + nx[k];
                    if (!mask[q] || label[q] >= 0) continue;
                    label[q] = id;
                    stack.push_back(static_cast<int>(q));
                }
            }
            boxes.push_back(box);
        }
    }

    for (const Box& box : boxes)
        all_rects.push_back(RectI{box.rect.x * step, box.rect.y * step, box.rect.w * step, box.rect.h * step});

    const double image_area = static_cast<double>(W) * H;
    double best = 0.0;
    RectI best_rect;
    for (const Box& box : boxes) {
        const double bbox_area = static_cast<double>(box.rect.w) * box.rect.h * step * step;
        const double area_frac = bbox_area / image_area;
        if (area_frac < 0.015 || area_frac > 0.72) continue;
        const double fill = static_cast<double>(box.area) /
                            std::max(1.0, static_cast<double>(box.rect.w) * box.rect.h);
        if (fill < 0.62) continue;

        const RectI r{box.rect.x * step, box.rect.y * step, box.rect.w * step, box.rect.h * step};
        if (r.w < W * 0.16 || r.h < H * 0.12) continue;

        // Widget evidence: a panel without glyphs is just a dark hole, so require
        // both edge density and bright-on-dark pixels inside the box.
        int edges = 0, bright = 0, samples = 0;
        for (int y = r.y + 3; y < r.bottom() - 3; y += 2 * step) {
            for (int x = r.x + 3; x < r.right() - 3; x += 2 * step) {
                ++samples;
                // luma_at is normalised 0..1; work in 0..255 for readable thresholds.
                const int l = static_cast<int>(img.luma_at(x, y) * 255.f);
                const int lx = static_cast<int>(img.luma_at(std::min(W - 1, x + step), y) * 255.f);
                const int ly = static_cast<int>(img.luma_at(x, std::min(H - 1, y + step)) * 255.f);
                if (std::abs(l - lx) > 22 || std::abs(l - ly) > 22) ++edges;
                if (l > 90) ++bright;
            }
        }
        if (samples < 64) continue;
        const double density = static_cast<double>(edges) / samples;
        const double bright_ratio = static_cast<double>(bright) / samples;
        if (density < 0.010 || bright_ratio < 0.004) continue;

        // Straight edges: a genuine window has four mostly-straight sides.
        int straight = 0, sides = 0;
        for (int side = 0; side < 4; ++side) {
            int hits = 0;
            const int probes = 48;
            for (int i = 1; i < probes; ++i) {
                int x = r.x + r.w * i / probes;
                int y = r.y + r.h * i / probes;
                if (side == 0) y = r.y + 2;
                if (side == 1) y = r.bottom() - 3;
                if (side == 2) x = r.x + 2;
                if (side == 3) x = r.right() - 3;
                const int sx = std::min(sw - 1, x / step);
                const int sy = std::min(sh - 1, y / step);
                if (mask[static_cast<size_t>(sy) * sw + sx]) ++hits;
            }
            if (hits > probes / 2) ++straight;
            ++sides;
        }
        const double straightness = static_cast<double>(straight) / sides;

        const double area_score = std::clamp((area_frac - 0.015) / 0.30, 0.0, 1.0);
        const double edge_score = std::clamp(density / 0.05, 0.0, 1.0);
        const double score = 0.40 * edge_score + 0.24 * fill + 0.20 * straightness + 0.16 * area_score;
        if (score > best) {
            best = score;
            best_rect = r;
        }
    }

    if (best < 0.42) return result;

    // A 1px divider (footer rule, tab underline) can split a window into two
    // dark components. Merge every fragment aligned with the winner so the
    // result covers the whole window, not just its main body.
    RectI merged = best_rect;
    for (int pass = 0; pass < 3; ++pass) {
        bool grew = false;
        for (const RectI& r : all_rects) {
            if (r.area() < 800) continue;
            const RectI near = merged.inflate(10);
            const int ox = std::min(merged.right(), r.right()) - std::max(merged.x, r.x);
            if (ox < std::min(merged.w, r.w) / 2) continue;
            const bool adjacent = r.y <= merged.bottom() + 12 && r.bottom() + 12 >= merged.y;
            if (!near.contains(r) && !merged.intersect(r).area() && !adjacent) continue;
            const int nx = std::min(merged.x, r.x);
            const int ny = std::min(merged.y, r.y);
            const int nr = std::max(merged.right(), r.right());
            const int nb = std::max(merged.bottom(), r.bottom());
            const RectI u{nx, ny, nr - nx, nb - ny};
            if (u.area() > merged.area()) {
                merged = u;
                grew = true;
            }
        }
        if (!grew) break;
    }

    // The mask stops at the rounded border and any 1px frame; include them.
    result.rect = merged.inflate(3).clip(W, H);
    result.confidence = static_cast<float>(std::clamp(best, 0.0, 1.0));
    result.reason = "dark neutral panel with widget density";
    (void)n;
    return result;
}

}  // namespace pgr
