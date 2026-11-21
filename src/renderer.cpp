// renderer.cpp - The 3D render passes and their GLSL shaders.
#include "renderer.h"

#include "textures.h"
#include "world.h"

#include <algorithm>
#include <string>

// Some older glext.h files lack these (anisotropic filtering extension).
#ifndef GL_TEXTURE_MAX_ANISOTROPY_EXT
#define GL_TEXTURE_MAX_ANISOTROPY_EXT 0x84FE
#endif
#ifndef GL_MAX_TEXTURE_MAX_ANISOTROPY_EXT
#define GL_MAX_TEXTURE_MAX_ANISOTROPY_EXT 0x84FF
#endif

namespace {

// --- Colours of the world -------------------------------------------------
const Vec3 SKY_HORIZON = {0.74f, 0.85f, 0.96f};
const Vec3 SKY_ZENITH = {0.30f, 0.52f, 0.88f};
const Vec3 WATER_FOG = {0.09f, 0.22f, 0.42f};
const Vec3 SUN_DIRECTION = normalize(Vec3{0.45f, 0.62f, -0.64f});

// --- Shaders --------------------------------------------------------------
// GLSL source is kept in C++ "raw string literals": R"( ... )" may span many
// lines and needs no escaping, so the shader code reads as normal code.

// Shared by the sky and the terrain: the colour of the sky in a direction.
// Terrain fades into exactly this colour with distance (fog), so far hills
// melt into the horizon instead of ending at a hard edge.
const char* SKY_FUNCTION = R"(
uniform vec3 uHorizon;
uniform vec3 uZenith;
uniform vec3 uSunDir;
uniform float uSunStrength;   // 1 normally, 0 under water

vec3 skyColor(vec3 dir) {
    // smoothstep starts flat, so there is no visible crease along the horizon.
    vec3 sky = mix(uHorizon, uZenith, smoothstep(0.0, 0.6, dir.y));
    float towardSun = max(dot(dir, uSunDir), 0.0);
    sky += vec3(1.0, 0.85, 0.6) * pow(towardSun, 12.0) * 0.22 * uSunStrength;  // warm haze around the sun
    return sky;
}
)";

const char* BLOCK_VERTEX = R"(#version 330 core
layout(location = 0) in vec3 aPosition;
layout(location = 1) in vec4 aData;      // u, v, texture layer, shade - each 0..255

uniform mat4 uViewProj;

out vec3 vTexCoord;   // (u, v, layer) for the texture array
out float vShade;
out vec3 vWorldPos;

void main() {
    vTexCoord = aData.xyz;
    vShade = aData.w / 255.0;
    vWorldPos = aPosition;
    gl_Position = uViewProj * vec4(aPosition, 1.0);
}
)";

const char* BLOCK_FRAGMENT = R"(
in vec3 vTexCoord;
in float vShade;
in vec3 vWorldPos;

uniform sampler2DArray uTextures;
uniform vec3 uCameraPos;
uniform float uFogStart;
uniform float uFogEnd;
uniform float uAlpha;

out vec4 fragColor;

void main() {
    vec4 texel = texture(uTextures, vTexCoord);
    if (texel.a < 0.5) discard;              // holes in leaves and glass: draw nothing
    vec3 color = texel.rgb * vShade;

    vec3 toPixel = vWorldPos - uCameraPos;
    float fog = smoothstep(uFogStart, uFogEnd, length(toPixel));
    color = mix(color, skyColor(normalize(toPixel)), fog);
    fragColor = vec4(color, uAlpha);
}
)";

// The sky is one triangle big enough to cover the whole screen. Its corners
// are computed from gl_VertexID, so it needs no vertex buffer at all.
const char* SKY_VERTEX = R"(#version 330 core
out vec2 vNdc;
void main() {
    vec2 p = vec2((gl_VertexID << 1) & 2, gl_VertexID & 2) * 2.0 - 1.0;  // (-1,-1) (3,-1) (-1,3)
    vNdc = p;
    gl_Position = vec4(p, 0.0, 1.0);
}
)";

