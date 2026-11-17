// Orchestrates detection, reconstruction, verification and reporting.
#pragma once

#include <string>
#include <vector>

#include "image.h"
#include "types.h"

namespace pgr {

struct ParseResult {
    Screen screen;
    Image reconstruction;
    Image diff;
    float fidelity = 0.f;
    float typography = 0.f;
    std::string error;
};

ParseResult parse_screenshot(const Image& image, const AnalysisOptions& options, const std::string& name);
ParseResult parse_frames(const std::vector<Image>& frames, const std::vector<std::string>& names,
                         const AnalysisOptions& options);

// Serialises a job result next to its artefacts and returns the directory.
bool write_artifacts(const ParseResult& result, const std::string& directory, std::string* error);

}  // namespace pgr
