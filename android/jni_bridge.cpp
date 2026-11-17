// JNI entry points used by the Android shell (ai.parsergui.studio.Native).
// Parsing stays in pgr_core; this file only marshals strings and JSON.
#include <jni.h>

#include <cstdio>
#include <string>
#include <vector>

#include "image.h"
#include "parser.h"

namespace {

std::string to_std(JNIEnv* env, jstring value) {
    if (!value) return {};
    const char* chars = env->GetStringUTFChars(value, nullptr);
    std::string out = chars ? chars : "";
    if (chars) env->ReleaseStringUTFChars(value, chars);
    return out;
}

std::string json_escape(const std::string& in) {
    std::string out;
    out.reserve(in.size() + 8);
    for (char c : in) {
        switch (c) {
            case '"': out += "\\\""; break;
            case '\\': out += "\\\\"; break;
            case '\n': out += "\\n"; break;
            case '\r': out += "\\r"; break;
            case '\t': out += "\\t"; break;
            default:
                if (static_cast<unsigned char>(c) < 0x20) {
                    char buf[8];
                    std::snprintf(buf, sizeof(buf), "\\u%04x", c);
                    out += buf;
                } else {
                    out += c;
                }
        }
    }
    return out;
}

jstring make_string(JNIEnv* env, const std::string& value) {
    return env->NewStringUTF(value.c_str());
}

}  // namespace

extern "C" JNIEXPORT jstring JNICALL Java_ai_parsergui_studio_Native_parse(JNIEnv* env, jclass, jstring jpath,
                                                                          jstring jout, jint max_colors) {
    const std::string path = to_std(env, jpath);
    const std::string out_dir = to_std(env, jout);
    std::string error;
    pgr::Image image = pgr::Image::load(path, &error);
    if (image.empty()) {
        return make_string(env, "{\"ok\":false,\"error\":\"" + json_escape(error) + "\"}");
    }
    pgr::AnalysisOptions options;
    options.max_colors = max_colors > 0 ? static_cast<int>(max_colors) : 24;
    options.detect_icons = true;
    pgr::ParseResult result = pgr::parse_frames({std::move(image)}, {path}, options);
    if (!result.error.empty()) {
        return make_string(env, "{\"ok\":false,\"error\":\"" + json_escape(result.error) + "\"}");
    }
    if (!pgr::write_artifacts(result, out_dir, &error)) {
        return make_string(env, "{\"ok\":false,\"error\":\"" + json_escape(error) + "\"}");
    }
    const auto& report = result.screen.report;
    char summary[320];
    std::snprintf(summary, sizeof(summary),
                  "\"fidelity\":%.2f,\"typography\":%.2f,\"elements\":%d,\"cards\":%d,\"buttons\":%d,\"inputs\":%d,"
                  "\"icons\":%d,\"grid\":%d,\"background\":\"#%02X%02X%02X\"",
                  report.fidelity, report.typography, report.elements, report.cards, report.buttons, report.inputs,
                  report.icons, result.screen.metrics.base_unit, result.screen.background.r,
                  result.screen.background.g, result.screen.background.b);
    const std::string json = "{\"ok\":true," + std::string(summary) + ",\"out\":\"" + json_escape(out_dir) + "\"}";
    return make_string(env, json);
}

extern "C" JNIEXPORT jstring JNICALL Java_ai_parsergui_studio_Native_version(JNIEnv* env, jclass) {
    return make_string(env, "parsergui 1.0.0 (android)");
}
