// Prints what the detector measured. Useful when a screenshot produces an
// unexpected tree: it shows the raw numbers, not the reconstruction.
#include <cstdio>
#include <cstdlib>
#include <string>

#include "color.h"
#include "detect.h"
#include "emit.h"
#include "image.h"

using namespace pgr;

int main(int argc, char** argv) {
    if (argc < 2) {
        std::fprintf(stderr, "usage: %s <image> [max_colors]\n", argv[0]);
        return 2;
    }
    std::string error;
    Image img = Image::load(argv[1], &error);
    if (img.empty()) {
        std::fprintf(stderr, "load failed: %s\n", error.c_str());
        return 1;
    }
    AnalysisOptions opt;
    if (argc > 2) opt.max_colors = std::atoi(argv[2]);
    Screen s = detect_screen(img, opt, argv[1]);
    std::printf("%dx%d bg=%s dpr=%d grid=%d margin=%d\n", s.width, s.height, hex_string(s.background).c_str(),
                s.report.device_pixel_ratio, s.metrics.base_unit, s.metrics.margin);
    std::printf("%-4s %-4s %-16s %-5s %-4s %-8s %-8s %-6s %-5s %s\n", "id", "par", "type", "box", "r", "fill",
                "border", "shadow", "conf", "text");
    for (const auto& e : s.elements) {
        char box[64];
        std::snprintf(box, sizeof(box), "%d,%d %dx%d", e.rect.x, e.rect.y, e.rect.w, e.rect.h);
        std::printf("%-4d %-4d %-16s %-15s %-4d %-8s %-8s %-6d %-5.0f %s\n", e.id, e.parent, to_string(e.type), box,
                    e.radius, hex_string(e.fill.top).c_str(),
                    e.border.present() ? hex_string(e.border.color).c_str() : "-", e.shadow.present ? 1 : 0,
                    e.confidence * 100.f, e.text.text.c_str());
    }
    std::printf("\n%s\n", emit_layout_txt(s).c_str());
    return 0;
}
