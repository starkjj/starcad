#include <SDL3/SDL.h>
#include <iostream>

#include "basic_box.h"
#include "example_console.h"
#include "imgui.h"
#include "imgui_impl_sdl3.h"
#include "imgui_impl_sdlrenderer3.h"

int main(int argc, char *argv[]) {

    if (!SDL_Init(SDL_INIT_VIDEO | SDL_INIT_EVENTS)) {
        std::cerr << "SDL_Init failed" << std::endl;
        return -1;
    }

    // I want resolution to 60% of window size
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
    io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;     // Enable Keyboard Controls
    io.ConfigFlags |= ImGuiConfigFlags_NavEnableGamepad;      // Enable Gamepad Controls
    ImGui::StyleColorsDark();

    // Setup Platform/Renderer backends
    ImGui_ImplSDL3_InitForSDLRenderer(window, renderer);
    ImGui_ImplSDLRenderer3_Init(renderer);

    // Our state
    bool show_demo_window = true;
    bool show_another_window = false;
    ImVec4 clear_color = ImVec4(0.45f, 0.55f, 0.60f, 1.00f);

    bool quit = false;

    bool show_command_palette = false;

    SDL_FPoint position{};
    bool drawing = false;
    SDL_Point start{};
    SDL_Point end{};

    BasicBox box{};

    while (!quit) {
        SDL_Event e;
        while (SDL_PollEvent(&e)) {
            ImGui_ImplSDL3_ProcessEvent(&e);

            if (e.type == SDL_EVENT_QUIT)
                quit = true;
            if (e.type == SDL_EVENT_WINDOW_CLOSE_REQUESTED && e.window.windowID == SDL_GetWindowID(window))
                quit = true;

            if (e.type == SDL_EVENT_MOUSE_MOTION) {
                position.x = e.motion.x;
                position.y = e.motion.y;

                if (drawing) {
                    end.x = position.x;
                    end.y = position.y;
                }
            }

            // if (e.type == SDL_EVENT_MOUSE_BUTTON_DOWN) {
            //     drawing = true;
            //     start.x = end.x = e.button.x;
            //     start.y = end.y = e.button.y;
            // }
            //
            // if (e.type == SDL_EVENT_MOUSE_BUTTON_UP) {
            //     drawing = false;
            // }

            if (e.type == SDL_EVENT_KEY_DOWN) {
                if (e.key.key == SDLK_SPACE) {
                    show_command_palette = !show_command_palette;
                }
            }
        }

        // Start the ImGui frame
        ImGui_ImplSDLRenderer3_NewFrame();
        ImGui_ImplSDL3_NewFrame();
        ImGui::NewFrame();

        ImGui::BeginMainMenuBar();
        if (ImGui::BeginMenu("File")) {
            ImGui::MenuItem("New");
            ImGui::MenuItem("Open");
            if (ImGui::MenuItem("Exit")) quit = true;
            ImGui::EndMenu();
        }
        ImGui::EndMainMenuBar();

        ShowExampleAppConsole();


        static char buff[32] = "";
        if (show_command_palette) {
            ImGui::Begin("Palette");
            ImGui::InputText(" ", buff, 32);
            ImGui::End();
        }

        // Rendering
        ImGui::Render();

        // Draw our default background
        // rgb(59, 68, 83) Other menu blue
        // rgb(46, 52, 64) Dark menu blue
        // rgb(44, 50, 64) Bright grid
        // rgb(38, 45, 55) Low grid

        SDL_SetRenderDrawColor(renderer, 33, 40, 48, 255);
        SDL_RenderClear(renderer);

        char mtext[64];
        sprintf(mtext, "%f, %f", position.x, position.y);

        SDL_SetRenderDrawColor(renderer, 255, 255, 255, 255);
        // SDL_RenderDebugText(renderer, position.x, position.y, mtext);

        if (drawing) {
            SDL_FRect rect;
            rect.x = start.x;
            rect.y = start.y;
            rect.w = end.x - start.x;
            rect.h = end.y - start.y;
            SDL_RenderRect(renderer, &rect);
        }


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
