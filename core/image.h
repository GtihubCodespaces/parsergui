// Image container and codec entry points. Decoding is delegated to stb_image,
// so PNG, JPEG, BMP, TGA, GIF and PSD all work from one code path.
#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include "types.h"

namespace pgr {

class Image {
public:
    Image() = default;
    Image(int w, int h, RGBA fill = RGBA(0, 0, 0, 255));

    static Image load(const std::string& path, std::string* error = nullptr);
    static Image load_memory(const uint8_t* data, size_t size, std::string* error = nullptr);
    static Image load_base64(const std::string& b64, std::string* error = nullptr);

    bool empty() const { return w_ <= 0 || h_ <= 0 || px_.size() < static_cast<size_t>(w_) * h_ * 4; }
    int width() const { return w_; }
    int height() const { return h_; }
    int stride() const { return w_ * 4; }
    RectI bounds() const { return RectI{0, 0, w_, h_}; }

    uint8_t* data() { return px_.data(); }
    const uint8_t* data() const { return px_.data(); }

    RGBA at(int x, int y) const;
    void set(int x, int y, RGBA c);
    RGBA at_clamped(int x, int y) const;
    float luma_at(int x, int y) const;
    bool inside(int x, int y) const { return x >= 0 && y >= 0 && x < w_ && y < h_; }

    Image crop(const RectI& r) const;
    Image resized(int nw, int nh) const;
    Image scaled_to_width(int nw) const;
    Image with_alpha_over(RGBA bg) const;

    // Encoders. PNG is lossless and is what the pipeline writes for previews.
    bool save_png(const std::string& path) const;
    bool save_bmp(const std::string& path) const;
    std::vector<uint8_t> encode_png() const;
    std::string encode_png_base64() const;
    std::string encode_data_uri() const;

private:
    int w_ = 0;
    int h_ = 0;
    std::vector<uint8_t> px_;  // RGBA8, row major
};

uint32_t hash_pixels(const Image& img);
bool write_file(const std::string& path, const void* data, size_t size);

}  // namespace pgr
