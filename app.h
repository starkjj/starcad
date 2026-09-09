#pragma once

#include <iostream>
#include <GLFW/glfw3.h>
#include <imgui.h>
#include <imgui_impl_glfw.h>
#include <imgui_impl_opengl3.h>

class Application {
public:
    Application();
    ~Application();

    auto init(int width, int height, const char* title) -> void;
    auto run() -> void;
    auto shutdown() -> void;


private:
    GLFWwindow* window{};

    bool is_running_{};

    auto init_window(int width, int height, const char* title) -> void;
    auto init_imgui() -> void;

    auto begin_frame() -> void;
    auto render_ui() -> void;
    auto end_frame() -> void;
};

