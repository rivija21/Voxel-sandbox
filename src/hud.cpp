// hud.cpp - 2D overlay drawing, including a font made with Windows GDI.
#include "hud.h"

#include "mathlib.h"

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstring>

namespace {

const char* HUD_VERTEX = R"(#version 330 core
layout(location = 0) in vec2 aPosition;
layout(location = 1) in vec3 aTexCoord;   // u, v, layer
layout(location = 2) in vec4 aColor;      // bytes 0..255, normalised to 0..1 by OpenGL
uniform mat4 uProjection;
out vec3 vTexCoord;
out vec4 vColor;
void main() {
    vTexCoord = aTexCoord;
    vColor = aColor;
    gl_Position = uProjection * vec4(aPosition, 0.0, 1.0);
}
)";

const char* HUD_FRAGMENT = R"(#version 330 core
in vec3 vTexCoord;
in vec4 vColor;
uniform sampler2DArray uBlocks;   // texture unit 0
uniform sampler2D uFont;          // texture unit 1
out vec4 fragColor;
void main() {
    if (vTexCoord.z < -1.5) {            // text: the font texture says how much of the pixel is covered
        fragColor = vec4(vColor.rgb, vColor.a * texture(uFont, vTexCoord.xy).r);
    } else if (vTexCoord.z < -0.5) {     // plain coloured rectangle
        fragColor = vColor;
    } else {                             // a block picture, tinted
        vec4 texel = texture(uBlocks, vTexCoord);
        if (texel.a < 0.5) discard;
        fragColor = texel * vColor;
    }
}
)";

constexpr float LAYER_COLOR = -1.0f;
constexpr float LAYER_FONT = -2.0f;
constexpr int FONT_COLUMNS = 16;
constexpr int FONT_ROWS = 6;  // 16 x 6 = 96 cells for characters 32..127

}  // namespace

Hud::~Hud() {
    if (fontTexture_) glDeleteTextures(1, &fontTexture_);
    if (vbo_) glDeleteBuffers(1, &vbo_);
    if (vao_) glDeleteVertexArrays(1, &vao_);
}

bool Hud::init(float uiScale) {
    scale_ = uiScale;
    if (!shader_.build("hud", HUD_VERTEX, HUD_FRAGMENT)) return false;
    shader_.use();
    shader_.setInt("uBlocks", 0);
    shader_.setInt("uFont", 1);

    if (!createFontTexture(static_cast<int>(std::lround(16.0f * uiScale)))) return false;

    glGenVertexArrays(1, &vao_);
    glGenBuffers(1, &vbo_);
    glBindVertexArray(vao_);
    glBindBuffer(GL_ARRAY_BUFFER, vbo_);
    glEnableVertexAttribArray(0);
    glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, sizeof(Vertex), reinterpret_cast<const void*>(offsetof(Vertex, x)));
    glEnableVertexAttribArray(1);
    glVertexAttribPointer(1, 3, GL_FLOAT, GL_FALSE, sizeof(Vertex), reinterpret_cast<const void*>(offsetof(Vertex, u)));
    glEnableVertexAttribArray(2);
    glVertexAttribPointer(2, 4, GL_UNSIGNED_BYTE, GL_TRUE, sizeof(Vertex), reinterpret_cast<const void*>(offsetof(Vertex, r)));
    glBindVertexArray(0);
    return true;
}

