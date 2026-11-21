# 03 - C++ semantics: every language feature in this code

This chapter is a reference for the C++ used in the project. Each section explains one feature,
shows where the game uses it (file and function), and points out common mistakes. Read it
straight through once, then use it as a lookup when a line of code puzzles you.

Contents:
1. Files: headers, sources and linkage
2. The preprocessor and X-macros
3. Integer and floating-point types
4. Enumerations
5. Structs, classes, constructors
6. Operator overloading
7. References and pointers
8. `const` and `constexpr`
9. Ownership, RAII and smart pointers
10. Standard library containers and algorithms
11. Lambdas
12. Templates
13. Casts
14. Function pointers and callbacks
15. `static`: the keyword with four meanings
16. Raw string literals
17. Compile-time checks: `static_assert`
18. Floating-point pitfalls
19. `extern "C"` and DLL exports
20. Undefined behaviour this code avoids
21. Naming conventions used here

---

## 1. Files: headers, sources and linkage

**Declaration vs definition.** A *declaration* says that something exists and what its type is.
A *definition* creates it (allocates the variable, or gives the function body).

```cpp
// world.h - declaration (in the class)
void update(const Vec3& center, double budgetSeconds);

// world.cpp - definition
void World::update(const Vec3& center, double budgetSeconds) { ... }
```

`World::` says "the `update` that belongs to class `World`". Every `.cpp` that includes
`world.h` can *call* `update`, and the linker connects all those calls to the one definition.

**The One Definition Rule (ODR).** Each non-inline function or global variable must be defined
in exactly **one** `.cpp` file. Define it in a header that two `.cpp` files include, and the
linker reports `multiple definition`.

**`#pragma once`** at the top of every header means "paste this file at most once per
translation unit", even if it is included many times indirectly (`game.h` includes `world.h`,
which includes `chunk.h`, which includes `blocks.h` ...). The older equivalent is an *include
guard*: `#ifndef WORLD_H / #define WORLD_H / ... / #endif`.

**`inline` functions in headers.** `mathlib.h` defines its functions in the header, which
every file includes. That is allowed because they are `inline`:

```cpp
inline float dot(const Vec3& a, const Vec3& b) { return a.x * b.x + a.y * b.y + a.z * b.z; }
```

`inline` relaxes the ODR: identical definitions in many files are merged by the linker. Member
functions written *inside* a class body (like `Vec3::operator+`) are implicitly inline. Small
functions in headers also let the compiler paste their body at the call site, which matters for
maths called millions of times.

**`extern` globals.** To share one global variable across files, *declare* it `extern` in a
header and *define* it in one `.cpp`:

```cpp
// blocks.h
extern const BlockInfo BLOCKS[];          // "exists somewhere"
// blocks.cpp
const BlockInfo BLOCKS[] = { ... };       // here it is
```

The OpenGL function pointers in `gl.h` / `gl.cpp` follow the same pattern.

> **Gotcha:** at namespace scope, `const` implies *internal linkage* (private to the file). A
> plain `const int X = 5;` in two `.cpp` files gives two separate, private copies. `BLOCKS` is
> still shared because `blocks.cpp` includes `blocks.h` first, and the earlier `extern`
> declaration gives the definition external linkage. Remove that `#include` and the linker would
> fail with `undefined reference to BLOCKS`.

**Unnamed namespaces** make things private to one `.cpp`:

```cpp
namespace {                       // textures.cpp
struct Rng { ... };               // nobody outside textures.cpp can see Rng
Tile grassTop(Rng& rng) { ... }
}
```

Every `.cpp` in the project puts its helpers in one. That avoids name clashes: `terrain.cpp` and
`mesher.cpp` could both have a helper called `setIfInside` without the linker complaining. It is
the modern replacement for marking free functions `static`.

**Forward declarations.** `player.h` writes `class World;` instead of `#include "world.h"`. A
reference or pointer to an *incomplete* type is fine; only code that uses its members needs the
full definition, and that code is in `player.cpp`. This keeps headers light and avoids circular
includes.

---

## 2. The preprocessor and X-macros

The preprocessor edits text before the compiler sees it.

