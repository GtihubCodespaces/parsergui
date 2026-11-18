#include "detect.h"

#include <algorithm>
#include <cassert>
#include <cctype>
#include <cmath>
#include <functional>
#include <map>
#include <set>
#include <unordered_map>

#include "color.h"
#include "font.h"
#include "segment.h"

namespace pgr {
namespace {

constexpr float kFillTolerance = 7.f;

RGBA median_ring_color(const Image& img, int inset) {
    std::vector<RGBA> samples;
    const int step = std::max(1, img.width() / 64);
    for (int x = inset; x < img.width() - inset; x += step) {
        samples.push_back(img.at(x, inset));
        samples.push_back(img.at(x, img.height() - 1 - inset));
    }
    const int step_y = std::max(1, img.height() / 64);
    for (int y = inset; y < img.height() - inset; y += step_y) {
        samples.push_back(img.at(inset, y));
        samples.push_back(img.at(img.width() - 1 - inset, y));
    }
    if (samples.empty()) return RGBA(0, 0, 0);
    std::vector<ColorCount> counts;
    for (const auto& s : samples) {
        bool merged = false;
        for (auto& c : counts) {
            if (near_color(c.color, s, 3.f)) {
                ++c.count;
                merged = true;
                break;
            }
        }
        if (!merged) counts.push_back(ColorCount{s, 1});
    }
    int best = 0;
    for (size_t i = 1; i < counts.size(); ++i)
        if (counts[i].count > counts[best].count) best = static_cast<int>(i);
    return counts.empty() ? RGBA(0, 0, 0) : counts[best].color;
}

bool color_matches(const Image& img, int x, int y, RGBA target, float tol) {
    if (!img.inside(x, y)) return false;
    const RGBA c = img.at(x, y);
    const int d = std::max({std::abs(int(c.r) - int(target.r)), std::abs(int(c.g) - int(target.g)),
                            std::abs(int(c.b) - int(target.b))});
    if (d <= 2) return true;
    if (d > 16) return false;
    return delta_e(c, target) <= tol;
}

// Colour distance that stays meaningful when two dark surfaces sit close in Lab.
float adaptive_tolerance(RGBA fill, RGBA outside) {
    return std::clamp(delta_e(fill, outside) * 0.32f, 1.6f, 6.0f);
}

RGBA sample_median(const Image& img, const std::vector<std::pair<int, int>>& pts) {
    if (pts.empty()) return RGBA(0, 0, 0);
    std::vector<RGBA> cols;
    cols.reserve(pts.size());
    for (const auto& p : pts) cols.push_back(img.at(p.first, p.second));
    std::sort(cols.begin(), cols.end(), [](RGBA a, RGBA b) { return luminance(a) < luminance(b); });
    return cols[cols.size() / 2];
}

// Returns the fill colour of a rectangle, ignoring its border and any children.
RGBA sample_fill(const Image& img, const RectI& rect, RGBA outside) {
    RectI r = rect.clip(img.width(), img.height());
    if (r.empty()) return outside;
    std::unordered_map<uint32_t, ColorCount> hist;
    const int inset = 2;
    const int area = std::max(1, (r.w - 2 * inset) * (r.h - 2 * inset));
    const int step = std::max(1, static_cast<int>(std::sqrt(static_cast<double>(area) / 20000.0)));
    for (int y = r.y + inset; y < r.bottom() - inset; y += step) {
        for (int x = r.x + inset; x < r.right() - inset; x += step) {
            RGBA c = img.at(x, y);
            if (delta_e(c, outside) < 4.f) continue;
            const uint32_t key = (uint32_t(c.r) << 16) | (uint32_t(c.g) << 8) | c.b;
            auto it = hist.find(key);
            if (it == hist.end())
                hist.emplace(key, ColorCount{c, 1});
            else
                ++it->second.count;
        }
    }
    if (hist.empty()) return outside;
    int best = 0;
    RGBA best_color = outside;
    for (const auto& kv : hist)
        if (kv.second.count > best) {
            best = kv.second.count;
            best_color = kv.second.color;
        }
    return best_color;
}

int scan_radius_corner(const Image& img, const RectI& rect, RGBA fill, RGBA outside, int corner) {
    const int limit = std::min({rect.w / 2, rect.h / 2, 40});
    if (limit < 2) return 0;
    const float tol = adaptive_tolerance(fill, outside);
    std::vector<int> profile;  // measured inset per row/column from the edge
    const int samples = std::min(20, limit);
    for (int d = 0; d < samples; ++d) {
        int inset = -1;
        for (int k = 0; k < limit; ++k) {
            int x = 0, y = 0;
            switch (corner) {
                case 0:
                    x = rect.x + k;
                    y = rect.y + d;
                    break;
                case 1:
                    x = rect.right() - 1 - k;
                    y = rect.y + d;
                    break;
                case 2:
                    x = rect.x + k;
                    y = rect.bottom() - 1 - d;
                    break;
                default:
                    x = rect.right() - 1 - k;
                    y = rect.bottom() - 1 - d;
                    break;
            }
            if (color_matches(img, x, y, fill, tol)) {
                inset = k;
                break;
            }
        }
        if (inset < 0) return 0;
        profile.push_back(inset);
    }
    int max_inset = 0;
    for (int v : profile) max_inset = std::max(max_inset, v);
    if (max_inset <= 1) return 0;
    int best_r = 0;
    double best_err = 1e18;
    for (int r = 1; r <= limit; ++r) {
        double err = 0;
        for (size_t d = 0; d < profile.size(); ++d) {
            const double dy = static_cast<double>(d) + 0.5;
            double pred = 0;
            if (dy < r) {
                const double t = r - dy;
                pred = r - std::sqrt(std::max(0.0, static_cast<double>(r) * r - t * t));
            }
            const double diff = profile[d] - pred;
            err += diff * diff;
        }
        if (err < best_err) {
            best_err = err;
            best_r = r;
        }
    }
    // A rounded corner needs a profile that actually recedes: reject clipped or
    // noisy corners where the first row already starts at the edge.
    if (profile.front() <= 0) return 0;
    if (best_err / profile.size() > 6.0) return 0;
    return best_r;
}

}  // namespace

int measure_radius(const Image& img, const RectI& rect, RGBA fill, RGBA outside, int inset) {
    RectI inner = rect;
    inset = std::clamp(inset, 0, 4);
    inner.x += inset;
    inner.y += inset;
    inner.w -= 2 * inset;
    inner.h -= 2 * inset;
    if (inner.w < 6 || inner.h < 4) return 0;
    std::vector<int> radii;
    for (int c = 0; c < 4; ++c) {
        const int r = scan_radius_corner(img, inner, fill, outside, c);
        if (r > 0) radii.push_back(r);
    }
    if (radii.size() < 2) return 0;
    std::sort(radii.begin(), radii.end());
    // Corners agree on real radii; a single clipped corner should not win.
    const int mid = radii[radii.size() / 2];
    const int result = radii.back() - radii.front() > std::max(2, mid / 2) ? radii.front() : mid;
    return result + inset;
}

BorderInfo measure_border(const Image& img, RectI& rect, RGBA fill, RGBA outside) {
    BorderInfo b;
    if (rect.w < 6 || rect.h < 6) return b;
    // The palette component covers the fill only: a 1px stroke is a separate
    // colour band immediately outside it. Walk outward, one side at a time, and
    // grow the rectangle when a uniform band is found.
    for (int pass = 0; pass < 3; ++pass) {
        bool changed = false;
        auto ring = [&](int side_index, RGBA* color, double* uniformity) {
            std::vector<std::pair<int, int>> pts;
            if (side_index == 0) {
                const int y = rect.y - 1;
                if (y < 0) return false;
                for (int x = rect.x + 2; x < rect.right() - 2; ++x) pts.emplace_back(x, y);
            } else if (side_index == 2) {
                const int y = rect.bottom();
                if (y >= img.height()) return false;
                for (int x = rect.x + 2; x < rect.right() - 2; ++x) pts.emplace_back(x, y);
            } else if (side_index == 3) {
                const int x = rect.x - 1;
                if (x < 0) return false;
                for (int y = rect.y + 2; y < rect.bottom() - 2; ++y) pts.emplace_back(x, y);
            } else {
                const int x = rect.right();
                if (x >= img.width()) return false;
                for (int y = rect.y + 2; y < rect.bottom() - 2; ++y) pts.emplace_back(x, y);
            }
            if (pts.size() < 6) return false;
            *color = sample_median(img, pts);
            int near = 0;
            for (const auto& p : pts)
                if (delta_e(img.at(p.first, p.second), *color) < 3.5f) ++near;
            *uniformity = static_cast<double>(near) / pts.size();
            return true;
        };
        for (int side = 0; side < 4; ++side) {
            RGBA color;
            double uniformity = 0;
            if (!ring(side, &color, &uniformity)) continue;
            if (uniformity < 0.72) continue;
            if (delta_e(color, fill) < 2.2f) continue;
            if (delta_e(color, outside) < 2.2f) continue;
            // Border shades stay close to the surface they frame; a neighbouring
            // element with an unrelated background is rejected here.
            if (std::fabs(luminance(color) - luminance(fill)) > 0.28f) continue;
            b.color = color;
            if (side == 0) ++b.top;
            else if (side == 1) ++b.right;
            else if (side == 2) ++b.bottom;
            else ++b.left;
            if (side == 0) rect.y -= 1, rect.h += 1;
            else if (side == 1) rect.w += 1;
            else if (side == 2) rect.h += 1;
            else rect.x -= 1, rect.w += 1;
            changed = true;
        }
        if (!changed) break;
    }
    return b;
}

bool measure_gradient(const Image& img, const RectI& rect, RGBA fill, FillInfo* out) {
    if (!out || rect.h < 8 || rect.w < 8) return false;
    const int band = std::max(2, rect.h / 12);
    std::vector<double> row_luma;
    std::vector<RGBA> row_colors;
    for (int y = rect.y + 1; y < rect.bottom() - 1; ++y) {
        std::vector<RGBA> cols;
        for (int x = rect.x + 1; x < rect.right() - 1; ++x) cols.push_back(img.at(x, y));
        if (cols.empty()) continue;
        std::sort(cols.begin(), cols.end(), [](RGBA a, RGBA b) { return luminance(a) < luminance(b); });
        const RGBA m = cols[cols.size() / 2];
        row_colors.push_back(m);
        row_luma.push_back(luminance(m));
    }
    if (row_colors.size() < 6) return false;
    const RGBA top = row_colors[std::min(row_colors.size() - 1, static_cast<size_t>(band))];
    const RGBA bottom = row_colors[row_colors.size() - 1 - std::min(row_colors.size() - 1, static_cast<size_t>(band))];

    // Monotonicity: a real gradient moves in one direction without flipping.
    int up = 0, down = 0;
    for (size_t i = 1; i < row_luma.size(); ++i) {
        const double d = row_luma[i] - row_luma[i - 1];
        if (d > 0.15) ++up;
        else if (d < -0.15) ++down;
    }
    const double total_steps = static_cast<double>(std::max<size_t>(1, row_luma.size() - 1));
    const double monotonic = std::max(up, down) / total_steps;
    const float dtop = delta_e(top, bottom);
    if (dtop > 3.5f && monotonic > 0.78) {
        out->top = top;
        out->bottom = bottom;
        out->gradient = true;
        return true;
    }
    out->top = fill;
    out->bottom = fill;
    out->gradient = false;
    return false;
}

ShadowInfo measure_shadow(const Image& img, const RectI& rect, RGBA outside) {
    ShadowInfo s;
    if (rect.w < 16 || rect.h < 10) return s;
    // Signed darkness: a shadow is darker than the surface around it. A lighter
    // halo is a border or a raised surface, not a shadow.
    auto ring_mean = [&](int inset, bool bottom_side) {
        double dl = 0;
        int n = 0;
        const int step = std::max(1, rect.w / 32);
        for (int x = rect.x + rect.w / 6; x < rect.right() - rect.w / 6; x += step) {
            const int y = bottom_side ? rect.bottom() - 1 + inset : rect.y - inset;
            if (!img.inside(x, y)) continue;
            dl += (luminance(outside) - luminance(img.at(x, y))) * 255.0;
            ++n;
        }
        return n > 0 ? dl / n : 0.0;
    };
    double spread_score = 0;
    int spread = 0;
    for (int d = 1; d <= 12; ++d) {
        const double top = ring_mean(d, false);
        const double bottom = ring_mean(d, true);
        spread_score += std::max(0.0, top) + std::max(0.0, bottom);
        if (top + bottom > 3.0) spread = d;
    }
    if (spread >= 2) {
        s.present = true;
        s.spread = spread;
        s.offset_y = 2;
        s.alpha = static_cast<float>(std::clamp(spread_score / 120.0, 0.05, 0.5));
        s.color = RGBA(0, 0, 0);
    }
    return s;
}

int infer_base_unit(const std::vector<int>& measurements, int* confidence) {
    if (confidence) *confidence = 0;
    if (measurements.empty()) return 8;
    // Ignore hairlines: border and divider thickness say nothing about the spacing
    // rhythm and would drag the estimate down to 2.
    std::vector<int> usable;
    for (int m : measurements)
        if (m >= 4) usable.push_back(m);
    if (usable.empty()) {
        if (confidence) *confidence = 0;
        return 8;
    }
    const int candidates[] = {4, 6, 8, 10, 12};
    int best_unit = 8;
    double best_score = -1;
    for (int u : candidates) {
        double score = 0;
        for (int m : usable) {
            const int rem = m % u;
            const int dist = std::min(rem, u - rem);
            if (dist == 0) score += 1.0;
            else if (dist == 1) score += 0.5;
            else score += 0.05;
        }
        score /= std::max<size_t>(1, usable.size());
        // Prefer the 8px rhythm when both explain the data, then 4px. Design systems
        // built on 4 still work with 8 as the reported step.
        if (u == 8) score *= 1.12;
        if (u == 4) score *= 1.06;
        if (score > best_score) {
            best_score = score;
            best_unit = u;
        }
    }
    if (confidence) *confidence = static_cast<int>(std::round(std::clamp(best_score, 0.0, 1.0) * 100));
    return best_unit;
}

namespace {

std::string glyph_class_of(int glyphs, int width) {
    if (glyphs <= 0) return "unknown";
    const double avg = static_cast<double>(width) / glyphs;
    if (avg < 7.0) return "numeric";
    return "alpha";
}

// The ink of a small monochrome glyph is the pixel furthest from the background:
// averaging the box would sample mostly background and lose the colour.
RGBA extreme_ink(const Image& img, const RectI& box, RGBA background) {
    RGBA best = background;
    float best_delta = 0.f;
    const Lab bg = to_lab(background);
    const RectI r = box.clip(img.width(), img.height());
    for (int y = r.y; y < r.bottom(); ++y)
        for (int x = r.x; x < r.right(); ++x) {
            const RGBA c = img.at(x, y);
            const float d = delta_e_lab(to_lab(c), bg);
            if (d > best_delta) {
                best_delta = d;
                best = c;
            }
        }
    return best;
}

bool saturated(RGBA c) {
    const int mx = std::max(std::max(c.r, c.g), c.b);
    const int mn = std::min(std::min(c.r, c.g), c.b);
    return mx - mn > 55 && mx > 90;
}

// Second pass, after the tree exists: roles that only make sense in context.
// A menu row is a label inside a popup, a tab is a label over an accent rule, a
// checkbox is a small box next to its label. Geometry alone cannot tell them apart.
void refine_roles(Screen& screen, int img_w, int img_h) {
    auto& els = screen.elements;
    if (els.empty()) return;
    auto is_text = [&](int id) {
        return id >= 0 && id < static_cast<int>(els.size()) && els[static_cast<size_t>(id)].text.size_px > 0.f;
    };

    int menu_bar = -1;
    int menu_y = img_h;
    for (const auto& e : els)
        if (e.type == ElementType::Panel && e.rect.h >= 26 && e.rect.h <= 56 && e.rect.y <= 48 &&
            e.rect.w >= img_w / 2 && e.rect.y < menu_y) {
            menu_bar = e.id;
            menu_y = e.rect.y;
        }
    if (menu_bar >= 0) {
        for (int c : els[static_cast<size_t>(menu_bar)].children)
            if (is_text(c) && els[static_cast<size_t>(c)].rect.h <= 26 &&
                els[static_cast<size_t>(c)].rect.x >= els[static_cast<size_t>(menu_bar)].rect.x + 80)
                els[static_cast<size_t>(c)].type = ElementType::MenuItem;
    }

    // Dropdowns: a floating panel with a vertical stack of labels on one edge.
    for (auto& e : els) {
        if (e.type != ElementType::Panel) continue;
        if (e.rect.w < 120 || e.rect.w > 420 || e.rect.h < 100 || e.rect.h > 320) continue;
        if (e.rect.y < 16 || e.rect.y > 300) continue;
        // A form panel also stacks labels; it is not a menu. Controls give it away.
        bool has_controls = false;
        for (int c : e.children) {
            const ElementType t = els[static_cast<size_t>(c)].type;
            if (t == ElementType::Input || t == ElementType::Toggle || t == ElementType::Slider ||
                t == ElementType::Button)
                has_controls = true;
        }
        if (has_controls) continue;
        std::vector<int> labels;
        for (int c : e.children)
            if (is_text(c)) labels.push_back(c);
        if (labels.size() < 3) continue;
        // Split into columns: a row label and its shortcut are two stacks. The
        // largest stack with a consistent vertical rhythm is the item column.
        std::vector<int> column;
        for (int c : labels) {
            std::vector<int> group;
            const int cx = els[static_cast<size_t>(c)].rect.x;
            for (int o : labels)
                if (std::abs(els[static_cast<size_t>(o)].rect.x - cx) <= 8) group.push_back(o);
            if (group.size() > column.size()) column = group;
        }
        if (column.size() < 3) continue;
        std::sort(column.begin(), column.end(), [&](int a, int b) {
            return els[static_cast<size_t>(a)].rect.y < els[static_cast<size_t>(b)].rect.y;
        });
        int consecutive = 0;
        for (size_t i = 1; i < column.size(); ++i) {
            const int dy = els[static_cast<size_t>(column[i])].rect.y -
                           els[static_cast<size_t>(column[i - 1])].rect.y;
            if (dy >= 18 && dy <= 52) ++consecutive;
        }
        if (consecutive < 2) continue;
        const int top = els[static_cast<size_t>(column.front())].rect.x;
        for (int c : column) {
            if (els[static_cast<size_t>(c)].rect.x - top <= 8)
                els[static_cast<size_t>(c)].type = ElementType::MenuItem;
        }
    }

    // Tabs: a label sitting on a thin saturated rule.
    for (auto& e : els) {
        if (e.type != ElementType::Label || e.text.size_px <= 0.f) continue;
        bool tab = false;
        for (const auto& rule : els) {
            if (rule.rect.h > 4 || rule.rect.h <= 0 || rule.rect.w < 16) continue;
            if (!saturated(rule.fill.top)) continue;
            if (rule.rect.y < e.rect.bottom() || rule.rect.y > e.rect.bottom() + 12) continue;
            if (rule.rect.x < e.rect.x - 8 || rule.rect.x > e.rect.x + 8) continue;
            tab = true;
            break;
        }
        if (!tab) continue;
        for (auto& sib : els)
            if (sib.type == ElementType::Label && std::abs(sib.rect.y - e.rect.y) <= 2 &&
                sib.rect.x >= e.rect.x - 4 && sib.rect.h <= e.rect.h + 4)
                sib.type = ElementType::Tab;
    }

    // List rows: wide short panels holding several cells.
    for (auto& e : els) {
        if (e.type != ElementType::Panel) continue;
        if (e.id == menu_bar) continue;
        if (e.rect.w >= img_w * 9 / 10) continue;  // menu bar, toolbar, status bar
        if (e.rect.h < 30 || e.rect.h > 52 || e.rect.w < 200) continue;
        int labels = 0;
        for (int c : e.children)
            if (is_text(c)) ++labels;
        if (labels >= 2) e.type = ElementType::ListItem;
    }
    // Sidebar: a tall narrow panel with a column of labels.
    for (auto& e : els) {
        if (e.type != ElementType::Panel) continue;
        if (e.rect.h < 280 || e.rect.w > 340 || e.rect.w < 120) continue;
        std::vector<int> labels;
        for (int c : e.children)
            if (is_text(c) && els[static_cast<size_t>(c)].type == ElementType::Label) labels.push_back(c);
        if (labels.size() < 4) continue;
        int min_x = img_w, max_x = 0;
        for (int c : labels) {
            min_x = std::min(min_x, els[static_cast<size_t>(c)].rect.x);
            max_x = std::max(max_x, els[static_cast<size_t>(c)].rect.x);
        }
        if (max_x - min_x > 24) continue;
        for (int c : labels) els[static_cast<size_t>(c)].type = ElementType::ListItem;
    }

    for (auto& e : els) {
        // Badges: a pill with its own text.
        if (e.rect.h >= 14 && e.rect.h <= 26 && e.rect.w <= 170 && e.radius >= 4) {
            bool has_text = false;
            for (int c : e.children)
                if (is_text(c)) has_text = true;
            if (has_text) e.type = ElementType::Badge;
        }
        // Toggles: a checkbox or a switch next to its label.
        const bool checkbox = e.rect.w >= 12 && e.rect.w <= 26 && e.rect.h >= 12 && e.rect.h <= 26;
        const bool switch_box = e.rect.w >= 28 && e.rect.w <= 64 && e.rect.h >= 14 && e.rect.h <= 30 &&
                                e.radius >= e.rect.h / 3;
        if ((checkbox || switch_box) && e.parent != menu_bar && e.type != ElementType::Icon) {
            bool labelled = false;
            for (const auto& other : els)
                if (other.id != e.id && is_text(other.id) && std::abs(other.rect.cy() - e.rect.cy()) <= 6 &&
                    other.rect.x >= e.rect.right() && other.rect.x - e.rect.right() <= 22) {
                    labelled = true;
                    break;
                }
            if (labelled) e.type = ElementType::Toggle;
        }
        // Sliders and progress bars: thin tracks, with or without a knob. Text
        // lines are also thin, so they must be excluded explicitly.
        if (e.rect.h <= 10 && e.rect.w >= 40 && e.text.size_px <= 0.f) {
            bool knob = false;
            for (const auto& other : els)
                if (other.rect.w >= 10 && other.rect.w <= 20 && other.rect.h >= 10 && other.rect.h <= 20 &&
                    std::abs(other.rect.cy() - e.rect.cy()) <= 6 && other.rect.x >= e.rect.right() - 10 &&
                    other.rect.x <= e.rect.right() + 16) {
                    knob = true;
                    break;
                }
            e.type = knob ? ElementType::Slider : ElementType::Progress;
        }
        // Icon-only toolbar buttons.
        if (e.type == ElementType::Panel && e.rect.w >= 24 && e.rect.w <= 48 && e.rect.h >= 24 && e.rect.h <= 48 &&
            e.radius >= 4) {
            e.type = ElementType::Button;
        }
    }

    // Inputs: a bordered field whose text is left aligned, unlike a button label.
    for (auto& e : els) {
        if (e.type != ElementType::Button) continue;
        if (!e.border.present() || e.rect.h < 28 || e.rect.h > 52 || e.rect.w < 80) continue;
        if (e.text.size_px <= 0.f) continue;
        const int left_gap = e.text.box.x - e.rect.x;
        const int centered = std::abs(e.text.box.cx() - e.rect.cx());
        if (left_gap >= 8 && left_gap <= e.rect.w / 2 && centered > 12) e.type = ElementType::Input;
    }

    (void)img_h;
}

ElementType classify(const Screen& screen, const Element& e, RGBA parent_fill, int image_area) {
    const int area = e.rect.area();
    if (area >= image_area * 0.90) return ElementType::Window;
    if (e.rect.h <= 2 && e.rect.w > 12) return ElementType::Divider;
    if (e.rect.w <= 2 && e.rect.h > 12) return ElementType::Divider;
    const bool has_text = !e.text.text.empty() || e.text.size_px > 0.f;
    const bool differs = delta_e(e.fill.top, parent_fill) > 3.f;

    // Icons are decided by the dedicated pass: it merges nearby strokes and can
    // name the glyph. Classifying a bare square here produced unnamed duplicates.
    if (e.rect.w >= 60 && e.rect.w <= 260 && e.rect.h >= 14 && e.rect.h <= 28 &&
        static_cast<float>(e.rect.w) / e.rect.h > 1.6f && e.radius >= 6 && !has_text && differs)
        return ElementType::Toggle;
    if (e.rect.h >= 24 && e.rect.h <= 56 && (has_text || (differs && e.radius >= 4))) {
        if (e.border.present() || differs || e.radius >= 4) return ElementType::Button;
        return ElementType::Label;
    }
    if (e.rect.h >= 28 && e.rect.h <= 64 && e.border.present() && has_text) return ElementType::Input;
    if (e.rect.h <= 26 && e.radius >= 8 && e.rect.w <= 180 && has_text) return ElementType::Badge;
    if (has_text) return e.text.size_px >= 22.f ? ElementType::Heading : ElementType::Label;
    if (e.rect.w >= 24 && e.rect.h >= 24) {
        if (e.rect.h <= 14 && e.rect.w >= 60) return ElementType::Progress;
        if (e.border.present() && e.rect.w <= 80 && e.rect.h <= 80) return ElementType::Image;
        if (!e.children.empty() || area > image_area / 40) return ElementType::Panel;
    }
    if (e.shadow.present && area > image_area / 30) return ElementType::Panel;
    return ElementType::Unknown;
}

}  // namespace

Screen detect_screen(const Image& source, const AnalysisOptions& options, const std::string& name, DetectDebug* debug) {
    Screen screen;
    screen.name = name;
    if (source.empty()) return screen;
    Image img = source;
    if (img.at(0, 0).a < 250) img = img.with_alpha_over(RGBA(255, 255, 255));
    screen.width = img.width();
    screen.height = img.height();
    screen.background = median_ring_color(img, std::min(3, std::max(0, std::min(img.width(), img.height()) / 20 - 1)));
    screen.palette = build_palette(img, options.max_colors);

    const int image_area = img.width() * img.height();
    std::vector<RGBA> palette_colors;
    palette_colors.reserve(screen.palette.size());
    for (const auto& p : screen.palette) palette_colors.push_back(p.color);
    const LabelMap map = palette_colors.empty() ? build_label_map(img, std::min(options.max_colors, 24))
                                                : build_label_map(img, palette_colors);
    const auto comps = components(map, std::max(3, options.min_element_size), 40000);
    if (debug) debug->components = static_cast<int>(comps.size());

    const int max_glyph = std::max(9, std::min(42, img.height() / 22));
    std::vector<TextRun> runs = merge_text_runs(comps, max_glyph, 2);
    if (debug) debug->text_runs = static_cast<int>(runs.size());

    // Mark components that are glyphs so they never become surfaces.
    std::vector<uint8_t> is_glyph(comps.size(), 0);
    for (size_t i = 0; i < comps.size(); ++i) {
        if (comps[i].bbox.h > max_glyph) continue;
        for (const auto& run : runs) {
            if (run.box.contains(comps[i].bbox)) {
                is_glyph[i] = 1;
                break;
            }
        }
    }

    // Surface candidates: large, non-glyph, reasonably solid components.
    struct Cand {
        RectI rect;
        RGBA color;
        int area = 0;
    };
    std::vector<Cand> cands;
    for (size_t i = 0; i < comps.size(); ++i) {
        if (is_glyph[i]) continue;
        const auto& c = comps[i];
        // Keep 2px tab indicators and rules, but drop 1px hairlines that are only
        // anti-aliasing noise. A thin line must be wide enough to be deliberate.
        if (c.bbox.h < 2 || c.bbox.w < 3) continue;
        if (c.bbox.h < 4 && c.bbox.w < 32) continue;
        if (c.bbox.area() < std::max(48, options.min_element_size * options.min_element_size)) continue;
        const double fill_ratio = static_cast<double>(c.area) / std::max(1, c.bbox.area());
        if (fill_ratio < 0.12) continue;
        cands.push_back(Cand{c.bbox, c.color, c.area});
    }
    // Merge near-duplicate rectangles (a card split by a same-colour divider).
    std::sort(cands.begin(), cands.end(), [](const Cand& a, const Cand& b) { return a.rect.area() > b.rect.area(); });
    std::vector<Cand> surfaces;
    for (const auto& c : cands) {
        bool duplicate = false;
        for (const auto& s : surfaces) {
            if (c.rect.iou(s.rect) > 0.90) {
                duplicate = true;
                break;
            }
            // A horizontal band contained in a larger box with the same colour is
            // part of that box, not its own element.
            if (s.rect.contains(c.rect) && delta_e(s.color, c.color) < 4.f &&
                c.rect.w >= s.rect.w * 0.8) {
                duplicate = true;
                break;
            }
        }
        if (!duplicate) surfaces.push_back(c);
    }
    if (debug) debug->surface_candidates = static_cast<int>(surfaces.size());

    // Root window covers the whole frame.
    Element root;
    root.id = 0;
    root.type = ElementType::Window;
    root.rect = img.bounds();
    root.fill.top = root.fill.bottom = screen.background;
    root.confidence = 1.f;
    root.z = 0;
    root.notes.push_back("full frame");
    screen.elements.push_back(root);
    screen.root = 0;

    for (const auto& s : surfaces) {
        if (s.rect.area() >= image_area * 0.985) continue;
        Element e;
        e.rect = s.rect.clip(img.width(), img.height());
        if (e.rect.empty()) continue;
        // Use the component colour: it is the exact surface that was segmented.
        e.fill.top = e.fill.bottom = s.color;
        e.id = static_cast<int>(screen.elements.size());
        screen.elements.push_back(e);
    }

    // Hierarchy: smallest strictly larger container wins.
    for (size_t i = 1; i < screen.elements.size(); ++i) {
        Element& e = screen.elements[i];
        int parent = 0;
        int best_area = image_area * 2;
        for (size_t j = 0; j < screen.elements.size(); ++j) {
            if (i == j) continue;
            const Element& p = screen.elements[j];
            if (p.rect.area() <= e.rect.area()) continue;
            if (!p.rect.contains(e.rect)) continue;
            if (p.rect.area() < best_area) {
                best_area = p.rect.area();
                parent = p.id;
            }
        }
        e.parent = parent;
    }
    for (size_t i = 1; i < screen.elements.size(); ++i) {
        Element& e = screen.elements[i];
        screen.elements[e.parent].children.push_back(e.id);
    }

    // Measurements per surface.
    for (auto& e : screen.elements) {
        if (e.type == ElementType::Window) continue;
        // The fill is the colour of the connected region that produced this
        // element. Sampling the interior instead would pick up large children.
        if (e.fill.top.a == 0) e.fill.top = e.fill.bottom = screen.background;
        FillInfo grad;
        if (measure_gradient(img, e.rect, e.fill.top, &grad)) e.fill = grad;
        const RGBA outside = (e.parent >= 0 && e.parent < static_cast<int>(screen.elements.size()))
                                 ? screen.elements[e.parent].fill.top
                                 : screen.background;
        e.border = measure_border(img, e.rect, e.fill.top, outside);
        e.rect = e.rect.clip(img.width(), img.height());
        e.radius = measure_radius(img, e.rect, e.fill.top, outside, e.border.max_thickness());
        e.shadow = measure_shadow(img, e.rect, outside);
        e.confidence = 0.55f;
    }

    // First classification pass: geometry only, enough to recognise controls so
    // their labels attach to them instead of becoming siblings.
    for (auto& e : screen.elements) {
        if (e.type == ElementType::Window) continue;
        const RGBA parent_fill = e.parent >= 0 ? screen.elements[e.parent].fill.top : screen.background;
        e.type = classify(screen, e, parent_fill, image_area);
    }

    // Text runs become elements, or attach to the control that contains them.
    for (const auto& run : runs) {
        if (run.glyphs < 2) continue;
        const FontEstimate font = estimate_font(img, run);
        int host = -1;
        int host_area = image_area * 2;
        for (const auto& e : screen.elements) {
            if (e.type == ElementType::Window) continue;
            if (!e.rect.contains(run.box)) continue;
            if (e.rect.area() < host_area) {
                host_area = e.rect.area();
                host = e.id;
            }
        }
        TextStyle style;
        style.box = run.box;
        style.color = dominant_ink(img, run.box.inflate(1), screen.background);
        if (delta_e(style.color, screen.background) < 4.f) style.color = run.color;
        style.family = font.family;
        style.size_px = font.size_px;
        style.weight = font.weight;
        style.italic = font.italic;
        style.line_height = font.cap_height > 0 ? static_cast<int>(std::lround(font.size_px * 1.45f)) : run.box.h;
        style.letter_spacing = font.letter_spacing;
        style.glyph_class = glyph_class_of(run.glyphs, run.box.w);
        style.confidence = font.confidence;
        style.align = TextAlign::Left;

        if (host >= 0) {
            Element& h = screen.elements[host];
            const bool control = h.type == ElementType::Button || h.type == ElementType::Input ||
                                 h.type == ElementType::Tab || h.type == ElementType::Badge ||
                                 h.type == ElementType::Toggle || h.type == ElementType::MenuItem;
            if (control) {
                const int center = h.rect.cx();
                const int text_center = run.box.cx();
                if (std::abs(center - text_center) <= 6) style.align = TextAlign::Center;
                if (h.text.text.empty()) {
                    h.text = style;
                } else {
                    h.text.text += "\n";
                }
                continue;
            }
        }

        Element e;
        e.id = static_cast<int>(screen.elements.size());
        e.parent = host >= 0 ? host : 0;
        e.rect = run.box;
        e.type = style.size_px >= 22.f && run.box.w < img.width() * 0.7 ? ElementType::Heading : ElementType::Label;
        e.text = style;
        e.fill.top = e.fill.bottom = screen.background;
        e.confidence = font.confidence;
        // Alignment against the parent box.
        if (e.parent >= 0) {
            const Element& p = screen.elements[e.parent];
            const int left_gap = run.box.x - p.rect.x;
            const int right_gap = p.rect.right() - run.box.right();
            if (std::abs(run.box.cx() - p.rect.cx()) <= 4) e.text.align = TextAlign::Center;
            else if (right_gap < left_gap / 2) e.text.align = TextAlign::Right;
        }
        screen.elements.push_back(e);
        screen.elements[e.parent].children.push_back(e.id);
    }

    // Icons: small components that the text pass did not consume.
    if (options.detect_icons) {
        const auto boxes = icon_candidates(comps, 48);
        for (const auto& box : boxes) {
            bool overlaps_text = false;
            for (const auto& run : runs)
                if (run.box.intersect(box).area() > box.area() / 3) {
                    overlaps_text = true;
                    break;
                }
            if (overlaps_text) continue;
            const RGBA ink = extreme_ink(img, box, screen.background);
            if (delta_e(ink, screen.background) < 6.f) continue;
            const IconMatch match = match_icon(img, box, ink);
            const float min_score = (box.w < 12 || box.h < 12) ? 0.52f : 0.32f;
            if (match.score < min_score) continue;
            bool duplicate = false;
            for (const auto& existing : screen.elements) {
                if (existing.type != ElementType::Icon) continue;
                if (existing.rect.intersect(box).area() > box.area() / 2) {
                    duplicate = true;
                    break;
                }
            }
            if (duplicate) continue;
            int host = 0;
            int host_area = image_area * 2;
            for (const auto& e : screen.elements) {
                if (!e.rect.contains(box)) continue;
                if (e.rect.area() < host_area) {
                    host_area = e.rect.area();
                    host = e.id;
                }
            }
            Element e;
            e.id = static_cast<int>(screen.elements.size());
            e.parent = host;
            e.rect = box;
            e.type = ElementType::Icon;
            e.icon.name = match.score >= 0.55f && !match.name.empty() ? match.name : "glyph";
            e.icon.score = match.score;
            e.icon.color = ink;
            e.icon.stroke_px = measure_stroke_px(img, box, ink);
            e.fill.top = e.fill.bottom = screen.background;
            e.confidence = match.score;
            screen.elements.push_back(e);
            screen.elements[host].children.push_back(e.id);
        }
    }

    // Second pass: text is attached now, refine control types.
    for (auto& e : screen.elements) {
        if (e.type == ElementType::Window || e.type == ElementType::Label || e.type == ElementType::Heading ||
            e.type == ElementType::Icon)
            continue;
        const RGBA parent_fill = e.parent >= 0 ? screen.elements[e.parent].fill.top : screen.background;
        e.type = classify(screen, e, parent_fill, image_area);
        if (e.type == ElementType::Label && !e.text.text.empty()) {
            e.type = e.text.size_px >= 22.f ? ElementType::Heading : ElementType::Label;
        }
    }

    // Padding and gaps.
    for (auto& e : screen.elements) {
        if (e.children.empty()) continue;
        int min_x = e.rect.right(), min_y = e.rect.bottom(), max_x = e.rect.x, max_y = e.rect.y;
        for (int child : e.children) {
            const auto& c = screen.elements[child];
            min_x = std::min(min_x, c.rect.x);
            min_y = std::min(min_y, c.rect.y);
            max_x = std::max(max_x, c.rect.right());
            max_y = std::max(max_y, c.rect.bottom());
        }
        e.padding_left = std::max(0, min_x - e.rect.x);
        e.padding_top = std::max(0, min_y - e.rect.y);
        e.padding_right = std::max(0, e.rect.right() - max_x);
        e.padding_bottom = std::max(0, e.rect.bottom() - max_y);
        if (e.children.size() > 1) {
            std::vector<int> gaps;
            for (size_t a = 0; a < e.children.size(); ++a)
                for (size_t b = a + 1; b < e.children.size(); ++b) {
                    const auto& ca = screen.elements[e.children[a]];
                    const auto& cb = screen.elements[e.children[b]];
                    const int vgap = cb.rect.y > ca.rect.bottom() ? cb.rect.y - ca.rect.bottom()
                                      : (ca.rect.y > cb.rect.bottom() ? ca.rect.y - cb.rect.bottom() : -1);
                    if (vgap >= 0 && vgap < 80) gaps.push_back(vgap);
                }
            if (!gaps.empty()) {
                std::sort(gaps.begin(), gaps.end());
                e.gap = gaps[gaps.size() / 2];
            }
        }
    }

    // Z-order: depth first, larger containers behind.
    int z = 0;
    std::function<void(int, int)> walk = [&](int id, int depth) {
        screen.elements[id].z = depth * 1000 + z++;
        for (int c : screen.elements[id].children) walk(c, depth + 1);
    };
    walk(0, 0);

    // Drop raw components that never became a real element, then remap ids so the
    // tree stays consistent. Without this, the anonymous strokes inside an icon
    // show up as duplicate boxes next to the icon that already covers them.
    if (screen.elements.size() > 1) {
        const size_t n = screen.elements.size();
        std::vector<int> remap(n, -1);
        std::vector<Element> kept;
        kept.reserve(n);
        for (size_t i = 0; i < n; ++i) {
            const Element& e = screen.elements[i];
            const bool empty = e.rect.area() <= 0;
            bool covered_by_icon = false;
            if (e.type == ElementType::Unknown && e.children.empty()) {
                for (const auto& other : screen.elements)
                    if (other.id != e.id && other.type == ElementType::Icon && other.rect.contains(e.rect)) {
                        covered_by_icon = true;
                        break;
                    }
            }
            if (empty || covered_by_icon) continue;
            remap[i] = static_cast<int>(kept.size());
            kept.push_back(e);
        }
        if (kept.empty()) {
            kept.push_back(screen.elements.front());
            remap[0] = 0;
        }
        for (auto& e : kept) {
            if (e.parent >= 0 && e.parent < static_cast<int>(n))
                e.parent = remap[static_cast<size_t>(e.parent)] >= 0 ? remap[static_cast<size_t>(e.parent)] : 0;
            std::vector<int> kids;
            kids.reserve(e.children.size());
            for (int c : e.children)
                if (c >= 0 && c < static_cast<int>(n) && remap[static_cast<size_t>(c)] >= 0)
                    kids.push_back(remap[static_cast<size_t>(c)]);
            e.children = std::move(kids);
        }
        screen.elements = std::move(kept);
    }

    refine_roles(screen, img.width(), img.height());

    // Layout metrics.
    std::vector<int> measurements;
    int margin = 0;
    for (const auto& e : screen.elements) {
        if (e.type == ElementType::Window) continue;
        if (e.parent == 0) {
            margin = std::max(margin, e.rect.y);
            measurements.push_back(e.rect.x);
            measurements.push_back(e.rect.y);
        }
        measurements.push_back(e.rect.w);
        measurements.push_back(e.rect.h);
        measurements.push_back(e.padding_left);
        measurements.push_back(e.padding_top);
        measurements.push_back(e.padding_right);
        measurements.push_back(e.padding_bottom);
        measurements.push_back(e.gap);
    }
    int unit_conf = 0;
    screen.metrics.base_unit = infer_base_unit(measurements, &unit_conf);
    screen.metrics.grid_confidence = unit_conf;
    screen.metrics.margin = margin;
    screen.metrics.content_width = img.width() - 2 * margin;
    std::vector<int> heights;
    for (const auto& e : screen.elements)
        if (e.parent == 0 && e.type != ElementType::Window) heights.push_back(e.rect.h);
    // Stroke thickness decides the device pixel ratio: a screenshot where every
    // border and divider is 2px and text is large is a 2x capture.
    int min_stroke = 99;
    int even = 0, strokes = 0;
    for (const auto& e : screen.elements) {
        const int t = e.border.max_thickness();
        if (t > 0) {
            min_stroke = std::min(min_stroke, t);
            even += (t % 2 == 0);
            ++strokes;
        }
    }
    screen.report.device_pixel_ratio = (img.width() >= 1200 && min_stroke >= 2 && strokes > 0 && even == strokes) ? 2 : 1;
    screen.metrics.text_scale = screen.report.device_pixel_ratio;

    // Report counters.
    screen.report.width = img.width();
    screen.report.height = img.height();
    screen.report.input_name = name;
    screen.report.elements = static_cast<int>(screen.elements.size());
    screen.report.text_runs = static_cast<int>(runs.size());
    screen.report.icons = 0;
    for (const auto& e : screen.elements) {
        switch (e.type) {
            case ElementType::Button: ++screen.report.buttons; break;
            case ElementType::Input: ++screen.report.inputs; break;
            case ElementType::Icon: ++screen.report.icons; break;
            case ElementType::Panel: ++screen.report.cards; break;
            default: break;
        }
        if (e.border.present()) ++screen.report.borders;
        if (e.radius > 0) ++screen.report.radii;
        if (e.shadow.present) ++screen.report.shadows;
    }
    if (debug) debug->merged = 0;

    if (screen.elements.size() <= 1) screen.report.warnings.push_back("no UI elements found: flat or photographic image");
    if (screen.report.device_pixel_ratio > 1)
        screen.report.warnings.push_back("capture looks like a " + std::to_string(screen.report.device_pixel_ratio) +
                                         "x retina screenshot; measurements are in device pixels");
    return screen;
}

SequenceInfo analyze_sequence(const std::vector<Image>& frames, const std::vector<std::string>& names) {
    SequenceInfo info;
    if (frames.size() < 2) return info;
    const int w = frames[0].width(), h = frames[0].height();
    info.frames = static_cast<int>(frames.size());
    info.width = w;
    info.height = h;
    for (size_t i = 1; i < frames.size(); ++i) {
        if (frames[i].width() != w || frames[i].height() != h) {
            info.warnings.push_back("frames must share dimensions; ignoring frame " + std::to_string(i));
            info.frames = static_cast<int>(i);
            break;
        }
    }
    if (info.frames < 2) return info;

    double total_delta = 0;
    int pairs = 0;
    for (int f = 1; f < info.frames; ++f) {
        const Image& a = frames[f - 1];
        const Image& b = frames[f];
        std::vector<uint8_t> diff = difference_mask(a, b, 6.f);
        if (diff.empty()) continue;
        const int cells = w * h;
        int changed = 0;
        for (uint8_t v : diff)
            if (v) ++changed;
        info.mean_delta += static_cast<float>(changed) / cells;
        ++pairs;

        // Changed regions become animation candidates.
        LabelMap map;
        map.width = w;
        map.height = h;
        map.labels.assign(static_cast<size_t>(w) * h, 0);
        map.palette.push_back(RGBA(255, 255, 255));
        for (int i = 0; i < w * h; ++i) map.labels[i] = diff[i] ? 1 : 0;
        auto comps = components(map, std::max(24, cells / 4000));
        for (const auto& c : comps) {
            if (c.bbox.w < 4 || c.bbox.h < 4) continue;
            AnimationTrack t;
            t.element_id = -1;
            t.property = "opacity";
            const RGBA ca = dominant_color(a, c.bbox);
            const RGBA cb = dominant_color(b, c.bbox);
            const float dcol = delta_e(ca, cb);
            if (dcol > 12.f) {
                t.property = "opacity";
                t.from = 0.4f;
                t.to = 1.f;
            } else {
                t.property = "x";
                t.from = 0.f;
                t.to = static_cast<float>(c.bbox.w / 2);
            }
            t.duration_ms = 160;
            t.easing = "ease-out";
            t.confidence = 0.35f;
            info.tracks.push_back(t);
        }
    }
    if (pairs > 0) info.mean_delta /= pairs;
    if (names.size() >= 2) {
        // Duration can only be measured when names carry a time in ms.
        auto time_of = [](const std::string& n) -> int {
            for (size_t i = 0; i + 2 < n.size(); ++i) {
                if (n.compare(i, 2, "t=") == 0 || n.compare(i, 3, "ms=") == 0) {
                    const size_t start = i + (n[i] == 't' ? 2 : 3);
                    int v = 0;
                    for (size_t k = start; k < n.size() && std::isdigit(static_cast<unsigned char>(n[k])); ++k)
                        v = v * 10 + (n[k] - '0');
                    if (v > 0) return v;
                }
            }
            return -1;
        };
        const int t0 = time_of(names[0]), t1 = time_of(names[1]);
        if (t0 >= 0 && t1 > t0) {
            for (auto& track : info.tracks) {
                track.duration_ms = t1 - t0;
                track.confidence = 0.75f;
            }
        }
    }
    if (!info.tracks.empty())
        info.warnings.push_back("transition durations are inferred from frame timing; supply t=<ms> filenames "
                                "for measured values");
    return info;
}

}  // namespace pgr
