# 07 - World generation, streaming and saving

Files: `chunk.h`, `noise.h/.cpp`, `terrain.h/.cpp`, `world.h/.cpp`, `savefile.h/.cpp`,
`tests/terrain_map.cpp`.

This chapter explains how an endless landscape comes out of one 32-bit number, how the game
keeps only the nearby part of it in memory, and how your changes survive a restart.

---

## 1. Chunks

The world is cut into **chunks**: columns 16 blocks wide, 16 deep and 128 tall (the full height
of the world).

```cpp
constexpr int CHUNK_SIZE = 16;     // x and z
constexpr int CHUNK_HEIGHT = 128;  // y
constexpr int CHUNK_VOLUME = 16 * 16 * 128;   // 32,768 blocks = 32 KB at 1 byte each
```

**Why chunks at all?** An endless world cannot be held in memory, and a single mesh for
"everything" would have to be rebuilt whenever one block changed. Chunks are the unit of
**loading** (generate what is near), **meshing** (rebuild only the chunk you edited) and
**drawing** (skip the chunks behind you).

**Why 16 x 128 x 16?** 16 is small enough that rebuilding a chunk is quick (0.5 ms) and large
enough that there are not too many draw calls. Making each chunk the full height keeps the code
simple: a chunk is found from (x, z) alone. Big games also split vertically into 16³ sections.

### Memory layout

```cpp
static int index(int x, int y, int z) { return (y * CHUNK_SIZE + z) * CHUNK_SIZE + x; }
```

x changes fastest, then z, then y. So one horizontal layer (256 blocks) is contiguous in memory,
and layer y starts at `y * 256`. The mesher uses this to find the highest non-air layer quickly,
by scanning backwards from the end of the array (`ChunkMesher::build`). It can then skip all the
empty sky above the terrain.

### Three kinds of coordinates

| Name | Type | Example | Meaning |
|---|---|---|---|
| world block coordinates | `int x, y, z` | (-1, 64, 35) | identifies one block anywhere |
| chunk coordinates | `int cx, cz` | (-1, 2) | which chunk: `cx = floorDiv(x, 16)` |
| local coordinates | `int lx, y, lz` | (15, 64, 3) | position inside its chunk: `lx = floorMod(x, 16)` |

Player positions are `float`s; `floorToInt` turns them into block coordinates.

**Negative numbers are the trap.** Block x = -1 is in chunk -1 (which covers -16..-1), at local
x = 15. C++ gives `-1 / 16 = 0` and `-1 % 16 = -1`, which are both wrong here, hence `floorDiv`
and `floorMod` ([03 §3](03-cpp-semantics.md)). The gameplay tests check this case explicitly.

### Finding chunks

`World` stores chunks in `std::unordered_map<uint64_t, std::unique_ptr<Chunk>>`. The key packs
both chunk coordinates into one 64-bit number: `cx` in the upper 32 bits, `cz` in the lower 32.
Looking up any block is "compute the chunk key, find it in the hash table, index the array".

---

## 2. Determinism: the world is a function

The most important design decision: **every block is a pure function of (seed, x, y, z).** No
randomness happens at generation time that is not derived from the seed. Consequences:

- The same seed always gives the same world, on any PC. Try `--seed 2026` twice.
- Chunks the player has not touched never need saving: when you walk back, they are generated
  again, identical. The save file stores **only modified chunks**.
- Neighbouring chunks agree at their borders without talking to each other, because both
  evaluate the same function.
- `tests/terrain_map.exe` can print the map without running the game.

Two tools make "random but repeatable" possible:

1. **Perlin noise** (section 3): smooth values that vary with position.
2. **Hashing** (`hash3` in `noise.h`): scrambles integers (a position and the seed) into a
   number that looks random but is always the same for the same inputs. It is used for "is there
   a tree in this cell?" and "how tall is it?".

```cpp
inline uint32_t hash3(int32_t x, int32_t y, uint32_t seed) {
    uint32_t h = seed ^ 0x27d4eb2du;
    h ^= static_cast<uint32_t>(x) * 0x85ebca6bu;   // multiply by large odd constants...
    h = (h << 13) | (h >> 19);                     // ...rotate...
    h ^= static_cast<uint32_t>(y) * 0xc2b2ae35u;
    h ^= h >> 16;  h *= 0x7feb352du;               // ...and "finalise": every input bit
    h ^= h >> 15;  h *= 0x846ca68bu;               // affects every output bit
    h ^= h >> 16;
    return h;
}
```