// Draws the printable ASCII characters with a Windows font into an off-screen
// bitmap (GDI), then copies that bitmap into an OpenGL texture.
bool Hud::createFontTexture(int pixelHeight) {
    HDC dc = CreateCompatibleDC(nullptr);
    HFONT font = CreateFontA(-pixelHeight, 0, 0, 0, FW_BOLD, FALSE, FALSE, FALSE, ANSI_CHARSET, OUT_TT_PRECIS,
                             CLIP_DEFAULT_PRECIS, ANTIALIASED_QUALITY, FIXED_PITCH | FF_MODERN, "Consolas");
    HGDIOBJ oldFont = SelectObject(dc, font);

    TEXTMETRICA metrics{};
    GetTextMetricsA(dc, &metrics);
    const int cellW = metrics.tmAveCharWidth;  // a fixed-pitch font: every character is this wide
    const int cellH = metrics.tmHeight;
    atlasW_ = FONT_COLUMNS * cellW;
    atlasH_ = FONT_ROWS * cellH;
    glyphW_ = static_cast<float>(cellW);
    glyphH_ = static_cast<float>(cellH);

    // A 32-bit bitmap whose pixels we can read directly. Negative height = rows stored top to bottom.
    BITMAPINFO info{};
    info.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
    info.bmiHeader.biWidth = atlasW_;
    info.bmiHeader.biHeight = -atlasH_;
    info.bmiHeader.biPlanes = 1;
    info.bmiHeader.biBitCount = 32;
    info.bmiHeader.biCompression = BI_RGB;
    void* bits = nullptr;
    HBITMAP bitmap = CreateDIBSection(dc, &info, DIB_RGB_COLORS, &bits, nullptr, 0);
    if (!bitmap || !bits) {
        SelectObject(dc, oldFont);
        DeleteObject(font);
        DeleteDC(dc);
        return false;
    }
    HGDIOBJ oldBitmap = SelectObject(dc, bitmap);
    std::memset(bits, 0, static_cast<size_t>(atlasW_) * atlasH_ * 4);

    SetTextColor(dc, RGB(255, 255, 255));  // white text on black
    SetBkMode(dc, TRANSPARENT);
    for (int c = 32; c < 128; ++c) {
        const char ch = static_cast<char>(c);
        const int cell = c - 32;
        TextOutA(dc, (cell % FONT_COLUMNS) * cellW, (cell / FONT_COLUMNS) * cellH, &ch, 1);
    }
    GdiFlush();  // make sure GDI has finished drawing before we read the pixels

    // White on black means any one channel is the "coverage" of the pixel.
    std::vector<uint8_t> coverage(static_cast<size_t>(atlasW_) * atlasH_);
    const uint8_t* src = static_cast<const uint8_t*>(bits);
    for (size_t i = 0; i < coverage.size(); ++i) coverage[i] = src[i * 4 + 1];

    glGenTextures(1, &fontTexture_);
    glBindTexture(GL_TEXTURE_2D, fontTexture_);
    glPixelStorei(GL_UNPACK_ALIGNMENT, 1);  // rows are 1-byte aligned, not the default 4
    glTexImage2D(GL_TEXTURE_2D, 0, GL_R8, atlasW_, atlasH_, 0, GL_RED, GL_UNSIGNED_BYTE, coverage.data());
    glPixelStorei(GL_UNPACK_ALIGNMENT, 4);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);

    // GDI objects are not freed automatically: select the originals back, then delete ours.
    SelectObject(dc, oldBitmap);
    SelectObject(dc, oldFont);
    DeleteObject(bitmap);
    DeleteObject(font);
    DeleteDC(dc);
    return true;
}

void Hud::begin(int screenWidth, int screenHeight) {
    screenW_ = screenWidth;
    screenH_ = screenHeight;
    vertices_.clear();
}

void Hud::quad(const float pos[4][2], const float uv[4][2], float layer, HudColor c) {
    // Two triangles: corners (0,1,2) and (0,2,3).
    static const int ORDER[6] = {0, 1, 2, 0, 2, 3};
    for (int i : ORDER) vertices_.push_back({pos[i][0], pos[i][1], uv[i][0], uv[i][1], layer, c.r, c.g, c.b, c.a});
}

void Hud::rect(float x, float y, float w, float h, HudColor color) {
    const float pos[4][2] = {{x, y}, {x + w, y}, {x + w, y + h}, {x, y + h}};
    const float uv[4][2] = {{0, 0}, {0, 0}, {0, 0}, {0, 0}};
    quad(pos, uv, LAYER_COLOR, color);
}

