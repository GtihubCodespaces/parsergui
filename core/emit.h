// Code generation. Two targets from one measurement tree: semantic ImGui widgets
// (editable) and raw ImDrawList primitives (pixel-accurate).
#pragma once

#include <string>

#include "json.h"
#include "types.h"

namespace pgr {

struct EmitOptions {
    enum class Mode { Semantic, DrawList, Both };
    Mode mode = Mode::Both;
    bool include_fonts = true;
    bool include_animations = true;
    bool include_palette = true;
    bool comment_measurements = true;
    int max_elements = 4000;
    std::string function_prefix = "ParsedUI";
    std::string source_name = "screenshot.png";
};

std::string emit_imgui(const Screen& screen, const EmitOptions& options);
std::string emit_stub_main(const Screen& screen, const EmitOptions& options);
Json screen_json(const Screen& screen);
std::string emit_json(const Screen& screen);
std::string emit_markdown(const Screen& screen);
std::string emit_layout_txt(const Screen& screen);

// Icon primitives reused by the emitter and by the preview renderer.
std::string icon_draw_code(const std::string& name, bool& known);

}  // namespace pgr
