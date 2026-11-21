// noise.h - Perlin "gradient noise": smooth random hills, in 2D and 3D.
//
// Plain random numbers jump about wildly from one block to the next. Perlin
// noise gives values that change SMOOTHLY as you move, which is what natural
// terrain looks like. The same seed always produces the same landscape.
#pragma once

#include <cstdint>

class PerlinNoise {
public:
    explicit PerlinNoise(uint32_t seed = 0);

    // Smooth noise, roughly in the range -1..1. Exactly 0 at whole-number coordinates.
    float noise2(float x, float y) const;
    float noise3(float x, float y, float z) const;

    // "Fractal Brownian motion": several layers (octaves) of noise added together.
    // Each octave is twice as detailed and half as strong as the one before:
    // big hills + medium bumps + small wrinkles. The result is about -1..1.
    float fbm2(float x, float y, int octaves) const;

private:
    uint8_t perm_[512];  // shuffled 0..255, written twice so we never index past the end
};

// Mixes integers into a well-scrambled 32-bit number. Same inputs -> same output.
// Used for "random but repeatable" choices such as where trees grow.
inline uint32_t hash3(int32_t x, int32_t y, uint32_t seed) {
    uint32_t h = seed ^ 0x27d4eb2du;
    h ^= static_cast<uint32_t>(x) * 0x85ebca6bu;
    h = (h << 13) | (h >> 19);
    h ^= static_cast<uint32_t>(y) * 0xc2b2ae35u;
    h ^= h >> 16;
    h *= 0x7feb352du;
    h ^= h >> 15;
    h *= 0x846ca68bu;
    h ^= h >> 16;
    return h;
}
