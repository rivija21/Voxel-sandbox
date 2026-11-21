# 10 - Exercises

The best way to learn this code is to change it. The exercises go from easy (change data in a
table) to hard (real engine features). Each one lists the files to touch, hints, and how to check
your work. Commit (or copy the folder) before each one so you can always get back.

Difficulty: ★ an evening · ★★ a weekend · ★★★ a project

---

## Level 1: warm-ups

### 10.1 ★ Add a new block

Add **Clay** (or Gold, Lava, Obsidian ...).

1. `blocks.h`: add `Clay` before `Count` in `Block::`, and `Clay` before `Count` in `Tex::`.
2. `blocks.cpp`: add a row to `BLOCKS` (name, textures, solid, opaque).
3. `textures.cpp`: write `Tile clay(Rng& rng)` and add it to `PAINTERS` in the same position.
4. Put it in `DEFAULT_HOTBAR`, or pick it with middle-click after generating some.

**Check:** If you forget step 2 or 3, the `static_assert`s refuse to compile, which is exactly
why they are there. Old save files still load: block ids are only ever added at the end.

### 10.2 ★ Day and night

Add `float timeOfDay` (0..1) to `Game`, advanced by `dt / 600` (a 10-minute day).

- `renderer.cpp`: make `SUN_DIRECTION` rotate with time
  (`(cos(a), sin(a), -0.3)` normalised, with `a = timeOfDay * 2π`).
- Blend the horizon and zenith colours toward dark blue at night, and orange at sunrise and
  sunset (`lerp` on `sin(a)`).
- Add `uniform float uDaylight;` to the block fragment shader and multiply `color` by it.
- A key (T) to skip time helps testing.

**Hint:** pass the time to `drawWorld` as a parameter instead of making it global.

### 10.3 ★ Change the view distance while playing

`PageUp` / `PageDown` change the view distance between 2 and 16.

**Hint:** `World` builds `offsets_` in its constructor. Move that into a
`setViewDistance(int)` method. The renderer and fog already read `world.viewDistance()` every
frame.

### 10.4 ★ A compass and a coordinates line

Draw "N", "E", "S", "W" letters along the top of the screen that slide as you turn (use
`player_.yaw`), and always show the XYZ line even without F3.

### 10.5 ★★ Keyboard-layout-independent movement

On AZERTY keyboards W/A/S/D are not in the usual place. Use **scan codes** (physical key
positions) for movement: bits 16-23 of `lParam` in `WM_KEYDOWN`. Store a second array
`scanDown[256]` in `InputState`. W, A, S and D are scan codes `0x11`, `0x1E`, `0x1F`, `0x20`.

---

## Level 2: features

### 10.6 ★★ Clouds

Either:

- **Flat clouds:** in the sky shader, where `dir.y > 0`, project the ray onto a plane at height
  120 (`t = (120 - camY) / dir.y`), sample 2D noise at the hit point (write a GLSL hash or value
  noise), threshold it, and blend white. Scroll with time.
- **Block clouds:** a separate mesh of flat white boxes at y = 120, built from 2D noise on the
  CPU and drawn with blending.

### 10.7 ★★ Compress the save file

Each modified chunk costs 32 KB, but most of a chunk is long runs of the same block (air above,
stone below). **Run-length encoding**: write `(count, block)` pairs instead of raw bytes.

- `savefile.cpp`: `VERSION = 2`. Write `uint16 count, uint8 block` pairs and read them back.
- Keep reading version 1 files (raw) for compatibility.
- Add a round-trip test to `tests/gameplay_tests.cpp`.

**Check:** a chunk that is half stone and half air should shrink from 32 KB to a few hundred
bytes.

### 10.8 ★★ Falling sand

When the block under Sand becomes Air (or Water), the sand falls.

**Hint:** in `World::setBlock`, after any change, check the block above. Keep a queue of
"blocks to check" and process a few per frame in `Game::updatePlaying` (falling one block per
0.05 s looks nice). Remember to mark chunks dirty.

### 10.9 ★★ Sound

Play a short sound when breaking or placing.

- `PlaySoundA(data, nullptr, SND_MEMORY | SND_ASYNC)` plays a WAV file held in memory. Link
  `-lwinmm` and `#include <mmsystem.h>`.
- Generate the WAV in code (like the textures): a 44-byte header + 0.08 s of decaying noise
  ("crunch") or a sine wave ("pop"). Store it in a `std::vector<uint8_t>` that lives as long as
  the game.

### 10.10 ★★ Third-person camera

Press `V` to put the camera 4 blocks behind the player and draw the player as a box. Do a short
raycast backwards from the head, so the camera does not go through walls. Reuse the line shader,
or build a small mesh with `GpuMesh`.

### 10.11 ★ Cache uniform locations

`Shader::setX` calls `glGetUniformLocation` every time. Cache locations in a
`std::unordered_map<std::string, GLint>` inside `Shader`, or look them up once after linking
and store them in named fields. Use the F3 counters, or time `drawWorld` with
`std::chrono`, before and after. (It will be a small gain here, which is a lesson too: measure
first.)

---

## Level 3: engine work

### 10.12 ★★ A real build system

Write a `Makefile` for `mingw32-make` that compiles each `.cpp` into `build/*.o` and links them:

