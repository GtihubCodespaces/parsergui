// Draws a known interface with the software rasteriser. The test then checks that
// parsergui recovers the values used here (positions, radii, colours, grid).
#include <cstdio>
#include <string>

#include "color.h"
#include "image.h"
#include "raster.h"
#include "types.h"

using namespace pgr;

int main(int argc, char** argv) {
    const int W = 960, H = 640;
    const std::string out = argc > 1 ? argv[1] : "fixture.png";

    Canvas c(W, H, RGBA(16, 18, 22));
    // Sidebar
    c.fill_rect(RectI{0, 0, 220, H}, RGBA(22, 25, 31));
    c.fill_rect(RectI{220, 0, 1, H}, RGBA(40, 44, 52));

    // Logo block
    c.fill_rect_rounded(RectF(24, 24, 32, 32), 8, RGBA(94, 129, 244));
    c.fill_rect_rounded(RectF(32, 32, 16, 16), 4, RGBA(255, 255, 255));

    // Nav items
    const char* nav[] = {"Overview", "Projects", "Agents", "Providers", "Settings"};
    for (int i = 0; i < 5; ++i) {
        const float y = 88.f + i * 40.f;
        if (i == 0) c.fill_rect_rounded(RectF(12, y - 6, 196, 34), 8, RGBA(34, 39, 48));
        c.fill_rect_rounded(RectF(24, y + 2, 14, 14), 4, RGBA(i == 0 ? 148 : 96, i == 0 ? 168 : 104, i == 0 ? 255 : 118));
        c.fill_rect_rounded(RectF(50, y + 5, 62, 7), 3, RGBA(i == 0 ? 228 : 138, i == 0 ? 232 : 144, i == 0 ? 242 : 156));
    }

    // Header
    c.fill_rect(RectI{220, 0, W - 220, 64}, RGBA(19, 21, 26));
    c.fill_rect(RectI{220, 63, W - 220, 1}, RGBA(38, 42, 50));
    c.fill_rect_rounded(RectF(244, 20, 240, 26), 6, RGBA(28, 31, 38));
    c.fill_rect_rounded(RectF(256, 30, 120, 7), 3, RGBA(112, 118, 130));
    c.fill_rect_rounded(RectF(760, 18, 96, 30), 8, RGBA(94, 129, 244));
    c.fill_rect_rounded(RectF(792, 29, 32, 7), 3, RGBA(255, 255, 255));
    c.fill_circle(886, 33, 13, RGBA(52, 58, 70));

    // Cards
    const int card_x = 252, card_y = 96, card_w = 320, card_h = 180;
    c.fill_rect_rounded(RectF(card_x, card_y, card_w, card_h), 12, RGBA(26, 29, 36));
    c.stroke_rect_rounded(RectF(card_x, card_y, card_w, card_h), 12, 1, RGBA(44, 49, 59));
    c.fill_rect_rounded(RectF(card_x + 20, card_y + 20, 120, 9), 4, RGBA(226, 230, 240));
    c.fill_rect_rounded(RectF(card_x + 20, card_y + 40, 220, 6), 3, RGBA(120, 126, 140));
    c.fill_rect_rounded(RectF(card_x + 20, card_y + 52, 180, 6), 3, RGBA(120, 126, 140));

    // Bar chart
    const int bars[] = {40, 72, 56, 96, 64, 84};
    for (int i = 0; i < 6; ++i) {
        const float bx = static_cast<float>(card_x + 20 + i * 30);
        const float bh = static_cast<float>(bars[i]);
        c.fill_rect_rounded(RectF(bx, card_y + card_h - 24 - bh, 18, bh), 4, RGBA(94, 129, 244));
    }

    // Second card with input + button
    c.fill_rect_rounded(RectF(card_x + card_w + 24, card_y, card_w, card_h), 12, RGBA(26, 29, 36));
    c.stroke_rect_rounded(RectF(card_x + card_w + 24, card_y, card_w, card_h), 12, 1, RGBA(44, 49, 59));
    c.fill_rect_rounded(RectF(card_x + card_w + 44, card_y + 24, 140, 9), 4, RGBA(226, 230, 240));
    c.fill_rect_rounded(RectF(card_x + card_w + 44, card_y + 56, 272, 36), 8, RGBA(20, 22, 27));
    c.stroke_rect_rounded(RectF(card_x + card_w + 44, card_y + 56, 272, 36), 8, 1, RGBA(52, 58, 70));
    c.fill_rect_rounded(RectF(card_x + card_w + 58, card_y + 70, 130, 7), 3, RGBA(100, 106, 120));
    c.fill_rect_rounded(RectF(card_x + card_w + 44, card_y + 108, 132, 32), 8, RGBA(94, 129, 244));
    c.fill_rect_rounded(RectF(card_x + card_w + 84, card_y + 120, 52, 7), 3, RGBA(255, 255, 255));
    c.fill_rect_rounded(RectF(card_x + card_w + 188, card_y + 108, 128, 32), 8, RGBA(38, 42, 51));
    c.stroke_rect_rounded(RectF(card_x + card_w + 188, card_y + 108, 128, 32), 8, 1, RGBA(60, 66, 80));

    // Toggle
    c.fill_rect_rounded(RectF(card_x + card_w + 44, card_y + 154, 44, 22), 11, RGBA(94, 129, 244));
    c.fill_circle(card_x + card_w + 44 + 33, card_y + 165, 8, RGBA(255, 255, 255));

    // Table rows
    for (int i = 0; i < 4; ++i) {
        const float y = 300.f + i * 46.f;
        c.fill_rect_rounded(RectF(252, y, 620, 38), 8, RGBA(23, 26, 32));
        c.fill_rect_rounded(RectF(268, y + 15, 18, 8), 3, RGBA(120, 128, 144));
        c.fill_rect_rounded(RectF(300, y + 14, 160, 9), 4, RGBA(200, 206, 220));
        c.fill_rect_rounded(RectF(520, y + 15, 80, 7), 3, RGBA(110, 118, 132));
        c.fill_rect_rounded(RectF(660, y + 10, 64, 18), 9, RGBA(36, 48, 40));
        c.fill_rect_rounded(RectF(672, y + 16, 40, 6), 3, RGBA(94, 200, 130));
    }

    // Text glyph blocks (stand-ins for rendered labels)
    c.fill_rect_rounded(RectF(252, 92, 120, 2), 1, RGBA(255, 255, 255, 0));

    c.add_noise(1234, 0.004f);
    Image img = c.to_image();
    if (!img.save_png(out)) {
        std::fprintf(stderr, "cannot write %s\n", out.c_str());
        return 1;
    }
    std::printf("wrote %s (%dx%d)\n", out.c_str(), W, H);
    return 0;
}
