// mesher.cpp - Visible-face extraction with per-corner ambient occlusion.
#include "mesher.h"

#include <cmath>

namespace {

enum Face { FACE_EAST, FACE_WEST, FACE_TOP, FACE_BOTTOM, FACE_SOUTH, FACE_NORTH };

struct FaceDef {
    int dir[3];        // the face's outward normal = direction of the neighbour it faces
    int corner[4][3];  // its 4 corners (each coordinate 0 or 1), counter-clockwise seen from outside
    float light;       // fixed brightness per direction: a cheap stand-in for real sunlight
};

// Corner order is bottom-left, bottom-right, top-right, top-left as seen from
// outside the block. Counter-clockwise order makes the triangle "front facing",
// so OpenGL's back-face culling throws away faces seen from behind.
const FaceDef FACES[6] = {
    {{1, 0, 0},  {{1, 0, 1}, {1, 0, 0}, {1, 1, 0}, {1, 1, 1}}, 0.80f},  // east  (+X)
    {{-1, 0, 0}, {{0, 0, 0}, {0, 0, 1}, {0, 1, 1}, {0, 1, 0}}, 0.80f},  // west  (-X)
    {{0, 1, 0},  {{0, 1, 1}, {1, 1, 1}, {1, 1, 0}, {0, 1, 0}}, 1.00f},  // top   (+Y)
    {{0, -1, 0}, {{0, 0, 0}, {1, 0, 0}, {1, 0, 1}, {0, 0, 1}}, 0.52f},  // bottom(-Y)
    {{0, 0, 1},  {{0, 0, 1}, {1, 0, 1}, {1, 1, 1}, {0, 1, 1}}, 0.66f},  // south (+Z)
    {{0, 0, -1}, {{1, 0, 0}, {0, 0, 0}, {0, 1, 0}, {1, 1, 0}}, 0.66f},  // north (-Z)
};

// Texture corner for each of the 4 vertices. v = 0 is the TOP of the picture,
// so the top two corners get v = 0 and grass hangs down the side, not up.
const uint8_t FACE_UV[4][2] = {{0, 1}, {1, 1}, {1, 0}, {0, 0}};

// Brightness for an ambient-occlusion level: 0 = corner boxed in, 3 = open.
const float AO_CURVE[4] = {0.38f, 0.58f, 0.78f, 1.0f};

// Should the face of `self` that touches `neighbour` be drawn?
bool faceVisible(BlockId self, BlockId neighbour) {
    if (neighbour == Block::Air) return true;
    if (isOpaque(neighbour)) return false;  // hidden behind a solid block
    // Two see-through blocks of the same kind: glass panes and water join up
    // seamlessly, but leaves keep their inner faces for a bushier look.
    if (neighbour == self) return self == Block::Leaves;
    return true;  // e.g. sand under water, water behind glass
}

}  // namespace

void ChunkMesher::copyNeighbourhood(const ChunkNeighbourhood& around) {
    for (int z = -1; z <= CHUNK_SIZE; ++z) {
        const int nz = (z < 0) ? 0 : (z >= CHUNK_SIZE ? 2 : 1);  // which neighbour row
        const int lz = z - (nz - 1) * CHUNK_SIZE;                 // z inside that neighbour
        for (int x = -1; x <= CHUNK_SIZE; ++x) {
            const int nx = (x < 0) ? 0 : (x >= CHUNK_SIZE ? 2 : 1);
            const int lx = x - (nx - 1) * CHUNK_SIZE;
            const Chunk& src = *around[nz * 3 + nx];

            padded_[padIndex(x, -1, z)] = Block::Bedrock;          // below the world: solid
            padded_[padIndex(x, CHUNK_HEIGHT, z)] = Block::Air;    // above the world: open sky
            for (int y = 0; y < CHUNK_HEIGHT; ++y) padded_[padIndex(x, y, z)] = src.get(lx, y, lz);
        }
    }
}

