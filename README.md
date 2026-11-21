# Voxel Sandbox

A Minecraft-style 3D block world written **from scratch** in C++17: an endless landscape you can
walk, swim and fly through, breaking and placing blocks. The program uses no game engine and no
libraries. It creates its own window with the Win32 API, talks to the graphics card through
OpenGL 3.3, and paints its own textures in code. The whole game is one `.exe` file.

This README is your starting point. The `docs/` folder is a study guide that explains every part
of the code, the maths behind it, and the C++ it uses, so you can come back later and understand
every line.

| What you get | How it works (where to read) |
|---|---|
| Endless terrain: plains, hills, snowy mountains, oceans, beaches, caves, forests | Perlin noise, chunk streaming, see [07](docs/07-world-generation.md) |
| Real 3D on the GPU at 60 fps, with fog and a sky with a sun | OpenGL 3.3 core profile and GLSL shaders, see [05](docs/05-opengl-pipeline.md) |
| Smooth corner shading on blocks, see-through water, leaves and glass | Meshing, ambient occlusion, render passes, see [08](docs/08-meshing-and-lighting.md) |
| Walking, jumping, swimming, flying, solid collisions | Axis-separated AABB physics, see [09](docs/09-physics-and-raycasting.md) |
| Break and place 13 kinds of block | DDA voxel raycasting, see [09](docs/09-physics-and-raycasting.md) |
| Your world is saved when you quit | A small binary file format, see [07](docs/07-world-generation.md) |

---

## 1. Build and run

You need `g++` from MSYS2, which you already have in `C:\msys64\ucrt64\bin`. Nothing else needs
installing: the OpenGL and Windows headers come with it.

From the project folder:

```powershell
.\build.bat
.\voxel-sandbox.exe
```

The first build takes about 40 seconds. Double-clicking `voxel-sandbox.exe` works too.

Other build commands:

| Command | What it does |
|---|---|
| `.\build.bat` | Optimised game, no console window, gives `voxel-sandbox.exe` |
| `.\build.bat debug` | Debug build with a console window for messages, gives `voxel-sandbox-debug.exe` |
| `.\test.bat` | Builds and runs the gameplay tests (no window needed) and the terrain map tool |

Every compiler flag is explained in [docs/01-setup-and-build.md](docs/01-setup-and-build.md).

## 2. Controls

| Key | Action |
|---|---|
| `W` `A` `S` `D` | Move |
| Mouse | Look around |
| `Space` | Jump. Swim up in water. Fly up when flying |
| `Shift` | Fly down |
| `Ctrl` | Sprint (fly faster when flying) |
| `F` | Toggle flying |
| Left click | Break the block under the crosshair (hold to keep breaking) |
| Right click | Place the selected block |
| Middle click | Copy the block you look at into the selected hotbar slot |
| `1`-`9`, mouse wheel | Choose a hotbar slot |
| `Esc` | Pause and free the mouse. Click to play again, or `Q` to save and quit |
| `F1` | Show or hide the controls |
| `F2` | Screenshot |
| `F3` | Debug info (fps, position, chunk counts, the block you look at) |
| `F5` | Save now |
| `F11` | Fullscreen |

## 3. Command-line options

```powershell
.\voxel-sandbox.exe --new             # new world, random seed
.\voxel-sandbox.exe --seed 2026       # new world from seed 2026 (same seed = same world)
.\voxel-sandbox.exe --view 12         # draw 12 chunks far (2-16, default 8)
.\voxel-sandbox.exe --debug-info      # start with the F3 overlay on
.\voxel-sandbox-debug.exe --gldebug   # print OpenGL errors in the console
.\voxel-sandbox.exe --screenshot shot.bmp --seed 2026 --pos 250 100 0 --look 90 -10
                                      # generate, save one picture, quit (used for testing)
```

Seeds worth trying: `2026` (big oceans, snowy peaks), `12345` (forests and hills), `777`.
Run `tests\terrain_map.exe 2026` after `test.bat` to see a map of any seed in the console.

## 4. Where your files go