---

## 3. Perlin noise (`noise.cpp`)

Plain random numbers per block would make a field of spikes. Terrain needs values that **change
smoothly** with position. Ken Perlin's *gradient noise* (1983, improved in 2002) does this.

### The algorithm (2D)

1. **A grid.** Picture a grid of whole-number points. The input point `(x, y)` lies in some cell
   with corners `(X, Y)`, `(X+1, Y)`, `(X, Y+1)` and `(X+1, Y+1)`.
2. **A random gradient at each corner.** Each corner gets a pseudo-random *direction* (one of 8),
   chosen by looking its coordinates up in a shuffled permutation table:
   `perm_[perm_[X] + Y]`. The **seed** decides the shuffle (`std::shuffle` with `std::mt19937`),
   so a different seed gives a completely different landscape.
3. **Dot products.** For each corner, take the vector from the corner to the point, and dot it
   with that corner's gradient. This gives 4 numbers: each corner's "opinion" of the height here.
4. **Blend** the 4 opinions with bilinear interpolation, using the **fade curve**
   `6t⁵ - 15t⁴ + 10t³` instead of a straight line for the weights. The fade curve has zero
   slope *and* zero curvature at the cell edges, so neighbouring cells join without visible
   creases.

Properties worth knowing:

- The result is roughly in -1..1, and **exactly 0 at every grid point** (the vector from the
  corner is zero there).
- Features are about 1 unit across. To get hills 96 blocks across, sample at `x / 96`.
- 3D noise is the same idea with 8 cube corners and 12 gradient directions. Caves use it.

### Fractal noise (fbm)

One layer of noise gives smooth, blobby hills. Real terrain has detail at every scale.
**Fractal Brownian motion** sums several *octaves*, each twice as detailed and half as strong:

```cpp
for (int i = 0; i < octaves; ++i) {
    sum += amplitude * noise2(x * frequency + i * 17.31f, y * frequency + i * 9.17f);
    total += amplitude;
    amplitude *= 0.5f;   // "gain"
    frequency *= 2.0f;   // "lacunarity"
}
return sum / total;
```

Big hills + medium bumps + small wrinkles. Dividing by `total` keeps the result around -1..1.
The small offset per octave matters: without it, every octave is 0 at the same grid points
(including the world origin), and that makes a visible flat spot.

---

## 4. The height of the ground

`TerrainGenerator::surfaceHeight(x, z)` combines four noise fields, each with its own seed so
they do not line up:

```cpp
continent    = continents_.fbm2(x / 420, z / 420, 5);                // oceans vs land (big, gentle)
hills        = hills_.fbm2(x / 96, z / 96, 4);                        // rolling hills
mountainMask = smoothstep(0.05, 0.35, mountains_.fbm2(x / 300, z / 300, 3));   // where mountains may be
ridge        = 1 - |ridges_.fbm2(x / 140, z / 140, 4)|;  ridge = ridge²;       // sharp crests

height = SEA_LEVEL + 1 + continent * 44 + hills * 9 + mountainMask * ridge * 55;
```

| Term | Effect |
|---|---|
| `continent * 44` | Large areas above or below sea level. A low value means an ocean |
| `hills * 9` | Bumps of a few blocks everywhere |
| `mountainMask` | 0 in most places, 1 in mountain regions; smoothstep makes the edges gradual |
| `ridge²` | **Ridged noise**: `|noise|` has sharp V-shaped valleys where the noise crosses 0, so `1 - |noise|` turns them into sharp *crests*. Squaring makes peaks narrower |

The height is clamped to 4..104, which leaves room above for trees and building.

**Tuning with the map tool.** The first version used `SEA_LEVEL + 4 + continent * 34`. The map
tool showed only **4% water** for seed 12345, so the world was nearly all dry land. Lowering the
base and widening the continent range gives 10% / 28% / 42% water for seeds 12345 / 777 / 2026.
Measuring like this beats guessing:

