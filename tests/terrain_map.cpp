// terrain_map.cpp - Prints a map of a world seed in the console, without starting the game.
//
//   tests\terrain_map.exe 2026
//
// Legend:  ~ deep water   - shallow water   . beach   , lowland   n hills
//          A mountains    * snow            @ the origin (0, 0)
// One character = 8 blocks across, one line = 16 blocks down (+Z is down the page).
#include "../src/terrain.h"

#include <chrono>
#include <cstdio>
#include <cstdlib>

int main(int argc, char** argv) {
    const uint32_t seed = argc > 1 ? static_cast<uint32_t>(std::strtoul(argv[1], nullptr, 10)) : 12345;
    TerrainGenerator gen(seed);
    const int R = 384, STEP = 8;
    int water = 0, total = 0, maxH = 0, minH = 999, snow = 0;
    int highX = 0, highZ = 0, deepX = 0, deepZ = 0;
    for (int z = -R; z <= R; z += STEP * 2) {
        for (int x = -R; x <= R; x += STEP) {
            const int h = gen.surfaceHeight(x, z);
            char c = h < SEA_LEVEL - 6 ? '~' : h < SEA_LEVEL ? '-' : h <= SEA_LEVEL + 1 ? '.' : h < 62 ? ',' : h < 76 ? 'n' : h < SNOW_LINE ? 'A' : '*';
            if (x == 0 && z == 0) c = '@';
            std::putchar(c);
            ++total;
            if (h < SEA_LEVEL) ++water;
            if (h >= SNOW_LINE) ++snow;
            if (h > maxH) { maxH = h; highX = x; highZ = z; }
            if (h < minH) { minH = h; deepX = x; deepZ = z; }
        }
        std::putchar('\n');
    }
    const IVec3 spawn = gen.findSpawn();
    std::printf("water %.0f%%  snow %.0f%%  height %d..%d\n", 100.0 * water / total, 100.0 * snow / total, minH, maxH);
    std::printf("highest %d at (%d, %d)   deepest %d at (%d, %d)   spawn (%d, %d, %d)\n", maxH, highX, highZ, minH, deepX, deepZ, spawn.x, spawn.y, spawn.z);

    // Timing: how long does one chunk take to generate?
    Chunk chunk;
    const auto t0 = std::chrono::steady_clock::now();
    const int N = 50;
    for (int i = 0; i < N; ++i) { chunk.cx = i; chunk.cz = -i; chunk.blocks.fill(0); gen.generate(chunk); }
    const double ms = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - t0).count() / N;
    std::printf("generate: %.2f ms per chunk\n", ms);

    // Find a cave near spawn: a cave block a few blocks under the surface.
    for (int r = 0; r < 200; ++r)
        for (int x = spawn.x - r; x <= spawn.x + r; ++x) {
            const int z = spawn.z + r;
            const int h = gen.surfaceHeight(x, z);
            for (int y = h - 12; y < h - 4; ++y)
                if (y > 3 && gen.isCave(x, y, z) && gen.isCave(x, y + 1, z) && gen.isCave(x, y - 1, z) && h > SEA_LEVEL + 1) {
                    std::printf("cave at (%d, %d, %d), surface %d\n", x, y, z, h);
                    return 0;
                }
        }
    return 0;
}
