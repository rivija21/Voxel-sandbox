// chunk.h - A 16 x 128 x 16 column of blocks: the unit the world is built from.
//
// The world is far too big to keep in memory or draw as one object, so it is
// cut into chunks. Chunks near the player are generated and drawn; chunks far
// away are thrown out (and re-generated from the seed if the player returns).
#pragma once

#include "blocks.h"
#include "gpu_mesh.h"

#include <array>

constexpr int CHUNK_SIZE = 16;    // blocks along X and along Z
constexpr int CHUNK_HEIGHT = 128; // blocks along Y (the whole height of the world)
constexpr int CHUNK_VOLUME = CHUNK_SIZE * CHUNK_SIZE * CHUNK_HEIGHT;  // 32768

struct Chunk {
    // Chunk coordinates. This chunk covers world X from cx*16 to cx*16+15
    // (and the same for Z). Example: block x = -1 is in chunk cx = -1.
    int cx = 0;
    int cz = 0;

    // One byte per block. "{}" fills it with zeros, and 0 is Block::Air.
    std::array<BlockId, CHUNK_VOLUME> blocks{};

    bool modified = false;  // the player changed it, so it must be saved
    bool dirty = true;      // the blocks changed since the mesh was built
    GpuMesh mesh;           // what the graphics card draws

    // Converts local (x, y, z) to a position in the flat array.
    // Layout: x changes fastest, then z, then y (a whole horizontal layer
    // of 256 blocks is contiguous in memory).
    static int index(int x, int y, int z) { return (y * CHUNK_SIZE + z) * CHUNK_SIZE + x; }

    BlockId get(int x, int y, int z) const { return blocks[index(x, y, z)]; }
    void set(int x, int y, int z, BlockId id) { blocks[index(x, y, z)] = id; }
};
