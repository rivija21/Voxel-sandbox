// game.cpp - The game's rules and per-frame flow.
#include "game.h"

#include "savefile.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <ctime>
#include <fstream>
#include <iterator>
#include <random>

namespace {

constexpr float MOUSE_SENSITIVITY = 0.0022f;  // radians of turn per raw mouse count
constexpr float REACH = 6.0f;                 // blocks: how far away you can break/place
constexpr double LOAD_BUDGET = 0.030;         // seconds per frame spent building the world while loading
constexpr double PLAY_BUDGET = 0.005;         // ...and while playing (keeps the frame rate smooth)

const HudColor WHITE{255, 255, 255, 255};
const HudColor LIGHT{215, 215, 215, 255};
const HudColor YELLOW{255, 226, 120, 255};

const char* const CONTROLS[][2] = {
    {"W A S D", "move"},
    {"Mouse", "look around"},
    {"Space", "jump / swim up / fly up"},
    {"Shift", "fly down"},
    {"Ctrl", "sprint (fly faster)"},
    {"F", "toggle flying"},
    {"Left click", "break block"},
    {"Right click", "place block"},
    {"Middle click", "copy block into hotbar"},
    {"1-9 / wheel", "choose block"},
    {"F1", "show / hide controls"},
    {"F2", "screenshot"},
    {"F3", "debug info"},
    {"F5", "save world"},
    {"F11", "fullscreen"},
    {"Esc", "pause / free the mouse"},
};
constexpr int CONTROL_KEY_CHARS = 14;  // width of the key column, in characters

float controlsPanelWidth(const Hud& hud) {
    size_t longest = 0;
    for (const auto& row : CONTROLS) longest = std::max(longest, std::string(row[1]).size());
    return hud.textWidth(std::string(CONTROL_KEY_CHARS + longest, ' ')) + 24 * hud.scale();
}

// Folder for the save file and screenshots: %LOCALAPPDATA%\VoxelSandbox.
// Not next to the .exe: Windows "Controlled folder access" (ransomware
// protection) stops unknown programs writing inside Documents, and programs
// are expected to keep their data in AppData anyway.
std::filesystem::path dataDirectory() {
    wchar_t buffer[MAX_PATH];
    std::filesystem::path dir;
    const DWORD n = GetEnvironmentVariableW(L"LOCALAPPDATA", buffer, MAX_PATH);
    if (n > 0 && n < MAX_PATH) {
        dir = std::filesystem::path(buffer) / "VoxelSandbox";
    } else {  // unusual: fall back to the folder holding the .exe
        const DWORD length = GetModuleFileNameW(nullptr, buffer, MAX_PATH);
        dir = std::filesystem::path(std::wstring(buffer, length)).parent_path();
    }
    std::error_code ignored;
    std::filesystem::create_directories(dir, ignored);
    return dir;
}

const char* facingName(float yaw) {
    static const char* const NAMES[4] = {"north (-Z)", "east (+X)", "south (+Z)", "west (-X)"};
    return NAMES[static_cast<int>(std::lround(degrees(yaw) / 90.0f)) % 4];
}

// Writes a 24-bit BMP. `bgr` holds bottom-up rows, each padded to 4 bytes,
// which is exactly the layout both glReadPixels and the BMP format use.
bool writeBmp(const std::filesystem::path& path, int width, int height, const std::vector<uint8_t>& bgr) {
    std::ofstream out(path, std::ios::binary);
    if (!out) return false;
    auto u16 = [&](uint16_t v) { out.put(static_cast<char>(v & 0xFF)); out.put(static_cast<char>(v >> 8)); };
    auto u32 = [&](uint32_t v) {
        for (int i = 0; i < 4; ++i) out.put(static_cast<char>((v >> (8 * i)) & 0xFF));
    };
    const uint32_t dataSize = static_cast<uint32_t>(bgr.size());
    // BITMAPFILEHEADER (14 bytes)
    out.put('B'); out.put('M');
    u32(14 + 40 + dataSize); u16(0); u16(0); u32(14 + 40);
    // BITMAPINFOHEADER (40 bytes)
    u32(40); u32(static_cast<uint32_t>(width)); u32(static_cast<uint32_t>(height));
    u16(1); u16(24); u32(0 /* BI_RGB: uncompressed */); u32(dataSize);
    u32(2835); u32(2835);  // 72 DPI, in pixels per metre
    u32(0); u32(0);
    out.write(reinterpret_cast<const char*>(bgr.data()), dataSize);
    return static_cast<bool>(out);
}

}  // namespace

