// Desktop host: GLFW window, OpenGL 3 textures, drag and drop.
#include <algorithm>
#include <cstdio>
#include <mutex>
#include <string>
#include <vector>

#include "imgui.h"
#include "imgui_impl_glfw.h"
#include "imgui_impl_opengl3.h"

#include <GLFW/glfw3.h>
#ifdef __APPLE__
#include <OpenGL/gl.h>
#else
#include <GL/gl.h>
#endif

#include "ui.h"

namespace {

std::mutex g_drop_mutex;
std::vector<std::string> g_pending_drops;

void drop_callback(GLFWwindow* window, int count, const char** paths) {
    (void)window;
    std::lock_guard<std::mutex> lock(g_drop_mutex);
    for (int i = 0; i < count; ++i) g_pending_drops.emplace_back(paths[i]);
}

GLuint upload_texture(const pgr::Image& image) {
    if (image.empty()) return 0;
    GLuint texture = 0;
    glGenTextures(1, &texture);
    glBindTexture(GL_TEXTURE_2D, texture);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, image.width(), image.height(), 0, GL_RGBA, GL_UNSIGNED_BYTE,
                 image.data());
    return texture;
}

class GlfwHost : public pgr::AppHost {
public:
    ImTextureID upload(const pgr::Image& image) override {
        return static_cast<ImTextureID>(upload_texture(image));
    }
    void release(ImTextureID texture) override {
        if (!texture) return;
        GLuint id = static_cast<GLuint>(texture);
        glDeleteTextures(1, &id);
    }
    void open_folder(const std::string& path) override {
#if defined(_WIN32)
        std::string command = "explorer \"" + path + "\"";
#elif defined(__APPLE__)
        std::string command = "open \"" + path + "\"";
#else
        std::string command = "xdg-open \"" + path + "\" >/dev/null 2>&1 &";
#endif
        std::system(command.c_str());
    }
};

void apply_style() {
    ImGuiStyle& style = ImGui::GetStyle();
    style.WindowPadding = ImVec2(12, 12);
    style.FramePadding = ImVec2(9, 6);
    style.ItemSpacing = ImVec2(8, 8);
    style.WindowRounding = 0.f;
    style.ChildRounding = 8.f;
    style.FrameRounding = 6.f;
    style.GrabRounding = 6.f;
    style.TabRounding = 6.f;
    style.ScrollbarRounding = 6.f;
    style.WindowBorderSize = 0.f;
    style.ChildBorderSize = 1.f;
    style.FrameBorderSize = 1.f;
    style.Colors[ImGuiCol_WindowBg] = ImVec4(0.051f, 0.063f, 0.082f, 1.f);
    style.Colors[ImGuiCol_ChildBg] = ImVec4(0.071f, 0.086f, 0.114f, 1.f);
    style.Colors[ImGuiCol_PopupBg] = ImVec4(0.071f, 0.086f, 0.114f, 1.f);
    style.Colors[ImGuiCol_Border] = ImVec4(0.141f, 0.169f, 0.212f, 1.f);
    style.Colors[ImGuiCol_Text] = ImVec4(0.906f, 0.918f, 0.945f, 1.f);
    style.Colors[ImGuiCol_TextDisabled] = ImVec4(0.545f, 0.580f, 0.651f, 1.f);
    style.Colors[ImGuiCol_FrameBg] = ImVec4(0.059f, 0.075f, 0.102f, 1.f);
    style.Colors[ImGuiCol_FrameBgHovered] = ImVec4(0.102f, 0.125f, 0.161f, 1.f);
    style.Colors[ImGuiCol_Button] = ImVec4(0.102f, 0.125f, 0.161f, 1.f);
    style.Colors[ImGuiCol_ButtonHovered] = ImVec4(0.157f, 0.192f, 0.243f, 1.f);
    style.Colors[ImGuiCol_ButtonActive] = ImVec4(0.247f, 0.361f, 0.753f, 1.f);
    style.Colors[ImGuiCol_Header] = ImVec4(0.102f, 0.125f, 0.161f, 1.f);
    style.Colors[ImGuiCol_Tab] = ImVec4(0.071f, 0.086f, 0.114f, 1.f);
    style.Colors[ImGuiCol_TabHovered] = ImVec4(0.157f, 0.192f, 0.243f, 1.f);
    style.Colors[ImGuiCol_TabSelected] = ImVec4(0.102f, 0.125f, 0.161f, 1.f);
    style.Colors[ImGuiCol_SliderGrab] = ImVec4(0.369f, 0.506f, 0.957f, 1.f);
    style.Colors[ImGuiCol_SliderGrabActive] = ImVec4(0.427f, 0.553f, 0.973f, 1.f);
    style.Colors[ImGuiCol_CheckMark] = ImVec4(0.369f, 0.506f, 0.957f, 1.f);
}

}  // namespace

int main(int argc, char** argv) {
    if (!glfwInit()) {
        std::fprintf(stderr, "glfwInit failed\n");
        return 1;
    }
    const char* glsl_version = "#version 130";
    glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 3);
    glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 0);
    GLFWwindow* window = glfwCreateWindow(1440, 900, "parsergui", nullptr, nullptr);
    if (!window) {
        std::fprintf(stderr, "cannot create window\n");
        glfwTerminate();
        return 1;
    }
    glfwMakeContextCurrent(window);
    glfwSwapInterval(1);
    glfwSetDropCallback(window, drop_callback);

    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImGui::GetIO().IniFilename = nullptr;
    apply_style();
    ImGui_ImplGlfw_InitForOpenGL(window, true);
    ImGui_ImplOpenGL3_Init(glsl_version);

    pgr::AppState state;
    for (int i = 1; i < argc; ++i) pgr::add_file(state, argv[i]);
    GlfwHost host;

    while (!glfwWindowShouldClose(window) && !state.request_quit) {
        glfwPollEvents();
        {
            std::lock_guard<std::mutex> lock(g_drop_mutex);
            for (const auto& path : g_pending_drops) state.drops.push_back(path);
            g_pending_drops.clear();
        }
        ImGui_ImplOpenGL3_NewFrame();
        ImGui_ImplGlfw_NewFrame();
        ImGui::NewFrame();
        pgr::draw_app(state, host);
        ImGui::Render();
        int fbw = 0, fbh = 0;
        glfwGetFramebufferSize(window, &fbw, &fbh);
        glViewport(0, 0, fbw, fbh);
        glClearColor(0.035f, 0.043f, 0.057f, 1.f);
        glClear(GL_COLOR_BUFFER_BIT);
        ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());
        glfwSwapBuffers(window);
    }

    if (state.tex_source) host.release(state.tex_source);
    if (state.tex_reconstruction) host.release(state.tex_reconstruction);
    if (state.tex_diff) host.release(state.tex_diff);
    ImGui_ImplOpenGL3_Shutdown();
    ImGui_ImplGlfw_Shutdown();
    ImGui::DestroyContext();
    glfwDestroyWindow(window);
    glfwTerminate();
    return 0;
}
