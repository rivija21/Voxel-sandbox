// dump_textures.cpp - Writes the game's block textures (Tex::Count layers of 16x16 RGBA) to a raw file.
#include "../../src/textures.h"

#include <cstdio>

int main(int argc, char** argv) {
    const std::vector<uint32_t> pixels = generateBlockTextures();
    FILE* f = std::fopen(argc > 1 ? argv[1] : "textures.raw", "wb");
    if (!f) return 1;
    std::fwrite(pixels.data(), sizeof(uint32_t), pixels.size(), f);
    std::fclose(f);
    std::printf("%zu pixels\n", pixels.size());
    return 0;
}
