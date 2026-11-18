#include "emit.h"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdio>
#include <functional>
#include <sstream>

#include "color.h"
#include "font.h"
#include "json.h"

namespace pgr {
namespace {

// Emits a valid C++ float literal: always a decimal point, never a bare "4f".
std::string f(float v) {
    char buf[32];
    if (std::fabs(v - std::round(v)) < 0.001f)
        std::snprintf(buf, sizeof(buf), "%.0f", v);
    else
        std::snprintf(buf, sizeof(buf), "%.2f", v);
    std::string s(buf);
    if (s.find('.') == std::string::npos) s += ".0";
    return s;
}

std::string u32(RGBA c) {
    char buf[64];
    std::snprintf(buf, sizeof(buf), "IM_COL32(%d,%d,%d,%d)", c.r, c.g, c.b, c.a);
    return buf;
}

std::string cpp_string(const std::string& s) {
    std::string out = "\"";
    for (char c : s) {
        switch (c) {
            case '"': out += "\\\""; break;
            case '\\': out += "\\\\"; break;
            case '\n': out += "\\n"; break;
            case '\t': out += "\\t"; break;
            default: out.push_back(c);
        }
    }
    out += "\"";
    return out;
}

std::string ident_prefix(const EmitOptions& opt) { return opt.function_prefix; }

struct Flat {
    const Element* e;
    int depth;
};

std::vector<Flat> flatten(const Screen& s) {
    std::vector<Flat> out;
    std::function<void(int, int)> walk = [&](int id, int depth) {
        if (id < 0 || id >= static_cast<int>(s.elements.size())) return;
        out.push_back(Flat{&s.elements[id], depth});
        for (int c : s.elements[id].children) walk(c, depth + 1);
    };
    if (s.root >= 0) walk(s.root, 0);
    std::sort(out.begin(), out.end(), [](const Flat& a, const Flat& b) { return a.e->z < b.e->z; });
    return out;
}

bool is_textual(ElementType t) {
    return t == ElementType::Label || t == ElementType::Heading || t == ElementType::Button ||
           t == ElementType::Input || t == ElementType::Tab || t == ElementType::Badge ||
           t == ElementType::MenuItem || t == ElementType::ListItem;
}

std::string role_color_name(const PaletteEntry& p, size_t index) {
    if (!p.role.empty()) {
        std::string name = p.role;
        name[0] = static_cast<char>(std::toupper(name[0]));
        return name;
    }
    return "Color" + std::to_string(index);
}

}  // namespace

std::string icon_draw_code(const std::string& name, bool& known) {
    known = true;
    const char* n = name.c_str();
    auto s = [&](const std::string& body) {
        return std::string("    else if (strcmp(name, \"") + n + "\") == 0) {\n" + body + "    }\n";
    };
    const std::string c = "c", r = "r", col = "col", th = "th";
    if (name == "search")
        return s("        dl->AddCircle(ImVec2(c.x - s * 0.06f, c.y - s * 0.06f), s * 0.30f, col, 16, th);\n"
                 "        dl->AddLine(ImVec2(c.x + s * 0.17f, c.y + s * 0.17f), ImVec2(c.x + s * 0.42f, c.y + s * 0.42f), col, th);\n");
    if (name == "close")
        return s("        dl->AddLine(ImVec2(c.x - s * 0.28f, c.y - s * 0.28f), ImVec2(c.x + s * 0.28f, c.y + s * 0.28f), col, th);\n"
                 "        dl->AddLine(ImVec2(c.x + s * 0.28f, c.y - s * 0.28f), ImVec2(c.x - s * 0.28f, c.y + s * 0.28f), col, th);\n");
    if (name == "check")
        return s("        const ImVec2 p[] = {ImVec2(c.x - s * 0.30f, c.y + s * 0.02f), ImVec2(c.x - s * 0.08f, c.y + s * 0.24f), ImVec2(c.x + s * 0.32f, c.y - s * 0.24f)};\n"
                 "        dl->AddPolyline(p, 3, col, 0, th);\n");
    if (name == "chevron_down")
        return s("        const ImVec2 p[] = {ImVec2(c.x - s * 0.26f, c.y - s * 0.12f), ImVec2(c.x, c.y + s * 0.14f), ImVec2(c.x + s * 0.26f, c.y - s * 0.12f)};\n"
                 "        dl->AddPolyline(p, 3, col, 0, th);\n");
    if (name == "chevron_right")
        return s("        const ImVec2 p[] = {ImVec2(c.x - s * 0.12f, c.y - s * 0.26f), ImVec2(c.x + s * 0.14f, c.y), ImVec2(c.x - s * 0.12f, c.y + s * 0.26f)};\n"
                 "        dl->AddPolyline(p, 3, col, 0, th);\n");
    if (name == "chevron_left")
        return s("        const ImVec2 p[] = {ImVec2(c.x + s * 0.12f, c.y - s * 0.26f), ImVec2(c.x - s * 0.14f, c.y), ImVec2(c.x + s * 0.12f, c.y + s * 0.26f)};\n"
                 "        dl->AddPolyline(p, 3, col, 0, th);\n");
    if (name == "chevron_up")
        return s("        const ImVec2 p[] = {ImVec2(c.x - s * 0.26f, c.y + s * 0.12f), ImVec2(c.x, c.y - s * 0.14f), ImVec2(c.x + s * 0.26f, c.y + s * 0.12f)};\n"
                 "        dl->AddPolyline(p, 3, col, 0, th);\n");
    if (name == "plus")
        return s("        dl->AddLine(ImVec2(c.x - s * 0.26f, c.y), ImVec2(c.x + s * 0.26f, c.y), col, th);\n"
                 "        dl->AddLine(ImVec2(c.x, c.y - s * 0.26f), ImVec2(c.x, c.y + s * 0.26f), col, th);\n");
    if (name == "minus")
        return s("        dl->AddLine(ImVec2(c.x - s * 0.26f, c.y), ImVec2(c.x + s * 0.26f, c.y), col, th);\n");
    if (name == "menu")
        return s("        for (int i = -1; i <= 1; ++i)\n            dl->AddLine(ImVec2(c.x - s * 0.28f, c.y + i * s * 0.20f), ImVec2(c.x + s * 0.28f, c.y + i * s * 0.20f), col, th);\n");
    if (name == "dots_vertical")
        return s("        for (int i = -1; i <= 1; ++i) dl->AddCircleFilled(ImVec2(c.x, c.y + i * s * 0.22f), th * 0.9f, col, 8);\n");
    if (name == "dots_horizontal")
        return s("        for (int i = -1; i <= 1; ++i) dl->AddCircleFilled(ImVec2(c.x + i * s * 0.22f, c.y), th * 0.9f, col, 8);\n");
    if (name == "user")
        return s("        dl->AddCircle(ImVec2(c.x, c.y - s * 0.16f), s * 0.17f, col, 16, th);\n"
                 "        dl->PathArcTo(ImVec2(c.x, c.y + s * 0.44f), s * 0.32f, 3.14159f, 6.28318f, 16);\n"
                 "        dl->PathStroke(col, 0, th);\n");
    if (name == "bell")
        return s("        dl->PathArcTo(ImVec2(c.x, c.y + s * 0.02f), s * 0.30f, 3.14159f, 6.28318f, 16);\n"
                 "        dl->PathStroke(col, 0, th);\n"
                 "        dl->AddLine(ImVec2(c.x - s * 0.30f, c.y + s * 0.02f), ImVec2(c.x - s * 0.30f, c.y + s * 0.22f), col, th);\n"
                 "        dl->AddLine(ImVec2(c.x + s * 0.30f, c.y + s * 0.02f), ImVec2(c.x + s * 0.30f, c.y + s * 0.22f), col, th);\n"
                 "        dl->AddLine(ImVec2(c.x - s * 0.38f, c.y + s * 0.22f), ImVec2(c.x + s * 0.38f, c.y + s * 0.22f), col, th);\n"
                 "        dl->AddCircleFilled(ImVec2(c.x, c.y + s * 0.30f), th * 0.8f, col, 8);\n");
    if (name == "home")
        return s("        const ImVec2 p[] = {ImVec2(c.x - s * 0.36f, c.y - s * 0.02f), ImVec2(c.x, c.y - s * 0.34f), ImVec2(c.x + s * 0.36f, c.y - s * 0.02f)};\n"
                 "        dl->AddPolyline(p, 3, col, 0, th);\n"
                 "        dl->AddRect(ImVec2(c.x - s * 0.26f, c.y - s * 0.02f), ImVec2(c.x + s * 0.26f, c.y + s * 0.34f), col, 1.0f, 0, th);\n");
    if (name == "settings")
        return s("        dl->AddCircle(ImVec2(c.x, c.y), s * 0.18f, col, 20, th);\n"
                 "        for (int i = 0; i < 6; ++i) {\n"
                 "            const float a = i * 3.14159f / 3.0f;\n"
                 "            dl->AddLine(ImVec2(c.x + cosf(a) * s * 0.24f, c.y + sinf(a) * s * 0.24f),\n"
                 "                        ImVec2(c.x + cosf(a) * s * 0.36f, c.y + sinf(a) * s * 0.36f), col, th);\n"
                 "        }\n");
    if (name == "star")
        return s("        ImVec2 p[10];\n"
                 "        for (int i = 0; i < 10; ++i) {\n"
                 "            const float a = -1.5708f + i * 3.14159f / 5.0f;\n"
                 "            const float rr = (i % 2 == 0) ? s * 0.36f : s * 0.16f;\n"
                 "            p[i] = ImVec2(c.x + cosf(a) * rr, c.y + sinf(a) * rr);\n"
                 "        }\n"
                 "        dl->AddPolyline(p, 10, col, ImDrawFlags_Closed, th);\n");
    if (name == "heart")
        return s("        dl->PathArcTo(ImVec2(c.x - s * 0.17f, c.y - s * 0.10f), s * 0.18f, 3.14159f, 6.28318f, 12);\n"
                 "        dl->PathStroke(col, 0, th);\n"
                 "        dl->PathArcTo(ImVec2(c.x + s * 0.17f, c.y - s * 0.10f), s * 0.18f, 3.14159f, 6.28318f, 12);\n"
                 "        dl->PathStroke(col, 0, th);\n"
                 "        dl->AddLine(ImVec2(c.x - s * 0.35f, c.y - s * 0.06f), ImVec2(c.x, c.y + s * 0.34f), col, th);\n"
                 "        dl->AddLine(ImVec2(c.x + s * 0.35f, c.y - s * 0.06f), ImVec2(c.x, c.y + s * 0.34f), col, th);\n");
    if (name == "play")
        return s("        const ImVec2 p[] = {ImVec2(c.x - s * 0.20f, c.y - s * 0.30f), ImVec2(c.x + s * 0.30f, c.y), ImVec2(c.x - s * 0.20f, c.y + s * 0.30f)};\n"
                 "        dl->AddPolyline(p, 3, col, ImDrawFlags_Closed, th);\n");
    if (name == "arrow_right")
        return s("        dl->AddLine(ImVec2(c.x - s * 0.32f, c.y), ImVec2(c.x + s * 0.30f, c.y), col, th);\n"
                 "        const ImVec2 p[] = {ImVec2(c.x + s * 0.08f, c.y - s * 0.20f), ImVec2(c.x + s * 0.30f, c.y), ImVec2(c.x + s * 0.08f, c.y + s * 0.20f)};\n"
                 "        dl->AddPolyline(p, 3, col, 0, th);\n");
    if (name == "arrow_left")
        return s("        dl->AddLine(ImVec2(c.x + s * 0.32f, c.y), ImVec2(c.x - s * 0.30f, c.y), col, th);\n"
                 "        const ImVec2 p[] = {ImVec2(c.x - s * 0.08f, c.y - s * 0.20f), ImVec2(c.x - s * 0.30f, c.y), ImVec2(c.x - s * 0.08f, c.y + s * 0.20f)};\n"
                 "        dl->AddPolyline(p, 3, col, 0, th);\n");
    if (name == "trash")
        return s("        dl->AddLine(ImVec2(c.x - s * 0.30f, c.y - s * 0.22f), ImVec2(c.x + s * 0.30f, c.y - s * 0.22f), col, th);\n"
                 "        dl->AddLine(ImVec2(c.x - s * 0.10f, c.y - s * 0.32f), ImVec2(c.x + s * 0.10f, c.y - s * 0.32f), col, th);\n"
                 "        dl->AddRect(ImVec2(c.x - s * 0.24f, c.y - s * 0.22f), ImVec2(c.x + s * 0.24f, c.y + s * 0.34f), col, 2.0f, 0, th);\n"
                 "        dl->AddLine(ImVec2(c.x - s * 0.08f, c.y - s * 0.12f), ImVec2(c.x - s * 0.08f, c.y + s * 0.24f), col, th);\n"
                 "        dl->AddLine(ImVec2(c.x + s * 0.08f, c.y - s * 0.12f), ImVec2(c.x + s * 0.08f, c.y + s * 0.24f), col, th);\n");
    if (name == "filter")
        return s("        const ImVec2 p[] = {ImVec2(c.x - s * 0.34f, c.y - s * 0.24f), ImVec2(c.x + s * 0.34f, c.y - s * 0.24f), ImVec2(c.x + s * 0.06f, c.y + s * 0.04f), ImVec2(c.x + s * 0.06f, c.y + s * 0.32f), ImVec2(c.x - s * 0.06f, c.y + s * 0.24f), ImVec2(c.x - s * 0.06f, c.y + s * 0.04f)};\n"
                 "        dl->AddPolyline(p, 6, col, ImDrawFlags_Closed, th);\n");
    if (name == "calendar")
        return s("        dl->AddRect(ImVec2(c.x - s * 0.32f, c.y - s * 0.26f), ImVec2(c.x + s * 0.32f, c.y + s * 0.32f), col, 2.0f, 0, th);\n"
                 "        dl->AddLine(ImVec2(c.x - s * 0.32f, c.y - s * 0.08f), ImVec2(c.x + s * 0.32f, c.y - s * 0.08f), col, th);\n"
                 "        dl->AddLine(ImVec2(c.x - s * 0.16f, c.y - s * 0.34f), ImVec2(c.x - s * 0.16f, c.y - s * 0.22f), col, th);\n"
                 "        dl->AddLine(ImVec2(c.x + s * 0.16f, c.y - s * 0.34f), ImVec2(c.x + s * 0.16f, c.y - s * 0.22f), col, th);\n");
    if (name == "download" || name == "upload")
        return s(std::string("        dl->AddLine(ImVec2(c.x, c.y - s * 0.30f), ImVec2(c.x, c.y + s * ") +
                 (name == "download" ? "0.14f" : "-0.14f") + "), col, th);\n" +
                 (name == "download"
                      ? "        const ImVec2 p[] = {ImVec2(c.x - s * 0.20f, c.y - s * 0.06f), ImVec2(c.x, c.y + s * 0.16f), ImVec2(c.x + s * 0.20f, c.y - s * 0.06f)};\n"
                      : "        const ImVec2 p[] = {ImVec2(c.x - s * 0.20f, c.y - s * 0.06f), ImVec2(c.x, c.y - s * 0.28f), ImVec2(c.x + s * 0.20f, c.y - s * 0.06f)};\n") +
                 "        dl->AddPolyline(p, 3, col, 0, th);\n"
                 "        dl->AddLine(ImVec2(c.x - s * 0.30f, c.y + s * 0.30f), ImVec2(c.x + s * 0.30f, c.y + s * 0.30f), col, th);\n");
    if (name == "edit")
        return s("        dl->AddLine(ImVec2(c.x - s * 0.28f, c.y + s * 0.28f), ImVec2(c.x + s * 0.24f, c.y - s * 0.24f), col, th);\n"
                 "        dl->AddLine(ImVec2(c.x + s * 0.10f, c.y - s * 0.34f), ImVec2(c.x + s * 0.32f, c.y - s * 0.12f), col, th);\n"
                 "        dl->AddLine(ImVec2(c.x - s * 0.32f, c.y + s * 0.32f), ImVec2(c.x - s * 0.16f, c.y + s * 0.28f), col, th);\n");
    if (name == "lock")
        return s("        dl->AddRect(ImVec2(c.x - s * 0.26f, c.y - s * 0.02f), ImVec2(c.x + s * 0.26f, c.y + s * 0.32f), col, 2.0f, 0, th);\n"
                 "        dl->PathArcTo(ImVec2(c.x, c.y - s * 0.04f), s * 0.17f, 3.14159f, 6.28318f, 12);\n"
                 "        dl->PathStroke(col, 0, th);\n");
    if (name == "info")
        return s("        dl->AddCircle(ImVec2(c.x, c.y), s * 0.32f, col, 20, th);\n"
                 "        dl->AddCircleFilled(ImVec2(c.x, c.y - s * 0.14f), th * 0.9f, col, 8);\n"
                 "        dl->AddLine(ImVec2(c.x, c.y - s * 0.02f), ImVec2(c.x, c.y + s * 0.18f), col, th);\n");
    known = false;
    return "";
}

std::string emit_imgui(const Screen& screen, const EmitOptions& opt) {
    std::ostringstream o;
    const std::string pre = ident_prefix(opt);
    const float dpr = screen.report.device_pixel_ratio > 0 ? static_cast<float>(screen.report.device_pixel_ratio) : 1.f;
    const float k = 1.f / dpr;
    const auto flat = flatten(screen);

    o << "// Generated by parsergui from " << opt.source_name << "\n";
    o << "// Source: " << screen.width << "x" << screen.height << " px, device pixel ratio "
      << screen.report.device_pixel_ratio << "\n";
    o << "// Elements: " << screen.report.elements << ", text runs: " << screen.report.text_runs
      << ", icons: " << screen.report.icons << "\n";
    o << "// Grid: " << screen.metrics.base_unit << " px base unit (" << screen.metrics.grid_confidence
      << "% of measured spacings), margins " << screen.metrics.margin << " px\n";
    o << "// Fidelity: " << f(screen.report.fidelity) << "/100 geometry, " << f(screen.report.typography)
      << "/100 typography\n";
    o << "// Build: add this file to your ImGui project, call " << pre << "::LoadFonts() after\n";
    o << "// ImGui::CreateContext(), then " << pre << "::Draw(ImGui::GetBackgroundDrawList(), origin, time)\n";
    o << "// or " << pre << "::Build() inside a window for editable widgets.\n";
    o << "#include \"imgui.h\"\n";
    o << "#include <cmath>\n#include <cstring>\n#include <cstdio>\n\n";
    o << "namespace " << pre << " {\n\n";

    if (opt.include_palette) {
        o << "namespace palette {\n";
        const size_t n = std::min<size_t>(16, screen.palette.size());
        std::vector<std::string> used;
        for (size_t i = 0; i < n; ++i) {
            std::string name = role_color_name(screen.palette[i], i);
            std::string unique = name;
            int suffix = 2;
            while (std::find(used.begin(), used.end(), unique) != used.end())
                unique = name + std::to_string(suffix++);
            used.push_back(unique);
            o << "    static const ImU32 " << unique << " = " << u32(screen.palette[i].color)
              << ";  // " << hex_string(screen.palette[i].color) << "\n";
        }
        o << "}  // namespace palette\n\n";
    }

    o << "// Measured layout constants (logical pixels).\n";
    o << "struct Metrics {\n";
    o << "    float base_unit = " << f(screen.metrics.base_unit * k) << "f;\n";
    o << "    float margin = " << f(screen.metrics.margin * k) << "f;\n";
    o << "    float content_width = " << f(screen.metrics.content_width * k) << "f;\n";
    o << "    float width = " << f(screen.width * k) << "f;\n";
    o << "    float height = " << f(screen.height * k) << "f;\n";
    o << "};\n\n";

    if (opt.include_fonts) {
        o << "// Font candidates ranked by measured metrics. Drop a TTF next to the binary\n";
        o << "// or point the macro at an absolute path.\n";
        o << "#ifndef PARSED_FONT_DIR\n#define PARSED_FONT_DIR \"\"\n#endif\n";
        std::vector<float> sizes;
        for (const auto& e : screen.elements)
            if (is_textual(e.type) && e.text.size_px > 0.f) sizes.push_back(e.text.size_px * k);
        std::sort(sizes.begin(), sizes.end());
        sizes.erase(std::unique(sizes.begin(), sizes.end(), [](float a, float b) { return std::fabs(a - b) < 0.6f; }),
                    sizes.end());
        if (sizes.size() > 8) sizes.resize(8);
        o << "static const float kFontSizes[] = {";
        for (size_t i = 0; i < sizes.size(); ++i) o << (i ? ", " : "") << f(sizes[i]) << "f";
        if (sizes.empty()) o << "13.f";
        o << "};\n";
        o << "// Candidates: ";
        std::vector<std::string> names = font_candidates(FontFamily::Sans, 400);
        for (size_t i = 0; i < names.size(); ++i) o << (i ? ", " : "") << names[i];
        o << "\nvoid LoadFonts() {\n";
        o << "    ImGuiIO& io = ImGui::GetIO();\n";
        o << "    // io.Fonts->AddFontFromFileTTF(\"path/to/Inter-Regular.ttf\", 14.0f);\n";
        o << "    // Call ImGui_ImplOpenGL3_CreateFontsTexture() after this when using the GL3 backend.\n";
        o << "}\n\n";
    }

    o << "// Vector icon set reconstructed from the screenshot.\n";
    o << "static void Icon(ImDrawList* dl, const char* name, ImVec2 c, float s, ImU32 col, float th) {\n";
    o << "    const float r = s * 0.5f; (void)r;\n";
    o << "    if (false) {}\n";
    // Emit every icon once; unreferenced branches are harmless.
    static const char* kAll[] = {"search", "close", "check", "chevron_down", "chevron_right", "chevron_left",
                                 "chevron_up", "plus", "minus", "menu", "dots_vertical", "dots_horizontal",
                                 "user", "bell", "home", "settings", "star", "heart", "play", "arrow_right",
                                 "arrow_left", "trash", "filter", "calendar", "download", "upload", "edit",
                                 "lock", "info"};
    for (const char* name : kAll) {
        bool known = false;
        o << icon_draw_code(name, known);
    }
    o << "    else { dl->AddRect(ImVec2(c.x - s * 0.3f, c.y - s * 0.3f), ImVec2(c.x + s * 0.3f, c.y + s * 0.3f), col, 2.0f, 0, th); }\n";
    o << "}\n\n";

    if (opt.include_animations) {
        o << "static float EaseOutCubic(float t) { t = t < 0 ? 0 : (t > 1 ? 1 : t); const float u = 1.0f - t; return 1.0f - u * u * u; }\n";
        o << "static float EaseInOutCubic(float t) { t = t < 0 ? 0 : (t > 1 ? 1 : t); return t < 0.5f ? 4.0f * t * t * t : 1.0f - powf(-2.0f * t + 2.0f, 3.0f) * 0.5f; }\n";
        o << "static float EaseOutBack(float t) { t = t < 0 ? 0 : (t > 1 ? 1 : t); const float c1 = 1.70158f, c3 = c1 + 1.0f; return 1.0f + c3 * powf(t - 1.0f, 3.0f) + c1 * powf(t - 1.0f, 2.0f); }\n\n";
    }

    // DrawList target.
    if (opt.mode == EmitOptions::Mode::DrawList || opt.mode == EmitOptions::Mode::Both) {
        o << "// Pixel-accurate reconstruction. Coordinates are logical pixels relative to o.\n";
        o << "void Draw(ImDrawList* dl, ImVec2 o, float time) {\n";
        o << "    (void)time;\n";
        int drawn = 0;
        for (const auto& fe : flat) {
            const Element& e = *fe.e;
            if (e.type == ElementType::Window) continue;
            if (drawn++ > opt.max_elements) break;
            const float x = e.rect.x * k, y = e.rect.y * k, w = e.rect.w * k, h = e.rect.h * k;
            if (w <= 0 || h <= 0) continue;
            o << "\n";
            if (opt.comment_measurements)
                o << "    // #" << e.id << " " << to_string(e.type) << " " << e.rect.w << "x" << e.rect.h
                  << "px conf " << f(e.confidence * 100.f) << "%"
                  << (e.shadow.present ? " shadow" : "") << (e.border.present() ? " border " : "")
                  << (e.radius ? " radius " + std::to_string(e.radius) : "") << "\n";
            const std::string p0 = "ImVec2(o.x + " + f(x) + "f, o.y + " + f(y) + "f)";
            const std::string p1 = "ImVec2(o.x + " + f(x + w) + "f, o.y + " + f(y + h) + "f)";
            if (e.shadow.present) {
                o << "    dl->AddRectFilled(ImVec2(o.x + " << f(x - e.shadow.spread * k) << "f, o.y + "
                  << f(y - e.shadow.spread * k + e.shadow.offset_y * k) << "f), ImVec2(o.x + "
                  << f(x + w + e.shadow.spread * k) << "f, o.y + " << f(y + h + e.shadow.spread * k) << "f), IM_COL32(0,0,0,"
                  << static_cast<int>(e.shadow.alpha * 40.f) << "), " << f((e.radius + e.shadow.spread) * k) << "f);\n";
            }
            if (e.type == ElementType::Icon) {
                o << "    Icon(dl, " << cpp_string(e.icon.name.empty() ? "glyph" : e.icon.name) << ", ImVec2(o.x + "
                  << f(x + w * 0.5f) << "f, o.y + " << f(y + h * 0.5f) << "f), " << f(std::min(w, h)) << "f, "
                  << u32(e.icon.color) << ", " << f(std::max(1.f, e.icon.stroke_px * k)) << "f);\n";
            } else if (e.type == ElementType::Divider) {
                o << "    dl->AddRectFilled(" << p0 << ", " << p1 << ", " << u32(e.fill.top) << ", 0.0f);\n";
            } else if (!e.text.text.empty() && is_textual(e.type) && e.type != ElementType::Button &&
                       e.type != ElementType::Input) {
                const float fs = e.text.size_px > 0 ? e.text.size_px * k : 13.f;
                o << "    dl->AddText(ImGui::GetFont(), " << f(fs) << "f, ImVec2(o.x + " << f(x) << "f, o.y + "
                  << f(y) << "f), " << u32(e.text.color) << ", " << cpp_string(e.text.text) << ");\n";
            } else if (e.text.text.empty() && is_textual(e.type)) {
                // Text was not recognised: keep the measured zone visible.
                const float bar_h = std::max(2.f, h * 0.55f);
                o << "    // text not recognised (" << e.text.glyph_class << ", " << f(e.text.size_px) << "px); supply text via the agent\n";
                o << "    dl->AddRectFilled(ImVec2(o.x + " << f(x) << "f, o.y + " << f(y + (h - bar_h) * 0.5f)
                  << "f), ImVec2(o.x + " << f(x + w * 0.85f) << "f, o.y + " << f(y + (h + bar_h) * 0.5f)
                  << "f), IM_COL32(128,128,128,40), " << f(bar_h * 0.5f) << "f);\n";
            } else {
                if (e.fill.gradient) {
                    o << "    dl->AddRectFilledMultiColor(" << p0 << ", " << p1 << ", " << u32(e.fill.top) << ", "
                      << u32(e.fill.top) << ", " << u32(e.fill.bottom) << ", " << u32(e.fill.bottom) << ");\n";
                } else {
                    o << "    dl->AddRectFilled(" << p0 << ", " << p1 << ", " << u32(e.fill.top) << ", "
                      << f(e.radius * k) << "f);\n";
                }
                if (e.border.present()) {
                    const float th = std::max(1.f, e.border.max_thickness() * k);
                    o << "    dl->AddRect(" << p0 << ", " << p1 << ", " << u32(e.border.color) << ", "
                      << f(e.radius * k) << "f, 0, " << f(th) << "f);\n";
                }
                if (!e.text.text.empty()) {
                    const float fs = e.text.size_px > 0 ? e.text.size_px * k : 13.f;
                    const float ty = y + (h - fs) * 0.5f;
                    float tx = x + e.padding_left * k;
                    if (e.text.align == TextAlign::Center) tx = x + (w - fs * 0.55f * e.text.text.size()) * 0.5f;
                    if (e.text.align == TextAlign::Right) tx = x + w - e.padding_right * k - fs * 0.55f * e.text.text.size();
                    o << "    dl->AddText(ImGui::GetFont(), " << f(fs) << "f, ImVec2(o.x + " << f(tx) << "f, o.y + "
                      << f(ty) << "f), " << u32(e.text.color) << ", " << cpp_string(e.text.text) << ");\n";
                }
            }
        }
        o << "}\n\n";
    }

    // Semantic target.
    if (opt.mode == EmitOptions::Mode::Semantic || opt.mode == EmitOptions::Mode::Both) {
        o << "// Editable widget tree. Absolute placement keeps the measured geometry; the\n";
        o << "// widgets themselves are stock ImGui so they can be wired to real state.\n";
        o << "void Build() {\n";
        o << "    ImDrawList* dl = ImGui::GetWindowDrawList();\n";
        o << "    const ImVec2 o = ImGui::GetCursorScreenPos();\n";
        int inputs = 0;
        for (const auto& fe : flat) {
            const Element& e = *fe.e;
            if (e.type == ElementType::Window || e.type == ElementType::Label || e.type == ElementType::Heading ||
                e.type == ElementType::Icon)
                continue;
            const float x = e.rect.x * k, y = e.rect.y * k, w = e.rect.w * k, h = e.rect.h * k;
            if (w <= 0 || h <= 0) continue;
            o << "\n    // #" << e.id << " " << to_string(e.type) << "\n";
            o << "    ImGui::SetCursorScreenPos(ImVec2(o.x + " << f(x) << "f, o.y + " << f(y) << "f));\n";
            switch (e.type) {
                case ElementType::Button: {
                    o << "    ImGui::PushStyleColor(ImGuiCol_Button, " << u32(e.fill.top) << ");\n";
                    o << "    ImGui::PushStyleColor(ImGuiCol_ButtonHovered, " << u32(relative_shift(e.fill.top, 0.06f)) << ");\n";
                    o << "    ImGui::PushStyleColor(ImGuiCol_ButtonActive, " << u32(relative_shift(e.fill.top, -0.06f)) << ");\n";
                    o << "    ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding, " << f(e.radius * k) << "f);\n";
                    o << "    ImGui::Button(" << cpp_string(e.text.text.empty() ? "##button_" + std::to_string(e.id) : e.text.text + "##" + std::to_string(e.id)) << ", ImVec2(" << f(w) << "f, " << f(h) << "f));\n";
                    o << "    ImGui::PopStyleVar();\n    ImGui::PopStyleColor(3);\n";
                    break;
                }
                case ElementType::Input: {
                    o << "    static char buf_" << e.id << "[256] = \"\";\n";
                    o << "    ImGui::SetNextItemWidth(" << f(w) << "f);\n";
                    o << "    ImGui::InputText(" << cpp_string("##input_" + std::to_string(e.id)) << ", buf_" << e.id
                      << ", sizeof(buf_" << e.id << "));\n";
                    ++inputs;
                    break;
                }
                case ElementType::Toggle: {
                    o << "    static bool on_" << e.id << " = true;\n";
                    o << "    ImGui::Checkbox(" << cpp_string("##toggle_" + std::to_string(e.id)) << ", &on_" << e.id << ");\n";
                    break;
                }
                case ElementType::Divider:
                    o << "    dl->AddLine(ImVec2(o.x + " << f(x) << "f, o.y + " << f(y + h * 0.5f) << "f), ImVec2(o.x + "
                      << f(x + w) << "f, o.y + " << f(y + h * 0.5f) << "f), " << u32(e.fill.top) << ", "
                      << f(std::max(1.f, h)) << "f);\n";
                    break;
                case ElementType::Panel:
                case ElementType::Window:
                case ElementType::Badge:
                case ElementType::MenuItem:
                case ElementType::ListItem:
                case ElementType::Tab:
                case ElementType::Progress:
                case ElementType::Slider:
                case ElementType::Image:
                case ElementType::Avatar:
                case ElementType::Chart:
                case ElementType::Scrollbar:
                case ElementType::Unknown:
                default:
                    if (e.fill.top.a > 0)
                        o << "    dl->AddRectFilled(ImVec2(o.x + " << f(x) << "f, o.y + " << f(y) << "f), ImVec2(o.x + "
                          << f(x + w) << "f, o.y + " << f(y + h) << "f), " << u32(e.fill.top) << ", " << f(e.radius * k) << "f);\n";
                    if (e.border.present())
                        o << "    dl->AddRect(ImVec2(o.x + " << f(x) << "f, o.y + " << f(y) << "f), ImVec2(o.x + "
                          << f(x + w) << "f, o.y + " << f(y + h) << "f), " << u32(e.border.color) << ", "
                          << f(e.radius * k) << "f, 0, " << f(std::max(1.f, e.border.max_thickness() * k)) << "f);\n";
                    if (!e.text.text.empty())
                        o << "    dl->AddText(ImGui::GetFont(), " << f((e.text.size_px > 0 ? e.text.size_px : 13.f) * k)
                          << "f, ImVec2(o.x + " << f(x + e.padding_left * k) << "f, o.y + "
                          << f(y + (h - (e.text.size_px > 0 ? e.text.size_px : 13.f) * k) * 0.5f) << "f), "
                          << u32(e.text.color) << ", " << cpp_string(e.text.text) << ");\n";
                    break;
            }
        }
        o << "}\n\n";
    }

    // Animation plan.
    if (opt.include_animations && !screen.sequence.tracks.empty()) {
        o << "// Animations measured across the supplied frames.\n";
        o << "struct AnimTrack { int element; const char* property; float from; float to; float duration_ms; const char* easing; };\n";
        o << "static const AnimTrack kAnimations[] = {\n";
        for (const auto& t : screen.sequence.tracks) {
            o << "    {" << t.element_id << ", " << cpp_string(t.property) << ", " << f(t.from) << "f, " << f(t.to)
              << "f, " << f(static_cast<float>(t.duration_ms)) << "f, " << cpp_string(t.easing) << "},\n";
        }
        o << "};\n";
        o << "static const int kAnimationCount = " << screen.sequence.tracks.size() << ";\n";
        o << "// Apply inside Draw(): k = EaseOutCubic(time * 1000.0f / duration_ms).\n\n";
    }

    o << "}  // namespace " << pre << "\n";
    return o.str();
}

std::string emit_stub_main(const Screen& screen, const EmitOptions& opt) {
    const std::string pre = ident_prefix(opt);
    std::ostringstream o;
    o << "// Minimal host for the generated reconstruction.\n";
    o << "#include \"imgui.h\"\n#include \"imgui_impl_glfw.h\"\n#include \"imgui_impl_opengl3.h\"\n";
    o << "#include <GLFW/glfw3.h>\n#include <cstdio>\n\n";
    o << "namespace " << pre << " { void LoadFonts(); void Draw(ImDrawList*, ImVec2, float); void Build(); }\n\n";
    o << "int main() {\n";
    o << "    if (!glfwInit()) return 1;\n";
    o << "    glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 3);\n";
    o << "    glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 0);\n";
    o << "    GLFWwindow* w = glfwCreateWindow(" << static_cast<int>(screen.width) << ", "
      << static_cast<int>(screen.height) << ", \"parsergui reconstruction\", nullptr, nullptr);\n";
    o << "    if (!w) return 1;\n    glfwMakeContextCurrent(w);\n    glfwSwapInterval(1);\n";
    o << "    IMGUI_CHECKVERSION(); ImGui::CreateContext();\n";
    o << "    ImGui_ImplGlfw_InitForOpenGL(w, true);\n    ImGui_ImplOpenGL3_Init(\"#version 130\");\n";
    o << "    " << pre << "::LoadFonts();\n";
    o << "    double start = glfwGetTime();\n";
    o << "    while (!glfwWindowShouldClose(w)) {\n        glfwPollEvents();\n";
    o << "        ImGui_ImplOpenGL3_NewFrame(); ImGui_ImplGlfw_NewFrame(); ImGui::NewFrame();\n";
    o << "        ImGui::SetNextWindowPos(ImVec2(0,0));\n        ImGui::SetNextWindowSize(ImGui::GetIO().DisplaySize);\n";
    o << "        ImGui::Begin(\"reconstruction\", nullptr, ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoBackground);\n";
    o << "        " << pre << "::Draw(ImGui::GetWindowDrawList(), ImGui::GetCursorScreenPos(), (float)(glfwGetTime()-start));\n";
    o << "        ImGui::End();\n        ImGui::Render();\n";
    o << "        int fbw, fbh; glfwGetFramebufferSize(w, &fbw, &fbh);\n";
    o << "        glViewport(0, 0, fbw, fbh); glClearColor(0.07f, 0.07f, 0.08f, 1.0f); glClear(GL_COLOR_BUFFER_BIT);\n";
    o << "        ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());\n        glfwSwapBuffers(w);\n    }\n";
    o << "    ImGui_ImplOpenGL3_Shutdown(); ImGui_ImplGlfw_Shutdown(); ImGui::DestroyContext();\n";
    o << "    glfwDestroyWindow(w); glfwTerminate(); return 0;\n}\n";
    return o.str();
}

Json screen_json(const Screen& screen) {
    Json root = Json::object();
    root["name"] = screen.name;
    root["width"] = screen.width;
    root["height"] = screen.height;
    root["background"] = hex_string(screen.background);
    root["device_pixel_ratio"] = screen.report.device_pixel_ratio;
    if (!screen.region.empty()) {
        Json region = Json::array();
        region.push_back(screen.region.x);
        region.push_back(screen.region.y);
        region.push_back(screen.region.w);
        region.push_back(screen.region.h);
        root["region"] = region;
        root["auto_gui"] = screen.auto_gui;
    }

    Json metrics = Json::object();
    metrics["base_unit"] = screen.metrics.base_unit;
    metrics["grid_confidence"] = screen.metrics.grid_confidence;
    metrics["margin"] = screen.metrics.margin;
    metrics["content_width"] = screen.metrics.content_width;
    metrics["text_scale"] = screen.metrics.text_scale;
    root["metrics"] = metrics;

    Json palette = Json::array();
    for (const auto& p : screen.palette) {
        Json e = Json::object();
        e["color"] = hex_string(p.color);
        e["role"] = p.role;
        e["share"] = static_cast<double>(p.share);
        e["pixels"] = p.pixels;
        palette.push_back(e);
    }
    root["palette"] = palette;

    Json elems = Json::array();
    for (const auto& e : screen.elements) {
        Json j = Json::object();
        j["id"] = e.id;
        j["parent"] = e.parent;
        j["type"] = to_string(e.type);
        j["rect"] = Json::array();
        j["rect"].push_back(e.rect.x);
        j["rect"].push_back(e.rect.y);
        j["rect"].push_back(e.rect.w);
        j["rect"].push_back(e.rect.h);
        j["radius"] = e.radius;
        j["z"] = e.z;
        j["confidence"] = static_cast<double>(e.confidence);
        j["fill"] = hex_string(e.fill.top);
        if (e.fill.gradient) j["fill_bottom"] = hex_string(e.fill.bottom);
        Json border = Json::object();
        border["left"] = e.border.left;
        border["top"] = e.border.top;
        border["right"] = e.border.right;
        border["bottom"] = e.border.bottom;
        border["color"] = hex_string(e.border.color);
        j["border"] = border;
        if (e.shadow.present) {
            Json sh = Json::object();
            sh["spread"] = e.shadow.spread;
            sh["offset_y"] = e.shadow.offset_y;
            sh["alpha"] = static_cast<double>(e.shadow.alpha);
            j["shadow"] = sh;
        }
        if (!e.text.text.empty() || e.text.size_px > 0.f) {
            Json t = Json::object();
            t["text"] = e.text.text;
            t["color"] = hex_string(e.text.color);
            t["family"] = to_string(e.text.family);
            t["size_px"] = static_cast<double>(e.text.size_px);
            t["weight"] = e.text.weight;
            t["italic"] = e.text.italic;
            t["align"] = to_string(e.text.align);
            t["confidence"] = static_cast<double>(e.text.confidence);
            t["glyph_class"] = e.text.glyph_class;
            j["text_style"] = t;
        }
        if (!e.icon.name.empty()) {
            Json i = Json::object();
            i["name"] = e.icon.name;
            i["score"] = static_cast<double>(e.icon.score);
            i["color"] = hex_string(e.icon.color);
            i["stroke_px"] = e.icon.stroke_px;
            j["icon"] = i;
        }
        j["padding"] = Json::array();
        j["padding"].push_back(e.padding_left);
        j["padding"].push_back(e.padding_top);
        j["padding"].push_back(e.padding_right);
        j["padding"].push_back(e.padding_bottom);
        j["gap"] = e.gap;
        if (!e.notes.empty()) {
            Json notes = Json::array();
            for (const auto& n : e.notes) notes.push_back(n);
            j["notes"] = notes;
        }
        elems.push_back(j);
    }
    root["elements"] = elems;

    if (screen.sequence.frames > 0) {
        Json seq = Json::object();
        seq["frames"] = screen.sequence.frames;
        seq["mean_delta"] = static_cast<double>(screen.sequence.mean_delta);
        Json tracks = Json::array();
        for (const auto& t : screen.sequence.tracks) {
            Json j = Json::object();
            j["element"] = t.element_id;
            j["property"] = t.property;
            j["from"] = static_cast<double>(t.from);
            j["to"] = static_cast<double>(t.to);
            j["duration_ms"] = t.duration_ms;
            j["easing"] = t.easing;
            j["confidence"] = static_cast<double>(t.confidence);
            tracks.push_back(j);
        }
        seq["tracks"] = tracks;
        root["sequence"] = seq;
    }

    Json report = Json::object();
    report["elements"] = screen.report.elements;
    report["text_runs"] = screen.report.text_runs;
    report["icons"] = screen.report.icons;
    report["buttons"] = screen.report.buttons;
    report["inputs"] = screen.report.inputs;
    report["panels"] = screen.report.cards;
    report["borders"] = screen.report.borders;
    report["radii"] = screen.report.radii;
    report["shadows"] = screen.report.shadows;
    report["fidelity"] = static_cast<double>(screen.report.fidelity);
    report["typography"] = static_cast<double>(screen.report.typography);
    report["elapsed_ms"] = screen.report.elapsed_ms;
    report["summary"] = screen.report.summary;
    Json warnings = Json::array();
    for (const auto& w : screen.report.warnings) warnings.push_back(w);
    report["warnings"] = warnings;
    root["report"] = report;

    return root;
}

std::string emit_json(const Screen& screen) { return screen_json(screen).dump(2); }

std::string emit_layout_txt(const Screen& screen) {
    std::ostringstream o;
    o << "parsergui layout — " << screen.name << " (" << screen.width << "x" << screen.height << ")\n";
    o << "grid " << screen.metrics.base_unit << "px, margin " << screen.metrics.margin << "px, dpr "
      << screen.report.device_pixel_ratio << "\n\n";
    std::function<void(int, int)> walk = [&](int id, int depth) {
        if (id < 0 || id >= static_cast<int>(screen.elements.size())) return;
        const Element& e = screen.elements[id];
        o << std::string(static_cast<size_t>(depth) * 2, ' ') << "#" << e.id << " " << to_string(e.type) << " ["
          << e.rect.x << "," << e.rect.y << " " << e.rect.w << "x" << e.rect.h << "]";
        if (e.radius) o << " r" << e.radius;
        if (e.border.present()) o << " b" << e.border.max_thickness();
        if (e.shadow.present) o << " shadow";
        if (e.padding_left || e.padding_top) o << " pad(" << e.padding_left << "," << e.padding_top << "," << e.padding_right << "," << e.padding_bottom << ")";
        if (e.gap) o << " gap " << e.gap;
        if (!e.text.text.empty()) o << " \"" << e.text.text << "\"";
        if (!e.icon.name.empty()) o << " icon:" << e.icon.name;
        o << "  " << static_cast<int>(e.confidence * 100) << "%\n";
        for (int c : e.children) walk(c, depth + 1);
    };
    walk(screen.root, 0);
    return o.str();
}

std::string emit_markdown(const Screen& screen) {
    std::ostringstream o;
    o << "# " << (screen.name.empty() ? "screenshot" : screen.name) << "\n\n";
    o << "- Source: " << screen.width << " x " << screen.height << " px, dpr "
      << screen.report.device_pixel_ratio << "\n";
    if (!screen.region.empty())
        o << "- GUI region: " << screen.region.w << " x " << screen.region.h << " at " << screen.region.x << ","
          << screen.region.y << (screen.auto_gui ? " (auto-detected)" : " (explicit crop)") << "\n";
    o << "- Elements: " << screen.report.elements << " (" << screen.report.cards << " panels, "
      << screen.report.buttons << " buttons, " << screen.report.inputs << " inputs, " << screen.report.icons
      << " icons)\n";
    o << "- Grid: " << screen.metrics.base_unit << " px base unit, " << screen.metrics.grid_confidence
      << "% confidence\n";
    o << "- Fidelity: " << f(screen.report.fidelity) << "/100 geometry, " << f(screen.report.typography)
      << "/100 typography\n";
    o << "- Processing: " << screen.report.elapsed_ms << " ms\n\n";
    o << "## Palette\n\n| role | hex | share |\n| --- | --- | --- |\n";
    for (const auto& p : screen.palette)
        o << "| " << (p.role.empty() ? "-" : p.role) << " | " << hex_string(p.color) << " | "
          << static_cast<int>(p.share * 100) << "% |\n";
    o << "\n## Elements\n\n| id | type | box | radius | border | text | confidence |\n| --- | --- | --- | --- | --- | --- | --- |\n";
    for (const auto& e : screen.elements) {
        if (e.type == ElementType::Window) continue;
        o << "| " << e.id << " | " << to_string(e.type) << " | " << e.rect.x << "," << e.rect.y << " "
          << e.rect.w << "x" << e.rect.h << " | " << (e.radius ? std::to_string(e.radius) : "-") << " | "
          << (e.border.present() ? std::to_string(e.border.max_thickness()) : "-") << " | "
          << (e.text.text.empty() ? "-" : e.text.text) << " | " << static_cast<int>(e.confidence * 100) << "% |\n";
    }
    if (!screen.report.warnings.empty()) {
        o << "\n## Warnings\n\n";
        for (const auto& w : screen.report.warnings) o << "- " << w << "\n";
    }
    return o.str();
}

}  // namespace pgr
