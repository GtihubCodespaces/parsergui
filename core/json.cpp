#include "json.h"

#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <sstream>

namespace pgr {
namespace {

void append_utf8(std::string& out, uint32_t cp) {
    if (cp < 0x80) {
        out.push_back(static_cast<char>(cp));
    } else if (cp < 0x800) {
        out.push_back(static_cast<char>(0xC0 | (cp >> 6)));
        out.push_back(static_cast<char>(0x80 | (cp & 0x3F)));
    } else if (cp < 0x10000) {
        out.push_back(static_cast<char>(0xE0 | (cp >> 12)));
        out.push_back(static_cast<char>(0x80 | ((cp >> 6) & 0x3F)));
        out.push_back(static_cast<char>(0x80 | (cp & 0x3F)));
    } else {
        out.push_back(static_cast<char>(0xF0 | (cp >> 18)));
        out.push_back(static_cast<char>(0x80 | ((cp >> 12) & 0x3F)));
        out.push_back(static_cast<char>(0x80 | ((cp >> 6) & 0x3F)));
        out.push_back(static_cast<char>(0x80 | (cp & 0x3F)));
    }
}

struct Parser {
    const std::string& s;
    size_t i = 0;
    std::string error;

    explicit Parser(const std::string& text) : s(text) {}

    void skip_ws() {
        while (i < s.size() && (s[i] == ' ' || s[i] == '\t' || s[i] == '\n' || s[i] == '\r')) ++i;
    }

    bool fail(const std::string& msg) {
        if (error.empty()) {
            std::ostringstream oss;
            oss << msg << " at byte " << i;
            error = oss.str();
        }
        return false;
    }

    bool parse_string(std::string& out) {
        if (i >= s.size() || s[i] != '"') return fail("expected string");
        ++i;
        while (i < s.size()) {
            const char c = s[i++];
            if (c == '"') return true;
            if (c == '\\') {
                if (i >= s.size()) return fail("truncated escape");
                const char e = s[i++];
                switch (e) {
                    case '"': out.push_back('"'); break;
                    case '\\': out.push_back('\\'); break;
                    case '/': out.push_back('/'); break;
                    case 'b': out.push_back('\b'); break;
                    case 'f': out.push_back('\f'); break;
                    case 'n': out.push_back('\n'); break;
                    case 'r': out.push_back('\r'); break;
                    case 't': out.push_back('\t'); break;
                    case 'u': {
                        if (i + 4 > s.size()) return fail("truncated unicode escape");
                        uint32_t cp = 0;
                        for (int k = 0; k < 4; ++k) {
                            const char h = s[i++];
                            cp <<= 4;
                            if (h >= '0' && h <= '9') cp |= static_cast<uint32_t>(h - '0');
                            else if (h >= 'a' && h <= 'f') cp |= static_cast<uint32_t>(h - 'a' + 10);
                            else if (h >= 'A' && h <= 'F') cp |= static_cast<uint32_t>(h - 'A' + 10);
                            else return fail("invalid unicode escape");
                        }
                        append_utf8(out, cp);
                        break;
                    }
                    default: return fail("invalid escape");
                }
            } else {
                out.push_back(c);
            }
        }
        return fail("unterminated string");
    }

