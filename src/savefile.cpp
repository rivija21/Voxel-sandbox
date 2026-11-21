// savefile.cpp - The world.dat file format.
//
//   offset  size   field
//   0       4      magic "VOXL"           (identifies the file type)
//   4       4      format version (1)
//   8       4      seed
//   12      12     player position x, y, z (float)
//   24      8      yaw, pitch (float)
//   32      1      flying (0/1)
//   33      9      hotbar block ids
//   42      4      number of chunks N
//   46      ...    N times: cx (int32), cz (int32), 32768 block ids
//
// All numbers are little-endian, as stored in memory on x86 PCs.
#include "savefile.h"

#include "world.h"

#include <cstring>
#include <fstream>
#include <system_error>

namespace {

constexpr char MAGIC[4] = {'V', 'O', 'X', 'L'};
constexpr uint32_t VERSION = 1;

// Function templates: one definition that works for any plain type T
// (int, float, ...). The compiler stamps out a copy for each T we use.
template <typename T>
void writeValue(std::ofstream& out, const T& value) {
    out.write(reinterpret_cast<const char*>(&value), sizeof(T));
}

template <typename T>
bool readValue(std::ifstream& in, T& value) {
    in.read(reinterpret_cast<char*>(&value), sizeof(T));
    return static_cast<bool>(in);
}

bool validBlock(BlockId id) { return id < Block::Count; }

}  // namespace

bool saveWorld(const std::filesystem::path& path, const World& world, const PlayerSave& player) {
    // Write to a temporary file first and swap it in at the end. If the game
    // crashes half-way through saving, the previous save is still intact.
    std::filesystem::path temp = path;
    temp += ".tmp";
    {
        std::ofstream out(temp, std::ios::binary);
        if (!out) return false;

        out.write(MAGIC, sizeof(MAGIC));
        writeValue(out, VERSION);
        writeValue(out, world.seed());
        writeValue(out, player.position.x);
        writeValue(out, player.position.y);
        writeValue(out, player.position.z);
        writeValue(out, player.yaw);
        writeValue(out, player.pitch);
        writeValue(out, static_cast<uint8_t>(player.flying ? 1 : 0));
        out.write(reinterpret_cast<const char*>(player.hotbar), HOTBAR_SLOTS);

        uint32_t count = 0;
        for (const auto& entry : world.chunks())
            if (entry.second->modified) ++count;
        writeValue(out, count);

        for (const auto& entry : world.chunks()) {
            const Chunk& chunk = *entry.second;
            if (!chunk.modified) continue;
            writeValue(out, static_cast<int32_t>(chunk.cx));
            writeValue(out, static_cast<int32_t>(chunk.cz));
            out.write(reinterpret_cast<const char*>(chunk.blocks.data()), CHUNK_VOLUME);
        }
        if (!out) return false;
    }  // the ofstream closes (and flushes) here, at the end of its scope

    std::error_code error;
    std::filesystem::rename(temp, path, error);
    return !error;
}

bool loadWorld(const std::filesystem::path& path, uint32_t& seed, PlayerSave& player,
               std::vector<std::unique_ptr<Chunk>>& chunks) {
    std::ifstream in(path, std::ios::binary);
    if (!in) return false;

    char magic[4];
    uint32_t version = 0;
    in.read(magic, sizeof(magic));
    if (!in || std::memcmp(magic, MAGIC, sizeof(MAGIC)) != 0) return false;
    if (!readValue(in, version) || version != VERSION) return false;

    uint8_t flying = 0;
    PlayerSave p;
    if (!readValue(in, seed) || !readValue(in, p.position.x) || !readValue(in, p.position.y) ||
        !readValue(in, p.position.z) || !readValue(in, p.yaw) || !readValue(in, p.pitch) || !readValue(in, flying))
        return false;
    p.flying = flying != 0;
    in.read(reinterpret_cast<char*>(p.hotbar), HOTBAR_SLOTS);
    if (!in) return false;
    for (BlockId id : p.hotbar)
        if (!validBlock(id)) return false;

    uint32_t count = 0;
    if (!readValue(in, count) || count > 100000) return false;  // sanity limit against a damaged file

    std::vector<std::unique_ptr<Chunk>> loaded;
    for (uint32_t i = 0; i < count; ++i) {
        auto chunk = std::make_unique<Chunk>();
        int32_t cx = 0, cz = 0;
        if (!readValue(in, cx) || !readValue(in, cz)) return false;
        chunk->cx = cx;
        chunk->cz = cz;
        in.read(reinterpret_cast<char*>(chunk->blocks.data()), CHUNK_VOLUME);
        if (!in) return false;
        // Never trust a file: a byte >= Block::Count would make BLOCKS[id] read
        // past the end of the table later.
        for (BlockId id : chunk->blocks)
            if (!validBlock(id)) return false;
        loaded.push_back(std::move(chunk));
    }

    player = p;
    chunks = std::move(loaded);
    return true;
}
