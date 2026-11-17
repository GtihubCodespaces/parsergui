#include "ui.h"

#include <algorithm>
#include <cctype>
#include <chrono>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <sstream>

#include "color.h"
#include "emit.h"
#include "render.h"

namespace fs = std::filesystem;

namespace pgr {
namespace {

std::string lower_ext(const std::string& path) {
    const size_t dot = path.rfind('.');
    if (dot == std::string::npos) return {};
    std::string ext = path.substr(dot + 1);
    std::transform(ext.begin(), ext.end(), ext.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return ext;
}

bool is_image(const std::string& path) {
    const std::string ext = lower_ext(path);
    return ext == "png" || ext == "jpg" || ext == "jpeg" || ext == "webp" || ext == "bmp" || ext == "tga" ||
           ext == "gif" || ext == "psd";
}

std::string timestamp_dir() {
    const std::time_t now = std::time(nullptr);
    std::tm tm{};
#ifdef _WIN32
    localtime_s(&tm, &now);
#else
    localtime_r(&now, &tm);
#endif
    char buf[32];
    std::strftime(buf, sizeof(buf), "%Y%m%d-%H%M%S", &tm);
    return buf;
}

void section_label(const char* text) {
    ImGui::PushStyleColor(ImGuiCol_Text, IM_COL32(139, 148, 166, 255));
    ImGui::TextUnformatted(text);
    ImGui::PopStyleColor();
}

void metric(const char* label, const char* value) {
    section_label(label);
    ImGui::PushStyleColor(ImGuiCol_Text, IM_COL32(231, 234, 241, 255));
    ImGui::TextUnformatted(value);
    ImGui::PopStyleColor();
}

}  // namespace

void add_file(AppState& state, const std::string& path) {
    if (!is_image(path)) {
        state.error = "Format non supporté : " + path;
        return;
    }
    std::error_code ec;
    if (!fs::exists(path, ec)) {
        state.error = "Fichier introuvable : " + path;
        return;
    }
    for (const auto& existing : state.files)
        if (existing == path) return;
    state.files.push_back(path);
    state.error.clear();
    state.status = std::to_string(state.files.size()) + " fichier(s) prêt(s).";
}

bool analyze(AppState& state, AppHost& host) {
    if (state.files.empty()) {
        state.error = "Aucune capture.";
        return false;
    }
    std::vector<Image> frames;
    std::vector<std::string> names;
    const size_t limit = state.frames_mode ? state.files.size() : 1;
    for (size_t i = 0; i < limit; ++i) {
        std::string error;
        Image img = Image::load(state.files[i], &error);
        if (img.empty()) {
            state.error = fs::path(state.files[i]).filename().string() + " : " + error;
            return false;
        }
        names.push_back(fs::path(state.files[i]).filename().string());
        frames.push_back(std::move(img));
    }
    AnalysisOptions options;
    options.max_colors = state.max_colors;
    options.detect_icons = state.icons;
    options.sequence = state.frames_mode && frames.size() > 1;
    state.result = parse_frames(frames, names, options);
    if (!state.result.error.empty()) {
        state.error = state.result.error;
        return false;
    }
    state.export_dir = (fs::path("parsergui-out") / timestamp_dir()).string();
    std::error_code ec;
    fs::create_directories(state.export_dir, ec);
    std::string write_error;
    if (!write_artifacts(state.result, state.export_dir, &write_error)) {
        state.error = write_error;
        return false;
    }
    if (state.tex_source) host.release(state.tex_source);
    if (state.tex_reconstruction) host.release(state.tex_reconstruction);
    if (state.tex_diff) host.release(state.tex_diff);
    state.tex_source = host.upload(frames.front());
    state.tex_reconstruction = host.upload(state.result.reconstruction);
    state.tex_diff = state.result.diff.empty() ? 0 : host.upload(state.result.diff);
    state.tex_width = frames.front().width();
    state.tex_height = frames.front().height();
    state.has_result = true;
    state.status = state.result.screen.report.summary;
    state.error.clear();
    return true;
}

void draw_app(AppState& state, AppHost& host) {
    // Consume drag and drop paths from the platform layer.
    for (const auto& path : state.drops) add_file(state, path);
    state.drops.clear();

    ImGuiViewport* viewport = ImGui::GetMainViewport();
    ImGui::SetNextWindowPos(viewport->WorkPos);
    ImGui::SetNextWindowSize(viewport->WorkSize);
    ImGui::Begin("parsergui", nullptr,
                 ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoSavedSettings |
                     ImGuiWindowFlags_NoBringToFrontOnFocus | ImGuiWindowFlags_MenuBar);

    if (ImGui::BeginMenuBar()) {
        if (ImGui::BeginMenu("Fichier")) {
            if (ImGui::MenuItem("Quitter")) state.request_quit = true;
            ImGui::EndMenu();
        }
        if (ImGui::BeginMenu("Analyse")) {
            if (ImGui::MenuItem("Analyser", "Entrée", false, !state.files.empty())) analyze(state, host);
            ImGui::MenuItem("Frames liées", nullptr, &state.frames_mode);
            ImGui::EndMenu();
        }
        if (ImGui::BeginMenu("Aide")) {
            ImGui::MenuItem("Les skills sont dans ./skills", nullptr, false, false);
            ImGui::EndMenu();
        }
        ImGui::EndMenuBar();
    }

    const float status_height = ImGui::GetTextLineHeightWithSpacing() + 12;
    const float avail = ImGui::GetContentRegionAvail().y - status_height;
    ImGui::BeginChild("##left", ImVec2(300, avail), ImGuiChildFlags_Borders);
    section_label("CAPTURES");
    ImGui::SetNextItemWidth(-70);
    ImGui::InputTextWithHint("##path", "chemin ou glisser-déposer", state.path_input, sizeof(state.path_input));
    ImGui::SameLine();
    if (ImGui::Button("Ajouter") && state.path_input[0] != '\0') {
        add_file(state, state.path_input);
        state.path_input[0] = '\0';
    }
    ImGui::BeginChild("##files", ImVec2(0, 150), ImGuiChildFlags_Borders);
    for (size_t i = 0; i < state.files.size(); ++i) {
        ImGui::PushID(static_cast<int>(i));
        ImGui::PushStyleColor(ImGuiCol_Text, IM_COL32(200, 208, 222, 255));
        ImGui::TextUnformatted(fs::path(state.files[i]).filename().string().c_str());
        ImGui::PopStyleColor();
        ImGui::SameLine(ImGui::GetContentRegionAvail().x - 20);
        if (ImGui::SmallButton("x")) {
            state.files.erase(state.files.begin() + static_cast<long>(i));
            ImGui::PopID();
            break;
        }
        ImGui::PopID();
    }
    ImGui::EndChild();

    section_label("OPTIONS");
    ImGui::SetNextItemWidth(90);
    ImGui::SliderInt("Couleurs", &state.max_colors, 6, 48);
    ImGui::Checkbox("Icônes", &state.icons);
    ImGui::Checkbox("Frames liées", &state.frames_mode);
    ImGui::Spacing();
    if (ImGui::Button("Analyser", ImVec2(-1, 34))) analyze(state, host);
    if (!state.export_dir.empty() && ImGui::Button("Ouvrir le dossier", ImVec2(-1, 0)))
        host.open_folder(state.export_dir);

    if (!state.error.empty()) {
        ImGui::Spacing();
        ImGui::PushStyleColor(ImGuiCol_Text, IM_COL32(248, 113, 113, 255));
        ImGui::TextWrapped("%s", state.error.c_str());
        ImGui::PopStyleColor();
    }
    ImGui::EndChild();

    ImGui::SameLine();
    ImGui::BeginChild("##center", ImVec2(ImGui::GetContentRegionAvail().x * 0.62f, avail), ImGuiChildFlags_Borders);
    if (ImGui::BeginTabBar("##tabs")) {
        if (ImGui::BeginTabItem("Aperçu")) {
            if (!state.has_result) {
                ImGui::TextUnformatted("Déposez une capture puis lancez l'analyse.");
            } else {
                const float w = (ImGui::GetContentRegionAvail().x - 16) / 3.f;
                const float h = std::max(80.f, w * (state.tex_height > 0 ? static_cast<float>(state.tex_height) / state.tex_width : 0.6f));
                auto image_block = [&](const char* label, ImTextureID tex) {
                    ImGui::BeginGroup();
                    section_label(label);
                    if (tex) {
                        ImGui::Image(tex, ImVec2(w, std::min(h, 460.f)));
                    } else {
                        ImGui::Dummy(ImVec2(w, std::min(h, 460.f)));
                    }
                    ImGui::EndGroup();
                };
                image_block("SOURCE", state.tex_source);
                ImGui::SameLine();
                image_block("RECONSTRUCTION", state.tex_reconstruction);
                ImGui::SameLine();
                image_block("DIFF", state.tex_diff);
                ImGui::Spacing();
                const auto& report = state.result.screen.report;
                ImGui::Text("Fidélité %.1f/100   typographie %.1f/100   %d éléments   %d ms", report.fidelity,
                            report.typography, report.elements, static_cast<int>(report.elapsed_ms));
                for (const auto& warning : report.warnings) {
                    ImGui::PushStyleColor(ImGuiCol_Text, IM_COL32(251, 191, 36, 255));
                    ImGui::TextWrapped("- %s", warning.c_str());
                    ImGui::PopStyleColor();
                }
            }
            ImGui::EndTabItem();
        }
        if (ImGui::BeginTabItem("Code ImGui")) {
            if (state.has_result) {
                static std::string code;
                static std::string cached_for;
                if (cached_for != state.export_dir) {
                    std::ifstream in(state.export_dir + "/parsed_ui.cpp", std::ios::binary);
                    std::ostringstream ss;
                    ss << in.rdbuf();
                    code = ss.str();
                    cached_for = state.export_dir;
                }
                if (ImGui::Button("Copier")) ImGui::SetClipboardText(code.c_str());
                ImGui::SameLine();
                ImGui::Text("%zu bytes", code.size());
                ImGui::BeginChild("##code", ImVec2(0, 0), ImGuiChildFlags_Borders, ImGuiWindowFlags_HorizontalScrollbar);
                ImGui::PushStyleColor(ImGuiCol_Text, IM_COL32(200, 208, 222, 255));
                ImGui::TextUnformatted(code.c_str());
                ImGui::PopStyleColor();
                ImGui::EndChild();
            } else {
                ImGui::TextUnformatted("Aucun code généré.");
            }
            ImGui::EndTabItem();
        }
        if (ImGui::BeginTabItem("Rapport")) {
            if (state.has_result) {
                const auto& screen = state.result.screen;
                const auto& report = screen.report;
                metric("PANELS", std::to_string(report.cards).c_str());
                ImGui::SameLine(140);
                metric("BOUTONS", std::to_string(report.buttons).c_str());
                ImGui::SameLine(280);
                metric("INPUTS", std::to_string(report.inputs).c_str());
                ImGui::SameLine(420);
                metric("ICÔNES", std::to_string(report.icons).c_str());
                ImGui::Spacing();
                section_label("PALETTE");
                for (size_t i = 0; i < screen.palette.size() && i < 16; ++i) {
                    const auto& p = screen.palette[i];
                    ImGui::ColorButton("##swatch", ImVec4(p.color.r / 255.f, p.color.g / 255.f, p.color.b / 255.f, 1.f),
                                       ImGuiColorEditFlags_NoTooltip, ImVec2(22, 22));
                    if (i % 8 != 7) ImGui::SameLine();
                }
                ImGui::Spacing();
                section_label("MESURES");
                if (ImGui::BeginTable("##elements", 6, ImGuiTableFlags_RowBg | ImGuiTableFlags_BordersInnerH | ImGuiTableFlags_ScrollY,
                                      ImVec2(0, 0))) {
                    ImGui::TableSetupColumn("#");
                    ImGui::TableSetupColumn("type");
                    ImGui::TableSetupColumn("x,y,w,h");
                    ImGui::TableSetupColumn("r");
                    ImGui::TableSetupColumn("bord");
                    ImGui::TableSetupColumn("conf");
                    ImGui::TableHeadersRow();
                    for (const auto& e : screen.elements) {
                        if (e.type == ElementType::Window) continue;
                        ImGui::TableNextRow();
                        ImGui::TableNextColumn();
                        ImGui::Text("%d", e.id);
                        ImGui::TableNextColumn();
                        ImGui::TextUnformatted(to_string(e.type));
                        ImGui::TableNextColumn();
                        ImGui::Text("%d,%d %dx%d", e.rect.x, e.rect.y, e.rect.w, e.rect.h);
                        ImGui::TableNextColumn();
                        ImGui::Text("%d", e.radius);
                        ImGui::TableNextColumn();
                        ImGui::Text("%d", e.border.max_thickness());
                        ImGui::TableNextColumn();
                        ImGui::Text("%d%%", static_cast<int>(e.confidence * 100));
                    }
                    ImGui::EndTable();
                }
            } else {
                ImGui::TextUnformatted("Aucun rapport.");
            }
            ImGui::EndTabItem();
        }
        if (ImGui::BeginTabItem("Layout")) {
            if (state.has_result) {
                const std::string layout = emit_layout_txt(state.result.screen);
                ImGui::BeginChild("##layout", ImVec2(0, 0), ImGuiChildFlags_Borders, ImGuiWindowFlags_HorizontalScrollbar);
                ImGui::TextUnformatted(layout.c_str());
                ImGui::EndChild();
            } else {
                ImGui::TextUnformatted("Aucun layout.");
            }
            ImGui::EndTabItem();
        }
        ImGui::EndTabBar();
    }
    ImGui::EndChild();

    ImGui::SameLine();
    ImGui::BeginChild("##right", ImVec2(0, avail), ImGuiChildFlags_Borders);
    section_label("ÉTAT");
    ImGui::TextWrapped("%s", state.status.c_str());
    ImGui::Separator();
    section_label("RACCOURCIS");
    ImGui::TextUnformatted("Ctrl+Entrée  analyser");
    ImGui::TextUnformatted("Ctrl+O       ajouter un chemin");
    ImGui::Separator();
    section_label("SORTIE");
    ImGui::TextWrapped("%s", state.export_dir.empty() ? "pas encore d'export" : state.export_dir.c_str());
    ImGui::Separator();
    section_label("MOTEUR");
    ImGui::TextUnformatted("grille 4/8 px, palette Lab");
    ImGui::TextUnformatted("bordure outward, rayon fitted");
    ImGui::TextUnformatted("SSIM + MAE sur re-rendu");
    ImGui::EndChild();

    ImGui::Separator();
    ImGui::PushStyleColor(ImGuiCol_Text, IM_COL32(139, 148, 166, 255));
    ImGui::Text("parsergui 1.0.0  ·  %zu fichier(s)  ·  %s", state.files.size(),
                state.has_result ? "résultat prêt" : "en attente");
    ImGui::PopStyleColor();

    if (ImGui::IsKeyChordPressed(ImGuiMod_Ctrl | ImGuiKey_Enter)) analyze(state, host);
    ImGui::End();
}

}  // namespace pgr
