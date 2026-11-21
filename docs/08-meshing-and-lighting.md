# 08 - Meshing, lighting and textures

Files: `mesher.h/.cpp`, `gpu_mesh.h/.cpp`, `textures.h/.cpp`, `blocks.h/.cpp`, `hud.cpp`
(icons).

The GPU draws triangles, not blocks. **Meshing** is how 32,768 bytes of block ids become a few
thousand triangles. On the way, the mesher also bakes in the shading that makes a blocky world
readable.

---

## 1. Why not draw every cube?

A chunk has 32,768 blocks x 6 faces = **196,608** possible faces. Almost all of them are
pressed against another solid block, where nobody can ever see them. Measured over 20 real
chunks (seed 2026), the mesher emits on average **1,162 solid faces + 96 water faces**, about
**0.6%** of the naive count. Meshing a chunk takes about **0.5 ms**.

The rule is simple: **emit a face only if the block on the other side of it lets you see
through.**

---

## 2. Looking across chunk borders: the padded copy

The faces on a chunk's edge depend on the neighbouring chunk. Asking the `World` for every
neighbour block (hash lookup, bounds checks) would be slow and full of special cases. Instead,
`ChunkMesher::copyNeighbourhood` copies the chunk **plus a 1-block border** from its 8
neighbours into an 18 x 130 x 18 array:

```
   z=-1  ┌──┬────────────────┬──┐   corners come from the 4 diagonal neighbours,
         │NW│     north      │NE│   edges from the 4 side neighbours,
   z=0   ├──┼────────────────┼──┤   the middle from the chunk itself.
         │  │                │  │
         │W │   this chunk   │E │   y = -1 is filled with Bedrock (solid: no bottom faces),
         │  │    16 x 16     │  │   y = 128 with Air (open sky).
   z=15  ├──┼────────────────┼──┤
         │SW│     south      │SE│
   z=16  └──┴────────────────┴──┘
        x=-1  x=0 ...... x=15  x=16
```

After that, `at(x + 1, y, z)` works for every block, including x = 15. It costs about 41,000
byte copies per chunk, which is far cheaper than the special cases it removes.

---

## 3. When is a face visible?

```cpp
bool faceVisible(BlockId self, BlockId neighbour) {
    if (neighbour == Block::Air) return true;
    if (isOpaque(neighbour)) return false;           // hidden behind a solid block
    if (neighbour == self) return self == Block::Leaves;
    return true;
}
```

| This block | Neighbour | Face drawn? | Why |
|---|---|---|---|
| Stone | Air | yes | the usual case |
| Stone | Dirt | no | both opaque: the face is buried |
| Sand | Water | yes | you can see the sea floor through water |
| Water | Water | no | inside a lake there is no surface |
| Water | Air | yes | the lake's surface |
| Glass | Glass | no | a wall of glass looks like one pane, not a grid |
| Leaves | Leaves | **yes** | the inner faces make trees look bushy through the holes |
| Water | Glass | yes | an aquarium |

`opaque` is a column in the `BLOCKS` table (`blocks.cpp`). Water, glass and leaves are not
opaque, everything else is.

---

## 4. The face table and winding order

```cpp
// mesher.cpp: direction, 4 corners (0/1 offsets), brightness
{{0, 1, 0}, {{0, 1, 1}, {1, 1, 1}, {1, 1, 0}, {0, 1, 0}}, 1.00f},  // top (+Y)
```

Each face lists its 4 corners **counter-clockwise as seen from outside the block**. OpenGL uses
this order to tell front from back: face culling throws away triangles that appear clockwise on
screen, which are the ones facing away ([05 §9](05-opengl-pipeline.md)).

**Checking a face's winding with the cross product.** For corners v0, v1, v2, the vector
`cross(v1 - v0, v2 - v0)` must point *outward* (along the face normal). Top face:

```
v0 = (0,1,1), v1 = (1,1,1), v2 = (1,1,0)
v1 - v0 = (1, 0, 0)
v2 - v0 = (1, 0, -1)
cross   = (0*(-1) - 0*0,  0*1 - 1*(-1),  1*0 - 0*1) = (0, 1, 0)   ✓ points up, out of the top
```

