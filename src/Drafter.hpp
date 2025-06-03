#pragma once
#include "SDL3/SDL.h"
#include "glm/glm.hpp"

class Drafter {
public:
    Drafter(SDL_Renderer* r);

    auto handle_event(SDL_Event e) -> void;
    auto render() -> void;

private:
    SDL_Renderer* renderer{};
    glm::vec2 start{};
    glm::vec2 end{};
    bool is_drawing{};
};