```text
> tests\terrain_map.exe 2026          (first lines; ~ deep water, - shallow, . beach,
                                        , lowland, n hills, A mountains, * snow)
A*AAAAAAAnn,,,.,..,,.....,,,,,,,,,,,,,,,,.....----..-...----------~~~~~~~~~~~~~~~~~~~~~~-.,,,nnn,
nnnnnAnnn,.-~~----------..,,,,.,,,,,,,,....---------------------~~~~~~~~-~~~~~~~~~~~~~~-,,,nnnnn,
,,,,,,,,.-~~~~~~~---------.,,....,,,.......-----------.,,,.---~~~~~~~~~~~--~--~~~~~~~~--,,,nnnnn,
...
water 42%  snow 1%  height 33..99
```

---

## 5. Filling a column (`TerrainGenerator::generate`)

For each of the 256 columns in the chunk:

```
y = height       surface block:  Sand if beach (height <= sea level + 1)
                                 Snow if height >= SNOW_LINE (90, plus 0-3 random)
                                 Grass otherwise
y = height-1..-3 Dirt (Sand on beaches)
below that       Stone
y = 0            Bedrock (unbreakable floor)
height+1 .. 48   Water (only where the ground is below sea level)
above            Air
```

Then caves are carved, and trees are placed on top.

---

## 6. Caves

Caves use two independent 3D noise fields, `a` and `b`. Each is near zero on a thin, twisting
*sheet* through space. Where **both** are near zero, the two sheets cross in a long winding
**tunnel**:

```cpp
return a * a + b * b < 0.0035f;    // both |a| and |b| roughly < 0.06
```

These are often called "spaghetti caves". Using one field alone (`|a| < 0.06`) would carve
flat, sheet-shaped caverns instead. The noise is stretched less vertically (`y / 18` vs
`x / 30`), so tunnels run more horizontally than vertically.

Caves are **not** carved under the sea or under beaches. Water here does not flow, so a cave
under the sea bed would leave a lake hanging over an air pocket. Where a tunnel reaches the
surface on land, it makes a natural cave entrance.

---

## 7. Trees, and the chunk-border problem

A tree's leaves spread 2 blocks from its trunk. A tree near a chunk's edge therefore puts
leaves into the neighbouring chunk, which may be generated earlier, later, or (after unloading)
twice. How can both chunks agree on the tree?

**Answer: every chunk "draws" every tree that could touch it, and keeps only its own blocks.**

1. Divide the world into **6 x 6 cells**, with at most one tree per cell (so trees never grow
   into each other).
2. For a cell, `hash3(cellX, cellZ, seed)` decides everything: the trunk's position inside the
   cell (1-4, away from the cell edge), whether there is a tree at all (compared with a
   forest-density noise, so forests are dense in places and sparse elsewhere), and its height
   (4-6 logs).
3. When generating a chunk, visit every cell whose tree could reach it: the chunk's area **plus
   2 blocks of margin** on each side.
4. Place each block of each tree with `setIfInside`, which ignores blocks outside this chunk.

The ground height under a neighbouring tree comes from `surfaceHeight(tx, tz)`, a pure function,
so this works even when that neighbour chunk does not exist yet. Each tree is processed in the
same order (cells in increasing z, then x) in every chunk, so overlapping leaves resolve the same
way everywhere. The result is seamless trees at every border, with no communication between
chunks.

Leaves are only placed into air (`onlyIntoAir`), and logs replace leaves, so one tree's leaves
never cut through another tree's trunk.

---

## 8. Spawning

`findSpawn` searches outward from (0, 0) in growing squares, 8 blocks apart, for a column that
is dry (above sea level + 2), not a mountain top (below 80) and not a cave mouth. The player is
placed on top of it. If the spot turns out to be inside a tree trunk, `Game::updateLoading`
moves the player up until they are free.

---

## 9. Streaming: loading and unloading as you move

`World::update(center, budget)` runs every frame.

**1. Unload.** For every loaded chunk, by distance from the player's chunk:

| Distance (chunks) | What happens |
|---|---|
| ≤ view + 1 | kept, with its mesh |
| > view + 1 | GPU mesh released (`dirty = true`, so it is rebuilt if you come back) |
| > view + 3 | **deleted**, *unless* `modified` (player changes must not be lost) |

The gap between the "load" and "unload" distances is *hysteresis*. Without it, walking back and
forth across a chunk border would load and unload the same chunks over and over.

