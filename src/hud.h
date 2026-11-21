// hud.h - The flat 2D layer drawn on top of the 3D world: text, crosshair, hotbar.
//
// Works like a tiny "immediate mode" UI: between begin() and end() you call
// rect(), text(), blockIcon() ...; each call appends triangles to one list,
// and end() sends the whole list to the GPU in a single draw call.
#pragma once

#include "blocks.h"
#include "gl.h"
#include "shader.h"

#include <cstdint>
#include <string>
#include <vector>

struct HudColor {
    uint8_t r, g, b, a;
};

class Hud {
public:
    Hud() = default;
    ~Hud();
    Hud(const Hud&) = delete;
    Hud& operator=(const Hud&) = delete;

    // uiScale: 1.0 at 100% Windows display scaling, 1.5 at 150%, ...
    bool init(float uiScale);

    void begin(int screenWidth, int screenHeight);
    void rect(float x, float y, float w, float h, HudColor color);
    void text(float x, float y, const std::string& s, HudColor color);  // drawn with a drop shadow
    void blockIcon(float centerX, float centerY, float size, BlockId block);
    void end(GLuint blockTextures);

    float textWidth(const std::string& s) const { return static_cast<float>(s.size()) * glyphW_; }
    float lineHeight() const { return glyphH_; }
    float scale() const { return scale_; }

private:
    struct Vertex {
        float x, y;            // screen pixels, (0,0) = top-left
        float u, v, layer;     // layer >= 0: block texture, -1: plain colour, -2: font
        uint8_t r, g, b, a;
    };
    void quad(const float pos[4][2], const float uv[4][2], float layer, HudColor color);
    void glyphs(float x, float y, const std::string& s, HudColor color);
    bool createFontTexture(int pixelHeight);

    Shader shader_;
    GLuint vao_ = 0, vbo_ = 0, fontTexture_ = 0;
    std::vector<Vertex> vertices_;
    int screenW_ = 1, screenH_ = 1;
    float scale_ = 1.0f;
    // Font atlas: 16 columns x 6 rows of equal cells holding ASCII 32..127.
    float glyphW_ = 8.0f, glyphH_ = 16.0f;
    int atlasW_ = 0, atlasH_ = 0;
};
