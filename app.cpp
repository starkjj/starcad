#include "app.h"

Application::Application()
{

}
Application::~Application()
{
    shutdown();
}

auto Application::init(int width, int height, const char* title) -> void
{
    init_window(width, height, title);
    init_imgui();

    is_running_ = true;
}

auto Application::run() -> void
{
    while(is_running_ && !glfwWindowShouldClose(window)) {
        begin_frame();
        render_ui();
        end_frame();
    }
}

auto Application::shutdown() -> void
{
    // ImGui_ImplOpenGL3_Shutdown();
    // ImGui_ImplGlfw_Shutdown();
    // ImGui::DestroyContext();

    glfwDestroyWindow(window);
    glfwTerminate();

    is_running_ = false;
}

auto Application::init_window(int width, int height, const char* title) -> void
{
    if (!glfwInit()) {
        std::cout << "Failed to initialize GLFW." << std::endl;
        return;
    }

    glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 3);
    glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 3);
    glfwWindowHint(GLFW_OPENGL_PROFILE, GLFW_OPENGL_CORE_PROFILE);
    glfwWindowHint(GLFW_OPENGL_FORWARD_COMPAT, GL_TRUE);

    window = glfwCreateWindow(width, height, title, NULL, NULL);
    if (!window)
    {
        std::cout << "Failed to create GLFW window." << std::endl;
        glfwTerminate();
        return;
    }

    glfwMakeContextCurrent(window);
}

auto Application::init_imgui() -> void
{
    // Setup Dear ImGui context
    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImGuiIO& io = ImGui::GetIO();
    io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;     // Enable Keyboard Controls

    // Setup Platform/Renderer backends
    ImGui_ImplGlfw_InitForOpenGL(window, true);          // Second param install_callback=true will install GLFW callbacks and chain to existing ones.
    ImGui_ImplOpenGL3_Init();
}

auto Application::begin_frame() -> void
{
    // ImGui::Render();
    // ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());
}
auto Application::render_ui() -> void {

}
auto Application::end_frame() -> void
{
    glfwPollEvents();
    glfwSwapBuffers(window);
}
