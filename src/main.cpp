#include <SDL3/SDL.h>
#include <iostream>
#include <glm/glm.hpp>

int main(int argc, char *argv[]) {

    if (!SDL_Init(SDL_INIT_VIDEO | SDL_INIT_EVENTS)) {
        std::cerr << "SDL_Init failed" << std::endl;
        return -1;
    }

    SDL_Window* window = SDL_CreateWindow("StarCad", 1040, 768, 0);
    if (window == nullptr) {
        std::cerr << "SDL_CreateWindow Error: " << SDL_GetError() << std::endl;
        SDL_Quit();
        return -1;
    }

    SDL_Renderer* renderer = SDL_CreateRenderer(window, NULL);
    if (renderer == nullptr) {
        std::cerr << "SDL_CreateRenderer Error: " << SDL_GetError() << std::endl;
        SDL_DestroyWindow(window);
        SDL_Quit();
        return -1;
    }

    SDL_Event e;
    bool quit = false;

    SDL_FPoint position{};

    bool drawing = false;
    SDL_Point start{};
    SDL_Point end{};

    while (!quit) {
        while (SDL_PollEvent(&e)) {
            if (e.type == SDL_EVENT_QUIT) {
                quit = true;
            }

            if (e.type == SDL_EVENT_MOUSE_MOTION) {
                position.x = e.motion.x;
                position.y = e.motion.y;

                if (drawing) {
                    end.x = position.x;
                    end.y = position.y;
                }
            }

            if (e.type == SDL_EVENT_MOUSE_BUTTON_DOWN) {
                drawing = true;
                start.x = end.x = e.button.x;
                start.y = end.y = e.button.y;
            }

            if (e.type == SDL_EVENT_MOUSE_BUTTON_UP) {
                drawing = false;
            }
        }

        // Draw our default background
        SDL_SetRenderDrawColor(renderer, 33, 40, 48, 255);
        SDL_RenderClear(renderer);

        char mtext[64];
        sprintf(mtext, "%f, %f", position.x, position.y);

        SDL_SetRenderDrawColor(renderer, 255, 255, 255, 255);
        SDL_RenderDebugText(renderer, position.x, position.y, mtext);

        if (drawing) {
            SDL_FRect rect;
            rect.x = start.x;
            rect.y = start.y;
            rect.w = end.x - start.x;
            rect.h = end.y - start.y;
            SDL_RenderRect(renderer, &rect);
        }

        SDL_RenderPresent(renderer);
    }

    SDL_DestroyRenderer(renderer);
    SDL_DestroyWindow(window);
    SDL_Quit();
    return 0;
}
