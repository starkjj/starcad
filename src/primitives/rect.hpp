#pragma once
#include "primitive.hpp"
#include "../commands.hpp"

class Rect : public Primitive, public Commands {
public:
    // Implement primitive functions
    auto set_position(glm::vec2 new_position) -> void override { position = new_position; }
    auto set_point(glm::vec2) -> void override { }
    auto get_volume() -> float override { return dimensions.x * dimensions.y; }

    // Implement commands
    auto move() -> void override;
    auto rotate() -> void override;
    auto scale() -> void override;
    auto erase() -> void override;

    // Shape specific functions
    auto get_width() const -> float { return dimensions.x; }
    auto get_height() const -> float { return dimensions.y; }
    auto get_size() const -> glm::vec2 { return dimensions; }

private:
    glm::vec2 dimensions{};

};