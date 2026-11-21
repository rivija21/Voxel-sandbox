# 05 - The OpenGL pipeline

Files: `gl.h/.cpp`, `window.cpp` (context creation), `shader.h/.cpp`, `gpu_mesh.h/.cpp`,
`renderer.h/.cpp`, `hud.cpp`, `textures.cpp`.

This chapter explains how the program gets a graphics card to draw a 3D world: the pipeline
every triangle passes through, the objects OpenGL uses to hold data, the shaders, and the order
of the render passes.

---

## 1. What OpenGL is

OpenGL is a **specification**: a long list of C functions (`glClear`, `glDrawElements` ...) and
what they must do. Your **graphics driver** (NVIDIA's, for your RTX 4050) implements them. Your
program calls functions; the driver turns them into commands for the GPU.

- **A state machine.** Most calls change some current setting ("the bound buffer is now 7",
  "depth testing is now on"), and later calls use those settings. Bugs often come from state
  left over by an earlier piece of code. That is why each render pass in `renderer.cpp` sets
  everything it relies on.
- **Core profile 3.3.** Version 3.3 (2010) is the baseline every PC GPU from the last ~15 years
  supports. The "core profile" removes the old 1990s fixed-function API (`glBegin`/`glEnd`, the
  built-in lights), which forces the modern approach of buffers and shaders.
- **Asynchronous.** Most calls only queue work. The GPU runs it later, in parallel with your C++
  code. `SwapBuffers` and `glReadPixels` are among the few calls that wait for the GPU to finish.

---

## 2. The pipeline

What happens to a triangle on its way to the screen:

```
 vertex data in a buffer (VBO)            BlockVertex: position, u, v, layer, shade
        │
        ▼
 ┌───────────────┐   runs once per vertex           your code (GLSL)
 │ VERTEX SHADER │   output: gl_Position (clip space) + values for the next stage
 └───────────────┘
        │
        ▼
 primitive assembly   groups vertices into triangles (using the index buffer)
 clipping            cuts away parts outside the view
 perspective divide  clip space -> normalised device coordinates (-1..1)
 viewport transform  -> pixel coordinates in the window
 face culling        drops triangles seen from behind
        │
        ▼
 RASTERISER          finds every pixel the triangle covers; INTERPOLATES the
        │            vertex shader's outputs across the triangle
        ▼
 ┌─────────────────┐ runs once per covered pixel ("fragment")   your code (GLSL)
 │ FRAGMENT SHADER │ output: a colour (or `discard` = draw nothing)
 └─────────────────┘
        │
        ▼
 depth test          is this fragment nearer than what is already stored?
 blending            mix with the colour already there (water)
        │
        ▼
 framebuffer (the back buffer)  ── SwapBuffers ──►  monitor
```

Only the two shaders are programmable. Everything else is fixed hardware that you switch on and
off (`glEnable(GL_DEPTH_TEST)`, `glEnable(GL_CULL_FACE)`, `glEnable(GL_BLEND)`).

**Interpolation** is the key idea. The vertex shader outputs `vShade` at the three corners of
a triangle, and the fragment shader receives a smoothly *blended* `vShade` for every pixel in
between. That is how per-corner ambient occlusion becomes a smooth gradient ([08](08-meshing-and-lighting.md)).

---

## 3. Getting an OpenGL 3.3 context on Windows

Windows' own OpenGL support stopped at version 1.1 (1997). Anything newer has to be requested
through an *extension*, which is why `Window::createContext` takes five steps:

1. **Pixel format.** Describe the framebuffer you want (32-bit colour, 24-bit depth buffer,
   8-bit stencil, double-buffered). `ChoosePixelFormat` picks the closest match, and
   `SetPixelFormat` applies it. A window's pixel format can be set **only once**.
2. **A temporary legacy context** (`wglCreateContext`). It is needed for one thing: with a
   context current, `wglGetProcAddress("wglCreateContextAttribsARB")` works.
3. **The real context.** `wglCreateContextAttribsARB` with attributes "version 3.3, core
   profile" (plus "debug" if `--gldebug`). The legacy context is then deleted.
4. **Load the functions.** `loadOpenGLFunctions` (gl.cpp) asks the driver for the address of
   every function newer than 1.1. See the X-macro in [03 §2](03-cpp-semantics.md).
5. **V-sync** with `wglSwapIntervalEXT(1)`, plus the debug callback if requested.

Libraries like GLFW, SDL and GLAD do exactly these steps inside. Here you can read them.

**Why can't we just call `glCreateShader`?** `opengl32.dll` only *exports* the 1.1 functions.
The rest live inside the driver's own DLL, whose functions are found at run time. `gl.cpp`
declares a function-pointer *variable* named `glCreateShader`, so calling it looks like calling
the real function.

---

## 4. OpenGL objects

OpenGL hides its data behind numbered **objects**. Every kind follows the same pattern:

```cpp
GLuint id;
glGenBuffers(1, &id);                 // 1. create: get a new number
glBindBuffer(GL_ARRAY_BUFFER, id);    // 2. bind: make it "the current array buffer"
glBufferData(GL_ARRAY_BUFFER, ...);   // 3. operate on "the current array buffer"
glDeleteBuffers(1, &id);              // 4. delete when done (in a destructor)
```

| Object | Created in | Holds |
|---|---|---|
| **Buffer** used as VBO (vertex buffer) | `GpuMesh::upload`, `Hud::init`, `Renderer::init` | vertex data in GPU memory |
| **Buffer** used as EBO (element/index buffer) | `ensureQuadIndices` (gpu_mesh.cpp) | which vertices form each triangle |
| **Vertex array (VAO)** | `GpuMesh::upload` ... | *how to read* the buffers: the attribute layout, plus which EBO to use |
| **Texture** | `Renderer::init` (block array), `Hud::createFontTexture` | images |
| **Shader** / **Program** | `Shader::build` | compiled GLSL |

**Binding points.** `GL_ARRAY_BUFFER`, `GL_ELEMENT_ARRAY_BUFFER`, `GL_TEXTURE_2D_ARRAY` ... are
like named slots on a workbench. A buffer is just bytes: the slot you bind it to decides how the
next call uses it.

> **The trap this code avoids** (gpu_mesh.cpp, `ensureQuadIndices`): the
> `GL_ELEMENT_ARRAY_BUFFER` binding is **stored inside the currently bound VAO**. Binding a
> buffer there "just to upload some data" silently changes whichever VAO happens to be bound.
> The shared index buffer is therefore uploaded through the `GL_ARRAY_BUFFER` slot, which is
> *not* part of VAO state.

---

## 5. Describing vertices

A chunk's vertex buffer is a packed array of `BlockVertex`, 16 bytes each:

```
byte:  0   4   8   12  13  14  15
      [ x ][ y ][ z ][u][v][L][S]      L = texture layer, S = shade
```

The VAO is told how to read it, once, when the mesh is first uploaded:

```cpp
glEnableVertexAttribArray(0);
glVertexAttribPointer(0,            // attribute location: matches "layout(location = 0)" in the shader
                      3, GL_FLOAT,  // 3 floats
                      GL_FALSE,     // (normalise: only meaningful for integers)
                      sizeof(BlockVertex),                 // stride: bytes from one vertex to the next
                      (void*)offsetof(BlockVertex, x));    // offset of the first one

glEnableVertexAttribArray(1);
glVertexAttribPointer(1, 4, GL_UNSIGNED_BYTE,
                      GL_FALSE,     // NOT normalised: the shader sees 0..255 as floats
                      sizeof(BlockVertex), (void*)offsetof(BlockVertex, u));
```

In the shader:

```glsl
layout(location = 0) in vec3 aPosition;
layout(location = 1) in vec4 aData;   // (u, v, layer, shade), each 0.0 .. 255.0
```

The HUD uses `GL_TRUE` (normalised) for its colour bytes instead, so `255` arrives as `1.0`.
Both are useful: non-normalised for small integers like a layer number, normalised for colours.

**Why pack so tightly?** The F3 overlay at spawn showed 150,000 quads in 92 chunks, about 1,600
per chunk. Across the ~200 chunks within view distance that is ~320,000 quads, or 1.3 million
vertices: 20 MB at 16 bytes each, but 46 MB with plain floats for everything (9 x 4 = 36
bytes). Memory size and bandwidth are usually what limit voxel games, so packing pays off.

---

## 6. Index buffers, the shared quad pattern and base vertex

Every face is a quad: 4 vertices, drawn as 2 triangles. Without indices you would store 6
vertices per quad, duplicating 2. With an **index buffer** you store the 4 unique vertices once
and list `0,1,2, 0,2,3` to say which ones form each triangle.

Since the index pattern is identical for every quad of every chunk
(`4q, 4q+1, 4q+2, 4q, 4q+2, 4q+3`), the project keeps **one** index buffer shared by all chunks
(`gpu_mesh.cpp`). It grows by doubling when a chunk needs more quads than it holds.

Drawing:

```cpp
glDrawElements(GL_TRIANGLES, opaqueQuads * 6, GL_UNSIGNED_INT, nullptr);   // the solid part
glDrawElementsBaseVertex(GL_TRIANGLES, waterQuads * 6, GL_UNSIGNED_INT, nullptr,
                         opaqueQuads * 4);                                 // the water part
```

A chunk's buffer holds solid quads first, then water quads. **Base vertex** adds
`opaqueQuads * 4` to every index, so the same shared `0,1,2,...` indices point into the water
section. One buffer and one VAO per chunk serve both passes.

---

## 7. Shaders

Shaders are small programs in **GLSL**, a C-like language. They are compiled at run time by the
driver: `Shader::build` calls `glShaderSource` → `glCompileShader` → `glAttachShader` →
`glLinkProgram`. If there is a mistake, it shows the driver's error log in a message box.

**GLSL essentials:**

| Thing | Example | Meaning |
|---|---|---|
| vector types | `vec2`, `vec3`, `vec4` | float vectors; `.xyz`, `.rgb`, `.w` pick components ("swizzling") |
| matrix | `mat4` | 4x4 float matrix; `matrix * vector` transforms |
| sampler | `sampler2D`, `sampler2DArray` | a handle for reading a texture with `texture(...)` |
| `in` (vertex shader) | `in vec3 aPosition` | per-vertex data from the buffer |
| `out` → `in` | `out float vShade` → `in float vShade` | passed from vertex to fragment shader, interpolated |
| `uniform` | `uniform mat4 uViewProj` | same for every vertex/pixel in a draw call; set from C++ |
| built-ins | `gl_Position`, `gl_VertexID` | special inputs/outputs |
| `discard` | | stop: this fragment draws nothing |

### The block shader (renderer.cpp)

```glsl
// vertex shader
void main() {
    vTexCoord = aData.xyz;                 // (u, v, layer)
    vShade = aData.w / 255.0;              // 0..1
    vWorldPos = aPosition;                 // for the fog distance
    gl_Position = uViewProj * vec4(aPosition, 1.0);   // world -> clip space (06-3d-math)
}

// fragment shader
void main() {
    vec4 texel = texture(uTextures, vTexCoord);       // look up the pixel-art texture
    if (texel.a < 0.5) discard;                       // holes in leaves and glass
    vec3 color = texel.rgb * vShade;                  // darken by face direction + AO
    vec3 toPixel = vWorldPos - uCameraPos;
    float fog = smoothstep(uFogStart, uFogEnd, length(toPixel));
    color = mix(color, skyColor(normalize(toPixel)), fog);   // fade into the sky behind it
    fragColor = vec4(color, uAlpha);                  // uAlpha: 1 for solid, 0.72 for water
}
```

### The sky shader

The sky is a single triangle that covers the whole screen. It needs **no vertex buffer**: the
vertex shader makes the corners from `gl_VertexID` (0, 1, 2):

```glsl
vec2 p = vec2((gl_VertexID << 1) & 2, gl_VertexID & 2) * 2.0 - 1.0;   // (-1,-1), (3,-1), (-1,3)
```

That triangle is twice the size of the screen, and the clipper cuts it to exactly the screen
rectangle. (Using one triangle rather than two avoids a diagonal seam, and it is slightly
faster.) Core OpenGL still insists that *some* VAO is bound, so an empty one (`emptyVao_`) is.

The fragment shader rebuilds the 3D direction each pixel looks in from the camera's axes and the
field of view, then colours it with `skyColor(dir)`: a blend from horizon colour to zenith colour
by height, plus a warm glow and a disc around the sun direction.

### Shared GLSL

The block shader's fog uses the *same* `skyColor` function as the sky, so distant hills fade into
exactly the colour of the sky behind them, including the glow near the sun. GLSL 3.30 has no
`#include`, so `Renderer::init` pastes the function into both sources with `std::string`
concatenation, after the required `#version` line.

### Uniforms from C++

```cpp
blockShader_.use();                                        // glUseProgram
blockShader_.setMat4("uViewProj", camera.viewProjection);  // glUniformMatrix4fv
blockShader_.setFloat("uAlpha", 1.0f);                     // glUniform1f
```

`setX` looks up the uniform's *location* by name with `glGetUniformLocation` each time.
That is simple, and fine for a few dozen calls per frame (caching locations is exercise 10.11).

> **Silent failure:** a misspelt uniform name gives location **-1**, and setting location -1 is
> silently ignored. The same happens if the shader compiler *removed* a uniform because it was
> unused. If a uniform "does nothing", check its spelling first.

**Samplers are texture unit numbers.** `blockShader_.setInt("uTextures", 0)` means "read from
texture unit 0". `glActiveTexture(GL_TEXTURE0); glBindTexture(...)` puts a texture into that
unit. The HUD shader uses two units: blocks on 0 and the font on 1.

---

## 8. Textures

### The block texture array

The 15 block pictures (16 x 16 each, painted by `textures.cpp`) are uploaded as **one 2D texture
array** with 15 layers:

```cpp
glTexImage3D(GL_TEXTURE_2D_ARRAY, 0, GL_RGBA8, 16, 16, Tex::Count, 0, GL_RGBA, GL_UNSIGNED_BYTE, pixels);
glGenerateMipmap(GL_TEXTURE_2D_ARRAY);
```

The shader reads it with a 3D coordinate: `(u, v, layer)`.

**Why an array and not one big "atlas" image?** Many voxel games pack all textures into one
large image. That causes *bleeding*: when the GPU samples near a tile's edge, or uses a smaller
mipmap, it blends in pixels from the neighbouring tile, and thin coloured lines appear at block
edges. Each array layer is a separate image, so this cannot happen.

### Filtering and mipmaps

| Setting | Value | Why |
|---|---|---|
| `MAG_FILTER` (texture magnified, i.e. up close) | `GL_NEAREST` | crisp square pixels; `GL_LINEAR` would blur the pixel art |
| `MIN_FILTER` (texture shrunk, far away) | `GL_NEAREST_MIPMAP_LINEAR` | use pre-shrunk *mipmaps* to avoid shimmering |
| `WRAP_S/T` | `GL_REPEAT` | |
| anisotropy | up to 8x if the driver supports it | keeps ground sharp at grazing angles |

**Mipmaps** are copies of the texture at 1/2, 1/4, 1/8 ... size (16x16, 8x8, 4x4, 2x2, 1x1),
made by `glGenerateMipmap`. A distant block covers maybe 2 pixels of screen. Sampling the full
16x16 texture there picks one random texel per frame, which flickers as you move. A pre-averaged
2x2 version gives a stable colour.

### The font texture

`Hud::createFontTexture` draws the 96 printable ASCII characters with Windows **GDI**
(`TextOutA`, Consolas Bold) into an off-screen bitmap, then copies one channel into a
single-channel `GL_R8` texture. The fragment shader uses it as **coverage**: how much of each
pixel the letter covers, multiplied into the text colour's alpha.

```cpp
glPixelStorei(GL_UNPACK_ALIGNMENT, 1);   // default is 4: each row padded to 4 bytes
```

OpenGL assumes by default that each image row starts on a 4-byte boundary. A 1-byte-per-pixel
image whose width is not a multiple of 4 would otherwise come out sheared diagonally. This
default catches everyone at least once.

---

## 9. Depth testing and face culling

**The depth buffer** stores, for every pixel, how far away the nearest thing drawn there is. With
`glEnable(GL_DEPTH_TEST)` and `glDepthFunc(GL_LESS)`, a new fragment is kept only if it is
*nearer* than the stored one. Chunks can therefore be drawn in any order, and nearer blocks still
hide farther ones. The buffer is reset to "infinitely far" each frame by
`glClear(GL_DEPTH_BUFFER_BIT)`.

**Face culling.** A solid block's faces that point away from you can never be seen.
`glEnable(GL_CULL_FACE)` + `glCullFace(GL_BACK)` makes the GPU skip triangles whose corners appear
**clockwise** on screen. That only works because the mesher lists every face's corners
**counter-clockwise as seen from outside** ([08](08-meshing-and-lighting.md)). Get one face's
winding wrong and that face becomes invisible from outside and visible from inside, a classic
bug.

---

## 10. Transparency: the two kinds

| Kind | Used for | How | Order matters? |
|---|---|---|---|
| **Alpha testing** (cut-out) | leaves, glass | `if (texel.a < 0.5) discard;`, so a pixel is fully drawn or not at all | No: depth testing still works |
| **Alpha blending** (see-through) | water | `glEnable(GL_BLEND)`, `glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA)`: new = 0.72 x water + 0.28 x what is behind | **Yes** |

Blending mixes with whatever is **already** in the framebuffer, so everything behind the water
must be drawn first. Hence the pass order:

1. All solid geometry (writes depth).
2. Water, **sorted far to near** by chunk, with **depth writes off** (`glDepthMask(GL_FALSE)`)
   so water surfaces never hide other water behind them. The depth *test* stays on, so water
   behind a hill is still hidden.
3. Face culling off for water, so you can see the surface from below when swimming.

Sorting by chunk is not perfect (two overlapping water faces inside one chunk can blend in the
wrong order), but with one flat colour of water it is impossible to notice. Proper fixes are
"order-independent transparency" techniques, well beyond this project.

---

## 11. One frame's render passes (Renderer::drawWorld)

| # | Pass | Depth test | Depth write | Cull | Blend | Shader |
|---|---|---|---|---|---|---|
| 1 | `glClear(GL_DEPTH_BUFFER_BIT)` | | | | | |
| 2 | Sky (full-screen triangle) | off | off | | off | sky |
| 3 | Solid chunks, frustum-culled | on, `LESS` | on | back | off | block, `uAlpha = 1` |
| 4 | Outline of the target block (`GL_LINES`) | on, `LEQUAL` | on | | on | line |
| 5 | Water chunks, far to near | on | **off** | **off** | on | block, `uAlpha = 0.72` |
| 6 | HUD: one batched draw call (`Hud::end`) | off | | off | on | hud |

No colour clear is needed because the sky covers every pixel. The outline uses `GL_LEQUAL` and
is drawn 0.003 blocks outside the cube, so its lines are not hidden by the block's own faces.

**How many draw calls?** With view distance 8, about 200 chunks are loaded, and frustum culling
leaves 60-100 of them in view (the test screenshots showed 66-92). That makes roughly 100 solid
draws, plus water, outline, sky and HUD: easy for any GPU. Press **F3** to see the live counts.

---

## 12. Reading pixels back: screenshots

`Game::takeScreenshot` calls `glReadPixels` on the back buffer *before* `SwapBuffers`:

```cpp
glReadPixels(0, 0, w, h, GL_BGR, GL_UNSIGNED_BYTE, pixels.data());
```

OpenGL returns rows **bottom-up**, 4-byte aligned, which happens to be exactly the BMP file
layout (BMP is bottom-up too, padded to 4 bytes per row, in BGR order). So `writeBmp` writes a
54-byte header followed by the pixels unchanged. Formats like PNG need compression, which would
need a library or a few hundred more lines.

---

## 13. Debugging OpenGL

| Problem | Likely cause |
|---|---|
| Black screen, no errors | Nothing bound: VAO, program or texture; the camera looking the wrong way; depth never cleared |
| Geometry is a mess of spikes | Wrong stride or offset in `glVertexAttribPointer`; vertex struct layout changed |
| One kind of face is missing | Wrong winding (face culling removes it) |
| Texture is black | Mipmap filter set but no mipmaps generated (the texture is "incomplete"); wrong texture unit |
| A uniform has no effect | Misspelt name (location -1), or the shader optimised it away |
| Works on NVIDIA, broken on Intel | GLSL that one driver accepts and another rejects: test with `--gldebug` |

Run `.\build.bat debug` then `.\voxel-sandbox-debug.exe --gldebug`. The driver prints
`[OpenGL ERROR] ...` in the console for any invalid call, and with
`GL_DEBUG_OUTPUT_SYNCHRONOUS` you can put a gdb breakpoint in `openglDebugCallback` and type
`bt` to see which line caused it.

For deeper digging, **RenderDoc** (free) captures a whole frame and lets you inspect every draw
call, buffer, texture and shader variable.

---

## Further reading

- *LearnOpenGL* (learnopengl.com): the best free tutorial; same OpenGL 3.3 core concepts.
- The OpenGL 3.3 core specification and the GLSL 3.30 specification (khronos.org): the final
  word when tutorials disagree.
- docs.gl: every function with examples, filterable by version.
