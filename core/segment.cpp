#include "segment.h"

#include <algorithm>
#include <functional>
#include <map>
#include <numeric>
#include <unordered_map>

#include "color.h"

namespace pgr {
namespace {

int find_root(std::vector<int>& parent, int i) {
    while (parent[i] != i) {
        parent[i] = parent[parent[i]];
        i = parent[i];
    }
    return i;
}

void unite(std::vector<int>& parent, int a, int b) {
    a = find_root(parent, a);
    b = find_root(parent, b);
    if (a != b) parent[b] = a;
}

}  // namespace

LabelMap build_label_map(const Image& img, int max_colors) {
    std::vector<RGBA> palette;
    for (const auto& c : quantize(img, std::max(2, max_colors))) palette.push_back(c.color);
    return build_label_map(img, palette);
}

LabelMap build_label_map(const Image& img, const std::vector<RGBA>& palette_in) {
    LabelMap map;
    if (img.empty()) return map;
    map.width = img.width();
    map.height = img.height();
    map.labels.assign(static_cast<size_t>(map.width) * map.height, 0);
    map.palette = palette_in;
    if (map.palette.size() > 65534) map.palette.resize(65534);

    // Cache Lab centres for the assignment pass.
    std::vector<Lab> centers;
    centers.reserve(map.palette.size());
    for (const auto& c : map.palette) centers.push_back(to_lab(c));

    // UI screenshots repeat the same few hundred RGB values, so a per-colour cache
    // removes almost every Lab conversion in this loop.
    std::unordered_map<uint32_t, uint16_t> lookup;
    lookup.reserve(4096);
    for (int y = 0; y < map.height; ++y) {
        for (int x = 0; x < map.width; ++x) {
            const RGBA c = img.at(x, y);
            uint16_t* slot = &map.labels[static_cast<size_t>(y) * map.width + x];
            if (c.a < 8) {
                *slot = 0;
                continue;
            }
            const uint32_t key = (uint32_t(c.r) << 16) | (uint32_t(c.g) << 8) | c.b;
            auto it = lookup.find(key);
            if (it != lookup.end()) {
                *slot = it->second;
                continue;
            }
            const Lab lab = to_lab(c);
            int best = 0;
            float best_d = 1e30f;
            for (size_t k = 0; k < centers.size(); ++k) {
                const float d = delta_e_lab(lab, centers[k]);
                if (d < best_d) {
                    best_d = d;
                    best = static_cast<int>(k);
                }
            }
            const uint16_t label = static_cast<uint16_t>(best + 1);
            lookup.emplace(key, label);
            *slot = label;
        }
    }
    return map;
}

std::vector<Component> components(const LabelMap& map, int min_area, int max_components) {
    std::vector<Component> out;
    if (map.labels.empty()) return out;
    const int w = map.width, h = map.height;

    // One pass over the whole image, union-find over label runs.
    std::vector<int> run_label;  // palette label per run
    std::vector<int> run_row, run_x0, run_x1;
    std::vector<int> parent;
    run_label.reserve(40000);
    run_row.reserve(40000);
    run_x0.reserve(40000);
    run_x1.reserve(40000);
    parent.reserve(40000);

    std::vector<int> prev_runs;
    for (int y = 0; y < h; ++y) {
        std::vector<int> cur_runs;
        int x = 0;
        while (x < w) {
            const uint16_t l = map.labels[static_cast<size_t>(y) * w + x];
            int x1 = x + 1;
            while (x1 < w && map.labels[static_cast<size_t>(y) * w + x1] == l) ++x1;
            if (l != 0) {
                const int id = static_cast<int>(parent.size());
                parent.push_back(id);
                run_label.push_back(l);
                run_row.push_back(y);
                run_x0.push_back(x);
                run_x1.push_back(x1 - 1);
                cur_runs.push_back(id);
                // 8-connectivity against the previous row (diagonals included).
                for (int pid : prev_runs) {
                    if (run_label[pid] != l) continue;
                    if (run_x1[pid] >= x - 1 && run_x0[pid] <= x1) unite(parent, id, pid);
                }
            }
            x = x1;
        }
        prev_runs.swap(cur_runs);
    }

    std::unordered_map<int, int> slot;
    std::vector<Component> comps;
    for (size_t i = 0; i < parent.size(); ++i) {
        const int root = find_root(parent, static_cast<int>(i));
        auto it = slot.find(root);
        int idx;
        if (it == slot.end()) {
            idx = static_cast<int>(comps.size());
            slot.emplace(root, idx);
            comps.push_back(Component{});
            comps[idx].label = static_cast<uint16_t>(run_label[i]);
            comps[idx].color = map.palette[run_label[i] - 1];
            comps[idx].bbox = RectI{run_x0[i], run_row[i], run_x1[i] - run_x0[i] + 1, 1};
        } else {
            idx = it->second;
        }
        Component& c = comps[idx];
        const int x0 = run_x0[i], x1 = run_x1[i], yy = run_row[i];
        const int nx0 = std::min(c.bbox.x, x0);
        const int ny0 = std::min(c.bbox.y, yy);
        const int nx1 = std::max(c.bbox.right() - 1, x1);
        const int ny1 = std::max(c.bbox.bottom() - 1, yy);
        c.bbox = RectI{nx0, ny0, nx1 - nx0 + 1, ny1 - ny0 + 1};
        c.area += (x1 - x0 + 1);
        c.runs.push_back(RectI{x0, yy, x1 - x0 + 1, 1});
    }

    out.reserve(comps.size());
    for (auto& c : comps) {
        if (c.area < min_area) continue;
        // Remove runs that are out of the final bbox (can happen with merged diagonals).
        out.push_back(std::move(c));
        if (static_cast<int>(out.size()) >= max_components) break;
    }
    std::sort(out.begin(), out.end(), [](const Component& a, const Component& b) { return a.area > b.area; });
    return out;
}

EdgeMap sobel(const Image& img, bool on_alpha) {
    EdgeMap map;
    if (img.empty()) return map;
    map.width = img.width();
    map.height = img.height();
    map.magnitude.assign(static_cast<size_t>(map.width) * map.height, 0);
    auto grey = [&](int x, int y) {
        RGBA c = img.at_clamped(x, y);
        if (on_alpha) return static_cast<float>(c.a);
        return 0.2126f * c.r + 0.7152f * c.g + 0.0722f * c.b;
    };
    for (int y = 0; y < map.height; ++y) {
        for (int x = 0; x < map.width; ++x) {
            const float tl = grey(x - 1, y - 1), t = grey(x, y - 1), tr = grey(x + 1, y - 1);
            const float l = grey(x - 1, y), r = grey(x + 1, y);
            const float bl = grey(x - 1, y + 1), bo = grey(x, y + 1), br = grey(x + 1, y + 1);
            const float gx = (tr + 2 * r + br) - (tl + 2 * l + bl);
            const float gy = (bl + 2 * bo + br) - (tl + 2 * t + tr);
            const float m = std::sqrt(gx * gx + gy * gy);
            map.magnitude[static_cast<size_t>(y) * map.width + x] = static_cast<uint8_t>(std::min(255.f, m));
        }
    }
    return map;
}

std::vector<uint8_t> difference_mask(const Image& a, const Image& b, float threshold) {
    std::vector<uint8_t> mask;
    if (a.width() != b.width() || a.height() != b.height()) return mask;
    mask.assign(static_cast<size_t>(a.width()) * a.height(), 0);
    for (int y = 0; y < a.height(); ++y) {
        for (int x = 0; x < a.width(); ++x) {
            const RGBA ca = a.at(x, y), cb = b.at(x, y);
            const float d = delta_e(ca, cb);
            mask[static_cast<size_t>(y) * a.width() + x] = d > threshold ? 255 : 0;
        }
    }
    return mask;
}

std::vector<TextRun> merge_text_runs(const std::vector<Component>& comps, int max_glyph_height, int max_gap) {
    struct Item {
        RectI box;
        RGBA color;
        uint16_t label;
        int glyph_w;
    };
    std::vector<Item> items;
    for (const auto& c : comps) {
        if (c.bbox.h > max_glyph_height) continue;
        if (c.bbox.w > max_glyph_height * 3) continue;
        if (c.area < 3) continue;
        items.push_back(Item{c.bbox, c.color, c.label, c.bbox.w});
    }
    // Group by label first: glyphs of one text run share a quantised colour.
    std::map<uint16_t, std::vector<int>> by_label;
    for (size_t i = 0; i < items.size(); ++i) by_label[items[i].label].push_back(static_cast<int>(i));

    std::vector<TextRun> runs;
    std::vector<int> used(items.size(), 0);
    for (auto& kv : by_label) {
        auto& ids = kv.second;
        std::sort(ids.begin(), ids.end(), [&](int a, int b) {
            if (items[a].box.y != items[b].box.y) return items[a].box.y < items[b].box.y;
            return items[a].box.x < items[b].box.x;
        });
        for (size_t i = 0; i < ids.size(); ++i) {
            if (used[ids[i]]) continue;
            int idx = ids[i];
            used[idx] = 1;
            RectI box = items[idx].box;
            std::vector<int> glyph_heights{items[idx].box.h};
            std::vector<int> glyph_widths{items[idx].box.w};
            int glyphs = 1;
            bool grew = true;
            while (grew) {
                grew = false;
                for (size_t j = 0; j < ids.size(); ++j) {
                    const int cand = ids[j];
                    if (used[cand]) continue;
                    const RectI& cb = items[cand].box;
                    const int v_overlap = std::min(box.bottom(), cb.bottom()) - std::max(box.y, cb.y);
                    const int min_h = std::min(box.h, cb.h);
                    if (min_h <= 0) continue;
                    const bool same_line = v_overlap > min_h * 0.55;
                    if (!same_line) continue;
                    const int gap = cb.x > box.right() ? cb.x - box.right() : (box.x > cb.right() ? box.x - cb.right() : 0);
                    const int limit = std::max(max_gap, static_cast<int>(box.h * 1.4f));
                    if (gap <= limit) {
                        box = RectI{std::min(box.x, cb.x), std::min(box.y, cb.y),
                                    std::max(box.right(), cb.right()) - std::min(box.x, cb.x),
                                    std::max(box.bottom(), cb.bottom()) - std::min(box.y, cb.y)};
                        glyph_heights.push_back(cb.h);
                        glyph_widths.push_back(cb.w);
                        ++glyphs;
                        used[cand] = 1;
                        grew = true;
                    }
                }
            }
            if (glyphs < 2) continue;  // a lone glyph is an icon candidate, not a line of text
            std::sort(glyph_heights.begin(), glyph_heights.end());
            std::sort(glyph_widths.begin(), glyph_widths.end());
            TextRun run;
            run.box = box;
            run.color = items[idx].color;
            run.label = items[idx].label;
            run.glyphs = glyphs;
            run.median_glyph_height = glyph_heights[glyph_heights.size() / 2];
            run.median_glyph_width = glyph_widths[glyph_widths.size() / 2];
            // Monospace estimate: width variation stays inside one pixel.
            const int wmin = glyph_widths.front(), wmax = glyph_widths.back();
            run.monospaced = (wmax - wmin) <= std::max(1, run.median_glyph_width / 3);
            runs.push_back(run);
        }
    }
    // Remove obvious box-like false positives: text is much wider than tall.
    std::vector<TextRun> filtered;
    for (auto& r : runs) {
        if (r.box.w < r.box.h) continue;
        if (r.box.area() > 0) filtered.push_back(r);
    }
    std::sort(filtered.begin(), filtered.end(), [](const TextRun& a, const TextRun& b) {
        if (a.box.y != b.box.y) return a.box.y < b.box.y;
        return a.box.x < b.box.x;
    });
    return filtered;
}

Projection project(const RectI& region, const std::function<bool(int, int)>& match) {
    Projection p;
    if (region.empty()) return p;
    p.row_counts.assign(region.h, 0);
    p.col_counts.assign(region.w, 0);
    for (int y = region.y; y < region.bottom(); ++y) {
        for (int x = region.x; x < region.right(); ++x) {
            if (match(x, y)) {
                ++p.row_counts[y - region.y];
                ++p.col_counts[x - region.x];
            }
        }
    }
    return p;
}

}  // namespace pgr
