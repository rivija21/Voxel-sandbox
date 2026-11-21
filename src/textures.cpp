// textures.cpp - Paints the block textures.
#include "textures.h"

#include "blocks.h"
#include "mathlib.h"

#include <algorithm>
#include <cmath>

// Everything in an unnamed namespace is private to this .cpp file.
namespace {

constexpr int N = TEXTURE_SIZE;

// Tiny deterministic random number generator ("xorshift32").
// The same seed always gives the same sequence, so textures never change between runs.
struct Rng {
    uint32_t state;
    explicit Rng(uint32_t seed) : state(seed ? seed : 1u) {}
    uint32_t next() {
        state ^= state << 13;
        state ^= state >> 17;
        state ^= state << 5;
        return state;
    }
    float uniform() { return static_cast<float>(next() & 0xFFFFFF) / 16777216.0f; }  // [0, 1)
    float range(float lo, float hi) { return lo + (hi - lo) * uniform(); }
    int below(int n) { return static_cast<int>(next() % static_cast<uint32_t>(n)); }
};

struct Color {
    float r, g, b;
    float a = 255;
};

Color shade(Color c, float f) { return {c.r * f, c.g * f, c.b * f, c.a}; }
Color mix(Color a, Color b, float t) {
    return {lerpf(a.r, b.r, t), lerpf(a.g, b.g, t), lerpf(a.b, b.b, t), lerpf(a.a, b.a, t)};
}

uint32_t pack(Color c) {
    auto byte = [](float v) {
        return static_cast<uint32_t>(std::lround(std::clamp(v, 0.0f, 255.0f)));
    };
    // Little-endian: the lowest byte is stored first, so memory reads R,G,B,A.
    return byte(c.r) | (byte(c.g) << 8) | (byte(c.b) << 16) | (byte(c.a) << 24);
}

// One picture being painted. px[y][x], y = 0 is the top row.
struct Tile {
    Color px[N][N];
};

// Fills the tile with `base`, each pixel randomly a bit lighter or darker.
void paintNoisy(Tile& t, Color base, float variation, Rng& rng) {
    for (int y = 0; y < N; ++y)
        for (int x = 0; x < N; ++x) t.px[y][x] = shade(base, 1.0f + rng.range(-variation, variation));
}

// Sprinkles `count` single pixels of colour c (with a little variation).
void speckle(Tile& t, Color c, int count, Rng& rng) {
    for (int i = 0; i < count; ++i) t.px[rng.below(N)][rng.below(N)] = shade(c, rng.range(0.92f, 1.08f));
}

const Color GRASS_GREEN = {96, 160, 56};
const Color DIRT_BROWN = {134, 96, 67};
const Color BARK_BROWN = {104, 82, 52};

Tile grassTop(Rng& rng) {
    Tile t;
    paintNoisy(t, GRASS_GREEN, 0.10f, rng);
    speckle(t, {74, 132, 42}, 45, rng);   // darker blades
    speckle(t, {124, 186, 72}, 16, rng);  // lighter blades
    return t;
}

Tile dirt(Rng& rng) {
    Tile t;
    paintNoisy(t, DIRT_BROWN, 0.09f, rng);
    speckle(t, {104, 73, 50}, 32, rng);
    speckle(t, {162, 124, 90}, 12, rng);
    return t;
}

Tile grassSide(Rng& rng) {
    Tile t = dirt(rng);
    for (int x = 0; x < N; ++x) {
        const int depth = 2 + rng.below(3);  // grass hangs 2-4 pixels down the side
        for (int y = 0; y < depth; ++y) t.px[y][x] = shade(GRASS_GREEN, rng.range(0.85f, 1.12f));
        if (rng.below(3) == 0) t.px[depth][x] = shade({74, 132, 42}, 0.95f);
    }
    return t;
}

Tile stone(Rng& rng) {
    Tile t;
    paintNoisy(t, {128, 128, 128}, 0.06f, rng);
    for (int i = 0; i < 10; ++i) {  // short dark cracks
        const int x = rng.below(N), y = rng.below(N), len = 2 + rng.below(3);
        for (int k = 0; k < len; ++k) t.px[y][(x + k) % N] = shade({100, 100, 100}, rng.range(0.92f, 1.05f));
    }
    speckle(t, {152, 152, 152}, 10, rng);
    return t;
}

Tile sand(Rng& rng) {
    Tile t;
    paintNoisy(t, {219, 207, 163}, 0.04f, rng);
    speckle(t, {194, 180, 136}, 28, rng);
    speckle(t, {236, 228, 190}, 10, rng);
    return t;
}

Tile water(Rng& rng) {
    Tile t;
    const float k = 2.0f * PI / N;  // whole sine periods across the tile, so it tiles seamlessly
    for (int y = 0; y < N; ++y)
        for (int x = 0; x < N; ++x) {
            const float w = 0.6f * std::sin((x + y) * k) + 0.4f * std::sin((x - 2 * y) * 2.0f * k);
            const Color c = mix({38, 84, 188}, {74, 128, 226}, 0.5f + 0.5f * w);
            t.px[y][x] = shade(c, 1.0f + rng.range(-0.03f, 0.03f));
        }
    return t;
}

Tile logSide(Rng& rng) {
    Tile t;
    for (int x = 0; x < N; ++x) {
        const float column = rng.range(0.88f, 1.08f);
        const bool groove = (x % 4 == 1);
        for (int y = 0; y < N; ++y) {
            float f = column * (1.0f + rng.range(-0.06f, 0.06f));
            if (groove) f *= 0.72f;
            t.px[y][x] = shade(BARK_BROWN, f);
        }
    }
    return t;
}

Tile logTop(Rng& rng) {
    Tile t;
    for (int y = 0; y < N; ++y)
        for (int x = 0; x < N; ++x) {
            const float dx = x - 7.5f, dy = y - 7.5f;
            const bool bark = std::max(std::fabs(dx), std::fabs(dy)) > 6.6f;  // square outer rim
            const int ring = static_cast<int>(std::sqrt(dx * dx + dy * dy) * 0.9f) % 2;
            const Color c = bark ? BARK_BROWN : (ring ? Color{176, 142, 92} : Color{152, 120, 76});
            t.px[y][x] = shade(c, 1.0f + rng.range(-0.04f, 0.04f));
        }
    return t;
}

Tile leaves(Rng& rng) {
    Tile t;
    for (int y = 0; y < N; ++y)
        for (int x = 0; x < N; ++x) {
            if (rng.uniform() < 0.18f)
                t.px[y][x] = {0, 0, 0, 0};  // a hole: alpha 0, the shader discards it
            else
                t.px[y][x] = shade({58, 128, 40}, rng.range(0.72f, 1.15f));
        }
    return t;
}

Tile planks(Rng& rng) {
    Tile t;
    for (int y = 0; y < N; ++y) {
        const int board = y / 4;
        const bool seamRow = (y % 4 == 3);
        Rng boardRng(1000 + board);  // same random brightness for a whole board
        const float boardShade = boardRng.range(0.9f, 1.06f);
        const int seamX = (board * 5 + 3) % N;
        for (int x = 0; x < N; ++x) {
            float f = boardShade * (1.0f + rng.range(-0.05f, 0.05f));
            if (seamRow) f *= 0.66f;
            else if (x == seamX) f *= 0.72f;
            t.px[y][x] = shade({176, 138, 86}, f);
        }
    }
    return t;
}

Tile glass(Rng&) {
    Tile t;
    for (int y = 0; y < N; ++y)
        for (int x = 0; x < N; ++x) {
            const bool edge = (x == 0 || y == 0 || x == N - 1 || y == N - 1);
            t.px[y][x] = edge ? Color{205, 232, 240} : Color{0, 0, 0, 0};
        }
    // Two little diagonal shines.
    for (int k = 0; k < 4; ++k) t.px[3 + k][6 - k] = {236, 248, 255};
    for (int k = 0; k < 3; ++k) t.px[10 + k][12 - k] = {236, 248, 255};
    return t;
}

Tile brick(Rng& rng) {
    Tile t;
    for (int y = 0; y < N; ++y) {
        const int row = y / 4;
        const int offset = (row % 2) * 4;  // every other row is shifted half a brick
        for (int x = 0; x < N; ++x) {
            const bool mortar = (y % 4 == 3) || ((x + offset) % 8 == 7);
            if (mortar) {
                t.px[y][x] = shade({182, 175, 165}, 1.0f + rng.range(-0.04f, 0.04f));
            } else {
                Rng brickRng(77 + row * 31 + (x + offset) / 8);
                const float f = brickRng.range(0.86f, 1.08f) * (1.0f + rng.range(-0.06f, 0.06f));
                t.px[y][x] = shade({150, 72, 56}, f);
            }
        }
    }
    return t;
}

Tile cobble(Rng& rng) {
    // A "Voronoi" pattern: random stone centres, each pixel belongs to the nearest
    // one, and pixels nearly equidistant from two centres become the dark gaps.
    constexpr int STONES = 9;
    float cx[STONES], cy[STONES], tone[STONES];
    for (int i = 0; i < STONES; ++i) {
        cx[i] = rng.range(0, N);
        cy[i] = rng.range(0, N);
        tone[i] = rng.range(0.85f, 1.12f);
    }
    Tile t;
    for (int y = 0; y < N; ++y)
        for (int x = 0; x < N; ++x) {
            float d1 = 1e9f, d2 = 1e9f;
            int nearest = 0;
            for (int i = 0; i < STONES; ++i) {
                // Wrap-around distance, so the texture tiles without seams.
                float dx = std::fabs(x + 0.5f - cx[i]), dy = std::fabs(y + 0.5f - cy[i]);
                dx = std::min(dx, N - dx);
                dy = std::min(dy, N - dy);
                const float d = std::sqrt(dx * dx + dy * dy);
                if (d < d1) { d2 = d1; d1 = d; nearest = i; }
                else if (d < d2) { d2 = d; }
            }
            if (d2 - d1 < 1.1f)
                t.px[y][x] = shade({78, 78, 78}, 1.0f + rng.range(-0.05f, 0.05f));
            else
                t.px[y][x] = shade({132, 132, 132}, tone[nearest] * (1.0f + rng.range(-0.05f, 0.05f)));
        }
    return t;
}

Tile snow(Rng& rng) {
    Tile t;
    paintNoisy(t, {242, 246, 250}, 0.025f, rng);
    speckle(t, {220, 228, 240}, 14, rng);
    return t;
}

Tile bedrock(Rng& rng) {
    Tile t;
    const Color tones[3] = {{42, 42, 42}, {74, 74, 74}, {104, 104, 104}};
    for (int y = 0; y < N; ++y)
        for (int x = 0; x < N; ++x) t.px[y][x] = tones[rng.below(3)];
    return t;
}

// A table of function pointers, in the same order as the Tex:: enum.
using Painter = Tile (*)(Rng&);
const Painter PAINTERS[] = {
    grassTop, grassSide, dirt, stone, sand, water, logSide, logTop,
    leaves, planks, glass, brick, cobble, snow, bedrock,
};
static_assert(sizeof(PAINTERS) / sizeof(PAINTERS[0]) == Tex::Count,
              "PAINTERS needs one entry per Tex:: layer");

}  // namespace

std::vector<uint32_t> generateBlockTextures() {
    std::vector<uint32_t> pixels(static_cast<size_t>(Tex::Count) * N * N);
    for (int layer = 0; layer < Tex::Count; ++layer) {
        Rng rng(0x9E3779B9u * (layer + 1));  // each layer gets its own random stream
        const Tile tile = PAINTERS[layer](rng);
        uint32_t* out = &pixels[static_cast<size_t>(layer) * N * N];
        for (int y = 0; y < N; ++y)
            for (int x = 0; x < N; ++x) out[y * N + x] = pack(tile.px[y][x]);
    }
    return pixels;
}
