// renderer.h - Draws the 3D world: sky, terrain, water and the block outline.
#pragma once

#include "gl.h"
#include "mathlib.h"
#include "raycast.h"
#include "shader.h"

#include <vector>

class World;
struct Chunk;

// Everything about the camera for one frame.
struct Camera {
    Vec3 position;
    Vec3 forward, right, up;  // unit vectors
    float fovY = 1.0f;        // vertical field of view, radians
    float aspect = 1.0f;      // width / height
    Mat4 view, projection, viewProjection;
};

Camera makeCamera(const Vec3& eye, const Vec3& forward, float fovY, float aspect);

struct RenderStats {
    int chunksDrawn = 0;
    int quadsDrawn = 0;
};

class Renderer {
public:
    Renderer() = default;
    ~Renderer();
    Renderer(const Renderer&) = delete;
    Renderer& operator=(const Renderer&) = delete;

    bool init();

    // Draws the sky alone (used on the loading screen).
    void drawSkyOnly(const Camera& camera);

    // Draws the full scene. `target` is outlined if it hit something.
    void drawWorld(const World& world, const Camera& camera, bool underwater, const RayHit& target,
                   RenderStats& stats);

    GLuint blockTextures() const { return blockTextures_; }

private:
    void setSkyUniforms(const Shader& shader, bool underwater) const;
    void drawSky(const Camera& camera, bool underwater);
    void drawOutline(const Camera& camera, const IVec3& block);

    Shader blockShader_, skyShader_, lineShader_;
    GLuint blockTextures_ = 0;
    GLuint emptyVao_ = 0;  // the sky needs no vertex data, but core OpenGL insists on a bound VAO
    GLuint lineVao_ = 0, lineVbo_ = 0;
    std::vector<const Chunk*> waterChunks_;  // reused every frame
};
