#include "types.h"

namespace pgr {

const char* to_string(ElementType t) {
    switch (t) {
        case ElementType::Window: return "Window";
        case ElementType::Panel: return "Panel";
        case ElementType::Button: return "Button";
        case ElementType::Toggle: return "Toggle";
        case ElementType::Input: return "Input";
        case ElementType::Slider: return "Slider";
        case ElementType::Progress: return "Progress";
        case ElementType::Tab: return "Tab";
        case ElementType::MenuItem: return "MenuItem";
        case ElementType::ListItem: return "ListItem";
        case ElementType::Label: return "Label";
        case ElementType::Heading: return "Heading";
        case ElementType::Icon: return "Icon";
        case ElementType::Image: return "Image";
        case ElementType::Divider: return "Divider";
        case ElementType::Badge: return "Badge";
        case ElementType::Avatar: return "Avatar";
        case ElementType::Chart: return "Chart";
        case ElementType::Scrollbar: return "Scrollbar";
        default: return "Unknown";
    }
}

const char* to_string(TextAlign a) {
    switch (a) {
        case TextAlign::Left: return "left";
        case TextAlign::Center: return "center";
        case TextAlign::Right: return "right";
        default: return "unknown";
    }
}

const char* to_string(FontFamily f) {
    switch (f) {
        case FontFamily::Sans: return "sans";
        case FontFamily::Serif: return "serif";
        case FontFamily::Mono: return "mono";
        case FontFamily::Display: return "display";
        case FontFamily::Rounded: return "rounded";
        case FontFamily::Condensed: return "condensed";
        default: return "unknown";
    }
}

}  // namespace pgr
