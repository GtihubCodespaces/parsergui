#include "raster.h"

#include <algorithm>
#include <cmath>

#include "color.h"

namespace pgr {
namespace {

inline uint8_t to_byte(float v) {
    if (v <= 0.f) return 0;
    if (v >= 255.f) return 255;
    return static_cast<uint8_t>(v + 0.5f);
}

inline float smoothstep(float edge0, float edge1, float x) {
    if (edge0 == edge1) return x < edge0 ? 0.f : 1.f;
    const float t = std::clamp((x - edge0) / (edge1 - edge0), 0.f, 1.f);
    return t * t * (3.f - 2.f * t);
}

}  // namespace

float sd_rounded_box(float px, float py, float cx, float cy, float hw, float hh, float radius) {
    const float r = std::clamp(radius, 0.f, std::min(hw, hh));
    const float qx = std::fabs(px - cx) - (hw - r);
    const float qy = std::fabs(py - cy) - (hh - r);
    const float ox = std::max(qx, 0.f);
    const float oy = std::max(qy, 0.f);
    return std::sqrt(ox * ox + oy * oy) + std::min(std::max(qx, qy), 0.f) - r;
}

Canvas::Canvas(int w, int h, RGBA fill) : w_(std::max(0, w)), h_(std::max(0, h)) {
    px_.assign(static_cast<size_t>(w_) * h_ * 4, 0);
    clear(fill);
}

void Canvas::clear(RGBA c) {
    for (size_t i = 0; i < px_.size(); i += 4) {
        px_[i] = c.r;
        px_[i + 1] = c.g;
        px_[i + 2] = c.b;
        px_[i + 3] = c.a;
    }
}

void Canvas::blend(int x, int y, RGBA c, float coverage) {
    if (x < 0 || y < 0 || x >= w_ || y >= h_) return;
    coverage = std::clamp(coverage, 0.f, 1.f) * (c.a / 255.f);
    if (coverage <= 0.f) return;
    uint8_t* p = &px_[(static_cast<size_t>(y) * w_ + x) * 4];
    p[0] = to_byte(p[0] * (1 - coverage) + c.r * coverage);
    p[1] = to_byte(p[1] * (1 - coverage) + c.g * coverage);
    p[2] = to_byte(p[2] * (1 - coverage) + c.b * coverage);
    p[3] = 255;
}

RGBA Canvas::pixel(int x, int y) const {
    if (x < 0 || y < 0 || x >= w_ || y >= h_) return RGBA{0, 0, 0, 0};
    const uint8_t* p = &px_[(static_cast<size_t>(y) * w_ + x) * 4];
    return RGBA{p[0], p[1], p[2], p[3]};
}

void Canvas::fill_rect(const RectI& r, RGBA c) {
    const int x0 = std::max(0, r.x), y0 = std::max(0, r.y);
    const int x1 = std::min(w_, r.right()), y1 = std::min(h_, r.bottom());
    for (int y = y0; y < y1; ++y)
        for (int x = x0; x < x1; ++x) blend(x, y, c, 1.f);
}

void Canvas::fill_rect(const RectF& r, RGBA c) {
    const int x0 = static_cast<int>(std::floor(r.x)), y0 = static_cast<int>(std::floor(r.y));
    const int x1 = static_cast<int>(std::ceil(r.x + r.w)), y1 = static_cast<int>(std::ceil(r.y + r.h));
    for (int y = y0; y < y1; ++y) {
        for (int x = x0; x < x1; ++x) {
            const float cov_x = std::min(x + 1.f, r.x + r.w) - std::max(static_cast<float>(x), r.x);
            const float cov_y = std::min(y + 1.f, r.y + r.h) - std::max(static_cast<float>(y), r.y);
            const float cov = std::clamp(cov_x, 0.f, 1.f) * std::clamp(cov_y, 0.f, 1.f);
            if (cov > 0.f) blend(x, y, c, cov);
        }
    }
}

void Canvas::fill_rect_rounded(const RectF& r, float radius, RGBA c) {
    fill_rect_rounded_vertical(r, radius, c, c);
}

void Canvas::fill_rect_rounded_vertical(const RectF& r, float radius, RGBA top, RGBA bottom) {
    if (r.w <= 0 || r.h <= 0) return;
    const float cx = r.x + r.w * 0.5f, cy = r.y + r.h * 0.5f;
    const float hw = r.w * 0.5f, hh = r.h * 0.5f;
    const int x0 = static_cast<int>(std::floor(r.x - 1)), x1 = static_cast<int>(std::ceil(r.x + r.w + 1));
    const int y0 = static_cast<int>(std::floor(r.y - 1)), y1 = static_cast<int>(std::ceil(r.y + r.h + 1));
    for (int y = y0; y < y1; ++y) {
        const float t = r.h > 0 ? std::clamp((y + 0.5f - r.y) / r.h, 0.f, 1.f) : 0.f;
        const RGBA col = top == bottom ? top : mix(top, bottom, t);
        for (int x = x0; x < x1; ++x) {
            const float sd = sd_rounded_box(x + 0.5f, y + 0.5f, cx, cy, hw, hh, radius);
            const float cov = std::clamp(0.5f - sd, 0.f, 1.f);
            if (cov > 0.f) blend(x, y, col, cov);
        }
    }
}

void Canvas::stroke_rect_rounded(const RectF& r, float radius, float thickness, RGBA c) {
    if (r.w <= 0 || r.h <= 0 || thickness <= 0) return;
    const float cx = r.x + r.w * 0.5f, cy = r.y + r.h * 0.5f;
    const float hw = r.w * 0.5f, hh = r.h * 0.5f;
    const int x0 = static_cast<int>(std::floor(r.x - thickness - 1));
    const int x1 = static_cast<int>(std::ceil(r.x + r.w + thickness + 1));
    const int y0 = static_cast<int>(std::floor(r.y - thickness - 1));
    const int y1 = static_cast<int>(std::ceil(r.y + r.h + thickness + 1));
    for (int y = y0; y < y1; ++y) {
        for (int x = x0; x < x1; ++x) {
            const float sd = sd_rounded_box(x + 0.5f, y + 0.5f, cx, cy, hw, hh, radius);
            const float band = std::fabs(sd + thickness * 0.5f) - thickness * 0.5f;
            const float cov = std::clamp(0.5f - band, 0.f, 1.f);
            if (cov > 0.f) blend(x, y, c, cov);
        }
    }
}

void Canvas::fill_circle(float cx, float cy, float radius, RGBA c) {
    const int x0 = static_cast<int>(std::floor(cx - radius - 1));
    const int x1 = static_cast<int>(std::ceil(cx + radius + 1));
    const int y0 = static_cast<int>(std::floor(cy - radius - 1));
    const int y1 = static_cast<int>(std::ceil(cy + radius + 1));
    for (int y = y0; y < y1; ++y)
        for (int x = x0; x < x1; ++x) {
            const float dx = x + 0.5f - cx, dy = y + 0.5f - cy;
            const float d = std::sqrt(dx * dx + dy * dy) - radius;
            const float cov = std::clamp(0.5f - d, 0.f, 1.f);
            if (cov > 0.f) blend(x, y, c, cov);
        }
}

void Canvas::stroke_circle(float cx, float cy, float radius, float thickness, RGBA c) {
    const int x0 = static_cast<int>(std::floor(cx - radius - thickness - 1));
    const int x1 = static_cast<int>(std::ceil(cx + radius + thickness + 1));
    const int y0 = static_cast<int>(std::floor(cy - radius - thickness - 1));
    const int y1 = static_cast<int>(std::ceil(cy + radius + thickness + 1));
    for (int y = y0; y < y1; ++y)
        for (int x = x0; x < x1; ++x) {
            const float dx = x + 0.5f - cx, dy = y + 0.5f - cy;
            const float d = std::fabs(std::sqrt(dx * dx + dy * dy) - radius) - thickness * 0.5f;
            const float cov = std::clamp(0.5f - d, 0.f, 1.f);
            if (cov > 0.f) blend(x, y, c, cov);
        }
}

void Canvas::line(float x0, float y0, float x1, float y1, float thickness, RGBA c) {
    const float dx = x1 - x0, dy = y1 - y0;
    const float len2 = dx * dx + dy * dy;
    const float half = std::max(0.5f, thickness * 0.5f);
    const int bx0 = static_cast<int>(std::floor(std::min(x0, x1) - half - 1));
    const int bx1 = static_cast<int>(std::ceil(std::max(x0, x1) + half + 1));
    const int by0 = static_cast<int>(std::floor(std::min(y0, y1) - half - 1));
    const int by1 = static_cast<int>(std::ceil(std::max(y0, y1) + half + 1));
    for (int y = by0; y < by1; ++y)
        for (int x = bx0; x < bx1; ++x) {
            const float px = x + 0.5f - x0, py = y + 0.5f - y0;
            const float t = len2 > 0 ? std::clamp((px * dx + py * dy) / len2, 0.f, 1.f) : 0.f;
            const float ex = px - dx * t, ey = py - dy * t;
            const float d = std::sqrt(ex * ex + ey * ey) - half;
            const float cov = std::clamp(0.5f - d, 0.f, 1.f);
            if (cov > 0.f) blend(x, y, c, cov);
        }
}

void Canvas::drop_shadow(const RectF& r, float radius, float spread, float offset_y, RGBA c, float alpha) {
    if (spread <= 0 || alpha <= 0) return;
    const RectF box{r.x - spread, r.y - spread + offset_y, r.w + 2 * spread, r.h + 2 * spread};
    const float cx = r.x + r.w * 0.5f, cy = r.y + r.h * 0.5f + offset_y;
    const float hw = r.w * 0.5f, hh = r.h * 0.5f;
    const int x0 = static_cast<int>(std::floor(box.x)), x1 = static_cast<int>(std::ceil(box.x + box.w));
    const int y0 = static_cast<int>(std::floor(box.y)), y1 = static_cast<int>(std::ceil(box.y + box.h));
    for (int y = y0; y < y1; ++y)
        for (int x = x0; x < x1; ++x) {
            const float sd = sd_rounded_box(x + 0.5f, y + 0.5f, cx, cy, hw, hh, radius);
            const float t = std::clamp(1.f - std::max(sd, 0.f) / spread, 0.f, 1.f);
            const float a = alpha * t * t;
            if (a > 0.002f) blend(x, y, c, a);
        }
}

void Canvas::blit(const Image& src, int dx, int dy, const RectI& src_rect, float opacity) {
    const RectI r = src_rect.empty() ? src.bounds() : src_rect;
    for (int y = 0; y < r.h; ++y) {
        for (int x = 0; x < r.w; ++x) {
            RGBA c = src.at(r.x + x, r.y + y);
            blend(dx + x, dy + y, c, opacity);
        }
    }
}

void Canvas::blit_clipped(const Image& src, int dx, int dy, const RectI& dst_clip, float opacity) {
    for (int y = dst_clip.y; y < dst_clip.bottom(); ++y) {
        for (int x = dst_clip.x; x < dst_clip.right(); ++x) {
            if (x < 0 || y < 0 || x >= w_ || y >= h_) continue;
            const int sx = x - dx, sy = y - dy;
            if (!src.inside(sx, sy)) continue;
            blend(x, y, src.at(sx, sy), opacity);
        }
    }
}

void Canvas::add_noise(uint32_t seed, float amount) {
    uint32_t state = seed ? seed : 1u;
    for (size_t i = 0; i < px_.size(); i += 4) {
        state ^= state << 13;
        state ^= state >> 17;
        state ^= state << 5;
        const float n = (static_cast<float>(state & 0xFFFF) / 65535.f - 0.5f) * 2.f * amount * 255.f;
        px_[i] = to_byte(px_[i] + n);
        px_[i + 1] = to_byte(px_[i + 1] + n);
        px_[i + 2] = to_byte(px_[i + 2] + n);
    }
}

Image Canvas::to_image() const {
    Image out(w_, h_);
    std::copy(px_.begin(), px_.end(), out.data());
    return out;
}

}  // namespace pgr
