# 02 - Architecture

This chapter covers how the 17 source files fit together: who owns what, what happens during one
frame, and how a block travels from a random seed to pixels on the screen.

---

## 1. The big picture

```
                         main.cpp
                 command line + game loop
                            │ owns
              ┌─────────────┴─────────────┐
           Window                        Game
   (Win32 window, input,       (rules, HUD layout, saving,
    the OpenGL context)          screenshots, block editing)
                                         │ owns
        ┌──────────────┬─────────────────┼──────────────┬──────────────┐
      World          Player           Renderer          Hud         RayHit
  (all chunks,     (physics)       (3D passes)      (2D overlay)   (crosshair target)
   streaming)          │                │                │
   │      │            │ reads      Shader x3        Shader, font texture
Terrain  ChunkMesher   │ blocks     texture array    (made with GDI)
 │         │           ▼            (textures.cpp)
Perlin   Chunk ──► GpuMesh (VAO + VBO on the GPU)
noise    (blocks)
```

The files fall into four layers. Each layer only uses the layers **below** it:

| Layer | Files | Knows about |
|---|---|---|
| **Gameplay** | `game`, `player`, `raycast`, `savefile`, `main` | everything below |
| **World data** | `world`, `chunk`, `terrain`, `noise`, `mesher`, `blocks` | maths, and the GPU mesh class |
| **Graphics engine** | `renderer`, `hud`, `shader`, `gpu_mesh`, `textures` | OpenGL, maths, block table |
| **Platform** | `window`, `gl`, `mathlib` | Windows and OpenGL only |

Keeping dependencies one-way is what made the tests possible. `gameplay_tests.cpp` compiles
`player.cpp`, `world.cpp` and `savefile.cpp` without the window or renderer.

**One honest compromise:** a `Chunk` holds its own `GpuMesh`, so the world layer touches
OpenGL. A stricter design keeps meshes in a map inside the renderer, keyed by chunk position.
Putting the mesh in the chunk keeps the code shorter, and the tests still work because a
`GpuMesh` that was never uploaded makes no OpenGL calls.

---

## 2. Who owns what

In C++, the **owner** of something is responsible for destroying it. This project uses three
ownership tools (explained in [03](03-cpp-semantics.md)):

| Object | Owner | How |
|---|---|---|
| `Window` | `main()` | local variable: destroyed when `main` returns |
| `Game` | `main()` | local variable inside `{ }`: destroyed *before* the window |
| `World` | `Game` | `std::unique_ptr<World>`, created in `Game::init` once the seed is known |
| `Player`, `Renderer`, `Hud` | `Game` | plain members |
| each `Chunk` | `World` | `std::unordered_map<uint64_t, std::unique_ptr<Chunk>>` |
| each chunk's VAO/VBO | its `GpuMesh` | destructor calls `glDeleteBuffers` / `glDeleteVertexArrays` |
| shaders, texture array | `Renderer` (via `Shader` members) | destructors |
| the shared quad index buffer | module-level variable in `gpu_mesh.cpp` | freed by `releaseSharedQuadIndices()` in `~Game` |
| the OpenGL context | `Window` | `wglDeleteContext` in `~Window` |
| `Window&` inside `Game` | nobody: it is a **reference** | `Game` uses the window but must not destroy it |

**Why the order matters.** Every OpenGL object (buffer, texture, shader) belongs to the OpenGL
*context*, and the context belongs to the window. `glDeleteBuffers` called after the context is
gone would crash. So `main.cpp` puts `Game` in an inner `{ }` block:

```cpp
Window window;                 // 1. created first
{
    Game game(window, options);// 2. created second
    ...
}                              // 3. game destroyed here: frees chunks, shaders, textures
return 0;                      // 4. window destroyed here: frees the context
```

C++ always destroys local objects in **reverse order of creation**. That rule, plus the braces,
guarantees the right order without any manual cleanup calls.

---

## 3. One frame, step by step

The game loop in `main.cpp` runs about 60 times a second:

```
main loop ─┬─ Window::processMessages()      read keyboard/mouse messages from Windows
           │
           ├─ Game::frame(dt)
           │    ├─ fps counter, message timers
           │    ├─ updateLoading()           (only at start-up: build the world, show progress)
           │    │       or
           │    ├─ updatePlaying(dt)
           │    │    ├─ F-keys, Esc, click-to-resume
           │    │    ├─ Player::turn()        mouse movement -> yaw / pitch
           │    │    ├─ Player::update()      keys -> velocity -> collide -> new position
           │    │    ├─ World::update()       unload far chunks, generate + mesh near ones (5 ms budget)
           │    │    ├─ raycastBlocks()       which block is under the crosshair?
           │    │    └─ handleBlockActions()  break / place / pick
           │    └─ render()
           │         ├─ makeCamera()          view and projection matrices
           │         ├─ Renderer::drawWorld()
           │         │    ├─ sky              one full-screen triangle
           │         │    ├─ opaque pass      every visible chunk (frustum culled)
           │         │    ├─ outline          12 black lines around the target block
           │         │    └─ water pass       transparent, sorted far to near
           │         ├─ drawHud()             crosshair, hotbar, text, menus: one draw call
           │         └─ screenshot?           read the pixels back if F2 was pressed
           │
           └─ Window::swapBuffers()          show the finished frame (waits for v-sync)
```

