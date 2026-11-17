#include "image.h"

#include <cstdio>
#include <cstring>
#include <fstream>

#include "stb/stb_image.h"
#include "stb/stb_image_write.h"

namespace pgr {
namespace {

void png_write_cb(void* context, void* data, int size) {
    auto* out = static_cast<std::vector<uint8_t>*>(context);
    const uint8_t* p = static_cast<const uint8_t*>(data);
    out->insert(out->end(), p, p + size);
}

const char* kB64 = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";

std::string b64_encode(const uint8_t* data, size_t n) {
    std::string out;
    out.reserve((n + 2) / 3 * 4);
    size_t i = 0;
    for (; i + 2 < n; i += 3) {
        uint32_t v = (uint32_t(data[i]) << 16) | (uint32_t(data[i + 1]) << 8) | data[i + 2];
        out.push_back(kB64[(v >> 18) & 63]);
        out.push_back(kB64[(v >> 12) & 63]);
        out.push_back(kB64[(v >> 6) & 63]);
        out.push_back(kB64[v & 63]);
    }
    if (i < n) {
        uint32_t v = uint32_t(data[i]) << 16;
        if (i + 1 < n) v |= uint32_t(data[i + 1]) << 8;
        out.push_back(kB64[(v >> 18) & 63]);
        out.push_back(kB64[(v >> 12) & 63]);
        out.push_back(i + 1 < n ? kB64[(v >> 6) & 63] : '=');
        out.push_back('=');
    }
    return out;
}

int b64_value(char c) {
    if (c >= 'A' && c <= 'Z') return c - 'A';
    if (c >= 'a' && c <= 'z') return c - 'a' + 26;
    if (c >= '0' && c <= '9') return c - '0' + 52;
    if (c == '+') return 62;
    if (c == '/') return 63;
    return -1;
}

}  // namespace

Image::Image(int w, int h, RGBA fill) : w_(w), h_(h) {
    if (w <= 0 || h <= 0) {
        w_ = h_ = 0;
        return;
    }
    px_.resize(static_cast<size_t>(w) * h * 4);
    for (size_t i = 0; i < px_.size(); i += 4) {
        px_[i] = fill.r;
        px_[i + 1] = fill.g;
        px_[i + 2] = fill.b;
        px_[i + 3] = fill.a;
    }
}

Image Image::load(const std::string& path, std::string* error) {
    std::ifstream in(path, std::ios::binary);
    if (!in) {
        if (error) *error = "cannot open " + path;
        return {};
    }
    std::vector<uint8_t> bytes((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
    return load_memory(bytes.data(), bytes.size(), error);
}

Image Image::load_memory(const uint8_t* data, size_t size, std::string* error) {
    if (!data || size == 0) {
        if (error) *error = "empty image buffer";
        return {};
    }
    int w = 0, h = 0, channels = 0;
    stbi_uc* pixels = stbi_load_from_memory(data, static_cast<int>(size), &w, &h, &channels, 4);
    if (!pixels) {
        if (error) *error = stbi_failure_reason() ? stbi_failure_reason() : "unsupported image";
        return {};
    }
    Image img(w, h);
    std::memcpy(img.px_.data(), pixels, static_cast<size_t>(w) * h * 4);
    stbi_image_free(pixels);
    return img;
}

Image Image::load_base64(const std::string& b64, std::string* error) {
    std::string payload = b64;
    const size_t comma = payload.find(',');
    if (payload.rfind("data:", 0) == 0 && comma != std::string::npos) payload = payload.substr(comma + 1);
    std::vector<uint8_t> out;
    out.reserve(payload.size() * 3 / 4 + 4);
    uint32_t acc = 0;
    int bits = 0;
    for (char c : payload) {
        if (c == '=' || c == '\n' || c == '\r' || c == ' ' || c == '\t') continue;
        int v = b64_value(c);
        if (v < 0) continue;
        acc = (acc << 6) | static_cast<uint32_t>(v);
        bits += 6;
        if (bits >= 8) {
            bits -= 8;
            out.push_back(static_cast<uint8_t>((acc >> bits) & 0xFF));
        }
    }
    return load_memory(out.data(), out.size(), error);
}

RGBA Image::at(int x, int y) const {
    if (!inside(x, y)) return RGBA{0, 0, 0, 0};
    const uint8_t* p = &px_[(static_cast<size_t>(y) * w_ + x) * 4];
    return RGBA{p[0], p[1], p[2], p[3]};
}

void Image::set(int x, int y, RGBA c) {
    if (!inside(x, y)) return;
    uint8_t* p = &px_[(static_cast<size_t>(y) * w_ + x) * 4];
    p[0] = c.r;
    p[1] = c.g;
    p[2] = c.b;
    p[3] = c.a;
}

RGBA Image::at_clamped(int x, int y) const {
    x = x < 0 ? 0 : (x >= w_ ? w_ - 1 : x);
    y = y < 0 ? 0 : (y >= h_ ? h_ - 1 : y);
    return at(x, y);
}

float Image::luma_at(int x, int y) const {
    RGBA c = at_clamped(x, y);
    return (0.2126f * c.r + 0.7152f * c.g + 0.0722f * c.b) / 255.f;
}

Image Image::crop(const RectI& r) const {
    RectI c = r.clip(w_, h_);
    if (c.empty()) return {};
    Image out(c.w, c.h);
    for (int y = 0; y < c.h; ++y)
        for (int x = 0; x < c.w; ++x) out.set(x, y, at(c.x + x, c.y + y));
    return out;
}

Image Image::resized(int nw, int nh) const {
    if (nw <= 0 || nh <= 0 || empty()) return {};
    Image out(nw, nh);
    const float sx = static_cast<float>(w_) / nw;
    const float sy = static_cast<float>(h_) / nh;
    for (int y = 0; y < nh; ++y) {
        for (int x = 0; x < nw; ++x) {
            const float fx = (x + 0.5f) * sx - 0.5f;
            const float fy = (y + 0.5f) * sy - 0.5f;
            const int x0 = static_cast<int>(std::floor(fx));
            const int y0 = static_cast<int>(std::floor(fy));
            const float tx = fx - x0, ty = fy - y0;
            auto blend = [&](RGBA a, RGBA b, float t) {
                return RGBA{static_cast<uint8_t>(a.r + (b.r - a.r) * t),
                            static_cast<uint8_t>(a.g + (b.g - a.g) * t),
                            static_cast<uint8_t>(a.b + (b.b - a.b) * t),
                            static_cast<uint8_t>(a.a + (b.a - a.a) * t)};
            };
            RGBA top = blend(at_clamped(x0, y0), at_clamped(x0 + 1, y0), tx);
            RGBA bot = blend(at_clamped(x0, y0 + 1), at_clamped(x0 + 1, y0 + 1), tx);
            out.set(x, y, blend(top, bot, ty));
        }
    }
    return out;
}

Image Image::scaled_to_width(int nw) const {
    if (nw <= 0 || w_ == 0) return {};
    const float k = static_cast<float>(nw) / w_;
    return resized(nw, std::max(1, static_cast<int>(std::lround(h_ * k))));
}

Image Image::with_alpha_over(RGBA bg) const {
    Image out(w_, h_);
    for (int y = 0; y < h_; ++y) {
        for (int x = 0; x < w_; ++x) {
            RGBA c = at(x, y);
            const float a = c.a / 255.f;
            out.set(x, y, RGBA{static_cast<uint8_t>(c.r * a + bg.r * (1 - a)),
                               static_cast<uint8_t>(c.g * a + bg.g * (1 - a)),
                               static_cast<uint8_t>(c.b * a + bg.b * (1 - a)), 255});
        }
    }
    return out;
}

std::vector<uint8_t> Image::encode_png() const {
    std::vector<uint8_t> out;
    if (empty()) return out;
    stbi_write_png_to_func(png_write_cb, &out, w_, h_, 4, px_.data(), w_ * 4);
    return out;
}

bool Image::save_png(const std::string& path) const {
    std::vector<uint8_t> bytes = encode_png();
    return !bytes.empty() && write_file(path, bytes.data(), bytes.size());
}

bool Image::save_bmp(const std::string& path) const {
    if (empty()) return false;
    return stbi_write_bmp(path.c_str(), w_, h_, 4, px_.data()) != 0;
}

std::string Image::encode_png_base64() const {
    std::vector<uint8_t> bytes = encode_png();
    return b64_encode(bytes.data(), bytes.size());
}

std::string Image::encode_data_uri() const {
    return "data:image/png;base64," + encode_png_base64();
}

uint32_t hash_pixels(const Image& img) {
    uint32_t h = 2166136261u;
    const uint8_t* p = img.data();
    const size_t n = static_cast<size_t>(img.width()) * img.height() * 4;
    for (size_t i = 0; i < n; i += 7) {
        h ^= p[i];
        h *= 16777619u;
    }
    return h;
}

bool write_file(const std::string& path, const void* data, size_t size) {
    std::ofstream out(path, std::ios::binary);
    if (!out) return false;
    out.write(static_cast<const char*>(data), static_cast<std::streamsize>(size));
    return out.good();
}

}  // namespace pgr
