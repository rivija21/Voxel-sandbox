// game.h - Ties everything together: world, player, rendering, HUD and rules.
#pragma once

#include "hud.h"
#include "player.h"
#include "raycast.h"
#include "renderer.h"
#include "window.h"
#include "world.h"

#include <cstdint>
#include <filesystem>
#include <memory>
#include <string>

// Settings chosen on the command line (see main.cpp).
struct GameOptions {
    bool newWorld = false;          // ignore any saved world
    bool seedGiven = false;
    uint32_t seed = 0;
    int viewDistance = 8;           // in chunks
    std::string screenshotPath;     // if set: wait for the world, save a screenshot, quit
    bool showDebugInfo = false;     // start with the F3 overlay on
    bool startPosGiven = false;
    Vec3 startPos;
    bool startLookGiven = false;
    float startYawDeg = 0.0f, startPitchDeg = 0.0f;
};

class Game {
public:
    Game(Window& window, const GameOptions& options);
    ~Game();

    bool init();
    void frame(float dt);  // one step of the game loop: update, then draw
    void shutdown();       // saves the world
    bool quitRequested() const { return quit_; }

private:
    void updateLoading();
    void updatePlaying(float dt);
    void handleBlockActions(float dt);
    void selectSlot(int slot);
    void render();
    void drawHud(bool underwater);
    void drawControls(float x, float y);
    void showMessage(const std::string& text);
    bool saveNow();
    void takeScreenshot(const std::filesystem::path& path);

    Window& window_;  // a reference: Game uses the window but does not own it
    GameOptions options_;
    std::filesystem::path saveDir_;  // %LOCALAPPDATA%\VoxelSandbox

    std::unique_ptr<World> world_;  // created in init(), once we know the seed
    Player player_;
    Vec3 spawnPoint_;
    Renderer renderer_;
    Hud hud_;
    RayHit target_;
    RenderStats stats_;

    BlockId hotbar_[HOTBAR_SLOTS] = {};
    int selectedSlot_ = 0;
    float slotNameTimer_ = 0.0f;  // how long to keep showing the selected block's name

    bool loading_ = true;
    float loadingProgress_ = 0.0f;
    int framesSinceLoaded_ = 0;
    bool quit_ = false;
    bool showDebug_ = false;
    bool showHelp_ = false;
    bool screenshotRequested_ = false;
    float actionCooldown_ = 0.0f;  // delay between repeated break/place while a button is held
    float fov_ = 70.0f;            // degrees; widens a little while sprinting

    std::string message_;
    float messageTimer_ = 0.0f;

    // Frame-rate counter.
    float fps_ = 0.0f, fpsTimer_ = 0.0f;
    int fpsFrames_ = 0;
};