**2. Load and mesh, nearest first.** `offsets_` is every (dx, dz) within the view distance,
sorted by distance once in the constructor. Each frame walks that list:

```cpp
for (const Offset& off : offsets_) {
    if (chunk exists && !chunk->dirty) continue;           // up to date: skip (very fast)
    for each of the 3x3 neighbourhood: generate if missing  // the mesher needs neighbours
    buildMesh(chunk);
    if (outOfTime()) return;                               // continue next frame
}
```

**The time budget** is the key to smoothness. Generating takes about 1.2 ms per chunk and meshing
about 0.5 ms. Building a whole view at once (about 270 chunks to generate and 200 to mesh) would
freeze the game for nearly half a second. Instead `update` stops after **5 ms** of work (30 ms on the loading screen)
and continues next frame. You see the world fill in from the centre outwards, and the frame rate
stays steady.

**Why chunks need their neighbours before meshing:** whether a face on a chunk's edge is visible
depends on the block *across the border*, and so does ambient occlusion
([08](08-meshing-and-lighting.md)). So the 3x3 neighbourhood is generated first. That is why
chunks exist one ring further out (view + 1) than the ones being drawn.

**The loading screen** (`Game::updateLoading`) asks `pendingChunks(position, 3)`, "how many
chunks within 3 still need work?", and starts the game when it reaches 0. The progress bar is
`1 - pending / total`.

---

## 10. Editing: `World::setBlock`

```cpp
chunk->set(lx, y, lz, id);
chunk->modified = true;   // must be saved
chunk->dirty = true;      // must be re-meshed
// on an edge? the neighbour's mesh depends on this block too:
if (lx == 0)  markDirty(cx - 1, cz);   ... (and +1, and z, and the diagonal at corners)
```

The diagonal neighbour matters only for ambient occlusion, which looks at diagonal blocks.
Forget it, and breaking a corner block leaves a dark shadow in the next chunk until something
else rebuilds it.

---

## 11. The save file (`savefile.cpp`)

**Where:** `%LOCALAPPDATA%\VoxelSandbox\world.dat`. It is written when you quit and when you
press F5.

**Format** (all values little-endian, the native byte order of x86 CPUs):

| Offset | Size | Field |
|---|---|---|
| 0 | 4 | magic `"VOXL"`: identifies the file type |
| 4 | 4 | format version (1) |
| 8 | 4 | seed |
| 12 | 12 | player position x, y, z (3 floats) |
| 24 | 8 | yaw, pitch |
| 32 | 1 | flying (0/1) |
| 33 | 9 | hotbar block ids |
| 42 | 4 | N = number of chunks that follow |
| 46 | N x 32,776 | each: cx (int32), cz (int32), then 32,768 block bytes |

A fresh world with no edits is just 46 bytes. Every chunk you change adds 32 KB (exercise 10.7
compresses it).

**Safe saving.** The file is written to `world.dat.tmp` first and then renamed over the old
`world.dat`. A crash or power cut in the middle of saving leaves the previous save intact,
never a half-written one.

**Never trust a file.** `loadWorld` checks the magic and version, sanity-limits N, and rejects any
block byte `>= Block::Count`. Without that last check, a corrupted byte (say 200) would later be
used as `BLOCKS[200]`, reading past the end of a 14-row table: undefined behaviour. The test suite
corrupts a byte on purpose to check that the file is rejected.

**Versioning.** If you change the format (add a field, compress chunks), bump `VERSION`. Old
files are then rejected cleanly instead of being misread. A kinder approach is to keep reading
version 1 and convert it.

**New worlds keep a backup.** Starting with `--new` or `--seed` when a save exists first copies
it to `world-backup.dat`, because quitting would otherwise overwrite it.

---

## Try it

- `tests\terrain_map.exe 1` up to `tests\terrain_map.exe 10`: which seed has the most mountains?
- Double `CHUNK_HEIGHT` to 256 and raise the mountains (`* 55` → `* 120`). What else had to
  change? (Hint: memory per chunk, the clamp in `surfaceHeight`.)
- Make caves bigger (`0.0035f` → `0.01f`) and walk underground with `--debug-info`.
- Add a desert: where a new "temperature" noise is high, use Sand instead of Grass and Dirt, and
  skip trees.