| Directive | Example | Meaning |
|---|---|---|
| `#include "x.h"` | `#include "world.h"` | Paste `x.h`. Quotes search the current file's folder first |
| `#include <x>` | `#include <vector>` | Paste a system / library header. Only the include paths are searched |
| `#define NAME` | `#define NOMINMAX` | Define a flag or text replacement |
| `#ifndef / #endif` | in `gl.h` | Compile a section only if a name is not defined |

**Why `WIN32_LEAN_AND_MEAN` and `NOMINMAX`** (top of `gl.h`): `windows.h` is enormous.
`WIN32_LEAN_AND_MEAN` leaves out rarely used parts (sockets, cryptography), which compiles
faster. `NOMINMAX` stops it defining **macros** called `min` and `max`. Those macros would
otherwise break `std::min(a, b)`, because the preprocessor rewrites `min(a, b)` before the
compiler sees it.

**Why `mathlib.h` is not called `math.h`:** a project header with the same name as a system
header can be picked up by mistake, depending on include paths. Unique names avoid that.

**X-macros** (`gl.h`). The list of OpenGL functions is written once, as a macro that takes
*another macro* `X` as its argument:

```cpp
#define GL_FUNCTION_LIST(X)                    \
    X(PFNGLCREATESHADERPROC, glCreateShader)   \
    X(PFNGLSHADERSOURCEPROC, glShaderSource)   \
    ...
```

Then it is expanded with different definitions of `X`:

```cpp
#define X(type, name) extern type name;           // gl.h: declarations
GL_FUNCTION_LIST(X)
#undef X
// expands to:  extern PFNGLCREATESHADERPROC glCreateShader;  extern ...

#define X(type, name) type name = nullptr;        // gl.cpp: definitions
#define X(type, name) name = reinterpret_cast<type>(getOpenGLProc(#name)); \
                      if (!name) missing += #name " ";   // gl.cpp: loading
```

`#name` is the **stringizing operator**: it turns the argument into a string literal, so
`#name` becomes `"glCreateShader"`. Adding a new OpenGL function is now a one-line change, and
the three lists can never get out of step.

> **Macro gotcha found while writing the tests:** macros split arguments at commas and only
> understand `()` as brackets, not `{}`. So `CHECK(a == IVec3{5, 10, 5}, "msg")` gets *four*
> arguments, and the compile fails with baffling errors. The fix is extra parentheses:
> `CHECK(a == (IVec3{5, 10, 5}), "msg")`. In general prefer functions, `constexpr` and
> templates over macros, and keep macros for jobs only they can do, like stringizing.

---

## 3. Integer and floating-point types

**Fixed-width integers** (`<cstdint>`):

| Type | Size | Used for |
|---|---|---|
| `uint8_t` | 1 byte, 0..255 | `BlockId`, so a chunk is 32 KB instead of 128 KB with `int` |
| `uint32_t` | 4 bytes, unsigned | seeds, hashes, pixels (`0xAABBGGRR`) |
| `int32_t` | 4 bytes, signed | chunk coordinates in the save file (fixed size on every PC) |
| `uint64_t` | 8 bytes | chunk map key: two 32-bit coordinates packed together |
| `size_t` | pointer-sized, unsigned | sizes and indices of containers |

Plain `int` is "at least 16 bits" by the standard (32 on Windows). In files and GPU data, where
the exact byte layout matters, always use the fixed-width types.

**Type aliases:** `using BlockId = uint8_t;` (`blocks.h`) gives a meaningful name. The
older syntax is `typedef uint8_t BlockId;`.

**Integer division rounds toward zero:** `-1 / 16 == 0` and `-1 % 16 == -1`. Chunk maths needs
"round toward minus infinity" (block -1 is in chunk -1, at local position 15), so `mathlib.h`
has:

```cpp
inline int floorDiv(int a, int b) { return (a >= 0) ? a / b : -((-a + b - 1) / b); }
inline int floorMod(int a, int b) { int m = a % b; return m < 0 ? m + b : m; }
```

`gameplay_tests.cpp` checks this: block `(-1, 11, -1)` must land in chunk `(-1, -1)` at local
`(15, 11, 15)`. Forgetting this is the classic voxel-engine bug: a seam of wrong blocks along
x = 0 and z = 0.

**Bit operations:**