| File | Location |
|---|---|
| Saved world | `%LOCALAPPDATA%\VoxelSandbox\world.dat` |
| Backup of the previous world (made when you start a new one) | `%LOCALAPPDATA%\VoxelSandbox\world-backup.dat` |
| Screenshots (`F2`) | `%LOCALAPPDATA%\VoxelSandbox\screenshots\` |

To open the folder, paste `%LOCALAPPDATA%\VoxelSandbox` into Explorer's address bar.

**Why not next to the .exe?** This PC has Windows Security's *Controlled folder access*
(ransomware protection) turned on. It blocks programs it does not know from writing anywhere in
`Documents`, and the first version of the game lost its save that way. Windows programs are
supposed to keep their data in `AppData`, so the game does that now. If it still cannot save, it
shows a warning instead of failing silently.

## 5. Project map

```
voxel-sandbox/
├── build.bat            compile the game
├── test.bat             compile and run the tests
├── README.md            this file
├── docs/                the study guide (start with 01)
├── src/
│   ├── main.cpp         entry point: command line + the game loop
│   ├── window.h/.cpp    Win32 window, OpenGL context, keyboard and mouse
│   ├── gl.h/.cpp        loads OpenGL 3.3 functions from the driver (X-macro)
│   ├── game.h/.cpp      the rules: input, block editing, HUD, saving, screenshots
│   ├── world.h/.cpp     owns the chunks; streams them in and out around the player
│   ├── chunk.h          one 16 x 128 x 16 column of blocks
│   ├── terrain.h/.cpp   procedural generation: heights, caves, trees
│   ├── noise.h/.cpp     Perlin noise and hashing
│   ├── mesher.h/.cpp    blocks -> triangles (face culling + ambient occlusion)
│   ├── gpu_mesh.h/.cpp  a chunk's triangles in GPU memory (VAO/VBO, shared indices)
│   ├── renderer.h/.cpp  sky, terrain, water and outline passes, and their shaders
│   ├── shader.h/.cpp    compiles GLSL programs
│   ├── textures.h/.cpp  paints the 16x16 pixel-art block textures in code
│   ├── hud.h/.cpp       2D overlay: text (via a Windows font), crosshair, hotbar
│   ├── player.h/.cpp    movement physics and collision
│   ├── raycast.h/.cpp   which block is under the crosshair
│   ├── savefile.h/.cpp  the world.dat binary format
│   ├── blocks.h/.cpp    the table of block types
│   └── mathlib.h        vectors, matrices, frustum culling
└── tests/
    ├── gameplay_tests.cpp   physics, raycast and save/load checks
    └── terrain_map.cpp      prints an ASCII map of a seed
```

About 3,400 lines of C++, most of them commented.

## 6. How to study this code

Read the guide in order. Each chapter builds on the one before it.

| # | Chapter | You will learn |
|---|---|---|
| 01 | [Setup and build](docs/01-setup-and-build.md) | The toolchain, every compiler flag, compiling vs linking, fixing build errors |
| 02 | [Architecture](docs/02-architecture.md) | How the files fit together, who owns what, what happens in one frame |
| 03 | [C++ semantics](docs/03-cpp-semantics.md) | Every C++ feature this code uses, with examples taken from it |
| 04 | [Windows and input](docs/04-windows-and-input.md) | Win32 windows, the message loop, raw mouse input, timing |
| 05 | [The OpenGL pipeline](docs/05-opengl-pipeline.md) | How the GPU draws, context creation, buffers, shaders, textures, render passes |
| 06 | [3D maths](docs/06-3d-math.md) | Vectors, matrices, the camera, perspective, frustum culling |
| 07 | [World generation](docs/07-world-generation.md) | Chunks, Perlin noise, terrain, caves, trees, streaming, the save file |
| 08 | [Meshing and lighting](docs/08-meshing-and-lighting.md) | Face culling, ambient occlusion, texture arrays, transparency |
| 09 | [Physics and raycasting](docs/09-physics-and-raycasting.md) | AABB collision, gravity and jumping maths, the DDA algorithm |
| 10 | [Exercises](docs/10-exercises.md) | Graded projects to extend the game, with hints |
| 11 | [Glossary](docs/11-glossary.md) | Every technical term in one place |

**Suggested path:** build it and play for ten minutes. Then read 01-02 and skim `main.cpp` and
`game.cpp` to see the overall flow. After that, take one subsystem at a time: read its chapter
with the source file open beside it. Each chapter names the functions it explains.

**A good habit:** change one number and rebuild. Make gravity weaker in `player.cpp`, the sea
higher in `terrain.h`, or the fog closer in `renderer.cpp`. Predicting what will happen, and then
seeing it, is the fastest way to learn.