const char* SKY_FRAGMENT = R"(
in vec2 vNdc;
uniform vec3 uCamForward;
uniform vec3 uCamRight;
uniform vec3 uCamUp;
uniform float uTanHalfFov;
uniform float uAspect;
out vec4 fragColor;

void main() {
    // Rebuild the direction this pixel looks in, from the camera's axes.
    vec3 dir = normalize(uCamForward + vNdc.x * uTanHalfFov * uAspect * uCamRight
                                     + vNdc.y * uTanHalfFov * uCamUp);
    vec3 color = skyColor(dir);
    float sunDisc = smoothstep(0.9990, 0.9994, dot(dir, uSunDir));
    color += vec3(1.0, 0.95, 0.8) * sunDisc * uSunStrength;
    fragColor = vec4(color, 1.0);
}
)";

const char* LINE_VERTEX = R"(#version 330 core
layout(location = 0) in vec3 aPosition;
uniform mat4 uViewProj;
void main() { gl_Position = uViewProj * vec4(aPosition, 1.0); }
)";

const char* LINE_FRAGMENT = R"(#version 330 core
uniform vec4 uColor;
out vec4 fragColor;
void main() { fragColor = uColor; }
)";

}  // namespace

Camera makeCamera(const Vec3& eye, const Vec3& forward, float fovY, float aspect) {
    const Vec3 worldUp{0.0f, 1.0f, 0.0f};
    Camera c;
    c.position = eye;
    c.forward = forward;
    c.right = normalize(cross(forward, worldUp));
    c.up = cross(c.right, forward);
    c.fovY = fovY;
    c.aspect = aspect;
    c.view = lookAt(eye, eye + forward, worldUp);
    c.projection = perspective(fovY, aspect, 0.05f, 1000.0f);
    c.viewProjection = c.projection * c.view;  // view first, then projection
    return c;
}

Renderer::~Renderer() {
    if (blockTextures_) glDeleteTextures(1, &blockTextures_);
    if (emptyVao_) glDeleteVertexArrays(1, &emptyVao_);
    if (lineVbo_) glDeleteBuffers(1, &lineVbo_);
    if (lineVao_) glDeleteVertexArrays(1, &lineVao_);
}

bool Renderer::init() {
    // The fragment shaders that use skyColor() get it pasted in after #version.
    const std::string blockFragment = std::string("#version 330 core\n") + SKY_FUNCTION + BLOCK_FRAGMENT;
    const std::string skyFragment = std::string("#version 330 core\n") + SKY_FUNCTION + SKY_FRAGMENT;
    if (!blockShader_.build("block", BLOCK_VERTEX, blockFragment.c_str())) return false;
    if (!skyShader_.build("sky", SKY_VERTEX, skyFragment.c_str())) return false;
    if (!lineShader_.build("line", LINE_VERTEX, LINE_FRAGMENT)) return false;

    // --- Block textures: one 2D texture ARRAY, one layer per picture. ---
    // An array (instead of one big atlas image) means neighbouring pictures
    // can never bleed into each other, and mipmaps work per picture.
    const std::vector<uint32_t> pixels = generateBlockTextures();
    glGenTextures(1, &blockTextures_);
    glBindTexture(GL_TEXTURE_2D_ARRAY, blockTextures_);
    glTexImage3D(GL_TEXTURE_2D_ARRAY, 0, GL_RGBA8, TEXTURE_SIZE, TEXTURE_SIZE, Tex::Count, 0, GL_RGBA,
                 GL_UNSIGNED_BYTE, pixels.data());
    glGenerateMipmap(GL_TEXTURE_2D_ARRAY);  // smaller copies for far-away blocks (less shimmer)
    glTexParameteri(GL_TEXTURE_2D_ARRAY, GL_TEXTURE_MAG_FILTER, GL_NEAREST);  // crisp pixel-art up close
    glTexParameteri(GL_TEXTURE_2D_ARRAY, GL_TEXTURE_MIN_FILTER, GL_NEAREST_MIPMAP_LINEAR);
    glTexParameteri(GL_TEXTURE_2D_ARRAY, GL_TEXTURE_WRAP_S, GL_REPEAT);
    glTexParameteri(GL_TEXTURE_2D_ARRAY, GL_TEXTURE_WRAP_T, GL_REPEAT);

    // Anisotropic filtering keeps the ground sharp when seen at a low angle.
    // It is an extension, so ask how much is supported and ignore it if none.
    GLfloat maxAniso = 0.0f;
    glGetFloatv(GL_MAX_TEXTURE_MAX_ANISOTROPY_EXT, &maxAniso);
    if (glGetError() == GL_NO_ERROR && maxAniso > 1.0f)
        glTexParameterf(GL_TEXTURE_2D_ARRAY, GL_TEXTURE_MAX_ANISOTROPY_EXT, std::min(8.0f, maxAniso));
    while (glGetError() != GL_NO_ERROR) {}  // clear any error from the probing above

    blockShader_.use();
    blockShader_.setInt("uTextures", 0);  // the sampler reads texture unit 0

    glGenVertexArrays(1, &emptyVao_);

    // The outline is 12 lines = 24 vertices, rewritten every frame.
    glGenVertexArrays(1, &lineVao_);
    glGenBuffers(1, &lineVbo_);
    glBindVertexArray(lineVao_);
    glBindBuffer(GL_ARRAY_BUFFER, lineVbo_);
    glBufferData(GL_ARRAY_BUFFER, 24 * sizeof(Vec3), nullptr, GL_DYNAMIC_DRAW);
    glEnableVertexAttribArray(0);
    glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, sizeof(Vec3), nullptr);
    glBindVertexArray(0);
    return true;
}