All six faces in the table were derived and checked this way. If you add a new shape (a slab,
stairs), check each face's winding like this. A wrong one is invisible from outside.

**Texture coordinates.** Each face gives its corners the UVs bottom-left (0,1), bottom-right
(1,1), top-right (1,0), top-left (0,0). Texture row 0 is the **top** of the picture, so side faces
show grass hanging *down* from the top edge, not growing up from the bottom.

**Which picture?** `BLOCKS[id]` has three texture layers: `texTop`, `texSide` and `texBottom`.
Grass uses grass-top / grass-side / dirt, and logs use log-top (rings) on top and bottom with bark
on the sides.

---

## 5. Lighting, part 1: one brightness per direction

Real lighting (shadows, light bouncing off surfaces) is expensive. This game uses two cheap
tricks that together make shapes easy to read.

The first is **a fixed brightness for each face direction**, as if the sun were above and
slightly to one side:

| Face | Brightness |
|---|---|
| top | 1.00 |
| east / west | 0.80 |
| north / south | 0.66 |
| bottom | 0.52 |

Without it, two walls meeting at a corner would be exactly the same colour, and the corner would
vanish. Because the values are fixed, no normal vectors or light calculations are needed in the
shader: the brightness is baked into each vertex's `shade` byte.

---

## 6. Lighting, part 2: ambient occlusion

Corners and creases receive less light from the sky because the surrounding blocks get in the
way. Darkening them is called **ambient occlusion (AO)**, and it is what makes block worlds look
solid instead of flat. This project uses the per-vertex method described by Mikola Lysenko
(0fps.net, 2013).

**For each corner of each face**, look at the 3 blocks that touch that corner in the layer just
in front of the face:

```
        side1   corner                Top face of block B, seen from above.
       ┌──────┬──────┐                For the corner marked *, look at the three
       │  S1  │  C   │                blocks one layer ABOVE the face:
       ├──────*──────┤                  S1, S2 = the two that share an edge with *
       │  B   │  S2  │                  C      = the diagonal one
       └──────┴──────┘
```

```cpp
ao = (side1 && side2) ? 0 : 3 - (side1 + side2 + diag);
```

- 3 = nothing nearby: full brightness.
- 2 or 1 = partly enclosed.
- 0 = **both sides solid**: the corner is boxed in. The diagonal does not matter then, since it
  is hidden anyway.

The level becomes a brightness through `AO_CURVE = {0.38, 0.58, 0.78, 1.0}` and is multiplied
with the face brightness into the vertex's `shade` byte. The GPU **interpolates** shade across
the face, so a dark corner fades smoothly into a bright one. Four numbers per face give soft
shadows in every crease.

**How the code finds those 3 blocks for any face.** A face's normal points along one axis; the
other two axes (`axisA`, `axisB`) span the face. For a corner with offsets `c` (each 0 or 1),
`c * 2 - 1` gives -1 or +1, the direction from the face's centre toward that corner along each
axis. From the block in front of the face, step that way along A (side1), along B (side2), and
along both (diagonal). One formula handles all 6 faces and 4 corners, with no 24-case table.

### The diagonal flip

A quad is drawn as two triangles, split along a diagonal. Brightness is interpolated *within each
triangle*, so the choice of diagonal changes the result:

```
 one dark corner (●), three bright (○)

 split along ●─○ (0-2)             split along ○─○ (1-3)
 ○───────○                          ○───────○
 │     ╱ │   the dark corner is    │ ╲     │   the dark corner stays
 │   ╱   │   shared by BOTH        │   ╲   │   in ONE triangle: a soft
 │ ╱     │   triangles: a dark     │     ╲ │   rounded shadow
 ●───────○   streak along the      ●───────○
             diagonal
```