```cpp
// world.h - pack two signed 32-bit ints into one 64-bit key
(static_cast<uint64_t>(static_cast<uint32_t>(cx)) << 32) | static_cast<uint32_t>(cz)

// game.cpp - round up to a multiple of 4: add 3, then clear the low two bits
const int rowSize = (w * 3 + 3) & ~3;

// noise.cpp - wrap any integer (even negative) into 0..255
const int X = xi & 255;

// textures.cpp - pack 4 bytes into a pixel
byte(c.r) | (byte(c.g) << 8) | (byte(c.b) << 16) | (byte(c.a) << 24)
```

The cast through `uint32_t` in the key matters. Converting a negative `int` straight to
`uint64_t` sign-extends it, filling the upper 32 bits with ones, which would corrupt `cx`'s half
of the key.

**Unsigned arithmetic wraps** (2^32 + 1 becomes 1), which is well defined. Signed overflow is
*undefined behaviour*. That is why `hash3` in `noise.h` does all its multiplying in `uint32_t`.

**`float` vs `double`.** Positions, vertices and matrices use `float` (4 bytes): that is what
GPUs use, and it halves memory. Time uses `double`: `Window::time()` counts seconds since start,
and a `float` would lose millisecond precision after a few hours.

---

## 4. Enumerations

```cpp
// blocks.h
using BlockId = uint8_t;
namespace Block {
enum : BlockId { Air, Grass, Dirt, Stone, ..., Count };
}
```

Three things are going on:

1. `enum : BlockId` sets the **underlying type**, so each value is a `uint8_t`.
2. The enum has **no name** and sits inside a **namespace**, so you write `Block::Grass`. The
   values convert to `BlockId` silently, so `chunk.set(x, y, z, Block::Grass)` needs no cast.
3. `Count` is a trick: since values number from 0, the last one equals the number of blocks.
   `BLOCKS[Block::Count]` and `static_assert` use it.

**Why not `enum class Block`?** An `enum class` is stricter: it never converts to an integer
without `static_cast`. That is safer, but this code constantly stores blocks in byte arrays and
uses them as indices, so every line would need a cast. The namespace trick keeps the short,
scoped names.

**Why not plain `enum { Air, Grass, ... }` at global scope?** Windows headers define **macros**
with short capital names such as `OPAQUE`, `TRANSPARENT` and `ERROR`. A global `enum { OPAQUE }`
would be silently rewritten by the preprocessor into `enum { 2 }`, a baffling error. Namespaced,
mixed-case names cannot collide with them.

---

## 5. Structs, classes, constructors

`struct` and `class` are identical except for the default access: `struct` members are
`public`, `class` members are `private`. This project follows a common convention:

- **`struct` for plain data** whose fields anyone may read or write: `Vec3`, `BlockVertex`,
  `BlockInfo`, `RayHit`, `PlayerInput`, `GameOptions`, `Chunk`.
- **`class` for objects with rules** that protect their insides: `World`, `Window`,
  `Renderer`, `GpuMesh`, `Shader`.

**Default member initialisers** give every field a safe starting value:

```cpp
struct RayHit {
    bool hit = false;
    IVec3 block;
    float distance = 0.0f;
};
```

**Aggregate initialisation** fills a struct's fields in order with braces:
`Vec3{1.0f, 2.0f, 3.0f}`, `HudColor{255, 255, 255, 70}`, and whole tables like `BLOCKS[]` and
`FACES[6]` in `mesher.cpp`.

**Constructors and the member initialiser list:**

```cpp
// terrain.cpp
TerrainGenerator::TerrainGenerator(uint32_t seed)
    : seed_(seed), continents_(seed), hills_(seed + 1), mountains_(seed + 2), ... {}
```

The list after `:` constructs each member directly. Members are **always initialised in the
order they are declared in the class**, whatever order the list uses. `-Wall` warns
(`-Wreorder`) if the two orders differ, because initialising one member from another that is
not ready yet is a real bug.

**`explicit`** (`PerlinNoise`, `World`, `Rng`) stops a one-argument constructor being used
for silent conversions. Without it, `PerlinNoise n = 42;` would compile, and so would passing a
plain number where a `PerlinNoise` was expected.

**`= default` / `= delete`:** `Window() = default;` asks for the compiler-generated constructor.
`Window(const Window&) = delete;` forbids copying (see section 9).

**`static` member functions** belong to the class, not to an object, so there is no `this`:
`Chunk::index(x, y, z)`, `World::key(cx, cz)`, and `Window::windowProc` (which has to be static,
see section 14).