    bool parse_value(Json& out) {
        skip_ws();
        if (i >= s.size()) return fail("unexpected end");
        const char c = s[i];
        if (c == '{') {
            ++i;
            out = Json::object();
            skip_ws();
            if (i < s.size() && s[i] == '}') {
                ++i;
                return true;
            }
            while (true) {
                skip_ws();
                std::string key;
                if (!parse_string(key)) return false;
                skip_ws();
                if (i >= s.size() || s[i] != ':') return fail("expected ':'");
                ++i;
                Json v;
                if (!parse_value(v)) return false;
                out[key] = std::move(v);
                skip_ws();
                if (i < s.size() && s[i] == ',') {
                    ++i;
                    continue;
                }
                if (i < s.size() && s[i] == '}') {
                    ++i;
                    return true;
                }
                return fail("expected ',' or '}'");
            }
        }
        if (c == '[') {
            ++i;
            out = Json::array();
            skip_ws();
            if (i < s.size() && s[i] == ']') {
                ++i;
                return true;
            }
            while (true) {
                Json v;
                if (!parse_value(v)) return false;
                out.push_back(std::move(v));
                skip_ws();
                if (i < s.size() && s[i] == ',') {
                    ++i;
                    continue;
                }
                if (i < s.size() && s[i] == ']') {
                    ++i;
                    return true;
                }
                return fail("expected ',' or ']'");
            }
        }
        if (c == '"') {
            std::string v;
            if (!parse_string(v)) return false;
            out = Json(v);
            return true;
        }
        if (s.compare(i, 4, "true") == 0) {
            i += 4;
            out = Json(true);
            return true;
        }
        if (s.compare(i, 5, "false") == 0) {
            i += 5;
            out = Json(false);
            return true;
        }
        if (s.compare(i, 4, "null") == 0) {
            i += 4;
            out = Json();
            return true;
        }
        // Number
        const size_t start = i;
        if (i < s.size() && (s[i] == '-' || s[i] == '+')) ++i;
        bool digits = false;
        while (i < s.size() && std::isdigit(static_cast<unsigned char>(s[i]))) {
            ++i;
            digits = true;
        }
        if (i < s.size() && s[i] == '.') {
            ++i;
            while (i < s.size() && std::isdigit(static_cast<unsigned char>(s[i]))) {
                ++i;
                digits = true;
            }
        }
        if (digits && i < s.size() && (s[i] == 'e' || s[i] == 'E')) {
            ++i;
            if (i < s.size() && (s[i] == '-' || s[i] == '+')) ++i;
            while (i < s.size() && std::isdigit(static_cast<unsigned char>(s[i]))) ++i;
        }
        if (!digits) return fail("invalid value");
        out = Json(std::strtod(s.substr(start, i - start).c_str(), nullptr));
        return true;
    }
};

}  // namespace

std::string json_escape(const std::string& s) {
    std::string out;
    out.reserve(s.size() + 8);
    for (char c : s) {
        switch (c) {
            case '"': out += "\\\""; break;
            case '\\': out += "\\\\"; break;
            case '\n': out += "\\n"; break;
            case '\r': out += "\\r"; break;
            case '\t': out += "\\t"; break;
            case '\b': out += "\\b"; break;
            case '\f': out += "\\f"; break;
            default:
                if (static_cast<unsigned char>(c) < 0x20) {
                    char buf[8];
                    std::snprintf(buf, sizeof(buf), "\\u%04x", static_cast<unsigned char>(c));
                    out += buf;
                } else {
                    out.push_back(c);
                }
        }
    }
    return out;
}

Json::Json(bool v) : type_(Type::Bool), bool_(v) {}
Json::Json(double v) : type_(Type::Number), number_(v) {}
Json::Json(int v) : type_(Type::Number), number_(v) {}
Json::Json(const char* v) : type_(Type::String), string_(v ? v : "") {}
Json::Json(const std::string& v) : type_(Type::String), string_(v) {}

Json Json::array() {
    Json j;
    j.type_ = Type::Array;
    return j;
}

Json Json::object() {
    Json j;
    j.type_ = Type::Object;
    return j;
}

Json& Json::operator[](const std::string& key) {
    if (type_ != Type::Object) {
        type_ = Type::Object;
        object_.clear();
    }
    for (auto& kv : object_)
        if (kv.first == key) return kv.second;
    object_.emplace_back(key, Json());
    return object_.back().second;
}

const Json* Json::find(const std::string& key) const {
    if (type_ != Type::Object) return nullptr;
    for (const auto& kv : object_)
        if (kv.first == key) return &kv.second;
    return nullptr;
}

void Json::push_back(Json v) {
    if (type_ != Type::Array) {
        type_ = Type::Array;
        array_.clear();
    }
    array_.push_back(std::move(v));
}

size_t Json::size() const {
    if (type_ == Type::Array) return array_.size();
    if (type_ == Type::Object) return object_.size();
    return 0;
}

Json& Json::at(size_t i) { return array_[i]; }
const Json& Json::at(size_t i) const { return array_[i]; }

std::string Json::as_string(const std::string& def) const {
    if (type_ == Type::String) return string_;
    if (type_ == Type::Number) {
        char buf[64];
        std::snprintf(buf, sizeof(buf), "%g", number_);
        return buf;
    }
    if (type_ == Type::Bool) return bool_ ? "true" : "false";
    return def;
}

double Json::as_number(double def) const {
    if (type_ == Type::Number) return number_;
    if (type_ == Type::String) return std::atof(string_.c_str());
    if (type_ == Type::Bool) return bool_ ? 1 : 0;
    return def;
}

int Json::as_int(int def) const { return static_cast<int>(as_number(def)); }

long long Json::as_int64(long long def) const {
    if (type_ == Type::Number) return static_cast<long long>(number_);
    return def;
}

bool Json::as_bool(bool def) const {
    if (type_ == Type::Bool) return bool_;
    if (type_ == Type::Number) return number_ != 0;
    if (type_ == Type::String) return string_ == "true" || string_ == "1";
    return def;
}

void Json::dump_to(std::string& out, int indent, int depth) const {
    const std::string pad = indent > 0 ? std::string(static_cast<size_t>(indent * depth), ' ') : std::string();
    const std::string pad_inner =
        indent > 0 ? std::string(static_cast<size_t>(indent * (depth + 1)), ' ') : std::string();
    switch (type_) {
        case Type::Null: out += "null"; break;
        case Type::Bool: out += bool_ ? "true" : "false"; break;
        case Type::Number: {
            char buf[64];
            if (std::fabs(number_) < 1e15 && number_ == std::floor(number_) &&
                std::fabs(number_) < 9.0e15)
                std::snprintf(buf, sizeof(buf), "%lld", static_cast<long long>(number_));
            else
                std::snprintf(buf, sizeof(buf), "%.6g", number_);
            out += buf;
            break;
        }
        case Type::String:
            out += '"';
            out += json_escape(string_);
            out += '"';
            break;
        case Type::Array: {
            if (array_.empty()) {
                out += "[]";
                break;
            }
            out += "[";
            for (size_t i = 0; i < array_.size(); ++i) {
                if (indent > 0) {
                    out += "\n";
                    out += pad_inner;
                }
                array_[i].dump_to(out, indent, depth + 1);
                if (i + 1 < array_.size()) out += ",";
            }
            if (indent > 0) {
                out += "\n";
                out += pad;
            }
            out += "]";
            break;
        }
        case Type::Object: {
            if (object_.empty()) {
                out += "{}";
                break;
            }
            out += "{";
            for (size_t i = 0; i < object_.size(); ++i) {
                if (indent > 0) {
                    out += "\n";
                    out += pad_inner;
                }
                out += '"';
                out += json_escape(object_[i].first);
                out += "\":";
                if (indent > 0) out += " ";
                object_[i].second.dump_to(out, indent, depth + 1);
                if (i + 1 < object_.size()) out += ",";
            }
            if (indent > 0) {
                out += "\n";
                out += pad;
            }
            out += "}";
            break;
        }
    }
}

std::string Json::dump(int indent) const {
    std::string out;
    dump_to(out, indent, 0);
    return out;
}

bool Json::parse(const std::string& text, Json* out, std::string* error) {
    Parser parser(text);
    Json value;
    if (!parser.parse_value(value)) {
        if (error) *error = parser.error;
        return false;
    }
    parser.skip_ws();
    if (parser.i != text.size()) {
        if (error) *error = "trailing data at byte " + std::to_string(parser.i);
        return false;
    }
    if (out) *out = std::move(value);
    return true;
}

}  // namespace pgr
