#include "app.h"

#include <cstdio>
#include <string>
#include <vector>

Application::Application()
{

}
Application::~Application()
{
    shutdown();
}

auto Application::init(int width, int height, const char* title) -> bool
{
    if (!init_window(width, height, title))
        return false;
    init_imgui();
    cad_ = std::make_unique<cad::Cad>();

    is_running_ = true;
    return true;
}

auto Application::run() -> void
{
    while(is_running_ && !glfwWindowShouldClose(window)) {
        frame();
        if (cad_->quit_requested)
            is_running_ = false;
    }
}

auto Application::load_script(const char* path) -> bool
{
    return cad_->load_script(path);
}

auto Application::set_window_size(int width, int height) -> void
{
    glfwRestoreWindow(window);
    glfwSetWindowSize(window, width, height);
}

// Saves the current framebuffer as a 24-bit BMP (used by the !screenshot script directive).
static auto save_framebuffer_bmp(const std::string& path, int w, int h) -> void
{
    std::vector<unsigned char> px((size_t)w * h * 4);
    glPixelStorei(GL_PACK_ALIGNMENT, 1);
    glReadPixels(0, 0, w, h, GL_RGBA, GL_UNSIGNED_BYTE, px.data());
    int row = (w * 3 + 3) & ~3;
    unsigned size = 54 + row * h;
    unsigned char hdr[54] = {'B', 'M'};
    auto put32 = [&](int off, unsigned v) { for (int i = 0; i < 4; i++) hdr[off + i] = (unsigned char)(v >> (8 * i)); };
    put32(2, size); put32(10, 54); put32(14, 40); put32(18, (unsigned)w); put32(22, (unsigned)h);
    hdr[26] = 1; hdr[28] = 24;
    put32(34, row * h);
    FILE* f = std::fopen(path.c_str(), "wb");
    if (!f) return;
    std::fwrite(hdr, 1, 54, f);
    std::vector<unsigned char> line(row, 0);
    for (int y = 0; y < h; y++) {
        for (int x = 0; x < w; x++) {
            const unsigned char* p = &px[((size_t)y * w + x) * 4];
            line[x * 3 + 0] = p[2]; line[x * 3 + 1] = p[1]; line[x * 3 + 2] = p[0];
        }
        std::fwrite(line.data(), 1, row, f);
    }
    std::fclose(f);
}

auto Application::frame() -> void
{
    begin_frame();
    render_ui();
    end_frame();
}

auto Application::shutdown() -> void
{
    if (!window)
        return;

    cad_.reset();
    ImGui_ImplOpenGL3_Shutdown();
    ImGui_ImplGlfw_Shutdown();
    ImGui::DestroyContext();

    glfwDestroyWindow(window);
    glfwTerminate();
    window = nullptr;

    is_running_ = false;
}

auto Application::init_window(int width, int height, const char* title) -> bool
{
    if (!glfwInit()) {
        std::cout << "Failed to initialize GLFW." << std::endl;
        return false;
    }

#ifdef __EMSCRIPTEN__
    // WebGL 2
    // The Emscripten GLFW port interprets the major version as the WebGL version.
    glfwWindowHint(GLFW_CLIENT_API, GLFW_OPENGL_ES_API);
    glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 2);
    glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 0);
#else
    glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 3);
    glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 3);
    glfwWindowHint(GLFW_OPENGL_PROFILE, GLFW_OPENGL_CORE_PROFILE);
    glfwWindowHint(GLFW_OPENGL_FORWARD_COMPAT, GL_TRUE);
#endif
    glfwWindowHint(GLFW_MAXIMIZED, GLFW_TRUE);

    window = glfwCreateWindow(width, height, title, NULL, NULL);
    if (!window)
    {
        std::cout << "Failed to create GLFW window." << std::endl;
        glfwTerminate();
        return false;
    }

    glfwMakeContextCurrent(window);
#ifndef __EMSCRIPTEN__
    glfwSwapInterval(1); // the browser paces frames itself
#endif
    return true;
}

#ifdef _WIN32
static auto file_exists(const char* path) -> bool
{
    if (FILE* f = std::fopen(path, "rb")) {
        std::fclose(f);
        return true;
    }
    return false;
}
#endif

auto Application::init_imgui() -> void
{
    // Setup Dear ImGui context
    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImGuiIO& io = ImGui::GetIO();
    io.IniFilename = nullptr; // fixed layout, nothing to persist

    // Fonts: a scalable font is required because drawing text is rendered at arbitrary zoom levels.
    ImFont* ui_font = nullptr;
    ImFont* text_font = nullptr;
#ifdef _WIN32
    if (file_exists("C:/Windows/Fonts/segoeui.ttf")) ui_font = io.Fonts->AddFontFromFileTTF("C:/Windows/Fonts/segoeui.ttf", 15.0f);
    if (file_exists("C:/Windows/Fonts/arial.ttf")) text_font = io.Fonts->AddFontFromFileTTF("C:/Windows/Fonts/arial.ttf", 15.0f);
#endif
    if (!ui_font) ui_font = io.Fonts->AddFontDefaultVector();
    if (!text_font) text_font = ui_font;
    io.FontDefault = ui_font;
    cad::set_text_font(text_font);
    cad::apply_theme();

    // Setup Platform/Renderer backends
    ImGui_ImplGlfw_InitForOpenGL(window, true);          // Second param install_callback=true will install GLFW callbacks and chain to existing ones.
#ifdef __EMSCRIPTEN__
    ImGui_ImplGlfw_InstallEmscriptenCallbacks(window, "#canvas");
    ImGui_ImplOpenGL3_Init("#version 300 es");
#else
    ImGui_ImplOpenGL3_Init();
#endif
}

auto Application::begin_frame() -> void
{
    glfwPollEvents();
    ImGui_ImplOpenGL3_NewFrame();
    ImGui_ImplGlfw_NewFrame();
    ImGui::NewFrame();
}
auto Application::render_ui() -> void {
    cad_->frame();
}
auto Application::end_frame() -> void
{
    ImGui::Render();
    int w, h;
    glfwGetFramebufferSize(window, &w, &h);
    glViewport(0, 0, w, h);
    glClearColor(0.13f, 0.16f, 0.19f, 1.0f);
    glClear(GL_COLOR_BUFFER_BIT);
    ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());
    if (!cad_->screenshot_request.empty()) {
        save_framebuffer_bmp(cad_->screenshot_request, w, h);
        cad_->screenshot_request.clear();
    }
    glfwSwapBuffers(window);
}
