// savefile.h - Saving and loading the world to a binary file.
//
// Only the seed, the player and the chunks the player CHANGED are stored.
// Everything else is regenerated from the seed, so a save stays small.
#pragma once

#include "blocks.h"
#include "chunk.h"
#include "mathlib.h"

#include <cstdint>
#include <filesystem>
#include <memory>
#include <vector>

class World;

struct PlayerSave {
    Vec3 position;
    float yaw = 0.0f;
    float pitch = 0.0f;
    bool flying = false;
    BlockId hotbar[HOTBAR_SLOTS] = {};
};

bool saveWorld(const std::filesystem::path& path, const World& world, const PlayerSave& player);

// On success fills seed, player and chunks. Returns false if the file is
// missing, from another version, or damaged.
bool loadWorld(const std::filesystem::path& path, uint32_t& seed, PlayerSave& player,
               std::vector<std::unique_ptr<Chunk>>& chunks);
