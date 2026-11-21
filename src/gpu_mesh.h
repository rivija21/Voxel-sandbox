// gpu_mesh.h - A chunk's triangles, stored in graphics-card memory.
#pragma once

#include "gl.h"

#include <cstdint>
#include <vector>

// One corner of one block face, exactly as the GPU reads it: 16 bytes.
// Packing u, v, layer and shade into single bytes keeps memory use low; the
// vertex shader receives them as the floats 0..255.
struct BlockVertex {
    float x, y, z;  // position in the world
    uint8_t u, v;   // texture coordinate: 0 or 1 (which corner of the picture)
    uint8_t layer;  // which picture in the texture array (a Tex:: value)
    uint8_t shade;  // brightness 0..255 (face direction x ambient occlusion)
};
static_assert(sizeof(BlockVertex) == 16, "BlockVertex must be 16 tightly packed bytes");

// Owns one vertex array object (VAO) and one vertex buffer (VBO).
// The buffer holds every quad (4 vertices) of a chunk: first all the solid
// quads, then all the water quads, so each group can be drawn separately.
//
// RAII: the destructor frees the GPU memory, so a GpuMesh can never leak it.
class GpuMesh {
public:
    GpuMesh() = default;
    ~GpuMesh() { release(); }
    GpuMesh(const GpuMesh&) = delete;             // one owner only
    GpuMesh& operator=(const GpuMesh&) = delete;

    void upload(const std::vector<BlockVertex>& vertices, int opaqueQuads, int waterQuads);
    void release();

    void drawOpaque() const;
    void drawWater() const;

    bool uploaded() const { return vao_ != 0; }
    int opaqueQuads() const { return opaqueQuads_; }
    int waterQuads() const { return waterQuads_; }

private:
    GLuint vao_ = 0;
    GLuint vbo_ = 0;
    int opaqueQuads_ = 0;
    int waterQuads_ = 0;
};

// Frees the index buffer that all meshes share. Call once at shutdown.
void releaseSharedQuadIndices();