Game::Game(Window& window, const GameOptions& options)
    : window_(window), options_(options), saveDir_(dataDirectory()), showDebug_(options.showDebugInfo) {}

Game::~Game() {
    world_.reset();               // frees every chunk mesh while OpenGL still exists
    releaseSharedQuadIndices();
}

bool Game::init() {
    if (!renderer_.init()) return false;
    if (!hud_.init(window_.uiScale())) return false;
    std::copy(std::begin(DEFAULT_HOTBAR), std::end(DEFAULT_HOTBAR), hotbar_);

    const std::filesystem::path savePath = saveDir_ / "world.dat";
    const bool screenshotMode = !options_.screenshotPath.empty();
    const bool fresh = options_.newWorld || options_.seedGiven || screenshotMode;

    uint32_t seed = 0;
    PlayerSave saved;
    std::vector<std::unique_ptr<Chunk>> savedChunks;
    const bool loaded = !fresh && loadWorld(savePath, seed, saved, savedChunks);

    if (!loaded) {
        seed = options_.seedGiven ? options_.seed : std::random_device{}();
        // Starting over would overwrite the old save on exit: keep a backup.
        std::error_code ignored;
        if (!screenshotMode && std::filesystem::exists(savePath, ignored))
            std::filesystem::copy_file(savePath, saveDir_ / "world-backup.dat",
                                       std::filesystem::copy_options::overwrite_existing, ignored);
    }

    world_ = std::make_unique<World>(seed, options_.viewDistance);
    for (auto& chunk : savedChunks) world_->addSavedChunk(std::move(chunk));

    const IVec3 spawn = world_->terrain().findSpawn();
    spawnPoint_ = {spawn.x + 0.5f, static_cast<float>(spawn.y), spawn.z + 0.5f};

    if (loaded) {
        player_.position = saved.position;
        player_.yaw = saved.yaw;
        player_.pitch = saved.pitch;
        player_.flying = saved.flying;
        std::copy(std::begin(saved.hotbar), std::end(saved.hotbar), hotbar_);
        showMessage("Welcome back - world loaded");
    } else {
        player_.position = spawnPoint_;
        showMessage("New world, seed " + std::to_string(seed));
    }
    if (options_.startPosGiven) player_.position = options_.startPos;
    if (options_.startLookGiven) {
        player_.yaw = radians(options_.startYawDeg);
        player_.pitch = radians(options_.startPitchDeg);
    }
    return true;
}

void Game::shutdown() {
    if (!options_.screenshotPath.empty()) return;  // test screenshots never touch the save
    if (!saveNow()) {
        // Losing someone's building silently would be the worst outcome: say so.
        const std::string text = "Could not save the world to\n" + (saveDir_ / "world.dat").string();
        MessageBoxA(nullptr, text.c_str(), "Voxel Sandbox", MB_ICONWARNING);
    }
}

void Game::frame(float dt) {
    fpsFrames_++;
    fpsTimer_ += dt;
    if (fpsTimer_ >= 0.5f) {
        fps_ = static_cast<float>(fpsFrames_) / fpsTimer_;
        fpsFrames_ = 0;
        fpsTimer_ = 0.0f;
    }
    messageTimer_ -= dt;
    slotNameTimer_ -= dt;

    if (window_.width() <= 0 || window_.height() <= 0) return;  // minimised
    if (loading_) updateLoading();
    else updatePlaying(dt);
    render();
}