**Double buffering.** All drawing goes into a hidden *back buffer*. `SwapBuffers` exchanges it
with the visible *front buffer* in one step, so you never see a half-drawn frame. With v-sync on,
the swap waits for the monitor's next refresh. That wait caps the game at the monitor's rate
(60 Hz = 16.7 ms per frame) and is why the CPU is mostly idle.

**`dt`, the time step.** `dt` is the number of seconds since the previous frame (about 0.0167).
Everything that moves is multiplied by `dt`: `position += velocity * dt`. A slow PC that only
manages 30 fps uses `dt = 0.033`, so the player still walks at the same speed in real seconds.
`dt` is capped at 0.1 s so that a long freeze (such as dragging the window) does not make the
player teleport through walls.

---

## 4. From seed to pixel: the life of a block

```
seed (e.g. 2026)
  │  PerlinNoise(seed) shuffles a permutation table
  ▼
TerrainGenerator::surfaceHeight(x, z)        noise -> "the ground here is at y = 63"
  ▼
TerrainGenerator::generate(chunk)            fills 32,768 bytes: Stone, Dirt, Grass, Water, ...
  ▼
Chunk::blocks   (std::array<uint8_t, 32768>, in RAM)
  ▼
ChunkMesher::build()                         keeps only faces next to air or water, adds shading
  ▼
std::vector<BlockVertex>  (16 bytes per corner, 4 corners per visible face)
  ▼
GpuMesh::upload() -> glBufferData            copies the vertices into GPU memory (VBO)
  ▼
Renderer::drawWorld() -> glDrawElements      "draw this chunk"
  ▼
vertex shader (BLOCK_VERTEX)                 corner position x camera matrices -> screen position
  ▼
rasteriser (fixed GPU hardware)              which pixels does each triangle cover?
  ▼
fragment shader (BLOCK_FRAGMENT)             texture colour x shade, blended into fog
  ▼
depth test                                   keep it only if nearer than what is already there
  ▼
back buffer -> SwapBuffers -> your monitor
```

The CPU does the top half once per chunk, and again only when a block in that chunk changes.
The GPU does the bottom half for every pixel, every frame.

---

## 5. The life of a click

What happens when you left-click a block:

1. Windows sends `WM_LBUTTONDOWN` to the window. `Window::handleMessage` sets
   `input_.mouseDown[0]` and `input_.mousePressed[0]`.
2. In the same frame, `Game::updatePlaying` has already run `raycastBlocks`, so `target_` holds
   the block under the crosshair and the face that was hit.
3. `Game::handleBlockActions` sees `mousePressed[0]` and calls
   `world_->setBlock(x, y, z, Block::Air)` (or `Water`, if water touches the hole).
4. `World::setBlock` changes one byte in the chunk and sets `modified = true` (it must be saved)
   and `dirty = true` (its mesh is out of date). If the block is on the chunk's edge, the
   neighbouring chunk is marked dirty too.
5. In the **next** frame, `World::update` walks the chunks nearest first, finds the dirty one,
   and calls `buildMesh`. That rebuilds its triangles (about 0.5 ms) and uploads them.
6. `Renderer::drawWorld` draws the new mesh, and the block is gone.

The delay is one frame (16 ms), too short to notice.

---

## 6. Start-up and shut-down

**Start-up** (`main` → `Window::create` → `Game::init`):

1. Parse the command line into `GameOptions`.
2. Create the window, the OpenGL 3.3 context, and load the OpenGL functions.
3. Build the shaders, paint and upload the textures, make the HUD font.
4. Load `world.dat` if it exists (and `--new` / `--seed` were not given), else pick a seed.
5. Create the `World`, insert saved chunks, find a spawn point on dry land.
6. **Loading screen:** `updateLoading` gives world building 30 ms per frame until the chunks
   within 3 of the player are ready (well under a second), then captures the mouse. The rest of the
   view distance streams in while you play.

**Shut-down** (closing the window, or `Q` while paused):

1. `processMessages` returns `false` (or `quitRequested()` is true) and the loop ends.
2. `Game::shutdown` saves the world (and shows a warning if that fails).
3. `}`: `~Game` frees every chunk mesh and the shared index buffer, then `~Renderer` and `~Hud`
   free shaders and textures.
4. `~Window` deletes the OpenGL context and destroys the window.

---

## 7. Knobs to turn

Good first experiments. Change one, rebuild, and see what happens.

| What | Where | Default |
|---|---|---|
| Gravity, jump height, walk/sprint/fly speed | `player.cpp`, top of file | 28, 8.6, 4.3/6.2/11 |
| Mouse sensitivity, reach | `game.cpp`, top of file | 0.0022 rad/count, 6 blocks |
| Sea level, snow line | `terrain.h` | 48, 90 |
| Terrain shape (continents, hills, mountains) | `TerrainGenerator::surfaceHeight` | see [07](07-world-generation.md) |
| Tree density | `TerrainGenerator::placeTrees`, `chance` | 5%-65% per 6x6 cell |
| Cave size | `TerrainGenerator::isCave`, `0.0035f` | bigger = wider tunnels |
| View distance | `--view N` | 8 chunks |
| Sky and water colours, sun direction | `renderer.cpp`, top of file | |
| Ambient occlusion strength | `AO_CURVE` in `mesher.cpp` | 0.38, 0.58, 0.78, 1.0 |
| Face brightness per direction | `FACES[].light` in `mesher.cpp` | top 1.0, sides 0.80/0.66, bottom 0.52 |
| Field of view | `fov_` in `game.h`, `targetFov` in `game.cpp` | 70°, 78° when sprinting |
| Textures | painter functions in `textures.cpp` | |
