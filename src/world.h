// world.h - All loaded chunks, plus the rules for loading and unloading them.
#pragma once

#include "chunk.h"
#include "mathlib.h"
#include "mesher.h"
#include "terrain.h"

#include <cstdint>
#include <memory>
#include <unordered_map>
#include <vector>

class World {
public:
    World(uint32_t seed, int viewDistance);

    const TerrainGenerator& terrain() const { return terrain_; }
    uint32_t seed() const { return terrain_.seed(); }
    int viewDistance() const { return viewDistance_; }  // in chunks

    // Block access in WORLD coordinates.
    // Above the world is Air, below is Bedrock. Unloaded places also report
    // Bedrock, so the player can never walk into land that does not exist yet.
    BlockId getBlock(int x, int y, int z) const;
    BlockId getBlock(const IVec3& p) const { return getBlock(p.x, p.y, p.z); }

    // Changes a block and schedules the affected chunk meshes to be rebuilt.
    // Returns false if that part of the world is not loaded.
    bool setBlock(int x, int y, int z, BlockId id);

    Chunk* findChunk(int cx, int cz);
    const Chunk* findChunk(int cx, int cz) const;
    bool isLoadedAt(const Vec3& p) const;

    // Called once per frame. Unloads distant chunks, then generates and meshes
    // the nearest missing ones until `budgetSeconds` of time has been used.
    // Spreading the work over many frames keeps the game smooth.
    void update(const Vec3& center, double budgetSeconds);

    // How many chunks within `radius` of `center` are not ready to draw yet.
    int pendingChunks(const Vec3& center, int radius) const;

    // Puts a chunk read from the save file into the world.
    void addSavedChunk(std::unique_ptr<Chunk> chunk);

    // Packs two 32-bit chunk coordinates into one 64-bit map key.
    static uint64_t key(int cx, int cz) {
        return (static_cast<uint64_t>(static_cast<uint32_t>(cx)) << 32) | static_cast<uint32_t>(cz);
    }

    const std::unordered_map<uint64_t, std::unique_ptr<Chunk>>& chunks() const { return chunks_; }

private:
    struct Offset { int dx, dz; };

    Chunk& generateChunk(int cx, int cz);
    void buildMesh(Chunk& chunk);
    void markDirty(int cx, int cz);

    TerrainGenerator terrain_;
    int viewDistance_;
    std::unordered_map<uint64_t, std::unique_ptr<Chunk>> chunks_;
    std::vector<Offset> offsets_;  // every chunk offset within view distance, nearest first
    ChunkMesher mesher_;
    std::vector<BlockVertex> meshScratch_;  // reused between meshes to avoid re-allocating
};
