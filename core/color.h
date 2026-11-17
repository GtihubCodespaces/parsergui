// Colour science helpers: sRGB transfer, CIELAB, median-cut quantisation and
// palette role assignment. Distances are computed in Lab so thresholds behave
// the same on dark and light themes.
#pragma once

#include <vector>

#include "types.h"

namespace pgr {

class Image;

struct Lab {
    float l = 0, a = 0, b = 0;
};

Lab to_lab(RGBA c);
RGBA from_lab(Lab lab);
RGBA from_lab(Lab lab, uint8_t alpha);
float delta_e(RGBA a, RGBA b);
float delta_e_lab(const Lab& a, const Lab& b);
float luminance(RGBA c);
float contrast_ratio(RGBA a, RGBA b);
RGBA mix(RGBA a, RGBA b, float t);
RGBA with_alpha(RGBA c, uint8_t a);
float saturation(RGBA c);
RGBA relative_shift(RGBA c, float amount);
RGBA readable_on(RGBA bg);

// Median cut returns at most max_colors entries sorted by pixel count.
struct ColorCount {
    RGBA color;
    int count = 0;
};
std::vector<ColorCount> quantize(const Image& img, int max_colors, const RectI& region = RectI{});
void refine_kmeans(const Image& img, std::vector<ColorCount>& colors, int iterations = 4);

// Returns the most frequent colour inside the region, ignoring fully transparent pixels.
RGBA dominant_color(const Image& img, const RectI& region, int* out_count = nullptr);

// Most frequent colour inside the region that is visibly different from the
// background. This is what text and icon ink looks like.
RGBA dominant_ink(const Image& img, const RectI& region, RGBA background, float min_delta = 8.f);

std::vector<PaletteEntry> build_palette(const Image& img, int max_colors = 24);

// True when two colours are indistinguishable for a human looking at a screenshot.
inline bool near_color(RGBA a, RGBA b, float tolerance = 3.0f) { return delta_e(a, b) <= tolerance; }

std::string hex_string(RGBA c);

}  // namespace pgr
