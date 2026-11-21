// terrain.h - Procedural world generation: hills, oceans, caves and trees.
//
// Everything here is a pure function of (seed, position): asking for the same
// block twice always gives the same answer. That is why the game never needs
// to save chunks the player has not touched - it can simply make them again.
#pragma once

#include "chunk.h"
#include "mathlib.h"
#include "noise.h"

constexpr int SEA_LEVEL = 48;  // water fills every empty block at or below this height
constexpr int SNOW_LINE = 90;  // ground above this is covered in snow

class TerrainGenerator {
public:
    explicit TerrainGenerator(uint32_t seed);

    uint32_t seed() const { return seed_; }

    // Height (y) of the top ground block in column (x, z), before caves are dug.
    int surfaceHeight(int x, int z) const;

    // True if the block at (x, y, z) is hollowed out by a cave.
    bool isCave(int x, int y, int z) const;

    // Fills in every block of `chunk` (uses chunk.cx and chunk.cz).
    void generate(Chunk& chunk) const;

    // A dry, reasonably flat place near the origin for the player to start.
    // Returns the block the player's feet should stand in.
    IVec3 findSpawn() const;

private:
    void placeTrees(Chunk& chunk) const;

    uint32_t seed_;
    // Separate noise "fields" for separate jobs, each with its own seed so
    // they do not line up with each other.
    PerlinNoise continents_, hills_, mountains_, ridges_, cavesA_, cavesB_, forests_;
};
