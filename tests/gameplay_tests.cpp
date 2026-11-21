// gameplay_tests.cpp - Tests for the game's rules, run without any window or GPU.
//
// Physics, raycasting and saving are plain C++ that only read and write blocks,
// so we can build a tiny world in memory and check the results with numbers.
// Run them with test.bat.
#include "../src/player.h"
#include "../src/raycast.h"
#include "../src/savefile.h"
#include "../src/world.h"

#include <cmath>
#include <cstdio>
#include <filesystem>
#include <fstream>

static int failures = 0;
#define CHECK(cond, ...)                                     \
    do {                                                     \
        if (cond) std::printf("  ok   ");                    \
        else { std::printf("  FAIL "); ++failures; }         \
        std::printf(__VA_ARGS__);                            \
        std::printf("\n");                                   \
    } while (0)

// 3x3 chunks around the origin with a stone floor whose top is at y = 11.
static void buildFlatWorld(World& world) {
    for (int cz = -1; cz <= 1; ++cz)
        for (int cx = -1; cx <= 1; ++cx) {
            auto c = std::make_unique<Chunk>();
            c->cx = cx;
            c->cz = cz;
            for (int z = 0; z < CHUNK_SIZE; ++z)
                for (int x = 0; x < CHUNK_SIZE; ++x)
                    for (int y = 0; y <= 10; ++y) c->set(x, y, z, Block::Stone);
            world.addSavedChunk(std::move(c));
        }
}

static void simulate(Player& p, const World& w, const PlayerInput& in, float seconds) {
    for (int i = 0; i < static_cast<int>(seconds * 60); ++i) p.update(1.0f / 60.0f, in, w);
}