```make
CXXFLAGS = -std=c++17 -O2 -Wall -Wextra -MMD -MP
SRC = $(wildcard src/*.cpp)
OBJ = $(SRC:src/%.cpp=build/%.o)

voxel-sandbox.exe: $(OBJ)
	g++ $^ -o $@ -mwindows -static -lopengl32 -lgdi32 -luser32

build/%.o: src/%.cpp
	g++ $(CXXFLAGS) -c $< -o $@

-include $(OBJ:.o=.d)
```

`-MMD -MP` makes g++ write `.d` files listing each `.cpp`'s headers, so changing a header
rebuilds exactly the right files. Two classic Make traps: the command lines under each rule
must start with a **Tab** character, not spaces, and the `build` folder must exist first
(`mkdir build`). Run with `mingw32-make -j8` (8 files in parallel). Then try
CMake (`pacman -S mingw-w64-ucrt-x86_64-cmake`), the most common C++ build tool.

### 10.13 ★★ Port to GLFW + GLAD + GLM

Install them with MSYS2 (`pacman -S mingw-w64-ucrt-x86_64-glfw mingw-w64-ucrt-x86_64-glm`) and
replace `window.cpp` (GLFW), `gl.cpp` (GLAD, or GLFW's `glfwGetProcAddress`) and parts of
`mathlib.h` (GLM). Compare the line counts and decide which layers you would rather own. Most
tutorials (LearnOpenGL) use this stack, so it is worth knowing both.

### 10.14 ★★★ Greedy meshing

Merge adjacent identical faces into large rectangles. For each of the 6 directions and each
slice of the chunk:

1. Build a 2D mask of "which faces in this slice are visible, and with what block / AO".
2. Scan it: grow a rectangle right while the next face matches, then grow it down while the
   whole next row matches. Emit one quad and clear those cells.

Texture coordinates must then go from 0 to the rectangle's width/height (with `GL_REPEAT`
wrapping). Merge only faces whose AO values match, or the shading breaks. Compare quad counts
in F3: expect 3-10x fewer.

### 10.15 ★★★ Real light: torches and dark caves

Store a light level 0-15 per block (a second array in `Chunk`).

- **Sky light:** 15 straight down from the sky until a solid block, then spread sideways,
  losing 1 per block.
- **Torch light:** a new Torch block emits 14 and spreads the same way.
- Spread with a **breadth-first flood fill** (a queue of positions). Removing light needs a
  second "un-light" BFS; that is the tricky part.
- In the mesher, sample the light of the block in front of each face (average the 4 blocks
  around each corner, like AO) and multiply it into `shade`.

Caves become truly dark, and a torch makes a warm circle of light.

### 10.16 ★★★ Generate and mesh on worker threads

The 5 ms budget per frame limits how fast the world can load. Move `TerrainGenerator::generate`
and `ChunkMesher::build` to background threads:

- A job queue (`std::deque` + `std::mutex` + `std::condition_variable`) feeds 2-4
  `std::thread`s.
- Workers produce finished `Chunk`s and vertex vectors and push them to a "done" queue. They
  must **not** call OpenGL: the context belongs to the main thread.
- The main thread takes finished results each frame, inserts the chunks and calls
  `GpuMesh::upload`.
- The hard part is meshing, which reads neighbour chunks that may be changing. Copy the padded
  neighbourhood on the main thread, or never mutate a chunk that a worker is reading.

### 10.17 ★★★ Flowing water

Give water a level (8 = source ... 1 = thin edge) stored in a separate array, and make it a
cellular automaton: each tick, water flows down if it can, otherwise sideways with level - 1.
Render the top surface at height `level / 9`. Process only "active" water blocks from a queue,
or it will be far too slow.

### 10.18 ★★ Anti-aliasing (MSAA)

The block edges are jagged ("aliased"). Two ways to fix it:

1. **Multisampled framebuffer object:** render into an FBO with `glTexImage2DMultisample` /
   `glRenderbufferStorageMultisample` (4 samples), then `glBlitFramebuffer` to the window. You
   will need to add those functions to `GL_FUNCTION_LIST`.
2. **Multisampled window:** the "dummy window" dance: create a throwaway window and context, get
   `wglChoosePixelFormatARB`, destroy it, then create the real window with
   `WGL_SAMPLES_ARB = 4`. (A window's pixel format can only be set once, which is why the
   dummy is needed.)

### 10.19 ★★★ Sun shadows

**Shadow mapping:** render the scene's depth from the sun's point of view into a depth texture
(orthographic projection centred on the player). In the block shader, transform each pixel into
the sun's view and compare depths: if something nearer to the sun covers it, the pixel is in
shadow. Expect to fight "shadow acne" (use a small depth bias) and to learn a lot.

### 10.20 ★★★ Make it a game

A sandbox is a toy; a goal makes it a game. Some ideas:

- **Survival lite:** breaking a block adds it to an inventory count; placing uses one up. Show
  the counts in the hotbar (the HUD can already draw text).
- **Build challenge:** show a target structure as a ghost outline (reuse `drawOutline`) and
  score how closely the player builds it.
- **Treasure hunt:** hide a Gold block in a cave using the seed, and give "warmer/colder" hints
  based on distance.

---

## How to approach any of these

1. **Read** the chapter about the files you will touch.
2. **Make the smallest change that shows something**, such as a coloured rectangle on the HUD or
   a `fprintf(stderr, ...)` in the debug build, before writing the real thing.
3. **Write a test** if the logic can run without a window (physics, world data, saving). See
   `tests/gameplay_tests.cpp`.
4. **Measure** with F3, `--debug-info`, `--screenshot`, and `std::chrono`, rather than guessing.