**`static constexpr` data members** are class-wide constants: `Player::WIDTH`,
`ChunkMesher::PAD_W`.

**`const` member functions** promise not to modify the object:
`float width() const { return width_; }`. They are the only ones you may call on a `const`
object or through a `const&`.

---

## 6. Operator overloading

`mathlib.h` lets vectors use ordinary maths syntax:

```cpp
Vec3 operator+(const Vec3& o) const { return {x + o.x, y + o.y, z + o.z}; }
Vec3& operator+=(const Vec3& o) { x += o.x; y += o.y; z += o.z; return *this; }
inline Vec3 operator*(float s, const Vec3& v) { return v * s; }   // allows 2.0f * v
```

- Binary operators that do not change their operands return a **new value** and are `const`.
- Compound assignment (`+=`) changes `*this` and returns a **reference** to it, so
  `a += b += c` works like it does for numbers.
- `float * Vec3` must be a **free function**, because a member operator always has the class
  object on its *left*.

**Indexing by axis**, in two versions:

```cpp
float& operator[](int axis)       { return axis == 0 ? x : (axis == 1 ? y : z); }  // can modify
float  operator[](int axis) const { return axis == 0 ? x : (axis == 1 ? y : z); }  // read only
```

The non-const version returns a **reference**, so `position[axis] += amount;` in
`Player::moveAxis` writes straight into `position.x`, `.y` or `.z`. This is what lets the physics
handle all three axes with one function.

> **Why not `return (&x)[axis];`?** You will see that trick online. It treats `x, y, z` as an
> array, which the language does not guarantee (the compiler could add padding). The ternary
> version is always correct, and an optimising compiler makes it just as fast.

---

## 7. References and pointers

| Form | Meaning | Example |
|---|---|---|
| `T&` | an alias for an existing object; can modify it | `int& opaqueQuads`: an *output parameter* that `ChunkMesher::build` fills in |
| `const T&` | an alias, read-only: the cheap way to pass big objects | `void update(const Vec3& center, ...)`, `const World& world` |
| `T*` | an address that may be `nullptr` | `Chunk* findChunk(int cx, int cz)` returns `nullptr` when not loaded |
| `const T*` | pointer to something you may not modify | `std::array<const Chunk*, 9>` in the mesher |
| `T* const` | a pointer that cannot be re-pointed | (not needed here) |

**Rules of thumb used here:**

- Pass small values (`int`, `float`, `BlockId`) **by value**.
- Pass objects **by `const&`** to avoid copying (a `Vec3` is 12 bytes; a `World` is huge, and
  copying one is forbidden anyway).
- Use a **pointer** when "nothing" is a valid answer. `findChunk` returns `nullptr` for an
  unloaded chunk, and the caller *must* check: `if (Chunk* c = findChunk(cx, cz)) c->dirty = true;`.
  That `if` declares and tests the variable in one go.
- Use a **reference member** when one object uses another that outlives it: `Game` stores
  `Window& window_`. A reference cannot be null or be re-pointed, which documents "the game always
  has exactly this window".

**`this`** is a pointer to the current object. `return *this;` returns the object itself.
`CreateWindowExA(..., this)` hands the `Window`'s address to Windows (section 14).

**`->` vs `.`**: `chunk.cx` on an object or reference, `chunkPtr->cx` on a pointer
(`->` is shorthand for `(*chunkPtr).cx`).

> **Dangling references** are the main danger. A reference to a local variable that has gone
> out of scope points at garbage. `World::generateChunk` returns `Chunk&` safely only because
> the chunk lives on the heap, owned by the map, and stays put when the map grows (section 9).

---

## 8. `const` and `constexpr`

- `const` local: `const int lx = floorMod(x, CHUNK_SIZE);` cannot change after
  initialisation. The code marks most locals `const`, so the ones that *do* change stand out.
- `const` method: see section 5.
- **`const` overloads**: `World` has two `findChunk`s. The `const` one returns
  `const Chunk*` and is chosen automatically when called on a `const World&` (as in
  `Player::update`). The non-const one returns `Chunk*` for code that edits chunks.
- `constexpr`: a value (or function) computable **at compile time**:
  `constexpr int CHUNK_VOLUME = CHUNK_SIZE * CHUNK_SIZE * CHUNK_HEIGHT;`. It can size arrays
  (`std::array<BlockId, CHUNK_VOLUME>`) and costs nothing at run time. Prefer it to `#define`
  for constants: it has a type and obeys scope.

