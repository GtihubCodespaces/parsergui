#include "color.h"

#include <algorithm>
#include <array>
#include <cstdio>
#include <map>
#include <tuple>

#include "image.h"

namespace pgr {
namespace {

float srgb_to_linear(float c) {
    return c <= 0.04045f ? c / 12.92f : std::pow((c + 0.055f) / 1.055f, 2.4f);
}

float linear_to_srgb(float c) {
    return c <= 0.0031308f ? c * 12.92f : 1.055f * std::pow(c, 1.f / 2.4f) - 0.055f;
}

uint8_t clamp_byte(float v) {
    if (v <= 0.f) return 0;
    if (v >= 255.f) return 255;
    return static_cast<uint8_t>(v + 0.5f);
}

struct Box {
    std::vector<ColorCount> colors;
    int count = 0;
};

int widest_channel(const std::vector<ColorCount>& colors) {
    int lo[3] = {255, 255, 255}, hi[3] = {0, 0, 0};
    for (const auto& c : colors) {
        const int v[3] = {c.color.r, c.color.g, c.color.b};
        for (int i = 0; i < 3; ++i) {
            lo[i] = std::min(lo[i], v[i]);
            hi[i] = std::max(hi[i], v[i]);
        }
    }
    int best = 0, span = hi[0] - lo[0];
    for (int i = 1; i < 3; ++i) {
        if (hi[i] - lo[i] > span) {
            span = hi[i] - lo[i];
            best = i;
        }
    }
    return best;
}

}  // namespace

Lab to_lab(RGBA c) {
    const float r = srgb_to_linear(c.r / 255.f);
    const float g = srgb_to_linear(c.g / 255.f);
    const float b = srgb_to_linear(c.b / 255.f);
    // sRGB D65 matrix.
    const float x = (0.4124564f * r + 0.3575761f * g + 0.1804375f * b) / 0.95047f;
    const float y = 0.2126729f * r + 0.7151522f * g + 0.0721750f * b;
    const float z = (0.0193339f * r + 0.1191920f * g + 0.9503041f * b) / 1.08883f;
    auto f = [](float t) { return t > 0.008856f ? std::cbrt(t) : (7.787f * t + 16.f / 116.f); };
    const float fx = f(x), fy = f(y), fz = f(z);
    Lab out;
    out.l = 116.f * fy - 16.f;
    out.a = 500.f * (fx - fy);
    out.b = 200.f * (fy - fz);
    return out;
}

RGBA from_lab(Lab lab) {
    const float fy = (lab.l + 16.f) / 116.f;
    const float fx = fy + lab.a / 500.f;
    const float fz = fy - lab.b / 200.f;
    auto inv = [](float t) { return t * t * t > 0.008856f ? t * t * t : (t - 16.f / 116.f) / 7.787f; };
    const float x = inv(fx) * 0.95047f;
    const float y = inv(fy);
    const float z = inv(fz) * 1.08883f;
    const float r = 3.2404542f * x - 1.5371385f * y - 0.4985314f * z;
    const float g = -0.9692660f * x + 1.8760108f * y + 0.0415560f * z;
    const float b = 0.0556434f * x - 0.2040259f * y + 1.0572252f * z;
    return RGBA{clamp_byte(linear_to_srgb(std::clamp(r, 0.f, 1.f)) * 255.f),
                clamp_byte(linear_to_srgb(std::clamp(g, 0.f, 1.f)) * 255.f),
                clamp_byte(linear_to_srgb(std::clamp(b, 0.f, 1.f)) * 255.f), 255};
}

RGBA from_lab(Lab lab, uint8_t alpha) {
    RGBA c = from_lab(lab);
    c.a = alpha;
    return c;
}

float delta_e_lab(const Lab& x, const Lab& y) {
    const float dl = x.l - y.l, da = x.a - y.a, db = x.b - y.b;
    return std::sqrt(dl * dl + da * da + db * db);
}

float delta_e(RGBA a, RGBA b) { return delta_e_lab(to_lab(a), to_lab(b)); }

float luminance(RGBA c) {
    return (0.2126f * srgb_to_linear(c.r / 255.f) + 0.7152f * srgb_to_linear(c.g / 255.f) +
            0.0722f * srgb_to_linear(c.b / 255.f));
}

float contrast_ratio(RGBA a, RGBA b) {
    const float la = luminance(a), lb = luminance(b);
    const float hi = std::max(la, lb), lo = std::min(la, lb);
    return (hi + 0.05f) / (lo + 0.05f);
}

RGBA mix(RGBA a, RGBA b, float t) {
    t = std::clamp(t, 0.f, 1.f);
    return RGBA{clamp_byte(a.r + (b.r - a.r) * t), clamp_byte(a.g + (b.g - a.g) * t),
                clamp_byte(a.b + (b.b - a.b) * t), clamp_byte(a.a + (b.a - a.a) * t)};
}

RGBA with_alpha(RGBA c, uint8_t a) {
    c.a = a;
    return c;
}

float saturation(RGBA c) {
    const int hi = std::max({c.r, c.g, c.b});
    const int lo = std::min({c.r, c.g, c.b});
    return hi == 0 ? 0.f : static_cast<float>(hi - lo) / hi;
}

RGBA relative_shift(RGBA c, float amount) {
    Lab lab = to_lab(c);
    lab.l = std::clamp(lab.l + amount * 100.f, 0.f, 100.f);
    return from_lab(lab);
}

RGBA readable_on(RGBA bg) {
    return luminance(bg) > 0.35f ? RGBA(17, 18, 22) : RGBA(248, 249, 251);
}

std::vector<ColorCount> quantize(const Image& img, int max_colors, const RectI& region) {
    std::vector<ColorCount> result;
    if (img.empty() || max_colors <= 0) return result;
    RectI r = region.empty() ? img.bounds() : region.clip(img.width(), img.height());
    if (r.empty()) return result;

    std::map<uint32_t, int> histogram;
    // Sample large images on a stride so quantisation stays fast without changing
    // the dominant colours much.
    const int step = std::max(1, static_cast<int>(std::sqrt(static_cast<double>(r.area()) / 40000.0)));
    for (int y = r.y; y < r.bottom(); y += step) {
        for (int x = r.x; x < r.right(); x += step) {
            RGBA c = img.at(x, y);
            if (c.a < 8) continue;
            const uint32_t key = (uint32_t(c.r) << 16) | (uint32_t(c.g) << 8) | c.b;
            ++histogram[key];
        }
    }
    if (histogram.empty()) return result;

    std::vector<ColorCount> all;
    all.reserve(histogram.size());
    for (const auto& kv : histogram) {
        all.push_back(ColorCount{RGBA(uint8_t(kv.first >> 16), uint8_t(kv.first >> 8), uint8_t(kv.first)), kv.second});
    }
    std::sort(all.begin(), all.end(), [](const ColorCount& a, const ColorCount& b) { return a.count > b.count; });
    if (static_cast<int>(all.size()) <= max_colors) {
        result = all;
    } else {
        std::vector<Box> boxes;
        Box first;
        first.colors = all;
        for (const auto& c : all) first.count += c.count;
        boxes.push_back(first);
        while (static_cast<int>(boxes.size()) < max_colors) {
            int pick = -1, best = 1;
            for (size_t i = 0; i < boxes.size(); ++i) {
                if (boxes[i].colors.size() < 2) continue;
                int score = boxes[i].count;
                if (score > best) {
                    best = score;
                    pick = static_cast<int>(i);
                }
            }
            if (pick < 0) break;
            Box box = boxes[pick];
            const int channel = widest_channel(box.colors);
            std::sort(box.colors.begin(), box.colors.end(), [channel](const ColorCount& a, const ColorCount& b) {
                const int va = channel == 0 ? a.color.r : (channel == 1 ? a.color.g : a.color.b);
                const int vb = channel == 0 ? b.color.r : (channel == 1 ? b.color.g : b.color.b);
                return va < vb;
            });
            // Split at the weighted median.
            int total = 0;
            for (const auto& c : box.colors) total += c.count;
            int acc = 0;
            size_t split = 1;
            for (size_t i = 0; i < box.colors.size(); ++i) {
                acc += box.colors[i].count;
                if (acc * 2 >= total) {
                    split = std::max<size_t>(1, std::min(box.colors.size() - 1, i + 1));
                    break;
                }
            }
            Box lo, hi;
            lo.colors.assign(box.colors.begin(), box.colors.begin() + split);
            hi.colors.assign(box.colors.begin() + split, box.colors.end());
            for (const auto& c : lo.colors) lo.count += c.count;
            for (const auto& c : hi.colors) hi.count += c.count;
            boxes[pick] = lo;
            boxes.push_back(hi);
        }
        for (const auto& box : boxes) {
            if (box.colors.empty()) continue;
            double r = 0, g = 0, b = 0, wsum = 0;
            for (const auto& c : box.colors) {
                const double w = c.count;
                r += c.color.r * w;
                g += c.color.g * w;
                b += c.color.b * w;
                wsum += w;
            }
            result.push_back(ColorCount{RGBA(clamp_byte(float(r / wsum)), clamp_byte(float(g / wsum)),
                                             clamp_byte(float(b / wsum))),
                                        box.count});
        }
    }
    std::sort(result.begin(), result.end(), [](const ColorCount& a, const ColorCount& b) { return a.count > b.count; });
    refine_kmeans(img, result, 2);
    return result;
}

void refine_kmeans(const Image& img, std::vector<ColorCount>& colors, int iterations) {
    if (colors.size() < 2 || img.empty()) return;
    const int step = std::max(1, static_cast<int>(std::sqrt(static_cast<double>(img.width()) * img.height() / 30000.0)));
    std::vector<Lab> centers;
    centers.reserve(colors.size());
    for (const auto& c : colors) centers.push_back(to_lab(c.color));

    for (int it = 0; it < iterations; ++it) {
        std::vector<double> sum_l(colors.size()), sum_a(colors.size()), sum_b(colors.size()), weight(colors.size());
        for (int y = 0; y < img.height(); y += step) {
            for (int x = 0; x < img.width(); x += step) {
                RGBA c = img.at(x, y);
                if (c.a < 8) continue;
                Lab lab = to_lab(c);
                int best = 0;
                float best_d = 1e9f;
                for (size_t k = 0; k < centers.size(); ++k) {
                    const float d = delta_e_lab(lab, centers[k]);
                    if (d < best_d) {
                        best_d = d;
                        best = static_cast<int>(k);
                    }
                }
                sum_l[best] += lab.l;
                sum_a[best] += lab.a;
                sum_b[best] += lab.b;
                weight[best] += 1;
            }
        }
        for (size_t k = 0; k < centers.size(); ++k) {
            if (weight[k] > 0) {
                centers[k].l = static_cast<float>(sum_l[k] / weight[k]);
                centers[k].a = static_cast<float>(sum_a[k] / weight[k]);
                centers[k].b = static_cast<float>(sum_b[k] / weight[k]);
            }
        }
    }
    // Keep counts from the median-cut pass: they describe real coverage.
    for (size_t k = 0; k < colors.size(); ++k) colors[k].color = from_lab(centers[k]);
    std::sort(colors.begin(), colors.end(), [](const ColorCount& a, const ColorCount& b) { return a.count > b.count; });
}

RGBA dominant_color(const Image& img, const RectI& region, int* out_count) {
    RectI r = region.empty() ? img.bounds() : region.clip(img.width(), img.height());
    if (out_count) *out_count = 0;
    if (r.empty()) return RGBA(0, 0, 0);
    std::map<uint32_t, int> histogram;
    for (int y = r.y; y < r.bottom(); ++y) {
        for (int x = r.x; x < r.right(); ++x) {
            RGBA c = img.at(x, y);
            if (c.a < 8) continue;
            ++histogram[(uint32_t(c.r) << 16) | (uint32_t(c.g) << 8) | c.b];
        }
    }
    uint32_t best = 0;
    int best_count = -1;
    for (const auto& kv : histogram)
        if (kv.second > best_count) {
            best_count = kv.second;
            best = kv.first;
        }
    if (best_count < 0) return RGBA(0, 0, 0);
    if (out_count) *out_count = best_count;
    return RGBA(uint8_t(best >> 16), uint8_t(best >> 8), uint8_t(best));
}

RGBA dominant_ink(const Image& img, const RectI& region, RGBA background, float min_delta) {
    RectI r = region.empty() ? img.bounds() : region.clip(img.width(), img.height());
    if (r.empty()) return background;
    std::map<uint32_t, int> histogram;
    const Lab bg_lab = to_lab(background);
    for (int y = r.y; y < r.bottom(); ++y) {
        for (int x = r.x; x < r.right(); ++x) {
            RGBA c = img.at(x, y);
            if (c.a < 8) continue;
            if (delta_e_lab(to_lab(c), bg_lab) < min_delta) continue;
            ++histogram[(uint32_t(c.r) << 16) | (uint32_t(c.g) << 8) | c.b];
        }
    }
    uint32_t best = 0;
    int best_count = -1;
    for (const auto& kv : histogram)
        if (kv.second > best_count) {
            best_count = kv.second;
            best = kv.first;
        }
    if (best_count < 0) return background;
    return RGBA(uint8_t(best >> 16), uint8_t(best >> 8), uint8_t(best));
}

std::vector<PaletteEntry> build_palette(const Image& img, int max_colors) {
    std::vector<PaletteEntry> out;
    auto counts = quantize(img, max_colors);
    if (counts.empty()) return out;
    int max_count = 0;
    for (const auto& c : counts) max_count = std::max(max_count, c.count);
    out.reserve(counts.size());
    for (const auto& c : counts) {
        PaletteEntry e;
        e.color = c.color;
        e.pixels = c.count;
        e.share = max_count > 0 ? static_cast<float>(c.count) / max_count : 0.f;
        out.push_back(e);
    }
    if (out.empty()) return out;
    // Role assignment: the biggest cluster is the background, the most chromatic
    // remaining one is the accent, the highest contrast against the background is
    // the text colour.
    out[0].role = "background";
    int accent = -1;
    float best_sat = 0.12f;
    for (size_t i = 1; i < out.size(); ++i) {
        const float s = saturation(out[i].color);
        if (s > best_sat) {
            best_sat = s;
            accent = static_cast<int>(i);
        }
    }
    if (accent >= 0) out[accent].role = "accent";
    int text = -1;
    float best_contrast = 0.f;
    for (size_t i = 1; i < out.size(); ++i) {
        if (static_cast<int>(i) == accent) continue;
        const float cr = contrast_ratio(out[0].color, out[i].color);
        if (cr > best_contrast) {
            best_contrast = cr;
            text = static_cast<int>(i);
        }
    }
    if (text >= 0 && best_contrast > 3.f) out[text].role = "text";
    for (size_t i = 1; i < out.size(); ++i) {
        if (out[i].role.empty()) {
            const float cr = contrast_ratio(out[0].color, out[i].color);
            out[i].role = cr < 1.35f ? "surface" : (cr < 3.f ? "border" : "text");
        }
    }
    return out;
}

std::string hex_string(RGBA c) {
    char buf[10];
    std::snprintf(buf, sizeof(buf), "#%02X%02X%02X", c.r, c.g, c.b);
    return buf;
}

}  // namespace pgr
