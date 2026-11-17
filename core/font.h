// Typography and icon recognition from pixels. Font *family* is a scored guess
// from stroke statistics; size and weight are direct measurements.
#pragma once

#include <string>
#include <vector>

#include "image.h"
#include "segment.h"
#include "types.h"

namespace pgr {

struct FontEstimate {
    FontFamily family = FontFamily::Sans;
    float size_px = 13.f;
    int weight = 400;
    bool italic = false;
    int letter_spacing = 0;
    float cap_height = 0.f;
    float x_height = 0.f;
    float mono_score = 0.f;
    float serif_score = 0.f;
    float round_score = 0.f;
    float confidence = 0.f;
    std::string rationale;
};

FontEstimate estimate_font(const Image& img, const TextRun& run);

// Candidate faces that match a family and size on each platform. The emitter
// writes these as a commented block so the generated code builds without
// shipping font binaries.
std::vector<std::string> font_candidates(FontFamily family, int weight);

struct IconTemplate {
    const char* name;
    const char* art;  // rows separated by '|', '#' inked, '.' empty
};

const std::vector<IconTemplate>& icon_templates();

struct IconMatch {
    std::string name;
    float score = 0.f;
    bool inverted = false;
};

// Matches the ink distribution inside the box against every template.
IconMatch match_icon(const Image& img, const RectI& box, RGBA ink);

// Groups nearby small components into plausible icon boxes.
std::vector<RectI> icon_candidates(const std::vector<Component>& comps, int max_size);

// Estimate of glyph stroke width in pixels, robust to anti-aliasing.
int measure_stroke_px(const Image& img, const RectI& box, RGBA ink);

}  // namespace pgr
