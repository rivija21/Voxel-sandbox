// noise.cpp - Ken Perlin's "improved noise" (2002), plus a 2D version.
#include "noise.h"

#include <algorithm>
#include <cmath>
#include <numeric>
#include <random>

namespace {

// 6t^5 - 15t^4 + 10t^3: an S-curve whose slope AND curvature are 0 at t=0 and t=1,
// so neighbouring grid cells join without visible creases.
float fade(float t) { return t * t * t * (t * (t * 6.0f - 15.0f) + 10.0f); }
float lerp(float a, float b, float t) { return a + t * (b - a); }

// Picks one of 8 gradient directions from the hash and dots it with (x, y).
float grad2(int hash, float x, float y) {
    switch (hash & 7) {
        case 0: return x + y;
        case 1: return -x + y;
        case 2: return x - y;
        case 3: return -x - y;
        case 4: return x;
        case 5: return -x;
        case 6: return y;
        default: return -y;
    }
}

// Picks one of 12 edge directions of a cube (16 cases, 4 repeated).
float grad3(int hash, float x, float y, float z) {
    switch (hash & 15) {
        case 0: return x + y;
        case 1: return -x + y;
        case 2: return x - y;
        case 3: return -x - y;
        case 4: return x + z;
        case 5: return -x + z;
        case 6: return x - z;
        case 7: return -x - z;
        case 8: return y + z;
        case 9: return -y + z;
        case 10: return y - z;
        case 11: return -y - z;
        case 12: return y + x;
        case 13: return -y + z;
        case 14: return y - x;
        default: return -y - z;
    }
}

}  // namespace

PerlinNoise::PerlinNoise(uint32_t seed) {
    // Fill 0..255 then shuffle with a seeded generator: a different seed gives a
    // different shuffle, which gives a completely different landscape.
    std::iota(perm_, perm_ + 256, 0);
    std::mt19937 rng(seed);
    std::shuffle(perm_, perm_ + 256, rng);
    std::copy(perm_, perm_ + 256, perm_ + 256);
}

float PerlinNoise::noise2(float x, float y) const {
    // 1. Which grid cell are we in? (& 255 wraps negative numbers correctly too.)
    const int xi = static_cast<int>(std::floor(x)), yi = static_cast<int>(std::floor(y));
    const int X = xi & 255, Y = yi & 255;
    // 2. Where inside the cell (0..1)?
    x -= xi;
    y -= yi;
    const float u = fade(x), v = fade(y);
    // 3. A pseudo-random gradient for each of the 4 corners.
    const int aa = perm_[perm_[X] + Y], ab = perm_[perm_[X] + Y + 1];
    const int ba = perm_[perm_[X + 1] + Y], bb = perm_[perm_[X + 1] + Y + 1];
    // 4. Blend the 4 corner contributions.
    return lerp(lerp(grad2(aa, x, y), grad2(ba, x - 1, y), u),
                lerp(grad2(ab, x, y - 1), grad2(bb, x - 1, y - 1), u), v);
}

float PerlinNoise::noise3(float x, float y, float z) const {
    const int xi = static_cast<int>(std::floor(x));
    const int yi = static_cast<int>(std::floor(y));
    const int zi = static_cast<int>(std::floor(z));
    const int X = xi & 255, Y = yi & 255, Z = zi & 255;
    x -= xi;
    y -= yi;
    z -= zi;
    const float u = fade(x), v = fade(y), w = fade(z);

    const int A = perm_[X] + Y, AA = perm_[A] + Z, AB = perm_[A + 1] + Z;
    const int B = perm_[X + 1] + Y, BA = perm_[B] + Z, BB = perm_[B + 1] + Z;

    // Same idea as 2D, but 8 cube corners blended along x, then y, then z.
    return lerp(lerp(lerp(grad3(perm_[AA], x, y, z), grad3(perm_[BA], x - 1, y, z), u),
                     lerp(grad3(perm_[AB], x, y - 1, z), grad3(perm_[BB], x - 1, y - 1, z), u), v),
                lerp(lerp(grad3(perm_[AA + 1], x, y, z - 1), grad3(perm_[BA + 1], x - 1, y, z - 1), u),
                     lerp(grad3(perm_[AB + 1], x, y - 1, z - 1), grad3(perm_[BB + 1], x - 1, y - 1, z - 1), u),
                     v),
                w);
}

float PerlinNoise::fbm2(float x, float y, int octaves) const {
    float sum = 0.0f, amplitude = 1.0f, frequency = 1.0f, total = 0.0f;
    for (int i = 0; i < octaves; ++i) {
        // The small offset per octave stops every octave being 0 at the same points.
        sum += amplitude * noise2(x * frequency + i * 17.31f, y * frequency + i * 9.17f);
        total += amplitude;
        amplitude *= 0.5f;  // each octave half as strong ("gain")
        frequency *= 2.0f;  // and twice as detailed ("lacunarity")
    }
    return sum / total;
}
