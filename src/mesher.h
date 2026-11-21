// mesher.h - Turns a chunk's blocks into triangles for the GPU ("meshing").
//
// The naive way - 6 faces x 32768 blocks - would be ~200,000 quads per chunk.
// Almost all of them are hidden between two solid blocks. The mesher only emits
// a face when the block next to it is see-through, which cuts that to a few
// thousand. It also bakes in simple lighting (ambient occlusion) per corner.
#pragma once

#include "chunk.h"

#include <array>
#include <vector>

// A chunk and its 8 neighbours, indexed [(dz + 1) * 3 + (dx + 1)].
// Element 4 is the chunk being meshed. The neighbours are needed to decide
// whether faces on the chunk's edge are visible, and for corner shading.
using ChunkNeighbourhood = std::array<const Chunk*, 9>;

class ChunkMesher {
public:
    // Fills `vertices` with 4 vertices per visible face: all solid faces first,
    // then all water faces. Reports how many quads of each kind it made.
    void build(const ChunkNeighbourhood& around, std::vector<BlockVertex>& vertices,
               int& opaqueQuads, int& waterQuads);

private:
    // A copy of the chunk with a 1-block border borrowed from the neighbours,
    // so looking at "the block to the left" never needs a special case.
    static constexpr int PAD_W = CHUNK_SIZE + 2;    // 18
    static constexpr int PAD_H = CHUNK_HEIGHT + 2;  // 130
    static int padIndex(int x, int y, int z) { return ((y + 1) * PAD_W + (z + 1)) * PAD_W + (x + 1); }
    BlockId at(int x, int y, int z) const { return padded_[padIndex(x, y, z)]; }

    void copyNeighbourhood(const ChunkNeighbourhood& around);
    void addFace(int x, int y, int z, BlockId id, int face, float baseX, float baseZ,
                 std::vector<BlockVertex>& out) const;

    std::vector<BlockId> padded_ = std::vector<BlockId>(PAD_W * PAD_W * PAD_H);
    std::vector<BlockVertex> water_;  // water faces, appended after the solid ones
};
