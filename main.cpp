#include "app.h"

#include <cstring>

#ifdef __EMSCRIPTEN__
#include <emscripten.h>
#endif

int main(int argc, char** argv) {
    static Application app{};

    if (!app.init(1600, 900, "MiniCAD"))
        return 1;

    // myfirstmeson --script file.scr : runs a script at startup
    for (int i = 1; i + 1 < argc; i++)
        if (std::strcmp(argv[i], "--script") == 0) {
            if (!app.load_script(argv[i + 1]))
                std::cout << "Cannot open script " << argv[i + 1] << std::endl;
            app.set_window_size(1600, 900); // deterministic size for scripted runs
        }

#ifdef __EMSCRIPTEN__
    // The browser owns the loop: run one frame per animation frame.
    emscripten_set_main_loop_arg([](void* a) { static_cast<Application*>(a)->frame(); }, &app, 0, true);
#else
    app.run();
#endif

    return 0;
}
