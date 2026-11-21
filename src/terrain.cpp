// terrain.cpp - Turns noise into landscape.
#include "terrain.h"

#include <algorithm>
#include <cmath>
#include <cstdlib>

namespace {

constexpr int TREE_CELL = 6;  // at most one tree per 6x6 cell, so trees never grow into each other

// Sets a block if (x, y, z) - in WORLD coordinates - falls inside this chunk.
// Trees use it: a tree near a chunk edge is "drawn" by every chunk it touches,
// and each chunk keeps only its own part.
void setIfInside(Chunk& chunk, int x, int y, int z, BlockId id, bool onlyIntoAir) {
    const int lx = x - chunk.cx * CHUNK_SIZE;
    const int lz = z - chunk.cz * CHUNK_SIZE;
    if (lx < 0 || lx >= CHUNK_SIZE || lz < 0 || lz >= CHUNK_SIZE || y < 0 || y >= CHUNK_HEIGHT) return;
    if (onlyIntoAir && chunk.get(lx, y, lz) != Block::Air) return;
    chunk.set(lx, y, lz, id);
}

}  // namespace

TerrainGenerator::TerrainGenerator(uint32_t seed)
    : seed_(seed),
      continents_(seed),
      hills_(seed + 1),
      mountains_(seed + 2),
      ridges_(seed + 3),
      cavesA_(seed + 4),
      cavesB_(seed + 5),
      forests_(seed + 6) {}

int TerrainGenerator::surfaceHeight(int x, int z) const {
    const float fx = static_cast<float>(x), fz = static_cast<float>(z);

    // Very large, gentle shapes decide ocean vs land.
    const float continent = continents_.fbm2(fx / 420.0f, fz / 420.0f, 5);
    // Medium bumps: rolling hills.
    const float hills = hills_.fbm2(fx / 96.0f, fz / 96.0f, 4);
    // Where mountains are allowed (0 = flat lands, 1 = mountain range).
    const float mountainMask = smoothstep(0.05f, 0.35f, mountains_.fbm2(fx / 300.0f, fz / 300.0f, 3));
    // "Ridged" noise: 1 - |noise| has sharp crests where the noise crosses zero,
    // which looks like mountain ridges. Squaring it sharpens the peaks further.
    float ridge = 1.0f - std::fabs(ridges_.fbm2(fx / 140.0f, fz / 140.0f, 4));
    ridge = ridge * ridge;

    const float height = SEA_LEVEL + 1.0f + continent * 44.0f + hills * 9.0f + mountainMask * ridge * 55.0f;
    return std::clamp(static_cast<int>(height), 4, CHUNK_HEIGHT - 24);  // keep room above for trees and building
}

bool TerrainGenerator::isCave(int x, int y, int z) const {
    if (y < 3) return false;
    // Two independent 3D noise fields. Each is near zero on a twisting sheet;
    // where BOTH are near zero the sheets cross in a long winding tunnel.
    const float fx = x / 30.0f, fy = y / 18.0f, fz = z / 30.0f;
    const float a = cavesA_.noise3(fx, fy, fz);
    const float b = cavesB_.noise3(fx, fy, fz);
    return a * a + b * b < 0.0035f;
}

void TerrainGenerator::generate(Chunk& chunk) const {
    const int baseX = chunk.cx * CHUNK_SIZE;
    const int baseZ = chunk.cz * CHUNK_SIZE;

    for (int lz = 0; lz < CHUNK_SIZE; ++lz) {
        for (int lx = 0; lx < CHUNK_SIZE; ++lx) {
            const int x = baseX + lx, z = baseZ + lz;
            const int height = surfaceHeight(x, z);
            const bool beach = height <= SEA_LEVEL + 1;  // includes the sea floor
            const bool snowy = height >= SNOW_LINE + static_cast<int>(hash3(x, z, seed_) % 4);
            // No caves under water or beaches: the static water would hang over them.
            const bool cavesAllowed = height > SEA_LEVEL + 1;

            const int top = std::max(height, SEA_LEVEL);
            for (int y = 0; y <= top; ++y) {
                BlockId id;
                if (y == 0) {
                    id = Block::Bedrock;  // unbreakable floor of the world
                } else if (y <= height) {
                    const int depth = height - y;  // how far below the surface
                    if (depth == 0) id = beach ? Block::Sand : (snowy ? Block::Snow : Block::Grass);
                    else if (depth <= 3) id = beach ? Block::Sand : Block::Dirt;
                    else id = Block::Stone;
                    if (cavesAllowed && isCave(x, y, z)) id = Block::Air;
                } else {
                    id = Block::Water;  // above the ground but below sea level
                }
                chunk.set(lx, y, lz, id);
            }
        }
    }
    placeTrees(chunk);
}