---

## 9. Ownership, RAII and smart pointers

**RAII** ("Resource Acquisition Is Initialisation") is the most important idea in C++: tie a
resource's lifetime to an object's lifetime. The constructor (or an `init` call) acquires it,
and the **destructor** releases it. Destructors run automatically when an object goes out of
scope, including on early `return`s.

| Class | Resource | Released in |
|---|---|---|
| `Shader` | GPU program object | `~Shader` → `glDeleteProgram` |
| `GpuMesh` | GPU vertex array + buffer | `~GpuMesh` → `release()` |
| `Renderer` | texture array, VAOs | `~Renderer` |
| `Hud` | font texture, VAO/VBO | `~Hud` |
| `Window` | window, device context, OpenGL context, cursor clip | `~Window` |
| `std::ofstream` (savefile.cpp) | open file | its destructor flushes and closes |

`saveWorld` uses an extra block `{ ... }` just to make the `ofstream` close **before** the file
is renamed:

```cpp
{
    std::ofstream out(temp, std::ios::binary);
    ...
}   // <- out is destroyed here: data flushed, file closed
std::filesystem::rename(temp, path, error);
```

**The copy problem and `= delete`.** If you could copy a `GpuMesh`, both copies would hold the
same buffer ID, and both destructors would delete it. Deleting it twice is a bug (the second time
might delete a *different* buffer that reused the number). So every resource-owning class
deletes its copy operations:

```cpp
GpuMesh(const GpuMesh&) = delete;              // no copy-construction
GpuMesh& operator=(const GpuMesh&) = delete;   // no copy-assignment
```

This is the **rule of three/five**: if a class needs a custom destructor, it almost certainly
needs to decide what copying (and moving) means as well.

**`std::unique_ptr<T>`** is a pointer that *owns* what it points to and deletes it
automatically. There is only ever one owner (copying is disallowed, moving is allowed).

```cpp
auto chunk = std::make_unique<Chunk>();      // allocate on the heap
...
chunks_[key(cx, cz)] = std::move(chunk);     // ownership moves into the map; `chunk` is now empty
```

Why are chunks heap-allocated through `unique_ptr`, not stored directly in the map?

