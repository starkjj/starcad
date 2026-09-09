#include "app.h"

int main() {
    Application app{};

    app.init(640, 480, "My App");
    app.run();

    return 0;
}
