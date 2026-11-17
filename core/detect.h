// Screen detection: turns pixels into a measured element tree.
#pragma once

#include <string>
#include <vector>

#include "image.h"
#include "types.h"

namespace pgr {

struct DetectDebug {
    int components = 0;
    int text_runs = 0;
    int surface_candidates = 0;
    int merged = 0;
    std::vector<std::string> log;
};

Screen detect_screen(const Image& img, const AnalysisOptions& options, const std::string& name,
                     DetectDebug* debug = nullptr);

// Measures one rectangle against its surroundings. Exposed because the agent
// tools use it to verify a patch against a screenshot.
int measure_radius(const Image& img, const RectI& rect, RGBA fill, RGBA outside, int inset = 0);
// Grows `rect` outward to include any uniform 1px stroke found around the fill.
BorderInfo measure_border(const Image& img, RectI& rect, RGBA fill, RGBA outside);
bool measure_gradient(const Image& img, const RectI& rect, RGBA fill, FillInfo* out);
ShadowInfo measure_shadow(const Image& img, const RectI& rect, RGBA outside);

// Base spacing unit from measured gaps, paddings and sizes. Returns confidence 0..100.
int infer_base_unit(const std::vector<int>& measurements, int* confidence);

// Multi-frame analysis: finds properties that change between frames and fits an
// approximate easing curve. Durations need timestamps in the file names.
SequenceInfo analyze_sequence(const std::vector<Image>& frames, const std::vector<std::string>& names);

}  // namespace pgr
