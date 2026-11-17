// Dear ImGui desktop shell. The UI only depends on imgui.h and pgr_core; the
// platform bits (window, textures, file drops) come through AppHost.
#pragma once

#include <string>
#include <vector>

#include "imgui.h"

#include "image.h"
#include "parser.h"

namespace pgr {

struct AppHost {
    virtual ~AppHost() = default;
    virtual ImTextureID upload(const Image& image) = 0;
    virtual void release(ImTextureID texture) = 0;
    virtual void open_folder(const std::string& path) = 0;
};

struct AppState {
    std::vector<std::string> files;
    std::vector<std::string> drops;  // pushed by the platform layer
    ParseResult result;
    bool has_result = false;
    bool frames_mode = true;
    int max_colors = 24;
    bool icons = true;
    int tab = 0;
    std::string status = "Déposez des captures, ou collez un chemin.";
    std::string error;
    std::string export_dir;
    char path_input[1024] = {};
    ImTextureID tex_source = 0;
    ImTextureID tex_reconstruction = 0;
    ImTextureID tex_diff = 0;
    int tex_width = 0;
    int tex_height = 0;
    bool request_quit = false;
};

void add_file(AppState& state, const std::string& path);
bool analyze(AppState& state, AppHost& host);
void draw_app(AppState& state, AppHost& host);

}  // namespace pgr
