#include "render.h"

#include <algorithm>
#include <cmath>
#include <cstdio>

#include "color.h"
#include "font.h"
#include "raster.h"

namespace pgr {
namespace {

void draw_icon_bitmap(Canvas& canvas, const RectI& box, const std::string& name, RGBA color) {
    const IconTemplate* tmpl = nullptr;
    for (const auto& t : icon_templates())
        if (name == t.name) {
            tmpl = &t;
            break;
        }
    if (!tmpl) {
        canvas.stroke_rect_rounded(RectF(box), 2.f, 1.f, color);
        return;
    }
    // Parse rows again (cheap, few icons).
    std::vector<std::string> rows;
    std::string s(tmpl->art);
    size_t start = 0;
    while (start <= s.size()) {
        const size_t bar = s.find('|', start);
        rows.push_back(s.substr(start, bar == std::string::npos ? std::string::npos : bar - start));
        if (bar == std::string::npos) break;
        start = bar + 1;
    }
    int tw = 0;
    for (const auto& r : rows) tw = std::max(tw, static_cast<int>(r.size()));
    const int th = static_cast<int>(rows.size());
    if (tw <= 0 || th <= 0) return;
    const float sx = static_cast<float>(box.w) / tw;
    const float sy = static_cast<float>(box.h) / th;
    for (int y = 0; y < box.h; ++y) {
        for (int x = 0; x < box.w; ++x) {
            const int tx = std::min(tw - 1, static_cast<int>(x / sx));
            const int ty = std::min(th - 1, static_cast<int>(y / sy));
            if (ty < static_cast<int>(rows.size()) && tx < static_cast<int>(rows[ty].size()) && rows[ty][tx] == '#')
                canvas.blend(box.x + x, box.y + y, color, 1.f);
        }
    }
}

}  // namespace

Image render_reconstruction(const Screen& screen, const Image* source, const RenderOptions& options) {
    Canvas canvas(screen.width, screen.height, screen.background);
    std::vector<const Element*> ordered;
    for (const auto& e : screen.elements)
        if (e.type != ElementType::Window) ordered.push_back(&e);
    std::sort(ordered.begin(), ordered.end(), [](const Element* a, const Element* b) { return a->z < b->z; });

    for (const Element* e : ordered) {
        if (!e) continue;
        const RectF r(e->rect);
        if (r.w <= 0 || r.h <= 0) continue;
        if (e->shadow.present && options.draw_shadows)
            canvas.drop_shadow(r, static_cast<float>(e->radius), static_cast<float>(e->shadow.spread),
                               static_cast<float>(e->shadow.offset_y), e->shadow.color, e->shadow.alpha);

        if (e->type == ElementType::Icon) {
            if (options.draw_icons) draw_icon_bitmap(canvas, e->rect, e->icon.name, e->icon.color);
            continue;
        }
        if (e->type == ElementType::Label || e->type == ElementType::Heading) {
            // Glyph pixels are carried from the source when available.
            if (options.carry_text_pixels && source && !e->text.box.empty()) {
                const int tol = 34;
                const Lab ink = to_lab(e->text.color);
                for (int y = e->text.box.y; y < e->text.box.bottom(); ++y) {
                    for (int x = e->text.box.x; x < e->text.box.right(); ++x) {
                        if (!source->inside(x, y)) continue;
                        const RGBA c = source->at(x, y);
                        if (delta_e_lab(to_lab(c), ink) < tol) canvas.blend(x, y, c, 1.f);
                    }
                }
            }
            continue;
        }
        if (e->fill.gradient)
            canvas.fill_rect_rounded_vertical(r, static_cast<float>(e->radius), e->fill.top, e->fill.bottom);
        else
            canvas.fill_rect_rounded(r, static_cast<float>(e->radius), e->fill.top);

        if (e->border.present()) {
            // Sides may differ; draw four segments for accuracy.
            const RGBA bc = e->border.color;
            if (e->border.top > 0)
                canvas.fill_rect(RectF(r.x + e->radius, r.y, std::max(0.f, r.w - 2 * e->radius),
                                       static_cast<float>(e->border.top)), bc);
            if (e->border.bottom > 0)
                canvas.fill_rect(RectF(r.x + e->radius, r.y + r.h - e->border.bottom,
                                       std::max(0.f, r.w - 2 * e->radius),
                                       static_cast<float>(e->border.bottom)), bc);
            if (e->border.left > 0)
                canvas.fill_rect(RectF(r.x, r.y + e->radius, static_cast<float>(e->border.left),
                                       std::max(0.f, r.h - 2 * e->radius)), bc);
            if (e->border.right > 0)
                canvas.fill_rect(RectF(r.x + r.w - e->border.right, r.y + e->radius,
                                       static_cast<float>(e->border.right),
                                       std::max(0.f, r.h - 2 * e->radius)), bc);
            canvas.stroke_rect_rounded(r, static_cast<float>(e->radius),
                                       static_cast<float>(std::max(1, e->border.max_thickness())), bc);
        }
        // Any element that carries a measured text box reuses the source glyphs:
        // the score must measure geometry, not the absence of OCR.
        if (options.carry_text_pixels && source && !e->text.box.empty()) {
            const Lab ink = to_lab(e->text.color);
            for (int y = e->text.box.y; y < e->text.box.bottom(); ++y)
                for (int x = e->text.box.x; x < e->text.box.right(); ++x) {
                    if (!source->inside(x, y)) continue;
                    const RGBA c = source->at(x, y);
                    if (delta_e_lab(to_lab(c), ink) < 34.f) canvas.blend(x, y, c, 1.f);
                }
        }
    }
    return canvas.to_image();
}

Similarity compare_images(const Image& a, const Image& b) {
    Similarity s;
    if (a.empty() || b.empty() || a.width() != b.width() || a.height() != b.height()) return s;
    const int w = a.width(), h = a.height();
    const int n = w * h;
    double mae = 0;
    double mse = 0;
    int off = 0;
    for (int y = 0; y < h; ++y) {
        for (int x = 0; x < w; ++x) {
            const RGBA ca = a.at(x, y), cb = b.at(x, y);
            const int dr = std::abs(int(ca.r) - int(cb.r));
            const int dg = std::abs(int(ca.g) - int(cb.g));
            const int db = std::abs(int(ca.b) - int(cb.b));
            mae += dr + dg + db;
            mse += double(dr) * dr + double(dg) * dg + double(db) * db;
            // Cheap perceptual proxy: 30 units of RGB error is roughly deltaE 10
            // on UI colours, and avoids a Lab conversion per pixel.
            if (dr * dr + dg * dg + db * db > 900) ++off;
        }
    }
    s.mae = static_cast<float>(mae / (3.0 * n));
    const double mse_mean = mse / (3.0 * n);
    s.psnr = mse_mean > 1e-9 ? static_cast<float>(10.0 * std::log10(255.0 * 255.0 / mse_mean)) : 99.f;
    s.off_pixels = static_cast<float>(off) / n;
    s.compared = n;

    // SSIM on luma, 8x8 windows.
    const int win = 8;
    const double C1 = 0.01 * 0.01, C2 = 0.03 * 0.03;
    double ssim_sum = 0;
    int windows = 0;
    std::vector<double> la(static_cast<size_t>(win) * win), lb(static_cast<size_t>(win) * win);
    for (int wy = 0; wy + win <= h; wy += win) {
        for (int wx = 0; wx + win <= w; wx += win) {
            double ma = 0, mb = 0;
            for (int y = 0; y < win; ++y)
                for (int x = 0; x < win; ++x) {
                    const double va = a.luma_at(wx + x, wy + y);
                    const double vb = b.luma_at(wx + x, wy + y);
                    la[y * win + x] = va;
                    lb[y * win + x] = vb;
                    ma += va;
                    mb += vb;
                }
            ma /= win * win;
            mb /= win * win;
            double va = 0, vb = 0, cov = 0;
            for (int i = 0; i < win * win; ++i) {
                const double da = la[i] - ma, db = lb[i] - mb;
                va += da * da;
                vb += db * db;
                cov += da * db;
            }
            va /= win * win - 1;
            vb /= win * win - 1;
            cov /= win * win - 1;
            const double num = (2 * ma * mb + C1) * (2 * cov + C2);
            const double den = (ma * ma + mb * mb + C1) * (va + vb + C2);
            ssim_sum += den != 0 ? num / den : 1.0;
            ++windows;
        }
    }
    s.ssim = windows > 0 ? static_cast<float>(ssim_sum / windows) : 0.f;
    const float mae_score = 1.f - std::clamp(s.mae / 40.f, 0.f, 1.f);
    s.fidelity = std::clamp((0.55f * s.ssim + 0.45f * mae_score) * 100.f, 0.f, 100.f);
    return s;
}

Image make_diff_image(const Image& a, const Image& b, int amplify) {
    if (a.empty() || b.empty() || a.width() != b.width() || a.height() != b.height()) return {};
    Image out(a.width(), a.height());
    for (int y = 0; y < a.height(); ++y) {
        for (int x = 0; x < a.width(); ++x) {
            const RGBA ca = a.at(x, y), cb = b.at(x, y);
            const int d = std::min(255, amplify * (std::abs(int(ca.r) - int(cb.r)) +
                                                   std::abs(int(ca.g) - int(cb.g)) +
                                                   std::abs(int(ca.b) - int(cb.b))) / 3);
            out.set(x, y, RGBA(static_cast<uint8_t>(std::min(255, d * 2)), static_cast<uint8_t>(d / 2), 20, 255));
        }
    }
    return out;
}

std::string fidelity_summary(const Similarity& s) {
    char buf[256];
    std::snprintf(buf, sizeof(buf), "SSIM %.4f, MAE %.2f, PSNR %.1f dB, off-pixels %.2f%%, fidelity %.1f/100",
                  s.ssim, s.mae, s.psnr, s.off_pixels * 100.f, s.fidelity);
    return buf;
}

}  // namespace pgr
