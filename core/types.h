// parsergui - shared value types. No dependencies beyond the C++ standard library.
#pragma once

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <string>
#include <vector>

namespace pgr {

struct RGBA {
    uint8_t r = 0, g = 0, b = 0, a = 255;

    constexpr RGBA() = default;
    constexpr RGBA(uint8_t r_, uint8_t g_, uint8_t b_, uint8_t a_ = 255)
        : r(r_), g(g_), b(b_), a(a_) {}

    bool operator==(const RGBA& o) const { return r == o.r && g == o.g && b == o.b && a == o.a; }
    bool operator!=(const RGBA& o) const { return !(*this == o); }
};

// Integer rectangle in pixels. x,y is the top-left corner, w,h positive.
struct RectI {
    int x = 0, y = 0, w = 0, h = 0;

    int right() const { return x + w; }
    int bottom() const { return y + h; }
    int area() const { return w > 0 && h > 0 ? w * h : 0; }
    bool empty() const { return w <= 0 || h <= 0; }
    bool contains(int px, int py) const { return px >= x && py >= y && px < x + w && py < y + h; }
    bool contains(const RectI& o) const {
        return o.x >= x && o.y >= y && o.right() <= right() && o.bottom() <= bottom();
    }
    RectI intersect(const RectI& o) const {
        int nx = std::max(x, o.x), ny = std::max(y, o.y);
        int nr = std::min(right(), o.right()), nb = std::min(bottom(), o.bottom());
        return RectI{nx, ny, std::max(0, nr - nx), std::max(0, nb - ny)};
    }
    RectI inflate(int d) const { return RectI{x - d, y - d, w + 2 * d, h + 2 * d}; }
    RectI clip(int cw, int ch) const {
        int nx = std::max(0, x), ny = std::max(0, y);
        int nr = std::min(cw, right()), nb = std::min(ch, bottom());
        return RectI{nx, ny, std::max(0, nr - nx), std::max(0, nb - ny)};
    }
    double iou(const RectI& o) const {
        RectI i = intersect(o);
        int uni = area() + o.area() - i.area();
        return uni > 0 ? static_cast<double>(i.area()) / uni : 0.0;
    }
    int cx() const { return x + w / 2; }
    int cy() const { return y + h / 2; }
};

// Floating point rectangle used by the layout model and the emitter.
struct RectF {
    float x = 0, y = 0, w = 0, h = 0;
    RectF() = default;
    RectF(float x_, float y_, float w_, float h_) : x(x_), y(y_), w(w_), h(h_) {}
    explicit RectF(const RectI& r)
        : x(static_cast<float>(r.x)), y(static_cast<float>(r.y)),
          w(static_cast<float>(r.w)), h(static_cast<float>(r.h)) {}
};

struct Vec2 {
    float x = 0, y = 0;
};

enum class ElementType {
    Unknown,
    Window,      // top level surface
    Panel,       // card, sidebar, toolbar, grouped surface
    Button,
    Toggle,      // checkbox, switch, radio
    Input,       // text field, combo, search box
    Slider,
    Progress,
    Tab,
    MenuItem,
    ListItem,
    Label,       // plain text
    Heading,     // large text, likely title
    Icon,
    Image,
    Divider,
    Badge,
    Avatar,
    Chart,
    Scrollbar,
};

const char* to_string(ElementType t);

enum class TextAlign { Left, Center, Right, Unknown };
const char* to_string(TextAlign a);

enum class FontFamily { Unknown, Sans, Serif, Mono, Display, Rounded, Condensed };
const char* to_string(FontFamily f);

// A border as measured on the image: thickness per side plus colour.
struct BorderInfo {
    int left = 0, top = 0, right = 0, bottom = 0;
    RGBA color;
    bool present() const { return left + top + right + bottom > 0; }
    int max_thickness() const { return std::max(std::max(left, top), std::max(right, bottom)); }
};

// A fill that was measured: either flat or a vertical gradient between two stops.
struct FillInfo {
    RGBA top;
    RGBA bottom;
    bool gradient = false;
};

struct ShadowInfo {
    bool present = false;
    int spread = 0;
    int offset_y = 0;
    RGBA color;
    float alpha = 0.f;
};

struct TextStyle {
    std::string text;          // empty when glyphs were not recognised
    std::string glyph_class;   // e.g. "alpha", "numeric", "mixed", "unknown"
    RGBA color;
    FontFamily family = FontFamily::Unknown;
    float size_px = 0.f;       // estimated cap height driven font size
    int line_height = 0;
    int weight = 400;
    bool italic = false;
    TextAlign align = TextAlign::Unknown;
    int letter_spacing = 0;
    int word_spacing = 0;
    float confidence = 0.f;
    RectI box;                 // tight glyph bounds
};

struct IconInfo {
    std::string name;          // matched template name or "glyph"
    float score = 0.f;
    RGBA color;
    bool monochrome = true;
    int stroke_px = 1;
};

// One reconstructed node of the interface.
struct Element {
    int id = 0;
    int parent = -1;
    std::vector<int> children;
    ElementType type = ElementType::Unknown;
    RectI rect;
    int radius = 0;
    FillInfo fill;
    BorderInfo border;
    ShadowInfo shadow;
    TextStyle text;
    IconInfo icon;
    int padding_left = 0, padding_top = 0, padding_right = 0, padding_bottom = 0;
    int gap = 0;
    int z = 0;
    float opacity = 1.f;
    float confidence = 0.f;
    std::vector<std::string> notes;
};

// Animation inferred from a multi-frame capture.
struct AnimationTrack {
    int element_id = -1;
    std::string property;      // "opacity", "x", "y", "w", "h", "radius"
    float from = 0.f, to = 0.f;
    int duration_ms = 0;
    std::string easing = "ease-out";
    float confidence = 0.f;
};

struct SequenceInfo {
    int frames = 0;
    int width = 0, height = 0;
    float mean_delta = 0.f;
    std::vector<AnimationTrack> tracks;
    std::vector<std::string> warnings;
};

struct PaletteEntry {
    RGBA color;
    int pixels = 0;
    float share = 0.f;
    std::string role;          // "background", "surface", "text", "accent", "border"
};

struct LayoutMetrics {
    int base_unit = 8;
    int margin = 0;
    int gutter = 0;
    int row_gap = 0;
    int grid_confidence = 0;   // 0..100
    int content_width = 0;
    int text_scale = 1;        // 1.0x, 1.25x, 1.5x, 2.0x
};

struct AnalysisOptions {
    int max_colors = 24;
    bool detect_text = true;
    bool detect_icons = true;
    bool sequence = true;
    bool strict = false;
    int min_element_size = 6;
    std::string target = "imgui";  // emitter target
};

struct AnalysisReport {
    std::string input_name;
    int width = 0, height = 0;
    int elements = 0;
    int text_runs = 0;
    int icons = 0;
    int cards = 0;
    int buttons = 0;
    int inputs = 0;
    int borders = 0;
    int radii = 0;
    int shadows = 0;
    float fidelity = 0.f;      // 0..100, geometry+colour reconstruction score
    float typography = 0.f;    // 0..100, text box/colour/size fidelity
    int device_pixel_ratio = 1;
    double elapsed_ms = 0.0;
    std::vector<std::string> warnings;
    std::string summary;
};

struct Screen {
    std::string name;
    int width = 0, height = 0;
    RGBA background;
    std::vector<Element> elements;
    std::vector<PaletteEntry> palette;
    LayoutMetrics metrics;
    SequenceInfo sequence;
    AnalysisReport report;
    int root = -1;

    const Element* find(int id) const {
        for (const auto& e : elements)
            if (e.id == id) return &e;
        return nullptr;
    }
};

}  // namespace pgr
