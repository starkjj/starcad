#include "Drafter.hpp"

Drafter::Drafter(SDL_Renderer *r) {
 renderer = r;
 is_drawing = false;
}

auto Drafter::handle_event(SDL_Event e) -> void {
    switch (e.type) {
        case SDL_EVENT_MOUSE_BUTTON_UP:
            if (e.button.button == SDL_BUTTON_LEFT) {
                start.x = e.button.x;
                start.y = e.button.y;
                end = start;
                is_drawing = true;
            } else if (e.button.button == SDL_BUTTON_LEFT && is_drawing) {
                end.x = e.button.x;
                end.y = e.button.y;
                is_drawing = false;
            }
            break;
        case SDL_EVENT_MOUSE_MOTION:
            if (is_drawing) {
                end.x = e.button.x;
                end.y = e.button.y;
            }
            break;
    }
}

auto Drafter::render() -> void {
    if (is_drawing || (start.x != end.x && start.y != end.y)) {
        float x = std::min(start.x, end.x);
        float y = std::min(start.y, end.y);
        float w = std::abs(end.x - start.x);
        float h = std::abs(end.y - start.y);
        SDL_FRect rect = {x, y, w, h};
        SDL_SetRenderDrawColor(renderer, 255, 255, 255, 255);
        SDL_RenderRect(renderer, &rect);
    }
}