The rule: **split along the diagonal whose two corners' AO values add up to more.** The shared
index buffer always splits along corners 0-2, so to split along 1-3 the mesher *rotates* the
corner order by one (`v1, v2, v3, v0`), which keeps the winding counter-clockwise.

> **Debugging story.** In the first test screenshots AO seemed missing. Instead of guessing,
> a 40-line throwaway test program meshed a floor with a wall on it (no window needed) and
> printed the corner shades: 166 instead of 255 next to the wall, so the maths was right. A
> 2x zoomed crop of the screenshot then showed the shading *was* there, just hidden by the busy
> grass texture. The fix was a stronger curve (0.45 → 0.38 for the darkest level), not a code
> change. Lesson: **measure before you change code.**

---

## 7. Water, glass and leaves

- **Water** faces go into a separate list, which is appended *after* the solid faces in the same
  buffer. `GpuMesh` records both counts, so the renderer draws them in different passes
  ([05 §10](05-opengl-pipeline.md)). Water's top surface is lowered to 0.875 when there is no
  water above it, which looks like a liquid surface instead of a solid cube. Water gets no AO.
- **Glass and leaves** are drawn with the solid pass. Their textures have fully transparent
  pixels (alpha 0), which the fragment shader `discard`s. Glass is a frame with two shine marks;
  leaves are about 18% holes.

---

## 8. Textures painted by code (`textures.cpp`)

There are no image files: each 16 x 16 picture is generated at start-up by a small "painter"
function. All of them use a seeded random generator (`Rng`, xorshift32), so the pictures are
identical every run.

| Technique | Textures | How |
|---|---|---|
| **Noisy fill** | grass, dirt, stone, sand, snow | base colour x random 0.9-1.1 per pixel (`paintNoisy`) |
| **Speckles** | most | sprinkle a few darker and lighter pixels (`speckle`) |
| **Layering** | grass side | start from the dirt picture, paint 2-4 green pixels down from the top of each column |
| **Stripes** | bark, planks | per-column / per-board brightness, darker grooves and seams |
| **Rings** | log top | `distance from centre` decides light/dark ring; a square rim of bark |
| **Bricks** | brick | rows of height 4; every other row offset by half a brick; mortar where `y % 4 == 3` or `(x + offset) % 8 == 7` |
| **Voronoi cells** | cobblestone | 9 random centres; each pixel belongs to its nearest; pixels nearly equidistant from two centres become dark gaps |
| **Sine waves** | water | two waves with *whole* periods across 16 pixels |
| **Holes** | leaves, glass | alpha = 0 pixels |

**Seamless tiling.** Neighbouring blocks show the same picture side by side, so a texture's
left edge must continue its right edge. Random-per-pixel noise tiles automatically. Patterns need
care:

- Water uses `sin(x * 2π/16)`, a whole number of waves across the tile, so the end matches the
  start.
- Cobblestone measures distance **with wrap-around**: `dx = min(dx, 16 - dx)`. A stone near the
  right edge continues on the left, as if the tile were wrapped around a cylinder.

Each pixel is packed as `R | G<<8 | B<<16 | A<<24`. On a little-endian CPU that puts the bytes in
memory as R, G, B, A, which is exactly what `GL_RGBA` + `GL_UNSIGNED_BYTE` expects.

**Hotbar icons** (`Hud::blockIcon`) reuse the same texture array: an isometric cube is a hexagon
made of three quads (top, left, right), shaded 100% / 80% / 65% like the 3D faces.

---

## 9. Where to go from here

The mesher is deliberately simple. The well-known next steps (all in [10](10-exercises.md)):

- **Greedy meshing.** Merge neighbouring identical faces into large rectangles: a flat 16 x 16
  grass field becomes 1 quad instead of 256. It usually cuts quads by 3-10x, but AO and texture
  tiling get harder.
- **Real light.** Torches and dark caves need a light level per block, spread outward with a
  breadth-first search (flood fill), then sampled per vertex like AO.
- **Meshing on a worker thread.** Build the vertex list on another CPU core and only upload on the
  main thread. OpenGL calls must stay on the thread that owns the context.
