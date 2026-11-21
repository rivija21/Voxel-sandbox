// blocks.cpp - The block property table.
#include "blocks.h"

// The order of rows MUST match the order of the Block:: enum.
// static_assert below catches it if a row is added to one but not the other.
// (A namespace-scope const would normally be private to this file; it is shared
// with other files only because blocks.h declared it "extern" first.)
const BlockInfo BLOCKS[] = {
    //  name       top            side           bottom        solid  opaque
    {"Air",     Tex::Dirt,     Tex::Dirt,     Tex::Dirt,     false, false},
    {"Grass",   Tex::GrassTop, Tex::GrassSide, Tex::Dirt,     true,  true},
    {"Dirt",    Tex::Dirt,     Tex::Dirt,     Tex::Dirt,     true,  true},
    {"Stone",   Tex::Stone,    Tex::Stone,    Tex::Stone,    true,  true},
    {"Sand",    Tex::Sand,     Tex::Sand,     Tex::Sand,     true,  true},
    {"Water",   Tex::Water,    Tex::Water,    Tex::Water,    false, false},
    {"Log",     Tex::LogTop,   Tex::LogSide,  Tex::LogTop,   true,  true},
    {"Leaves",  Tex::Leaves,   Tex::Leaves,   Tex::Leaves,   true,  false},
    {"Planks",  Tex::Planks,   Tex::Planks,   Tex::Planks,   true,  true},
    {"Glass",   Tex::Glass,    Tex::Glass,    Tex::Glass,    true,  false},
    {"Brick",   Tex::Brick,    Tex::Brick,    Tex::Brick,    true,  true},
    {"Cobble",  Tex::Cobble,   Tex::Cobble,   Tex::Cobble,   true,  true},
    {"Snow",    Tex::Snow,     Tex::Snow,     Tex::Snow,     true,  true},
    {"Bedrock", Tex::Bedrock,  Tex::Bedrock,  Tex::Bedrock,  true,  true},
};

static_assert(sizeof(BLOCKS) / sizeof(BLOCKS[0]) == Block::Count,
              "BLOCKS needs exactly one row per Block:: id");

const BlockId DEFAULT_HOTBAR[HOTBAR_SLOTS] = {
    Block::Grass, Block::Dirt,  Block::Stone, Block::Cobble, Block::Planks,
    Block::Log,   Block::Brick, Block::Glass, Block::Leaves,
};
