#pragma once

#include <iostream>
#include <memory>

#ifdef __EMSCRIPTEN__
#define GLFW_INCLUDE_ES3
#endif
#include <GLFW/glfw3.h>
#include <imgui.h>
#include <imgui_impl_glfw.h>
#include <imgui_impl_opengl3.h>

#include "cad/cad.h"

class Application {
public:
    Application();
    ~Application();

    auto init(int width, int height, const char* title) -> bool;
    auto run() -> void;
    auto frame() -> void; // one iteration of the main loop (used directly by the browser build)
    auto shutdown() -> void;
    auto load_script(const char* path) -> bool;
    auto set_window_size(int width, int height) -> void;

private:
    GLFWwindow* window{};
    std::unique_ptr<cad::Cad> cad_;

    bool is_running_{};

    auto init_window(int width, int height, const char* title) -> bool;
    auto init_imgui() -> void;

    auto begin_frame() -> void;
    auto render_ui() -> void;
    auto end_frame() -> void;
};
