#include "command_palette.hpp"

 CommandPalette::CommandPalette() {
     show_command_palette = false;
 }

auto CommandPalette::handle_event(SDL_Event e) -> void {
    if (e.type == SDL_EVENT_KEY_DOWN) {
        if (e.key.key != SDLK_SPACE) {
            show_command_palette = true;
        }

        if (e.key.key == SDLK_SPACE && show_command_palette) {
            show_command_palette = false;
        }
    }

    if (e.type == SDL_EVENT_MOUSE_MOTION) {
        mouse_position.x = e.motion.x;
        mouse_position.y = e.motion.y;
    }
}

auto CommandPalette::render() -> void {
    static char buff[32] = "";
    if (show_command_palette) {
        ImGui::SetNextWindowPos(ImVec2(mouse_position.x + 8, mouse_position.y - 42));
        ImGui::Begin("##", &show_command_palette, ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_AlwaysAutoResize);
        ImGui::SetKeyboardFocusHere(0);
        ImGui::InputText(" ", buff, 32);
        ImGui::End();
    }
}
