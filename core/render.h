// Re-renders the measured tree with the software rasteriser and compares it to
// the source screenshot. This is how the fidelity number is produced: it is a
// measurement, not an estimate.
#pragma once

#include <string>

#include "image.h"
#include "types.h"

namespace pgr {

struct Similarity {
    float mae = 0.f;        // mean absolute error, 0..255
    float psnr = 0.f;       // dB
    float ssim = 0.f;       // 0..1
    float fidelity = 0.f;   // 0..100
    float off_pixels = 0.f; // share of pixels with deltaE > 10
    int compared = 0;
};

struct RenderOptions {
    bool carry_text_pixels = true;   // reuse source glyphs so OCR does not affect geometry score
    bool draw_icons = true;
    bool draw_shadows = true;
    bool draw_unknown = true;
};

Image render_reconstruction(const Screen& screen, const Image* source, const RenderOptions& options);

Similarity compare_images(const Image& a, const Image& b);
Image make_diff_image(const Image& a, const Image& b, int amplify = 3);

// Heat map of per-element error, used by the UI to point at the worst regions.
std::string fidelity_summary(const Similarity& s);

}  // namespace pgr