void Game::updateLoading() {
    world_->update(player_.position, LOAD_BUDGET);

    // Normal start: play once the area around you is ready; the rest streams
    // in while you play. Screenshot mode: wait for everything.
    const int radius = options_.screenshotPath.empty() ? 3 : world_->viewDistance();
    int total = 0;
    for (int dz = -radius; dz <= radius; ++dz)
        for (int dx = -radius; dx <= radius; ++dx)
            if (dx * dx + dz * dz <= radius * radius) ++total;
    const int pending = world_->pendingChunks(player_.position, radius);
    loadingProgress_ = 1.0f - static_cast<float>(pending) / static_cast<float>(total);
    if (pending > 0) return;

    loading_ = false;
    // The start position might be inside a tree trunk: climb out.
    while (player_.insideSolid(*world_) && player_.position.y < CHUNK_HEIGHT) player_.position.y += 1.0f;
    if (options_.screenshotPath.empty() && window_.hasFocus()) window_.setMouseCaptured(true);
}

void Game::selectSlot(int slot) {
    selectedSlot_ = slot;
    slotNameTimer_ = 2.0f;
}

void Game::showMessage(const std::string& text) {
    message_ = text;
    messageTimer_ = 3.0f;
}

void Game::updatePlaying(float dt) {
    const InputState& in = window_.input();

    // Keys that work whether or not the game is paused.
    if (in.pressed[VK_F11]) window_.toggleFullscreen();
    if (in.pressed[VK_F3]) showDebug_ = !showDebug_;
    if (in.pressed[VK_F1]) showHelp_ = !showHelp_;
    if (in.pressed[VK_F2]) screenshotRequested_ = true;
    if (in.pressed[VK_F5]) showMessage(saveNow() ? "World saved" : "Could not save the world");
    if (in.pressed[VK_ESCAPE]) window_.setMouseCaptured(false);

    const bool playing = window_.mouseCaptured();
    if (!playing && options_.screenshotPath.empty()) {
        if (in.mousePressed[0] && window_.hasFocus()) {
            window_.setMouseCaptured(true);
            actionCooldown_ = 0.3f;  // this click resumes the game; it should not also break a block
        }
        if (in.pressed['Q']) quit_ = true;
    }

    PlayerInput controls;
    if (playing) {
        player_.turn(in.mouseDX, in.mouseDY, MOUSE_SENSITIVITY);
        controls.forward = (in.down['W'] ? 1.0f : 0.0f) - (in.down['S'] ? 1.0f : 0.0f);
        controls.strafe = (in.down['D'] ? 1.0f : 0.0f) - (in.down['A'] ? 1.0f : 0.0f);
        controls.jump = in.down[VK_SPACE];
        controls.descend = in.down[VK_SHIFT];
        controls.sprint = in.down[VK_CONTROL];

        if (in.pressed['F']) {
            player_.flying = !player_.flying;
            showMessage(player_.flying ? "Flying: Space = up, Shift = down" : "Walking");
        }
        for (int i = 0; i < HOTBAR_SLOTS; ++i)
            if (in.pressed['1' + i]) selectSlot(i);
        if (in.wheel != 0) selectSlot(floorMod(selectedSlot_ - in.wheel, HOTBAR_SLOTS));
    }

    // Only simulate where the ground exists, or we could fall through
    // a chunk that has not been generated yet.
    if (world_->isLoadedAt(player_.position)) player_.update(dt, controls, *world_);
    if (player_.position.y < -40.0f) {  // fell out of the world somehow
        player_.position = spawnPoint_;
        player_.velocity = {};
    }

    // Sprinting widens the view a little, which makes speed feel faster.
    const bool sprinting = controls.sprint && controls.forward > 0.0f;
    const float targetFov = sprinting ? 78.0f : 70.0f;
    fov_ += (targetFov - fov_) * (1.0f - std::exp(-10.0f * dt));

    world_->update(player_.position, PLAY_BUDGET);
    target_ = raycastBlocks(*world_, player_.eyePosition(), player_.lookDirection(), REACH);
    if (playing) handleBlockActions(dt);
}

