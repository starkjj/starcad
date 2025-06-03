#pragma once

// Standard object manipulation commands

class Commands {
public:

    virtual auto move() -> void = 0;
    virtual auto rotate() -> void = 0;
    virtual auto scale() -> void = 0;
    virtual auto erase() -> void = 0;

    // Create a new empty parent shape and make the shapes children?
    auto group() -> void { };

    // Create enums for parameters
    // angle/h/v, length, amount
    auto array() -> void { }
    auto extend() -> void { }
    auto trim() -> void { }
};