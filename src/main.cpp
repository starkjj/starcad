#include <SDL3/SDL.h>

#include <iostream>

#include "imgui.h"
#include "imgui_impl_sdl3.h"
#include "imgui_impl_sdlrenderer3.h"

#include "widgets/command_palette.hpp"
#include "Drafter.hpp"

// Some default colors
// rgb(59, 68, 83) Other menu blue
// rgb(46, 52, 64) Dark menu blue
// rgb(44, 50, 64) Bright grid
// rgb(38, 45, 55) Dim grid

int main(int argc, char *argv[]) {

    if (!SDL_Init(SDL_INIT_VIDEO | SDL_INIT_EVENTS)) {
        std::cerr << "SDL_Init failed" << std::endl;
        return -1;
    }

    // TODO: Make window size automatically 60% of monitor resolution
    SDL_Window* window = SDL_CreateWindow("StarCad", 1280, 720, 0);
    if (window == nullptr) {
        std::cerr << "SDL_CreateWindow Error: " << SDL_GetError() << std::endl;
        SDL_Quit();
        return -1;
    }

    SDL_Renderer* renderer = SDL_CreateRenderer(window, NULL);
    SDL_SetRenderVSync(renderer, 1);
    if (renderer == nullptr) {
        std::cerr << "SDL_CreateRenderer Error: " << SDL_GetError() << std::endl;
        SDL_DestroyWindow(window);
        SDL_Quit();
        return -1;
    }

    // Setup DearImGui context
    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImGuiIO& io = ImGui::GetIO(); (void)io;
    io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;   // Enable keyboard
    io.ConfigFlags |= ImGuiConfigFlags_NavEnableGamepad;    // Enable gamepad
    io.ConfigFlags |= ImGuiConfigFlags_DockingEnable;       // Enable docking
    ImGui::StyleColorsDark();

    // Setup Platform/Renderer backends for ImGui
    ImGui_ImplSDL3_InitForSDLRenderer(window, renderer);
    ImGui_ImplSDLRenderer3_Init(renderer);

    // Application "alive" state
    bool quit = false;

    Drafter drafter(renderer);
    CommandPalette command_palette;

    SDL_Cursor* cursor = SDL_CreateSystemCursor(SDL_SYSTEM_CURSOR_CROSSHAIR);

    // Update loop
    while (!quit) {
        SDL_SetCursor(cursor);
        SDL_Event e;

        // Input updates
        while (SDL_PollEvent(&e)) {
            ImGui_ImplSDL3_ProcessEvent(&e);

            if (e.type == SDL_EVENT_QUIT)
                quit = true;
            if (e.type == SDL_EVENT_WINDOW_CLOSE_REQUESTED && e.window.windowID == SDL_GetWindowID(window))
                quit = true;

            command_palette.handle_event(e);
            drafter.handle_event(e);
        }

        // Start the ImGui frame
        ImGui_ImplSDLRenderer3_NewFrame();
        ImGui_ImplSDL3_NewFrame();
        ImGui::NewFrame();

        // ImGui MenuBar
        ImGui::BeginMainMenuBar();
        if (ImGui::BeginMenu("File")) {
            ImGui::MenuItem("New");
            ImGui::MenuItem("Open");
            if (ImGui::MenuItem("Exit")) quit = true;
            ImGui::EndMenu();
        }
        ImGui::EndMainMenuBar();

        // ImGui additional widgets go here!
        command_palette.render();
        ImGui::Render();

        // SDL Rendering
        SDL_SetRenderDrawColor(renderer, 33, 40, 48, 255);
        SDL_RenderClear(renderer);

        // drafter.render();

        ImGui_ImplSDLRenderer3_RenderDrawData(ImGui::GetDrawData(), renderer);
        SDL_RenderPresent(renderer);
    }

    ImGui_ImplSDLRenderer3_Shutdown();
    ImGui_ImplSDL3_Shutdown();
    ImGui::DestroyContext();

    SDL_DestroyRenderer(renderer);
    SDL_DestroyWindow(window);
    SDL_Quit();
    return 0;
}