void Hud::glyphs(float x, float y, const std::string& s, HudColor color) {
    // Snap to whole pixels so the 1:1 font texture stays perfectly sharp.
    float penX = std::round(x);
    const float penY = std::round(y);
    for (char ch : s) {
        int c = static_cast<unsigned char>(ch);
        if (c < 32 || c > 127) c = '?';
        const int cell = c - 32;
        const float u0 = (cell % FONT_COLUMNS) * glyphW_ / atlasW_;
        const float v0 = (cell / FONT_COLUMNS) * glyphH_ / atlasH_;
        const float u1 = u0 + glyphW_ / atlasW_;
        const float v1 = v0 + glyphH_ / atlasH_;
        const float pos[4][2] = {{penX, penY}, {penX + glyphW_, penY}, {penX + glyphW_, penY + glyphH_}, {penX, penY + glyphH_}};
        const float uv[4][2] = {{u0, v0}, {u1, v0}, {u1, v1}, {u0, v1}};
        quad(pos, uv, LAYER_FONT, color);
        penX += glyphW_;
    }
}

void Hud::text(float x, float y, const std::string& s, HudColor color) {
    const float shadow = std::max(1.0f, std::round(scale_));
    glyphs(x + shadow, y + shadow, s, {0, 0, 0, static_cast<uint8_t>(color.a * 0.7f)});
    glyphs(x, y, s, color);
}

void Hud::blockIcon(float cx, float cy, float size, BlockId block) {
    // An isometric cube: a hexagon split into a top diamond and two side faces.
    const float r = size * 0.5f;
    const float w = r * 0.866f;  // cos(30 degrees)
    const float top[2] = {cx, cy - r}, bottom[2] = {cx, cy + r}, center[2] = {cx, cy};
    const float upperLeft[2] = {cx - w, cy - r * 0.5f}, upperRight[2] = {cx + w, cy - r * 0.5f};
    const float lowerLeft[2] = {cx - w, cy + r * 0.5f}, lowerRight[2] = {cx + w, cy + r * 0.5f};
    const float uv[4][2] = {{0, 0}, {1, 0}, {1, 1}, {0, 1}};
    const BlockInfo& info = blockInfo(block);

    const float topFace[4][2] = {{top[0], top[1]}, {upperRight[0], upperRight[1]}, {center[0], center[1]}, {upperLeft[0], upperLeft[1]}};
    const float leftFace[4][2] = {{upperLeft[0], upperLeft[1]}, {center[0], center[1]}, {bottom[0], bottom[1]}, {lowerLeft[0], lowerLeft[1]}};
    const float rightFace[4][2] = {{center[0], center[1]}, {upperRight[0], upperRight[1]}, {lowerRight[0], lowerRight[1]}, {bottom[0], bottom[1]}};
    quad(topFace, uv, info.texTop, {255, 255, 255, 255});
    quad(leftFace, uv, info.texSide, {205, 205, 205, 255});   // shaded like the 3D faces
    quad(rightFace, uv, info.texSide, {165, 165, 165, 255});
}

void Hud::end(GLuint blockTextures) {
    if (vertices_.empty()) return;

    glDisable(GL_DEPTH_TEST);
    glDisable(GL_CULL_FACE);
    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);

    shader_.use();
    // y grows DOWN the screen here (top = 0), the usual convention for 2D UI.
    shader_.setMat4("uProjection", ortho(0.0f, static_cast<float>(screenW_), static_cast<float>(screenH_), 0.0f, -1.0f, 1.0f));
    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D_ARRAY, blockTextures);
    glActiveTexture(GL_TEXTURE1);
    glBindTexture(GL_TEXTURE_2D, fontTexture_);

    glBindVertexArray(vao_);
    glBindBuffer(GL_ARRAY_BUFFER, vbo_);
    // GL_STREAM_DRAW: "written once, used once" - the HUD is rebuilt every frame.
    glBufferData(GL_ARRAY_BUFFER, vertices_.size() * sizeof(Vertex), vertices_.data(), GL_STREAM_DRAW);
    glDrawArrays(GL_TRIANGLES, 0, static_cast<GLsizei>(vertices_.size()));

    glBindVertexArray(0);
    glActiveTexture(GL_TEXTURE0);
    glDisable(GL_BLEND);
    glEnable(GL_DEPTH_TEST);
    vertices_.clear();
}
