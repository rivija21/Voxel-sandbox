// gpu_mesh.cpp - Uploading and drawing chunk meshes.
#include "gpu_mesh.h"

#include <algorithm>
#include <cstddef>  // offsetof

namespace {

// Every quad is drawn as two triangles: corners (0,1,2) and (0,2,3).
// That pattern is the same for every quad of every chunk, so ONE index buffer
// is shared by all meshes instead of each chunk storing its own copy.
GLuint g_quadIndexBuffer = 0;
int g_quadIndexCapacity = 0;  // how many quads the shared buffer can describe

void ensureQuadIndices(int quads) {
    if (quads <= g_quadIndexCapacity) return;
    int capacity = std::max(4096, g_quadIndexCapacity);
    while (capacity < quads) capacity *= 2;

    std::vector<uint32_t> indices(static_cast<size_t>(capacity) * 6);
    for (int q = 0; q < capacity; ++q) {
        const uint32_t first = static_cast<uint32_t>(q) * 4;
        uint32_t* out = &indices[static_cast<size_t>(q) * 6];
        out[0] = first;     out[1] = first + 1; out[2] = first + 2;  // triangle 1
        out[3] = first;     out[4] = first + 2; out[5] = first + 3;  // triangle 2
    }

    if (!g_quadIndexBuffer) glGenBuffers(1, &g_quadIndexBuffer);
    // Upload through the GL_ARRAY_BUFFER binding point. Binding it as
    // GL_ELEMENT_ARRAY_BUFFER here would silently change whichever VAO is bound.
    // (A buffer is just bytes; the binding point only says how it is used.)
    glBindBuffer(GL_ARRAY_BUFFER, g_quadIndexBuffer);
    glBufferData(GL_ARRAY_BUFFER, indices.size() * sizeof(uint32_t), indices.data(), GL_STATIC_DRAW);
    glBindBuffer(GL_ARRAY_BUFFER, 0);
    g_quadIndexCapacity = capacity;
}

}  // namespace

void releaseSharedQuadIndices() {
    if (g_quadIndexBuffer) glDeleteBuffers(1, &g_quadIndexBuffer);
    g_quadIndexBuffer = 0;
    g_quadIndexCapacity = 0;
}

void GpuMesh::upload(const std::vector<BlockVertex>& vertices, int opaqueQuads, int waterQuads) {
    if (vertices.empty()) {  // e.g. a chunk of pure air: nothing to draw
        release();
        return;
    }
    ensureQuadIndices(opaqueQuads + waterQuads);

    if (!vao_) {
        // First upload: create the objects and describe the vertex layout once.
        // The VAO remembers all of this, so drawing later is just "bind VAO, draw".
        glGenVertexArrays(1, &vao_);
        glGenBuffers(1, &vbo_);
        glBindVertexArray(vao_);
        glBindBuffer(GL_ARRAY_BUFFER, vbo_);

        // Attribute 0 = position: 3 floats.
        glEnableVertexAttribArray(0);
        glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, sizeof(BlockVertex),
                              reinterpret_cast<const void*>(offsetof(BlockVertex, x)));
        // Attribute 1 = (u, v, layer, shade): 4 unsigned bytes, NOT normalised,
        // so the shader sees the numbers 0..255 as floats.
        glEnableVertexAttribArray(1);
        glVertexAttribPointer(1, 4, GL_UNSIGNED_BYTE, GL_FALSE, sizeof(BlockVertex),
                              reinterpret_cast<const void*>(offsetof(BlockVertex, u)));

        glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, g_quadIndexBuffer);  // stored in the VAO
    } else {
        glBindVertexArray(vao_);
        glBindBuffer(GL_ARRAY_BUFFER, vbo_);
    }

    glBufferData(GL_ARRAY_BUFFER, vertices.size() * sizeof(BlockVertex), vertices.data(), GL_STATIC_DRAW);
    glBindVertexArray(0);

    opaqueQuads_ = opaqueQuads;
    waterQuads_ = waterQuads;
}

void GpuMesh::release() {
    if (vbo_) glDeleteBuffers(1, &vbo_);
    if (vao_) glDeleteVertexArrays(1, &vao_);
    vao_ = vbo_ = 0;
    opaqueQuads_ = waterQuads_ = 0;
}

void GpuMesh::drawOpaque() const {
    if (opaqueQuads_ == 0) return;
    glBindVertexArray(vao_);
    glDrawElements(GL_TRIANGLES, opaqueQuads_ * 6, GL_UNSIGNED_INT, nullptr);
}

void GpuMesh::drawWater() const {
    if (waterQuads_ == 0) return;
    glBindVertexArray(vao_);
    // The water quads start after the opaque ones. "Base vertex" adds that
    // offset to every index, so the shared 0,1,2,0,2,3,... indices still work.
    glDrawElementsBaseVertex(GL_TRIANGLES, waterQuads_ * 6, GL_UNSIGNED_INT, nullptr, opaqueQuads_ * 4);
}