void Renderer::setSkyUniforms(const Shader& shader, bool underwater) const {
    shader.setVec3("uHorizon", underwater ? WATER_FOG : SKY_HORIZON);
    shader.setVec3("uZenith", underwater ? WATER_FOG : SKY_ZENITH);
    shader.setVec3("uSunDir", SUN_DIRECTION);
    shader.setFloat("uSunStrength", underwater ? 0.0f : 1.0f);
}

void Renderer::drawSky(const Camera& camera, bool underwater) {
    glDisable(GL_DEPTH_TEST);
    glDepthMask(GL_FALSE);
    skyShader_.use();
    setSkyUniforms(skyShader_, underwater);
    skyShader_.setVec3("uCamForward", camera.forward);
    skyShader_.setVec3("uCamRight", camera.right);
    skyShader_.setVec3("uCamUp", camera.up);
    skyShader_.setFloat("uTanHalfFov", std::tan(camera.fovY * 0.5f));
    skyShader_.setFloat("uAspect", camera.aspect);
    glBindVertexArray(emptyVao_);
    glDrawArrays(GL_TRIANGLES, 0, 3);
    glDepthMask(GL_TRUE);
}

void Renderer::drawSkyOnly(const Camera& camera) {
    glClear(GL_DEPTH_BUFFER_BIT);
    drawSky(camera, false);
    glBindVertexArray(0);
}

void Renderer::drawOutline(const Camera& camera, const IVec3& block) {
    // A cube has 8 corners; numbering them 0-7 with bits (x=1, y=2, z=4),
    // each edge joins two corners that differ in exactly one bit.
    const float grow = 0.003f;  // slightly bigger than the block so the lines are not hidden in its faces
    Vec3 lines[24];
    int n = 0;
    for (int a = 0; a < 8; ++a) {
        for (int bit = 1; bit <= 4; bit <<= 1) {
            if (a & bit) continue;
            const int b = a | bit;
            for (int corner : {a, b}) {
                lines[n++] = Vec3{block.x + ((corner & 1) ? 1.0f + grow : -grow),
                                  block.y + ((corner & 2) ? 1.0f + grow : -grow),
                                  block.z + ((corner & 4) ? 1.0f + grow : -grow)};
            }
        }
    }

    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    glDepthFunc(GL_LEQUAL);
    lineShader_.use();
    lineShader_.setMat4("uViewProj", camera.viewProjection);
    lineShader_.setVec4("uColor", 0.0f, 0.0f, 0.0f, 0.6f);
    glBindVertexArray(lineVao_);
    glBindBuffer(GL_ARRAY_BUFFER, lineVbo_);
    glBufferSubData(GL_ARRAY_BUFFER, 0, sizeof(lines), lines);
    glDrawArrays(GL_LINES, 0, 24);
    glDepthFunc(GL_LESS);
    glDisable(GL_BLEND);
}