int main() {
    World world(1, 4);
    buildFlatWorld(world);
    PlayerInput none;

    std::printf("Falling and landing\n");
    Player p;
    p.position = {8.5f, 20.0f, 8.5f};
    simulate(p, world, none, 2.0f);
    CHECK(std::fabs(p.position.y - 11.0f) < 1e-4f, "lands on the floor top (y = %.4f)", p.position.y);
    CHECK(p.onGround, "onGround after landing");

    std::printf("Walking into a wall\n");
    for (int y = 11; y <= 13; ++y)
        for (int z = 0; z < 16; ++z) world.setBlock(12, y, z, Block::Stone);  // wall at x = 12
    p.position = {8.5f, 11.0f, 8.5f};
    p.yaw = radians(90.0f);  // east (+X)
    PlayerInput forward;
    forward.forward = 1.0f;
    simulate(p, world, forward, 3.0f);
    CHECK(std::fabs(p.position.x - (12.0f - Player::WIDTH / 2)) < 1e-3f, "stops at the wall (x = %.4f)", p.position.x);
    CHECK(std::fabs(p.position.z - 8.5f) < 1e-3f, "does not drift sideways (z = %.4f)", p.position.z);

    std::printf("Sliding along a wall\n");
    p.position = {8.5f, 11.0f, 8.5f};
    p.yaw = radians(45.0f);  // north-east, diagonal into the wall
    simulate(p, world, forward, 2.0f);
    CHECK(std::fabs(p.position.x - (12.0f - Player::WIDTH / 2)) < 1e-3f && p.position.z < 3.0f,
          "slides north along the wall (x = %.3f, z = %.3f)", p.position.x, p.position.z);

    std::printf("Jumping onto a step\n");
    world.setBlock(10, 11, 3, Block::Stone);  // a 2-block-long step up to the wall
    world.setBlock(11, 11, 3, Block::Stone);
    p.position = {8.5f, 11.0f, 3.5f};
    p.velocity = {};
    p.yaw = radians(90.0f);
    PlayerInput jumpForward = forward;
    jumpForward.jump = true;
    simulate(p, world, forward, 0.5f);      // walk up to the step
    simulate(p, world, jumpForward, 0.6f);  // then jump while pushing forward
    simulate(p, world, forward, 0.2f);   // walk on top of the step (wall at 12 stops us)
    simulate(p, world, none, 0.5f);
    CHECK(std::fabs(p.position.y - 12.0f) < 1e-3f, "stands on the step (y = %.4f, x = %.3f)", p.position.y, p.position.x);

    std::printf("Overlap check used for placing blocks\n");
    p.position = {5.5f, 11.0f, 5.5f};
    CHECK(p.overlapsBlock(5, 11, 5) && p.overlapsBlock(5, 12, 5), "player occupies its two blocks");
    CHECK(!p.overlapsBlock(5, 13, 5) && !p.overlapsBlock(6, 11, 5) && !p.overlapsBlock(5, 10, 5),
          "touching neighbours are not overlapped");

    std::printf("Raycast\n");
    RayHit hit = raycastBlocks(world, {5.5f, 12.62f, 5.5f}, normalize(Vec3{0, -1, 0.0001f}), 6.0f);
    CHECK(hit.hit && hit.block == (IVec3{5, 10, 5}) && hit.normal == (IVec3{0, 1, 0}),
          "looking down hits the floor top (block %d %d %d, normal %d %d %d)", hit.block.x, hit.block.y, hit.block.z,
          hit.normal.x, hit.normal.y, hit.normal.z);
    hit = raycastBlocks(world, {5.5f, 12.62f, 5.5f}, Vec3{1, 0, 0}, 6.0f);
    CHECK(!hit.hit, "wall 6.5 blocks away is out of reach");
    hit = raycastBlocks(world, {7.5f, 12.62f, 5.5f}, Vec3{1, 0, 0}, 6.0f);
    CHECK(hit.hit && hit.block == (IVec3{12, 12, 5}) && hit.normal == (IVec3{-1, 0, 0}),
          "looking east hits the wall's west face (block %d %d %d)", hit.block.x, hit.block.y, hit.block.z);
    hit = raycastBlocks(world, {-3.5f, 12.62f, -3.5f}, normalize(Vec3{-1, -1, -1}), 6.0f);
    CHECK(hit.hit && hit.block.y == 10 && hit.block.x < 0 && hit.block.z < 0,
          "works at negative coordinates (block %d %d %d)", hit.block.x, hit.block.y, hit.block.z);

    std::printf("Negative coordinates\n");
    world.setBlock(-1, 11, -1, Block::Brick);
    CHECK(world.getBlock(-1, 11, -1) == Block::Brick, "block at (-1, 11, -1) lands in chunk (-1, -1)");
    CHECK(world.findChunk(-1, -1)->get(15, 11, 15) == Block::Brick, "...at local (15, 11, 15)");

    std::printf("Save and load\n");
    const auto file = std::filesystem::temp_directory_path() / "voxel-test-world.dat";
    PlayerSave ps;
    ps.position = {1.5f, 2.5f, -3.5f};
    ps.yaw = 1.25f;
    ps.flying = true;
    for (int i = 0; i < HOTBAR_SLOTS; ++i) ps.hotbar[i] = static_cast<BlockId>(i + 1);
    CHECK(saveWorld(file, world, ps), "saveWorld succeeds");
    CHECK(saveWorld(file, world, ps), "saving again over an existing file succeeds");
    uint32_t seed = 0;
    PlayerSave loadedPlayer;
    std::vector<std::unique_ptr<Chunk>> chunks;
    CHECK(loadWorld(file, seed, loadedPlayer, chunks), "loadWorld succeeds");
    CHECK(seed == 1 && chunks.size() == 9, "seed %u, %zu chunks", seed, chunks.size());
    CHECK(loadedPlayer.position.z == -3.5f && loadedPlayer.flying && loadedPlayer.hotbar[8] == 9, "player restored");
    bool same = true;
    for (const auto& c : chunks) same = same && c->blocks == world.findChunk(c->cx, c->cz)->blocks;
    CHECK(same, "every block identical after the round trip");

    // A damaged file must be rejected, not crash.
    {
        std::fstream f(file, std::ios::in | std::ios::out | std::ios::binary);
        f.seekp(46 + 8 + 100);
        f.put(static_cast<char>(200));  // an invalid block id
    }
    CHECK(!loadWorld(file, seed, loadedPlayer, chunks), "file with an invalid block id is rejected");
    std::filesystem::remove(file);

    std::printf(failures ? "\n%d FAILED\n" : "\nall tests passed\n", failures);
    return failures ? 1 : 0;
}
