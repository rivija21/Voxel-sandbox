# 01 - Setup and build

This chapter covers what is on your PC, how `g++` turns 17 `.cpp` files into one `.exe`, what
every flag does, and how to fix the errors you are most likely to meet.

---

## 1. The toolchain on this PC

| Tool | Where | What it is |
|---|---|---|
| **MSYS2** | `C:\msys64` | A package manager and Unix-like environment for Windows. It installs compilers and libraries |
| **UCRT64 environment** | `C:\msys64\ucrt64` | One of MSYS2's "environments". Its programs use Microsoft's modern C runtime (UCRT), which is part of Windows 10/11 |
| **g++ 15.2** | `C:\msys64\ucrt64\bin\g++.exe` | The GNU C++ compiler (MinGW-w64 build). It is already on your `PATH` |
| **gdb** | `C:\msys64\ucrt64\bin\gdb.exe` | The GNU debugger |
| **objdump, nm, strip** | same folder | Tools for looking inside `.exe` and `.o` files |
| Windows + OpenGL headers | `C:\msys64\ucrt64\include` | `windows.h`, `GL/gl.h`, `GL/glext.h`, `GL/wglext.h` |
| Import libraries | `C:\msys64\ucrt64\lib` | `libopengl32.a`, `libgdi32.a`, `libuser32.a` |

Check it works from any terminal:

```powershell
g++ --version     # g++.exe (Rev8, Built by MSYS2 project) 15.2.0
```

If that says "not recognized", add `C:\msys64\ucrt64\bin` to your `PATH` (Windows: *Edit the
system environment variables* → *Environment Variables* → `Path` → *New*), then open a new
terminal.

**Why no extra libraries?** Most OpenGL tutorials install GLFW (windows), GLAD (function loading)
and GLM (maths). This project writes small versions of all three itself, in `window.cpp`,
`gl.cpp` and `mathlib.h`, so every layer is visible. Exercise 10.13 shows how to swap them in later.

---

## 2. From source code to an .exe

Building a C++ program happens in four stages. `g++` runs all of them for you:

```
 main.cpp ──► preprocessor ──► compiler ──► assembler ──► main.o ─┐
 game.cpp ──► preprocessor ──► compiler ──► assembler ──► game.o ─┤
 ...  (one pipeline per .cpp file)                                ├─► linker ──► voxel-sandbox.exe
 world.cpp ─► preprocessor ──► compiler ──► assembler ──► world.o ┤      ▲
                                                                  │      │
                         libopengl32.a  libgdi32.a  libstdc++.a ──┘──────┘
```

1. **Preprocessor.** It runs `#include` (pastes the header text in), `#define` (text
   substitution) and `#ifdef`. The output is one long C++ file called a **translation unit**.
   `game.cpp` becomes about 100,000 lines once `windows.h` and the standard headers are pasted in.
2. **Compiler.** It turns C++ into assembly for your CPU. All type checking happens here, and so
   do most errors (`error: 'foo' was not declared in this scope`).
3. **Assembler.** It turns assembly into machine code in an **object file** (`.o`). An object
   file has *holes* where it calls functions defined elsewhere.
4. **Linker** (`ld`). It glues all the `.o` files together and fills the holes. When
   `game.o` calls `World::update`, the linker finds that function in `world.o`. When `window.o`
   calls `SwapBuffers`, it finds the entry in `libgdi32.a`, which says "this lives in
   `gdi32.dll`". Linker errors look like `undefined reference to 'X'`.

**Headers declare, sources define.** `world.h` says "a function `World::update(const Vec3&,
double)` exists". `world.cpp` contains its body. Every file that calls `update` includes the
header so the compiler can check the call, and the linker connects the call to the one body.
Chapter [03](03-cpp-semantics.md) covers this in more detail.

You can watch each stage yourself:

```powershell
g++ -std=c++17 -E src\blocks.cpp -o blocks.i     # preprocess only: open blocks.i
g++ -std=c++17 -S src\blocks.cpp -o blocks.s     # compile to assembly: open blocks.s
g++ -std=c++17 -c src\blocks.cpp -o blocks.o     # make an object file
nm -C blocks.o                                    # list the symbols it defines (and needs)
```

---

## 3. What build.bat does

```bat
g++ -std=c++17 -O2 -Wall -Wextra -mwindows -s  src\main.cpp src\window.cpp ... ^
    -o voxel-sandbox.exe -static -lopengl32 -lgdi32 -luser32