void TerrainGenerator::placeTrees(Chunk& chunk) const {
    const int baseX = chunk.cx * CHUNK_SIZE;
    const int baseZ = chunk.cz * CHUNK_SIZE;
    // Leaves spread 2 blocks from the trunk, so trees rooted up to 2 blocks
    // OUTSIDE this chunk can still reach into it. Visit their cells too.
    const int cellX0 = floorDiv(baseX - 2, TREE_CELL), cellX1 = floorDiv(baseX + CHUNK_SIZE + 1, TREE_CELL);
    const int cellZ0 = floorDiv(baseZ - 2, TREE_CELL), cellZ1 = floorDiv(baseZ + CHUNK_SIZE + 1, TREE_CELL);

    for (int cellZ = cellZ0; cellZ <= cellZ1; ++cellZ) {
        for (int cellX = cellX0; cellX <= cellX1; ++cellX) {
            // A hash of the cell decides everything about its tree, so every
            // chunk that looks at this cell agrees on the same tree.
            const uint32_t h = hash3(cellX, cellZ, seed_ ^ 0x5eed7ee5u);
            const int tx = cellX * TREE_CELL + 1 + static_cast<int>(h & 3);         // 1..4 into the cell
            const int tz = cellZ * TREE_CELL + 1 + static_cast<int>((h >> 2) & 3);

            // Forests are dense in some areas and sparse in others.
            const float forest = forests_.fbm2(tx / 160.0f, tz / 160.0f, 3);
            const float chance = 0.05f + 0.60f * smoothstep(-0.05f, 0.30f, forest);
            if (static_cast<float>((h >> 8) & 0xFFFF) / 65536.0f >= chance) continue;

            const int ground = surfaceHeight(tx, tz);
            if (ground <= SEA_LEVEL + 1 || ground >= SNOW_LINE - 2) continue;  // no trees on beaches or peaks
            if (isCave(tx, ground, tz)) continue;                               // nothing to stand on

            const int trunk = 4 + static_cast<int>((h >> 24) % 3);  // 4-6 logs tall
            const int top = ground + trunk;

            // Leaves: two wide layers (radius 2) then two narrow ones (radius 1).
            for (int y = top - 2; y <= top + 1; ++y) {
                const int radius = (y <= top - 1) ? 2 : 1;
                for (int dz = -radius; dz <= radius; ++dz) {
                    for (int dx = -radius; dx <= radius; ++dx) {
                        const bool corner = std::abs(dx) == radius && std::abs(dz) == radius;
                        if (corner) {
                            // Round the shape: drop the top corners always, the wide corners randomly.
                            if (radius == 1 && y == top + 1) continue;
                            if (radius == 2 && (hash3(tx + dx, tz + dz * 7 + y * 131, seed_) & 1)) continue;
                        }
                        setIfInside(chunk, tx + dx, y, tz + dz, Block::Leaves, true);
                    }
                }
            }
            for (int y = ground + 1; y <= top; ++y) setIfInside(chunk, tx, y, tz, Block::Log, false);
            setIfInside(chunk, tx, ground, tz, Block::Dirt, false);  // grass does not grow under a trunk
        }
    }
}

IVec3 TerrainGenerator::findSpawn() const {
    // Search outward in growing squares until we find dry, low land.
    auto good = [this](int x, int z) {
        const int h = surfaceHeight(x, z);
        return h > SEA_LEVEL + 2 && h < 80 && !isCave(x, h, z);
    };
    for (int r = 0; r < 4096; r += 8) {
        for (int i = -r; i <= r; i += 8) {
            const int candidates[4][2] = {{i, -r}, {i, r}, {-r, i}, {r, i}};
            for (const auto& c : candidates)
                if (good(c[0], c[1])) return {c[0], surfaceHeight(c[0], c[1]) + 1, c[1]};
        }
    }
    return {0, surfaceHeight(0, 0) + 1, 0};
}