void Game::handleBlockActions(float dt) {
    const InputState& in = window_.input();
    actionCooldown_ -= dt;

    if (in.mousePressed[2] && target_.hit) {  // middle click: "pick" the block you look at
        hotbar_[selectedSlot_] = world_->getBlock(target_.block);
        selectSlot(selectedSlot_);
    }
    if (!target_.hit) return;

    // A click acts at once; holding the button repeats every 0.22 s.
    const bool breakIt = in.mousePressed[0] || (in.mouseDown[0] && actionCooldown_ <= 0.0f);
    const bool placeIt = in.mousePressed[1] || (in.mouseDown[1] && actionCooldown_ <= 0.0f);

    if (breakIt) {
        const IVec3 b = target_.block;
        if (world_->getBlock(b) == Block::Bedrock) return;  // the floor of the world stays
        // If water touches the hole, let it flow in rather than leave an air bubble.
        const IVec3 around[5] = {{b.x + 1, b.y, b.z}, {b.x - 1, b.y, b.z}, {b.x, b.y, b.z + 1}, {b.x, b.y, b.z - 1}, {b.x, b.y + 1, b.z}};
        bool water = false;
        for (const IVec3& p : around) water = water || world_->getBlock(p) == Block::Water;
        world_->setBlock(b.x, b.y, b.z, water ? Block::Water : Block::Air);
        actionCooldown_ = 0.22f;
    } else if (placeIt) {
        const IVec3 p = target_.block + target_.normal;  // the empty space in front of the face we hit
        const BlockId here = world_->getBlock(p);
        const BlockId block = hotbar_[selectedSlot_];
        const bool spaceFree = (here == Block::Air || here == Block::Water);
        const bool blocksPlayer = isSolid(block) && player_.overlapsBlock(p.x, p.y, p.z);
        if (spaceFree && !blocksPlayer && p.y >= 0 && p.y < CHUNK_HEIGHT) {
            world_->setBlock(p.x, p.y, p.z, block);
            actionCooldown_ = 0.22f;
        }
    }
}

bool Game::saveNow() {
    if (!world_ || !options_.screenshotPath.empty()) return false;
    PlayerSave p;
    p.position = player_.position;
    p.yaw = player_.yaw;
    p.pitch = player_.pitch;
    p.flying = player_.flying;
    std::copy(std::begin(hotbar_), std::end(hotbar_), p.hotbar);
    return saveWorld(saveDir_ / "world.dat", *world_, p);
}

void Game::takeScreenshot(const std::filesystem::path& path) {
    const int w = window_.width(), h = window_.height();
    const int rowSize = (w * 3 + 3) & ~3;  // round each row up to a multiple of 4 bytes
    std::vector<uint8_t> pixels(static_cast<size_t>(rowSize) * h);
    glPixelStorei(GL_PACK_ALIGNMENT, 4);
    glReadPixels(0, 0, w, h, GL_BGR, GL_UNSIGNED_BYTE, pixels.data());  // reads the back buffer
    if (!writeBmp(path, w, h, pixels)) showMessage("Could not write " + path.string());
}

void Game::render() {
    const int w = window_.width(), h = window_.height();
    glViewport(0, 0, w, h);

    const Vec3 eye = player_.eyePosition();
    const Camera camera = makeCamera(eye, player_.lookDirection(), radians(fov_),
                                     static_cast<float>(w) / static_cast<float>(h));
    const bool underwater =
        !loading_ && world_->getBlock(floorToInt(eye.x), floorToInt(eye.y), floorToInt(eye.z)) == Block::Water;

    if (loading_) renderer_.drawSkyOnly(camera);
    else renderer_.drawWorld(*world_, camera, underwater, target_, stats_);
    drawHud(underwater);

    // Screenshots copy the finished picture back from the GPU before it is shown.
    if (!loading_ && !options_.screenshotPath.empty()) {
        if (++framesSinceLoaded_ >= 3) {
            takeScreenshot(options_.screenshotPath);
            quit_ = true;
        }
    } else if (screenshotRequested_) {
        screenshotRequested_ = false;
        char stamp[32];
        const std::time_t now = std::time(nullptr);
        std::strftime(stamp, sizeof(stamp), "%Y%m%d-%H%M%S", std::localtime(&now));
        const std::filesystem::path dir = saveDir_ / "screenshots";
        std::error_code ignored;
        std::filesystem::create_directories(dir, ignored);
        const std::filesystem::path file = dir / (std::string("screenshot-") + stamp + ".bmp");
        takeScreenshot(file);
        showMessage("Saved " + file.filename().string() + " in %LOCALAPPDATA%\\VoxelSandbox\\screenshots");
    }
}

