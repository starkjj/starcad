#pragma once
#include "primitive.hpp"
#include "../commands.hpp"

// Base class for all shapes (and maybe text)
class Line : public Primitive, public Commands {
public:
    // Implement primitive functions
    auto set_position(glm::vec2 new_position) -> void override;
    auto get_volume() -> float override;

    // Implement commands
    auto move() -> void override;
    auto rotate() -> void override;
    auto scale() -> void override;
    auto erase() -> void override;

    // Shape specific functions
    auto get_points() const -> glm::vec2 { return points; }
    auto set_points() -> void;

private:
    glm::vec2 points{};
};