void Renderer::drawWorld(const World& world, const Camera& camera, bool underwater, const RayHit& target,
                         RenderStats& stats) {
    stats = {};
    const float viewBlocks = static_cast<float>(world.viewDistance() * CHUNK_SIZE);
    const float fogStart = underwater ? 0.0f : viewBlocks * 0.5f;
    const float fogEnd = underwater ? 26.0f : viewBlocks - CHUNK_SIZE * 0.75f;

    glClear(GL_DEPTH_BUFFER_BIT);  // no colour clear needed: the sky paints every pixel
    drawSky(camera, underwater);

    // ---- Pass 1: solid blocks (and the see-through pixels of leaves/glass are discarded) ----
    glEnable(GL_DEPTH_TEST);
    glDepthFunc(GL_LESS);
    glEnable(GL_CULL_FACE);  // skip triangles facing away from us (their counter-clockwise side points away)
    glCullFace(GL_BACK);
    glDisable(GL_BLEND);

    blockShader_.use();
    setSkyUniforms(blockShader_, underwater);
    blockShader_.setMat4("uViewProj", camera.viewProjection);
    blockShader_.setVec3("uCameraPos", camera.position);
    blockShader_.setFloat("uFogStart", fogStart);
    blockShader_.setFloat("uFogEnd", fogEnd);
    blockShader_.setFloat("uAlpha", 1.0f);
    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D_ARRAY, blockTextures_);

    const Frustum frustum = Frustum::fromMatrix(camera.viewProjection);
    const int camCx = floorDiv(floorToInt(camera.position.x), CHUNK_SIZE);
    const int camCz = floorDiv(floorToInt(camera.position.z), CHUNK_SIZE);
    const int maxDist = world.viewDistance();
    waterChunks_.clear();

    for (const auto& entry : world.chunks()) {
        const Chunk& chunk = *entry.second;
        if (!chunk.mesh.uploaded()) continue;
        const int dx = chunk.cx - camCx, dz = chunk.cz - camCz;
        if (dx * dx + dz * dz > maxDist * maxDist) continue;  // beyond the fog
        const Vec3 boxMin{static_cast<float>(chunk.cx * CHUNK_SIZE), 0.0f, static_cast<float>(chunk.cz * CHUNK_SIZE)};
        const Vec3 boxMax = boxMin + Vec3{CHUNK_SIZE, CHUNK_HEIGHT, CHUNK_SIZE};
        if (!frustum.boxVisible(boxMin, boxMax)) continue;  // behind us or off to the side

        chunk.mesh.drawOpaque();
        stats.chunksDrawn++;
        stats.quadsDrawn += chunk.mesh.opaqueQuads();
        if (chunk.mesh.waterQuads() > 0) waterChunks_.push_back(&chunk);
    }

    // ---- The black outline around the block under the crosshair ----
    if (target.hit) drawOutline(camera, target.block);

    // ---- Pass 2: water. Blending mixes it with what is already drawn, which
    // only works if things behind it were drawn first: so it comes last, and
    // chunks are sorted far-to-near. Depth writes are off so water never hides
    // other water behind it. Culling is off so the surface shows from below.
    auto distance2 = [&](const Chunk* c) {
        const float x = c->cx * CHUNK_SIZE + 8.0f - camera.position.x;
        const float z = c->cz * CHUNK_SIZE + 8.0f - camera.position.z;
        return x * x + z * z;
    };
    std::sort(waterChunks_.begin(), waterChunks_.end(),
              [&](const Chunk* a, const Chunk* b) { return distance2(a) > distance2(b); });

    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    glDepthMask(GL_FALSE);
    glDisable(GL_CULL_FACE);
    blockShader_.use();
    blockShader_.setFloat("uAlpha", 0.72f);
    for (const Chunk* chunk : waterChunks_) {
        chunk->mesh.drawWater();
        stats.quadsDrawn += chunk->mesh.waterQuads();
    }
    glDepthMask(GL_TRUE);
    glEnable(GL_CULL_FACE);
    glDisable(GL_BLEND);
    glBindVertexArray(0);
}
