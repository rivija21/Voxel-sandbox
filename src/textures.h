// textures.h - Pixel-art block textures, painted by code at start-up.
//
// No image files are needed: each 16x16 picture is drawn with a little random
// noise, so the game is a single .exe with nothing else to ship.
#pragma once

#include <cstdint>
#include <vector>

constexpr int TEXTURE_SIZE = 16;  // pixels per side of one block texture

// Returns Tex::Count pictures, one after another. Each is TEXTURE_SIZE rows of
// TEXTURE_SIZE pixels; row 0 is the TOP of the picture. One uint32_t per pixel,
// laid out in memory as the bytes R, G, B, A (what OpenGL's GL_RGBA expects).
std::vector<uint32_t> generateBlockTextures();
