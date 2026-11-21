// main.cpp - Program entry point: reads the command line and runs the game loop.
//
//   voxel-sandbox.exe                     continue the saved world (or start a new one)
//   voxel-sandbox.exe --new               start a new world with a random seed
//   voxel-sandbox.exe --seed 1234         start a new world from seed 1234
//   voxel-sandbox.exe --view 12           draw 12 chunks far (default 8, range 2-16)
//   voxel-sandbox.exe --gldebug           ask the driver to report OpenGL mistakes
//   voxel-sandbox.exe --debug-info        start with the F3 overlay on
//   voxel-sandbox.exe --screenshot a.bmp  generate, save a screenshot, quit (for testing)
//        with optional --pos X Y Z and --look YAW PITCH (degrees)
#include "game.h"
#include "window.h"

#include <algorithm>
#include <cstdlib>
#include <string>

// Laptops with two GPUs (Intel/AMD + NVIDIA/AMD) run programs on the weak
// built-in GPU by default. Exporting these two variables asks the drivers to
// use the fast one instead. "extern C" keeps the names un-mangled so the
// drivers can find them.
extern "C" {
__declspec(dllexport) unsigned long NvOptimusEnablement = 1;
__declspec(dllexport) int AmdPowerXpressRequestHighPerformance = 1;
}

namespace {

const char* USAGE =
    "Usage: voxel-sandbox.exe [options]\n\n"
    "  --new                  start a new world (random seed)\n"
    "  --seed N               start a new world from seed N\n"
    "  --view N               view distance in chunks, 2-16 (default 8)\n"
    "  --gldebug              report OpenGL errors (run build.bat debug to see them)\n"
    "  --debug-info           start with the F3 debug overlay showing\n"
    "  --screenshot FILE.bmp  wait for the world, save a screenshot, quit\n"
    "  --pos X Y Z            start position (feet)\n"
    "  --look YAW PITCH       start view direction in degrees (yaw 0 = north)\n";

// Returns false (and shows the usage) on a bad argument.
bool parseArguments(int argc, char** argv, GameOptions& options, bool& glDebug) {
    for (int i = 1; i < argc; ++i) {
        const std::string arg = argv[i];
        // Lambda: grabs the next argument, or fails if there is none.
        auto next = [&](const char*& out) {
            if (i + 1 >= argc) return false;
            out = argv[++i];
            return true;
        };
        const char* a = nullptr;
        const char* b = nullptr;
        const char* c = nullptr;

        if (arg == "--new") {
            options.newWorld = true;
        } else if (arg == "--seed" && next(a)) {
            options.seed = static_cast<uint32_t>(std::strtoul(a, nullptr, 10));
            options.seedGiven = true;
        } else if (arg == "--view" && next(a)) {
            options.viewDistance = std::atoi(a);
            if (options.viewDistance < 2) options.viewDistance = 2;
            if (options.viewDistance > 16) options.viewDistance = 16;
        } else if (arg == "--gldebug") {
            glDebug = true;
        } else if (arg == "--debug-info") {
            options.showDebugInfo = true;
        } else if (arg == "--screenshot" && next(a)) {
            options.screenshotPath = a;
        } else if (arg == "--pos" && next(a) && next(b) && next(c)) {
            options.startPos = {std::strtof(a, nullptr), std::strtof(b, nullptr), std::strtof(c, nullptr)};
            options.startPosGiven = true;
        } else if (arg == "--look" && next(a) && next(b)) {
            options.startYawDeg = std::strtof(a, nullptr);
            options.startPitchDeg = std::strtof(b, nullptr);
            options.startLookGiven = true;
        } else {
            return false;
        }
    }
    return true;
}

}  // namespace

int main(int argc, char** argv) {
    GameOptions options;
    bool glDebug = false;
    if (!parseArguments(argc, argv, options, glDebug)) {
        MessageBoxA(nullptr, USAGE, "Voxel Sandbox", MB_ICONINFORMATION);
        return 1;
    }

    Window window;
    if (!window.create("Voxel Sandbox", 1280, 720, glDebug)) return 1;

    // The braces give `game` a shorter life than `window`: C++ destroys objects
    // in reverse order of creation, so the game frees its GPU resources while the
    // OpenGL context (owned by the window) still exists.
    {
        Game game(window, options);
        if (!game.init()) return 1;

        // ---- The game loop: one pass = one frame on screen. ----
        double previous = window.time();
        while (window.processMessages() && !game.quitRequested()) {
            const double now = window.time();
            // dt = seconds since the last frame. Capped so that a long pause
            // (dragging the window, a breakpoint) does not launch the player.
            const float dt = static_cast<float>(std::min(now - previous, 0.1));
            previous = now;

            game.frame(dt);
            if (window.width() > 0 && window.height() > 0) {
                window.swapBuffers();  // with v-sync this waits for the monitor: ~60 frames a second
            } else {
                Sleep(10);  // minimised: nothing to draw, so do not spin the CPU
            }
        }
        game.shutdown();
    }
    return 0;
}