void ChunkMesher::build(const ChunkNeighbourhood& around, std::vector<BlockVertex>& vertices,
                        int& opaqueQuads, int& waterQuads) {
    copyNeighbourhood(around);
    vertices.clear();
    water_.clear();

    const Chunk& chunk = *around[4];
    const float baseX = static_cast<float>(chunk.cx * CHUNK_SIZE);
    const float baseZ = static_cast<float>(chunk.cz * CHUNK_SIZE);

    // Skip the empty sky: find the highest layer that has any block in it.
    // (Blocks are stored layer by layer, so scan backwards from the end.)
    int topY = -1;
    for (int i = CHUNK_VOLUME - 1; i >= 0; --i) {
        if (chunk.blocks[i] != Block::Air) {
            topY = i / (CHUNK_SIZE * CHUNK_SIZE);
            break;
        }
    }

    for (int y = 0; y <= topY; ++y) {
        for (int z = 0; z < CHUNK_SIZE; ++z) {
            for (int x = 0; x < CHUNK_SIZE; ++x) {
                const BlockId id = at(x, y, z);
                if (id == Block::Air) continue;
                for (int face = 0; face < 6; ++face) {
                    const int* d = FACES[face].dir;
                    if (!faceVisible(id, at(x + d[0], y + d[1], z + d[2]))) continue;
                    addFace(x, y, z, id, face, baseX, baseZ, id == Block::Water ? water_ : vertices);
                }
            }
        }
    }

    opaqueQuads = static_cast<int>(vertices.size() / 4);
    waterQuads = static_cast<int>(water_.size() / 4);
    vertices.insert(vertices.end(), water_.begin(), water_.end());
}

void ChunkMesher::addFace(int x, int y, int z, BlockId id, int face, float baseX, float baseZ,
                          std::vector<BlockVertex>& out) const {
    const FaceDef& f = FACES[face];
    const BlockInfo& info = blockInfo(id);
    const uint8_t layer = (face == FACE_TOP) ? info.texTop : (face == FACE_BOTTOM ? info.texBottom : info.texSide);

    // Water's surface sits a little lower than a full block, unless more water is on top.
    const bool water = (id == Block::Water);
    const float topHeight = (water && at(x, y + 1, z) != Block::Water) ? 0.875f : 1.0f;

    // The normal lies along one axis; the other two axes (a and b) span the face.
    const int normalAxis = f.dir[0] != 0 ? 0 : (f.dir[1] != 0 ? 1 : 2);
    const int axisA = (normalAxis + 1) % 3;
    const int axisB = (normalAxis + 2) % 3;

    BlockVertex v[4];
    int ao[4];
    for (int i = 0; i < 4; ++i) {
        const int* c = f.corner[i];

        // Ambient occlusion: look at the 3 blocks touching this corner in the
        // layer just in front of the face (two "sides" and one diagonal).
        // The more of them are solid, the darker the corner.
        ao[i] = 3;
        if (!water) {
            const int front[3] = {x + f.dir[0], y + f.dir[1], z + f.dir[2]};
            const int stepA = c[axisA] * 2 - 1;  // -1 or +1: towards this corner
            const int stepB = c[axisB] * 2 - 1;
            int s1[3] = {front[0], front[1], front[2]};
            int s2[3] = {front[0], front[1], front[2]};
            int cc[3] = {front[0], front[1], front[2]};
            s1[axisA] += stepA;
            s2[axisB] += stepB;
            cc[axisA] += stepA;
            cc[axisB] += stepB;
            const int side1 = isOpaque(at(s1[0], s1[1], s1[2])) ? 1 : 0;
            const int side2 = isOpaque(at(s2[0], s2[1], s2[2])) ? 1 : 0;
            const int diag = isOpaque(at(cc[0], cc[1], cc[2])) ? 1 : 0;
            // Two solid sides close the corner completely, whatever the diagonal is.
            ao[i] = (side1 && side2) ? 0 : 3 - (side1 + side2 + diag);
        }

        v[i].x = baseX + static_cast<float>(x + c[0]);
        v[i].y = static_cast<float>(y) + (c[1] ? topHeight : 0.0f);
        v[i].z = baseZ + static_cast<float>(z + c[2]);
        v[i].u = FACE_UV[i][0];
        v[i].v = FACE_UV[i][1];
        v[i].layer = layer;
        v[i].shade = static_cast<uint8_t>(std::lround(f.light * AO_CURVE[ao[i]] * 255.0f));
    }

    // A quad is split into two triangles along a diagonal. The GPU blends
    // brightness across each triangle, so the diagonal matters: split along
    // the pair of corners that agree best, or a dark streak appears.
    // The shared index buffer always splits along corners 0-2, so to split
    // along 1-3 we rotate the corner order by one instead.
    if (ao[0] + ao[2] < ao[1] + ao[3]) {
        out.push_back(v[1]); out.push_back(v[2]); out.push_back(v[3]); out.push_back(v[0]);
    } else {
        out.push_back(v[0]); out.push_back(v[1]); out.push_back(v[2]); out.push_back(v[3]);
    }
}