```

| Flag | Meaning | Why this project needs it |
|---|---|---|
| `-std=c++17` | Use the C++17 language standard | `std::filesystem` (save paths), `std::clamp`, `std::size` were added in C++17 |
| `-O2` | Optimise | Generating one chunk takes ~30,000 noise calls: 1.2 ms with `-O2`, 4.1 ms with `-O0` (measured with `terrain_map`) |
| `-Wall -Wextra` | Turn on most warnings | Catches unused variables, signed/unsigned mix-ups and similar. The project builds with **zero** warnings: keep it that way |
| `-mwindows` | Build a GUI program (PE subsystem "Windows GUI") | Without it a black console window opens behind the game |
| `-s` | Strip symbol names | Makes the `.exe` smaller. Debug builds leave it out |
| `-o voxel-sandbox.exe` | Name of the output | |
| `-static` | Link the C++ runtime (`libstdc++`, `libgcc`, `winpthread`) *into* the .exe | The game runs on any Windows 10/11 PC, even without MSYS2 |
| `-lopengl32` | Link `libopengl32.a` | `glClear`, `wglCreateContext`, `wglGetProcAddress` ... (`opengl32.dll`) |
| `-lgdi32` | Link `libgdi32.a` | `ChoosePixelFormat`, `SwapBuffers`, and drawing the HUD font (`gdi32.dll`) |
| `-luser32` | Link `libuser32.a` | Windows, messages, the cursor, raw input (`user32.dll`) |

`-lNAME` means "search for `libNAME.a`". The order matters with GNU `ld`: libraries go **after**
the sources that use them.

**What does `-static` *not* include?** System DLLs. You can see exactly what the finished game
needs with `objdump`:

```powershell
objdump -p voxel-sandbox.exe | Select-String "DLL Name"
#   GDI32.dll  KERNEL32.dll  OPENGL32.dll  USER32.dll  api-ms-win-crt-*.dll
```

All of these are part of Windows. The `api-ms-win-crt-*` names are the Universal C Runtime
(UCRT) that the "UCRT64" environment is named after. The finished file is about 1.25 MB.

**Note on `main` with `-mwindows`.** Windows GUI programs traditionally start at `WinMain`.
MinGW's start-up code calls an ordinary `int main(int argc, char** argv)` in either subsystem,
so `main.cpp` uses the familiar form and gets the command line for free.

---

## 4. Debug builds and gdb

```powershell
.\build.bat debug                       # makes voxel-sandbox-debug.exe
.\voxel-sandbox-debug.exe --gldebug     # console shows "[OpenGL ERROR] ..." lines
```

The debug build uses `-g -O0` and leaves out `-mwindows`:

- `-g` stores the source file and line of every instruction, so a debugger can map machine code
  back to your code.
- `-O0` turns optimisation off, so every variable still exists and lines run in order.
- Without `-mwindows` the program gets a console, so `std::printf` and `std::fprintf(stderr,
  ...)` output shows up there.

`--gldebug` asks the driver for a *debug context*. The driver then calls `openglDebugCallback`
in `window.cpp` whenever an OpenGL call is wrong (bad enum, wrong buffer size, ...).

**Using gdb** (run it from the project folder):

```text
gdb .\voxel-sandbox-debug.exe
(gdb) break Player::moveAxis      # stop when this function is called
(gdb) run --new                   # start, with program arguments
(gdb) bt                          # backtrace: which functions called this one
(gdb) print position              # look at a variable
(gdb) print velocity.y
(gdb) next                        # run one line (step over calls)
(gdb) step                        # run one line (step into calls)
(gdb) continue                    # carry on until the next breakpoint
(gdb) quit
```

If the game crashes, run it under gdb. When it stops, type `bt`: the top lines show exactly where
the crash happened.

**VS Code.** Install the *C/C++* extension. Then *Run → Add Configuration → C++ (GDB/LLDB)* and
set `"program"` to `${workspaceFolder}/voxel-sandbox-debug.exe` and `"miDebuggerPath"` to
`C:/msys64/ucrt64/bin/gdb.exe`. Now F5 builds nothing but runs the debugger with breakpoints you
set by clicking in the margin. Run `build.bat debug` first.

---

## 5. Faster rebuilds (separate compilation)

`build.bat` compiles all 17 files every time (about 40 s). Professional builds compile each
`.cpp` to a `.o` once, and later only recompile the files that changed:

```powershell
g++ -std=c++17 -O2 -c src\world.cpp -o build\world.o     # compile only (-c), no link
# ...once for every .cpp...
g++ build\*.o -o voxel-sandbox.exe -mwindows -static -lopengl32 -lgdi32 -luser32   # link
```

Change `world.cpp`, recompile just `world.o`, and link again: a few seconds instead of 40. The
catch is that if a **header** changes, every `.cpp` that includes it must be recompiled too.
Build systems (Make, CMake, Ninja) track that for you. Exercise 10.12 builds a Makefile, and
`mingw32-make` is already installed.

---

## 6. Tests

```powershell
.\test.bat
```

This compiles `tests\gameplay_tests.cpp` together with the game's *logic* files (world, physics,
raycast, save file), but not the window or renderer, and runs 22 checks. Examples: "a falling
player lands exactly on the floor", "walking into a wall stops 0.3 blocks from it", "a damaged
save file is rejected". No window opens. Run it after changing physics or the save format.

It also builds `tests\terrain_map.exe`, which prints a map of any seed:

```powershell
.\tests\terrain_map.exe 2026
```

Because world generation is a pure function of the seed, the map tool gives exactly the world the
game would make.

---

## 7. Common problems

| Symptom | Cause | Fix |
|---|---|---|
| `'g++' is not recognized` | `C:\msys64\ucrt64\bin` not on `PATH` | See section 1 |
| `cannot open output file voxel-sandbox.exe: Permission denied` | The game is still running (Windows locks running `.exe` files) | Close the game, build again |
| `undefined reference to 'glClear'` / `'SwapBuffers'` / `'ChoosePixelFormat'` | A `-l` library is missing, or it comes before the source files | Keep `-lopengl32 -lgdi32 -luser32` at the end |
| `undefined reference to 'World::update...'` | A `.cpp` is missing from the command | Add it to `SOURCES` in `build.bat` |
| `multiple definition of 'X'` | A non-`inline` function or variable is *defined* in a header that several `.cpp` files include | Declare it in the header (`extern`), define it in one `.cpp`, or mark it `inline` |
| Message box "Shader ... failed to build" | A GLSL typo in `renderer.cpp` or `hud.cpp` | The box shows the driver's error with a line number *inside that shader* |
| Message box "does not support OpenGL 3.3" | Very old or missing graphics driver, or a remote desktop session | Update the GPU driver. Run on the PC itself, not over RDP |
| "Could not save the world" warning | Something blocks writing to `%LOCALAPPDATA%\VoxelSandbox` | See the next row |
| A program is blocked from writing in `Documents` | Windows Security → *Ransomware protection* → *Controlled folder access* is on here | Keep data out of protected folders (this game does), or add the program under *Allow an app through Controlled folder access* |
| The new `.exe` vanishes or will not start | Antivirus quarantined an unknown, freshly built program | Restore it in Windows Security → *Protection history* |
| Game runs slowly on a laptop | It started on the integrated GPU | `main.cpp` exports `NvOptimusEnablement` to prefer the fast GPU. You can also choose it in Windows *Settings → Display → Graphics* |
