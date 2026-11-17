// Software rasteriser used for two things: generating fixtures in tests, and
// re-rendering a reconstruction so it can be compared with the source image.
// Anti-aliasing is analytic: coverage comes from a signed distance field, which
// keeps edges stable at any radius and makes the diff meaningful.
#pragma once

#include <vector>

#include "image.h"
#include "types.h"

namespace pgr {

class Canvas {
public:
    Canvas(int w, int h, RGBA fill = RGBA(0, 0, 0, 255));

    int width() const { return w_; }
    int height() const { return h_; }
    bool empty() const { return w_ <= 0 || h_ <= 0; }

    void clear(RGBA c);
    void blend(int x, int y, RGBA c, float coverage);
    RGBA pixel(int x, int y) const;

    void fill_rect(const RectI& r, RGBA c);
    void fill_rect(const RectF& r, RGBA c);
    void fill_rect_rounded(const RectF& r, float radius, RGBA c);
    void fill_rect_rounded_vertical(const RectF& r, float radius, RGBA top, RGBA bottom);
    void stroke_rect_rounded(const RectF& r, float radius, float thickness, RGBA c);
    void fill_circle(float cx, float cy, float radius, RGBA c);
    void stroke_circle(float cx, float cy, float radius, float thickness, RGBA c);
    void line(float x0, float y0, float x1, float y1, float thickness, RGBA c);
    void drop_shadow(const RectF& r, float radius, float spread, float offset_y, RGBA c, float alpha);
    void blit(const Image& src, int dx, int dy, const RectI& src_rect, float opacity = 1.f);
    void blit_clipped(const Image& src, int dx, int dy, const RectI& dst_clip, float opacity = 1.f);
    void add_noise(uint32_t seed, float amount);

    Image to_image() const;
    const std::vector<uint8_t>& bytes() const { return px_; }

private:
    int w_ = 0, h_ = 0;
    std::vector<uint8_t> px_;
};

// Signed distance to a rounded box, in pixels, negative inside.
float sd_rounded_box(float px, float py, float cx, float cy, float hw, float hh, float radius);

}  // namespace pgr
