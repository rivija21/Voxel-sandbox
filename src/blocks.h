// blocks.h - Every kind of block and its properties.
//
// A block is stored as a single byte (BlockId). The byte is an index into the
// BLOCKS table, which says how that kind of block looks and behaves. This is
// "data-driven design": to add a block you add a row, not new code paths.
#pragma once

#include <cstdint>

using BlockId = uint8_t;

// An unnamed enum inside a namespace gives us Block::Grass, Block::Stone, ...
// as plain BlockId-compatible constants, without polluting the global namespace
// (windows.h already defines macros with short names, so this avoids clashes).
namespace Block {
enum : BlockId {
    Air,
    Grass,
    Dirt,
    Stone,
    Sand,
    Water,
    Log,
    Leaves,
    Planks,
    Glass,
    Brick,
    Cobble,
    Snow,
    Bedrock,
    Count  // not a block: the number of block kinds
};
}

// Layers of the texture array (see textures.cpp). One 16x16 picture each.
namespace Tex {
enum : uint8_t {
    GrassTop, GrassSide, Dirt, Stone, Sand, Water, LogSide, LogTop,
    Leaves, Planks, Glass, Brick, Cobble, Snow, Bedrock,
    Count
};
}

struct BlockInfo {
    const char* name;
    uint8_t texTop, texSide, texBottom;  // texture layer for each kind of face
    bool solid;   // the player bumps into it and the crosshair can select it
    bool opaque;  // completely hides whatever is behind it, and darkens corners (AO)
};

// Defined in blocks.cpp. The size is left out here on purpose so blocks.cpp can
// check (with static_assert) that the table really has Block::Count rows.
extern const BlockInfo BLOCKS[];

inline const BlockInfo& blockInfo(BlockId id) { return BLOCKS[id]; }
inline bool isSolid(BlockId id) { return BLOCKS[id].solid; }
inline bool isOpaque(BlockId id) { return BLOCKS[id].opaque; }

// The blocks the player starts with in the hotbar (keys 1-9).
constexpr int HOTBAR_SLOTS = 9;
extern const BlockId DEFAULT_HOTBAR[HOTBAR_SLOTS];
