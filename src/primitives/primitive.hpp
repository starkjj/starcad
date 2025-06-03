#pragma once
#include "glm/glm.hpp"

enum Shading {
    kEmpty,
    kDiagonalLines,
    kDiagonalLinesWide,
    kCrossHatching,
    kTriangles,
};

class Primitive {
public:
    virtual ~Primitive() = default;
    auto get_position() const -> glm::vec2 { return position; }
    auto get_color() const -> glm::vec4 { return color; }
    auto get_shading() const -> Shading { return shading; }
    auto set_color(glm::vec4 new_color) -> void { color = new_color; }
    auto set_shading(Shading new_shading) -> void { shading = new_shading; }

    virtual auto set_position(glm::vec2 new_position) -> void = 0;
    virtual auto set_point(glm::vec2) -> void = 0;
    virtual auto get_volume() -> float = 0;

protected:
    glm::vec2 position{};
    glm::vec4 color{};
    Shading shading{};
};