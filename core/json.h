// Minimal JSON DOM. Ordered objects, no exceptions, used by the HTTP API, the
// provider registry and job persistence.
#pragma once

#include <map>
#include <string>
#include <utility>
#include <vector>

namespace pgr {

class Json {
public:
    enum class Type { Null, Bool, Number, String, Array, Object };

    Json() = default;
    Json(bool v);
    Json(double v);
    Json(int v);
    Json(const char* v);
    Json(const std::string& v);
    static Json array();
    static Json object();

    Type type() const { return type_; }
    bool is_null() const { return type_ == Type::Null; }
    bool is_bool() const { return type_ == Type::Bool; }
    bool is_number() const { return type_ == Type::Number; }
    bool is_string() const { return type_ == Type::String; }
    bool is_array() const { return type_ == Type::Array; }
    bool is_object() const { return type_ == Type::Object; }

    // Object access. Non-const operator[] inserts an object member.
    Json& operator[](const std::string& key);
    const Json* find(const std::string& key) const;
    bool has(const std::string& key) const { return find(key) != nullptr; }

    // Array access.
    void push_back(Json v);
    size_t size() const;
    Json& at(size_t i);
    const Json& at(size_t i) const;

    std::string as_string(const std::string& def = "") const;
    double as_number(double def = 0.0) const;
    int as_int(int def = 0) const;
    long long as_int64(long long def = 0) const;
    bool as_bool(bool def = false) const;

    const std::vector<Json>& items() const { return array_; }
    const std::vector<std::pair<std::string, Json>>& members() const { return object_; }

    std::string dump(int indent = 2) const;
    static bool parse(const std::string& text, Json* out, std::string* error = nullptr);

private:
    void dump_to(std::string& out, int indent, int depth) const;

    Type type_ = Type::Null;
    bool bool_ = false;
    double number_ = 0.0;
    std::string string_;
    std::vector<Json> array_;
    std::vector<std::pair<std::string, Json>> object_;
};

std::string json_escape(const std::string& s);

}  // namespace pgr