1. **Size.** A chunk is 32 KB, too big to copy around casually.
2. **Stable addresses.** When an `unordered_map` grows, it may move its *elements*. With
   `unique_ptr` only the small pointer moves and the `Chunk` itself stays put, so a `Chunk&` or
   `const Chunk*` held elsewhere (the mesher's neighbourhood, the water list in the renderer)
   stays valid.
3. **Non-copyable member.** `Chunk` contains a `GpuMesh`, which cannot be copied.

`Game` holds `std::unique_ptr<World>` for a different reason. The world cannot be built until
`init()` knows the seed (from the save file or the command line), and `world_.reset()` in `~Game`
destroys it at a chosen moment.

**`std::move`** does not move anything by itself. It *casts* to an "rvalue reference", which
tells the receiving function that it may steal the contents. After `std::move(chunk)` the source
`unique_ptr` is null.

---

## 10. Standard library containers and algorithms

| Container | Where | Why this one |
|---|---|---|
| `std::array<T, N>` | `Chunk::blocks`, `ChunkNeighbourhood` | Fixed size known at compile time. No heap allocation. Knows its size, can be compared with `==` |
| `std::vector<T>` | vertex lists, offsets, pixels | Growable array, contiguous in memory (so it can be passed to `glBufferData`) |
| `std::unordered_map<K, V>` | chunks by key | Hash table: average O(1) lookup by chunk position |
| `std::string` | messages, shader source assembly | Text that owns its memory |
| `std::filesystem::path` | save and screenshot locations | Joins paths with `/`, handles Windows wide-character names |

**Reusing vectors to avoid allocations.** `World::meshScratch_` and the mesher's `water_` are
members, not locals. `clear()` empties a vector but **keeps its capacity**, so after the first few
chunks, meshing never calls the memory allocator.

**Erasing while iterating** (`World::update`):

```cpp
for (auto it = chunks_.begin(); it != chunks_.end();) {
    if (shouldDelete) { it = chunks_.erase(it); continue; }  // erase returns the next iterator
    ++it;
}
```

Calling `erase(it)` and then `++it` would use an iterator that no longer points at a valid
element: undefined behaviour, and a common crash.

**Algorithms used:**

| Call | Where | Does |
|---|---|---|
| `std::sort(begin, end, comparator)` | `World` (offsets), `Renderer` (water chunks) | sort by a rule you supply |
| `std::fill`, `std::copy` | `Window::processMessages`, hotbar | fill / copy a range |
| `std::iota`, `std::shuffle` | `PerlinNoise` | 0,1,2,...,255 then shuffle with a seeded generator |
| `std::min`, `std::max`, `std::clamp` | everywhere | |
| `std::begin(arr)`, `std::end(arr)`, `std::size(arr)` | game.cpp | work on plain C arrays too |

**Random numbers:** `std::random_device{}()` gives a truly random new-world seed. `std::mt19937`
(Mersenne Twister) is a *deterministic* generator: the same seed always gives the same sequence,
which is what makes worlds reproducible. Textures use an even simpler hand-written generator
(`Rng` in `textures.cpp`, xorshift32) to show there is no magic involved.

**Time:** `std::chrono::steady_clock` (in `World::update`) measures the meshing time budget.
"Steady" means it never jumps when the system clock is adjusted.

**Error codes instead of exceptions:** `std::filesystem` functions throw on failure unless you
pass a `std::error_code&`. The game uses the error-code overloads, so a failed save becomes a
message and never an uncaught exception.

---

## 11. Lambdas

A lambda is an unnamed function written inline:

```cpp
// world.cpp - sort offsets nearest-first
std::sort(offsets_.begin(), offsets_.end(), [](const Offset& a, const Offset& b) {
    return a.dx * a.dx + a.dz * a.dz < b.dx * b.dx + b.dz * b.dz;
});

// world.cpp - a little helper that reads local variables
auto outOfTime = [&]() {
    return std::chrono::duration<double>(Clock::now() - start).count() > budgetSeconds;
};
```

The `[...]` part is the **capture list**: which surrounding variables the lambda may use.

| Capture | Meaning | Example |
|---|---|---|
| `[]` | nothing | the sort comparator |
| `[&]` | everything, by reference | `outOfTime`, the `u16` / `u32` writers in `writeBmp` |
| `[this]` | the current object's members | `good` in `TerrainGenerator::findSpawn` |
| `[=]` | everything, by copy | (not used) |

Capturing by reference is cheap, but the lambda must not outlive the variables it refers to.
All lambdas here are used immediately, inside the function that created them, so that is safe.

---

## 12. Templates

`savefile.cpp` writes many different types the same way:

```cpp
template <typename T>
void writeValue(std::ofstream& out, const T& value) {
    out.write(reinterpret_cast<const char*>(&value), sizeof(T));
}

writeValue(out, VERSION);          // T = uint32_t: writes 4 bytes
writeValue(out, player.position.x);// T = float:    writes 4 bytes
```

The compiler **instantiates** a separate function for each `T` used. `std::vector<T>`,
`std::unique_ptr<T>` and `std::array<T, N>` are templates as well.

> **Careful:** writing raw bytes like this is only valid for "trivially copyable" types (plain
> numbers and simple structs). It would be wrong for `std::string` or `std::vector`, whose
> bytes are a pointer to memory elsewhere. It also bakes in this CPU's byte order
> (little-endian), which the file format documents.

---

## 13. Casts

| Cast | Use | Example |
|---|---|---|
| `static_cast<T>(x)` | Safe, checked conversions between related types: numbers, enums, `void*` → `T*` | `static_cast<float>(x)`, `static_cast<int>(std::floor(v))` |
| `reinterpret_cast<T>(x)` | "Treat these bits as another type". Pointer ↔ pointer, pointer ↔ integer | `reinterpret_cast<const char*>(&value)` (savefile), `reinterpret_cast<Window*>(GetWindowLongPtrA(...))` |
| `const_cast<T>(x)` | Remove `const` | not used (needing it usually means a design problem) |
| `(T)x` C-style | Tries all of the above, silently | avoided: it hides which kind of conversion happens |

**Function pointers via `void*`** (`gl.cpp`). `wglGetProcAddress` returns a generic function
pointer, `PROC`. Casting it directly to `PFNGLCREATESHADERPROC` triggers g++'s
`-Wcast-function-type` warning, because the signatures differ. Going through `void*` first
says "I know what I am doing" and keeps the build warning-free. It is conditionally supported
by the standard, and fully supported on Windows.

**Reading bytes is allowed; reading other types through the wrong pointer is not.** Viewing any
object as `char*` / `unsigned char*` is explicitly allowed (that is how `writeValue` works).
Viewing, say, a `float` through an `int*` breaks the *strict aliasing* rule, and the optimiser
may then produce wrong code.

---

## 14. Function pointers and callbacks

A **function pointer** holds the address of a function, so code can call "whichever function
this variable points to".

```cpp
// textures.cpp - a table of painter functions, in Tex:: order
using Painter = Tile (*)(Rng&);       // "pointer to a function taking Rng&, returning Tile"
const Painter PAINTERS[] = { grassTop, grassSide, dirt, ... };
const Tile tile = PAINTERS[layer](rng);
```

The OpenGL function pointers in `gl.h` are the same idea. `PFNGLCREATESHADERPROC` is a
`typedef` from `glext.h` for "pointer to a function with glCreateShader's signature".

**Callbacks** are functions *you* write that *someone else* calls:

- `Window::windowProc`: Windows calls it for every message.
- `openglDebugCallback` (window.cpp): the driver calls it on errors.

Windows calls `windowProc` as a plain function, not as a member of an object, so it must be a
`static` member (no hidden `this` parameter). To find *our* `Window` object, `CreateWindowEx`'s
last argument passes `this` along. The first message stores it in the window's "user data" slot,
and every later message reads it back:

```cpp
if (message == WM_NCCREATE) {
    auto* create = reinterpret_cast<CREATESTRUCTA*>(lParam);
    self = static_cast<Window*>(create->lpCreateParams);
    SetWindowLongPtrA(hwnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(self));
} else {
    self = reinterpret_cast<Window*>(GetWindowLongPtrA(hwnd, GWLP_USERDATA));
}
return self->handleMessage(message, wParam, lParam);   // back in normal member-function land
```

This "static trampoline" pattern appears whenever a C API needs to call C++ objects.

**`CALLBACK` / `APIENTRY`** mark the *calling convention* (how arguments are passed in
registers and on the stack). On 64-bit Windows there is only one convention, so they change
nothing, but they are required for 32-bit builds and are good form.

---

## 15. `static`: the keyword with four meanings

| Where | Meaning | Example |
|---|---|---|
| Inside a function | Created **once**, on first use, and kept between calls | `static HMODULE opengl32 = LoadLibraryA("opengl32.dll");` in `getOpenGLProc` |
| Member function | Belongs to the class, no `this` | `Chunk::index`, `Window::windowProc` |
| Member variable | One shared copy for the whole class | `static constexpr float WIDTH` |
| Namespace-scope variable/function | Private to this `.cpp` (old style) | replaced by unnamed namespaces here |

Function-local statics are initialised in a thread-safe way since C++11. Use them sparingly:
they are hidden global state.

---

## 16. Raw string literals

GLSL shaders live inside the C++ files as text:

```cpp
const char* LINE_FRAGMENT = R"(#version 330 core
uniform vec4 uColor;
out vec4 fragColor;
void main() { fragColor = uColor; }
)";
```

`R"( ... )"` is a **raw string literal**. Newlines and quotes inside it are kept literally, and
backslashes need no escaping. Without it, every line would need `"...\n"`. `Renderer::init`
joins strings with `std::string` + to prepend `#version` and paste the shared `skyColor`
function into two shaders.

---

## 17. Compile-time checks: `static_assert`

```cpp
static_assert(sizeof(BlockVertex) == 16, "BlockVertex must be 16 tightly packed bytes");
static_assert(sizeof(BLOCKS) / sizeof(BLOCKS[0]) == Block::Count, "BLOCKS needs one row per Block:: id");
static_assert(sizeof(PAINTERS) / sizeof(PAINTERS[0]) == Tex::Count, "PAINTERS needs one entry per Tex:: layer");
```

These are checked by the compiler and cost nothing at run time. The first protects the GPU
vertex layout (the shader reads exactly 16 bytes per vertex). The other two catch the mistake of
adding a block or texture to the enum but forgetting its table row.

`BLOCKS` is declared `extern const BlockInfo BLOCKS[];`, with **no size**, on purpose. With
`BLOCKS[Block::Count]` the array would always have `Count` elements: missing rows would be
silently zero-filled, and the assert could never fail.

---

## 18. Floating-point pitfalls

- **Truncation vs floor.** `(int)-0.5f` is `0`, but the block containing x = -0.5 is block -1.
  Always use `floorToInt` (`std::floor` then convert) for world → block conversion.
- **Never test floats for exact equality after arithmetic.** The physics compares with a small
  tolerance `EPS = 0.001f` (`player.cpp`), so standing exactly on a block counts as touching,
  not overlapping.
- **Precision shrinks far from the origin.** A `float` has about 7 significant digits. At
  x = 1,000,000, the smallest step is 0.06 blocks, and movement and rendering get jittery.
  Big engines store positions relative to a nearby origin.
- **Infinity is a usable value.** The raycast sets `tMax = INFINITY` for an axis the ray never
  crosses. Every comparison then correctly says "never".
- **Frame-rate-independent smoothing** uses `std::exp`: `blend = 1 - exp(-rate * dt)`. Applying
  it twice with `dt/2` gives the same result as once with `dt`, which a plain
  `value += (target - value) * 0.1f` does not ([09](09-physics-and-raycasting.md)).
- `std::lround` rounds to the nearest integer (halves away from zero) and returns `long`, used
  when converting shade and colour values to bytes.

---

## 19. `extern "C"` and DLL exports

```cpp
// main.cpp
extern "C" {
__declspec(dllexport) unsigned long NvOptimusEnablement = 1;
__declspec(dllexport) int AmdPowerXpressRequestHighPerformance = 1;
}
```

C++ **mangles** names: it encodes types into symbol names so that overloaded functions can
coexist. `extern "C"` turns mangling off, so the symbol is literally `NvOptimusEnablement`.
`__declspec(dllexport)` lists it in the `.exe`'s export table. NVIDIA's and AMD's drivers look
for these exact names when a program starts, and choose the powerful GPU if they find them.

---

## 20. Undefined behaviour this code avoids

*Undefined behaviour* (UB) means the standard makes no promise: the program may work, crash, or
misbehave only with `-O2`. These are the kinds of UB this code guards against:

| UB | Where it could happen | How it is avoided |
|---|---|---|
| Out-of-bounds array index | `BLOCKS[id]` with a corrupt id from a file | `loadWorld` rejects any byte `>= Block::Count` (and a test checks it) |
| Signed integer overflow | hash functions | all hashing in `uint32_t` |
| Using an invalidated iterator | erasing chunks in a loop | `it = erase(it)` |
| Dangling pointer / reference | chunks referenced while the map changes | chunks live behind `unique_ptr` |
| Double free | copying resource owners | copy operations deleted |
| Reading uninitialised memory | `Tile px` in textures | every painter writes every pixel before it is read |
| Calling OpenGL after the context is gone | shutdown order | `Game` destroyed in an inner scope before `Window` |

When in doubt, build the debug version and compile with `-fsanitize=undefined` on Linux, or use
the `_GLIBCXX_ASSERTIONS` define with MinGW. It makes the standard library check indices:
`g++ -D_GLIBCXX_ASSERTIONS ...`.

---

## 21. Naming conventions used here

| Kind | Style | Example |
|---|---|---|
| Types (class, struct, alias) | `PascalCase` | `ChunkMesher`, `BlockVertex`, `BlockId` |
| Functions, variables | `camelCase` | `surfaceHeight`, `opaqueQuads` |
| Private member variables | `camelCase_` (trailing underscore) | `chunks_`, `viewDistance_` |
| Compile-time constants | `UPPER_SNAKE_CASE` | `CHUNK_SIZE`, `SEA_LEVEL`, `AO_CURVE` |
| Enum values | `PascalCase` in a namespace | `Block::Grass`, `Tex::LogSide` |
| Shader inputs | `aName` (attribute), `vName` (varying), `uName` (uniform) | `aPosition`, `vShade`, `uViewProj` |

The trailing underscore lets a method say `width_` (member) and `width` (parameter or accessor)
without confusion. The shader prefixes tell you at a glance where a GLSL value comes from:
vertex data, the previous shader stage, or C++.