void Game::drawControls(float x, float y) {
    const float s = hud_.scale();
    const float lh = hud_.lineHeight();
    const float panelW = controlsPanelWidth(hud_);
    const float panelH = lh * (std::size(CONTROLS) + 1.5f) + 12 * s;
    hud_.rect(x, y, panelW, panelH, {0, 0, 0, 150});

    float ty = y + 10 * s;
    hud_.text(x + 12 * s, ty, "Controls", YELLOW);
    ty += lh * 1.5f;
    for (const auto& row : CONTROLS) {
        hud_.text(x + 12 * s, ty, row[0], WHITE);
        hud_.text(x + 12 * s + hud_.textWidth(std::string(CONTROL_KEY_CHARS, ' ')), ty, row[1], LIGHT);
        ty += lh;
    }
}

void Game::drawHud(bool underwater) {
    const float W = static_cast<float>(window_.width());
    const float H = static_cast<float>(window_.height());
    const float s = hud_.scale();
    const float lh = hud_.lineHeight();
    hud_.begin(window_.width(), window_.height());

    if (loading_) {
        const std::string title = "Generating world...";
        hud_.text((W - hud_.textWidth(title)) / 2, H * 0.42f, title, WHITE);
        const float barW = 320 * s, barH = 10 * s;
        const float x = (W - barW) / 2, y = H * 0.42f + lh + 14 * s;
        hud_.rect(x - 3 * s, y - 3 * s, barW + 6 * s, barH + 6 * s, {0, 0, 0, 110});
        hud_.rect(x, y, barW * loadingProgress_, barH, {120, 200, 90, 255});
        hud_.end(renderer_.blockTextures());
        return;
    }

    if (underwater) hud_.rect(0, 0, W, H, {20, 60, 140, 80});
    const bool paused = !window_.mouseCaptured() && options_.screenshotPath.empty();

    // Crosshair: dark outline first, white plus on top.
    if (!paused) {
        const float len = std::round(9 * s), t = std::max(2.0f, std::round(2 * s));
        const float cx = std::round(W / 2), cy = std::round(H / 2);
        const HudColor shadow{0, 0, 0, 140};
        hud_.rect(cx - len - 1, cy - t / 2 - 1, 2 * len + 2, t + 2, shadow);
        hud_.rect(cx - t / 2 - 1, cy - len - 1, t + 2, 2 * len + 2, shadow);
        hud_.rect(cx - len, cy - t / 2, 2 * len, t, WHITE);
        hud_.rect(cx - t / 2, cy - len, t, 2 * len, WHITE);
    }

    // Hotbar.
    const float slot = std::round(46 * s), gap = std::round(4 * s);
    const float barW = HOTBAR_SLOTS * slot + (HOTBAR_SLOTS - 1) * gap;
    const float x0 = std::round((W - barW) / 2), y0 = H - slot - std::round(14 * s);
    hud_.rect(x0 - gap, y0 - gap, barW + 2 * gap, slot + 2 * gap, {0, 0, 0, 110});
    for (int i = 0; i < HOTBAR_SLOTS; ++i) {
        const float x = x0 + i * (slot + gap);
        const bool selected = (i == selectedSlot_);
        hud_.rect(x, y0, slot, slot, selected ? HudColor{255, 255, 255, 70} : HudColor{255, 255, 255, 22});
        if (selected) {  // a white frame around the chosen slot
            const float b = std::max(2.0f, std::round(2 * s));
            hud_.rect(x - b, y0 - b, slot + 2 * b, b, WHITE);
            hud_.rect(x - b, y0 + slot, slot + 2 * b, b, WHITE);
            hud_.rect(x - b, y0, b, slot, WHITE);
            hud_.rect(x + slot, y0, b, slot, WHITE);
        }
        hud_.blockIcon(x + slot / 2, y0 + slot / 2, slot * 0.7f, hotbar_[i]);
    }
    if (slotNameTimer_ > 0.0f) {
        const std::string name = blockInfo(hotbar_[selectedSlot_]).name;
        const auto alpha = static_cast<uint8_t>(255 * std::min(1.0f, slotNameTimer_));
        hud_.text((W - hud_.textWidth(name)) / 2, y0 - lh - 10 * s, name, {255, 255, 255, alpha});
    }

    if (messageTimer_ > 0.0f) {
        const auto alpha = static_cast<uint8_t>(255 * std::min(1.0f, messageTimer_));
        hud_.text((W - hud_.textWidth(message_)) / 2, H * 0.18f, message_, {255, 226, 120, alpha});
    }

    if (showDebug_) {
        char lines[8][128];
        const Vec3& p = player_.position;
        std::snprintf(lines[0], sizeof(lines[0]), "Voxel Sandbox   %.0f fps", fps_);
        std::snprintf(lines[1], sizeof(lines[1]), "XYZ    %.2f / %.2f / %.2f", p.x, p.y, p.z);
        std::snprintf(lines[2], sizeof(lines[2]), "Chunk  %d, %d   facing %s", floorDiv(floorToInt(p.x), CHUNK_SIZE),
                      floorDiv(floorToInt(p.z), CHUNK_SIZE), facingName(player_.yaw));
        std::snprintf(lines[3], sizeof(lines[3]), "Chunks %d loaded, %d drawn", static_cast<int>(world_->chunks().size()),
                      stats_.chunksDrawn);
        std::snprintf(lines[4], sizeof(lines[4]), "Quads  %d drawn", stats_.quadsDrawn);
        std::snprintf(lines[5], sizeof(lines[5]), "Mode   %s%s%s", player_.flying ? "flying" : "walking",
                      player_.onGround ? ", on ground" : "", player_.inWater ? ", in water" : "");
        if (target_.hit)
            std::snprintf(lines[6], sizeof(lines[6]), "Target %s at %d %d %d", blockInfo(world_->getBlock(target_.block)).name,
                          target_.block.x, target_.block.y, target_.block.z);
        else
            std::snprintf(lines[6], sizeof(lines[6]), "Target none");
        std::snprintf(lines[7], sizeof(lines[7]), "Seed   %u   view %d chunks", world_->seed(), world_->viewDistance());

        hud_.rect(4 * s, 4 * s, hud_.textWidth(std::string(44, ' ')) + 12 * s, lh * 8 + 12 * s, {0, 0, 0, 120});
        for (int i = 0; i < 8; ++i) hud_.text(10 * s, 10 * s + i * lh, lines[i], i == 0 ? YELLOW : WHITE);
    } else if (!paused && !showHelp_) {
        hud_.text(10 * s, 10 * s, "F1: controls", {255, 255, 255, 170});
    }

    if (paused) {
        hud_.rect(0, 0, W, H, {0, 0, 0, 120});
        const std::string title = "Paused";
        const std::string hint = "Click to play     Q: save and quit";
        hud_.text((W - hud_.textWidth(title)) / 2, H * 0.12f, title, YELLOW);
        hud_.text((W - hud_.textWidth(hint)) / 2, H * 0.12f + lh * 1.5f, hint, WHITE);
        drawControls(std::round((W - controlsPanelWidth(hud_)) / 2), H * 0.12f + lh * 3.5f);
    } else if (showHelp_) {
        drawControls(W - controlsPanelWidth(hud_) - 10 * s, 10 * s);
    }

    hud_.end(renderer_.blockTextures());
}
