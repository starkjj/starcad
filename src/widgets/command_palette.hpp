#pragma once
#include "SDL3/SDL_events.h"
#include "imgui.h"

class CommandPalette {
public:
    CommandPalette();

    auto handle_event(SDL_Event e) -> void;
    auto render() -> void;

private:
    ImVec2 mouse_position{};
    bool show_command_palette{};
};