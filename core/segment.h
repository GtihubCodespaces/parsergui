// Segmentation primitives: palette labelling, edge maps, connected components on
// run lengths, and glyph-sized component merging into text lines.
#pragma once

#include <cstdint>
#include <functional>
#include <vector>

#include "image.h"
#include "types.h"

namespace pgr {

struct LabelMap {
    int width = 0, height = 0;
    std::vector<uint16_t> labels;  // 0 = unassigned, otherwise palette index + 1
    std::vector<RGBA> palette;

    uint16_t label_at(int x, int y) const {
        if (x < 0 || y < 0 || x >= width || y >= height) return 0;
        return labels[static_cast<size_t>(y) * width + x];
    }
    RGBA color_at(int x, int y) const {
        const uint16_t l = label_at(x, y);
        return l > 0 && l <= palette.size() ? palette[l - 1] : RGBA(0, 0, 0, 0);
    }
};

LabelMap build_label_map(const Image& img, int max_colors);
LabelMap build_label_map(const Image& img, const std::vector<RGBA>& palette);

struct Component {
    RectI bbox;
    int area = 0;
    uint16_t label = 0;
    RGBA color;
    std::vector<RectI> runs;  // horizontal spans, useful for coverage tests
};

// 8-connected components, one pass per label. Components smaller than min_area
// are dropped (anti-aliasing halos, dust).
std::vector<Component> components(const LabelMap& map, int min_area, int max_components = 20000);

struct EdgeMap {
    int width = 0, height = 0;
    std::vector<uint8_t> magnitude;
    uint8_t at(int x, int y) const {
        if (x < 0 || y < 0 || x >= width || y >= height) return 0;
        return magnitude[static_cast<size_t>(y) * width + x];
    }
};
EdgeMap sobel(const Image& img, bool on_alpha = false);
std::vector<uint8_t> difference_mask(const Image& a, const Image& b, float threshold);

// Merges small, similarly coloured boxes that sit on the same text line.
struct TextRun {
    RectI box;
    RGBA color;
    uint16_t label = 0;
    int glyphs = 0;
    int median_glyph_height = 0;
    int median_glyph_width = 0;
    int stroke_px = 1;
    bool monospaced = false;
};
std::vector<TextRun> merge_text_runs(const std::vector<Component>& comps, int max_glyph_height, int max_gap);

// Rows and columns that are fully covered by pixels matching the predicate.
struct Projection {
    std::vector<int> row_counts;
    std::vector<int> col_counts;
};
Projection project(const RectI& region, const std::function<bool(int, int)>& match);

}  // namespace pgr
