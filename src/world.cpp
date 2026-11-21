// world.cpp - Chunk storage and streaming.
#include "world.h"

#include <algorithm>
#include <chrono>

World::World(uint32_t seed, int viewDistance) : terrain_(seed), viewDistance_(viewDistance) {
    // Precompute every (dx, dz) inside a circle of radius viewDistance and sort
    // them nearest-first, so the world always fills in from the player outwards.
    for (int dz = -viewDistance; dz <= viewDistance; ++dz)
        for (int dx = -viewDistance; dx <= viewDistance; ++dx)
            if (dx * dx + dz * dz <= viewDistance * viewDistance) offsets_.push_back({dx, dz});

    std::sort(offsets_.begin(), offsets_.end(), [](const Offset& a, const Offset& b) {
        return a.dx * a.dx + a.dz * a.dz < b.dx * b.dx + b.dz * b.dz;
    });
}

Chunk* World::findChunk(int cx, int cz) {
    auto it = chunks_.find(key(cx, cz));
    return it == chunks_.end() ? nullptr : it->second.get();
}

const Chunk* World::findChunk(int cx, int cz) const {
    auto it = chunks_.find(key(cx, cz));
    return it == chunks_.end() ? nullptr : it->second.get();
}

bool World::isLoadedAt(const Vec3& p) const {
    return findChunk(floorDiv(floorToInt(p.x), CHUNK_SIZE), floorDiv(floorToInt(p.z), CHUNK_SIZE)) != nullptr;
}

BlockId World::getBlock(int x, int y, int z) const {
    if (y < 0) return Block::Bedrock;
    if (y >= CHUNK_HEIGHT) return Block::Air;
    const Chunk* chunk = findChunk(floorDiv(x, CHUNK_SIZE), floorDiv(z, CHUNK_SIZE));
    if (!chunk) return Block::Bedrock;
    return chunk->get(floorMod(x, CHUNK_SIZE), y, floorMod(z, CHUNK_SIZE));
}

void World::markDirty(int cx, int cz) {
    if (Chunk* c = findChunk(cx, cz)) c->dirty = true;
}

bool World::setBlock(int x, int y, int z, BlockId id) {
    if (y < 0 || y >= CHUNK_HEIGHT) return false;
    const int cx = floorDiv(x, CHUNK_SIZE), cz = floorDiv(z, CHUNK_SIZE);
    Chunk* chunk = findChunk(cx, cz);
    if (!chunk) return false;

    const int lx = floorMod(x, CHUNK_SIZE), lz = floorMod(z, CHUNK_SIZE);
    if (chunk->get(lx, y, lz) == id) return true;
    chunk->set(lx, y, lz, id);
    chunk->modified = true;
    chunk->dirty = true;

    // A block on a chunk's edge also changes the NEIGHBOUR's mesh: one of its
    // faces may appear or vanish, and its corner shading changes. Corners of a
    // chunk touch three neighbours (two sides and a diagonal).
    const int ox = (lx == 0) ? -1 : (lx == CHUNK_SIZE - 1 ? 1 : 0);
    const int oz = (lz == 0) ? -1 : (lz == CHUNK_SIZE - 1 ? 1 : 0);
    if (ox) markDirty(cx + ox, cz);
    if (oz) markDirty(cx, cz + oz);
    if (ox && oz) markDirty(cx + ox, cz + oz);
    return true;
}

Chunk& World::generateChunk(int cx, int cz) {
    auto chunk = std::make_unique<Chunk>();
    chunk->cx = cx;
    chunk->cz = cz;
    terrain_.generate(*chunk);
    Chunk& ref = *chunk;
    chunks_[key(cx, cz)] = std::move(chunk);  // the map now owns it
    return ref;
}

void World::buildMesh(Chunk& chunk) {
    ChunkNeighbourhood around{};
    for (int dz = -1; dz <= 1; ++dz)
        for (int dx = -1; dx <= 1; ++dx) around[(dz + 1) * 3 + (dx + 1)] = findChunk(chunk.cx + dx, chunk.cz + dz);

    int opaqueQuads = 0, waterQuads = 0;
    mesher_.build(around, meshScratch_, opaqueQuads, waterQuads);
    chunk.mesh.upload(meshScratch_, opaqueQuads, waterQuads);
    chunk.dirty = false;
}

void World::addSavedChunk(std::unique_ptr<Chunk> chunk) {
    chunk->modified = true;
    chunk->dirty = true;
    const uint64_t k = key(chunk->cx, chunk->cz);
    chunks_[k] = std::move(chunk);  // replaces (and frees) any generated version
}

void World::update(const Vec3& center, double budgetSeconds) {
    using Clock = std::chrono::steady_clock;
    const auto start = Clock::now();
    auto outOfTime = [&]() {
        return std::chrono::duration<double>(Clock::now() - start).count() > budgetSeconds;
    };

    const int pcx = floorDiv(floorToInt(center.x), CHUNK_SIZE);
    const int pcz = floorDiv(floorToInt(center.z), CHUNK_SIZE);

    // 1. Forget far-away chunks. Their GPU mesh goes first; untouched chunks are
    //    deleted entirely (they can be regenerated from the seed), but chunks
    //    the player changed stay in memory so their changes are not lost.
    const int keepMesh = viewDistance_ + 1;
    const int keepData = viewDistance_ + 3;
    for (auto it = chunks_.begin(); it != chunks_.end();) {
        Chunk& c = *it->second;
        const int dx = c.cx - pcx, dz = c.cz - pcz;
        const int d2 = dx * dx + dz * dz;
        if (d2 > keepMesh * keepMesh && c.mesh.uploaded()) {
            c.mesh.release();
            c.dirty = true;
        }
        if (d2 > keepData * keepData && !c.modified) {
            it = chunks_.erase(it);  // erase returns the next valid iterator
            continue;
        }
        ++it;
    }

    // 2. Nearest first: make sure each chunk AND its 8 neighbours exist (the
    //    mesher looks across the border), then build its mesh.
    for (const Offset& off : offsets_) {
        const int cx = pcx + off.dx, cz = pcz + off.dz;
        const Chunk* existing = findChunk(cx, cz);
        if (existing && !existing->dirty) continue;  // already up to date

        for (int nz = -1; nz <= 1; ++nz) {
            for (int nx = -1; nx <= 1; ++nx) {
                if (findChunk(cx + nx, cz + nz)) continue;
                generateChunk(cx + nx, cz + nz);
                if (outOfTime()) return;  // carry on next frame
            }
        }
        buildMesh(*findChunk(cx, cz));
        if (outOfTime()) return;
    }
}

int World::pendingChunks(const Vec3& center, int radius) const {
    const int pcx = floorDiv(floorToInt(center.x), CHUNK_SIZE);
    const int pcz = floorDiv(floorToInt(center.z), CHUNK_SIZE);
    int pending = 0;
    for (const Offset& off : offsets_) {
        if (off.dx * off.dx + off.dz * off.dz > radius * radius) break;  // sorted, so we can stop
        const Chunk* c = findChunk(pcx + off.dx, pcz + off.dz);
        if (!c || c->dirty) ++pending;
    }
    return pending;
}
