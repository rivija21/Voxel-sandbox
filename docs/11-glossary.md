# 11 - Glossary

Every technical term used in this guide, with the chapter that explains it.

| Term | Meaning | See |
|---|---|---|
| **AABB** | Axis-aligned bounding box: a box whose edges are parallel to the axes. The player and every block are AABBs | 09 |
| **Aggregate initialisation** | Filling a struct's fields in order with braces: `Vec3{1, 2, 3}` | 03 |
| **Alpha** | The 4th colour channel: opacity. 255 = solid, 0 = invisible | 05, 08 |
| **Alpha blending** | Mixing a new colour with the one already on screen by its alpha (water) | 05 |
| **Alpha testing / cut-out** | Throwing a pixel away (`discard`) when its alpha is low (leaves, glass) | 05 |
| **Ambient occlusion (AO)** | Darkening corners and creases that receive less sky light | 08 |
| **Anisotropic filtering** | Texture filtering that stays sharp when a surface is seen at a grazing angle | 05 |
| **API** | Application programming interface: the functions a library or OS offers (Win32, OpenGL) | 04, 05 |
| **Aspect ratio** | Width / height of the window | 06 |
| **Back buffer** | The hidden image being drawn; swapped to the screen when finished | 02, 05 |
| **Back-face culling** | Skipping triangles that face away from the camera | 05, 08 |
| **Base vertex** | A number added to every index in a draw call | 05 |
| **Binding point** | A named slot (`GL_ARRAY_BUFFER` ...) that the next OpenGL call works on | 05 |
| **BMP** | A simple, uncompressed image format: used for screenshots | 05 |
| **Callback** | A function you write that some other code (Windows, the driver) calls | 03, 04 |
| **Chunk** | A 16 x 128 x 16 column of blocks: the unit of loading, meshing and drawing | 07 |
| **Clip space** | Coordinates output by the vertex shader, before the divide by w | 06 |
| **Column-major** | Matrix storage order, column after column (OpenGL's) | 06 |
| **Context (OpenGL)** | The driver's whole state for one program; owns every GPU object | 05 |
| **Cross product** | A vector perpendicular to two others | 06 |
| **DDA** | "Digital differential analyser": stepping through a grid along a line; the raycast algorithm | 09 |
| **Declaration / definition** | "This exists" vs "here it is" | 03 |
| **Depth buffer / depth test** | Per-pixel distance; keeps only the nearest surface | 05 |
| **Determinism** | Same inputs always give the same outputs; why worlds are reproducible from a seed | 07 |
| **Device context (HDC)** | A Windows handle for something you can draw into | 04 |
| **Dot product** | `a.x*b.x + a.y*b.y + a.z*b.z` = `|a||b|cos θ`: how much two directions agree | 06 |
| **Double buffering** | Drawing into a hidden buffer and swapping, to avoid showing half-drawn frames | 02 |
| **Draw call** | One `glDraw*` command | 05 |
| **dt (delta time)** | Seconds since the previous frame | 02 |
| **EBO / index buffer** | A buffer listing which vertices form each triangle | 05 |
| **EPS (epsilon)** | A tiny tolerance for comparing floating-point numbers | 03, 09 |
| **fbm** | Fractal Brownian motion: several octaves of noise added together | 07 |
| **Fog** | Fading distant surfaces into the sky colour; hides where the world ends | 05 |
| **Fragment / fragment shader** | A candidate pixel / the GPU program that colours it | 05 |
| **Frustum / frustum culling** | The visible volume / skipping chunks outside it | 06 |
| **GDI** | Windows' old 2D drawing API; used to render the HUD font | 05 |
| **GLSL** | The OpenGL Shading Language | 05 |
| **Gradient noise** | Noise built from random directions at grid points (Perlin) | 07 |
| **Greedy meshing** | Merging neighbouring identical faces into big rectangles | 08, 10 |
| **Handle** | An opaque number standing for an OS object (`HWND`, `HDC` ...) | 04 |
| **Hash function** | Turns inputs into a scrambled, repeatable number | 07 |
| **Homogeneous coordinates** | Adding w (1 for points, 0 for directions) so matrices can also translate | 06 |
| **Hysteresis** | Different thresholds for "on" and "off" to stop flickering back and forth (chunk load/unload distances) | 07 |
| **Import library** | A `.a` file that tells the linker which DLL a function lives in | 01 |
| **Interpolation** | Blending values smoothly between known points (vertex → pixel, noise corners) | 05, 06 |
| **Lambda** | An unnamed inline function: `[&](int x) { ... }` | 03 |
| **Linker** | Joins object files and libraries into the `.exe` | 01 |
| **Little-endian** | Storing the lowest byte of a number first (x86); matters for files and pixel packing | 03, 07 |
| **Mesh / meshing** | Triangles for the GPU / turning blocks into them | 08 |
| **Message loop** | The loop that takes Windows messages from the queue and dispatches them | 04 |
| **Mipmap** | Pre-shrunk copies of a texture used for distant surfaces | 05 |
| **NDC** | Normalised device coordinates: -1..1 on every axis after the perspective divide | 06 |
| **Normal** | A unit vector perpendicular to a surface; for blocks, which face was hit | 06, 09 |
| **Object file (.o)** | Compiled machine code for one `.cpp`, with holes for the linker | 01 |
| **ODR** | One Definition Rule: each non-inline thing is defined once in the program | 03 |
| **Octave** | One layer of noise at a particular scale in fbm | 07 |
| **Opaque** | Blocks that fully hide what is behind them | 08 |
| **Orthographic projection** | Projection without perspective; used for the HUD | 06 |
| **Perlin noise** | Smooth pseudo-random noise (Ken Perlin) | 07 |
| **Perspective projection** | Projection where distant things look smaller | 06 |
| **Pixel format** | The colour/depth/stencil layout of the window's framebuffer | 05 |
| **Preprocessor** | The pass that handles `#include` and `#define` before compiling | 01, 03 |
| **RAII** | Tying a resource's lifetime to an object's lifetime (destructor releases) | 03 |
| **Rasteriser** | GPU hardware that finds which pixels a triangle covers | 05 |
| **Raw input** | Mouse movement straight from the device, without acceleration | 04 |
| **Raw string literal** | `R"( ... )"`: text with no escaping; used for shaders | 03 |
| **Raycast** | Following a line through the world to see what it hits | 09 |
| **Reference** | An alias for an existing object: `T&` | 03 |
| **Render pass** | One group of draws with the same settings (sky, solid, water, HUD) | 05 |
| **Ridged noise** | `1 - |noise|`: sharp crests, for mountain ridges | 07 |
| **Sampler** | A GLSL handle for reading a texture (= a texture unit number) | 05 |
| **Seed** | The number that a whole world is generated from | 07 |
| **Shader / program** | A GPU program / a linked vertex + fragment shader pair | 05 |
| **smoothstep** | An S-shaped 0→1 ramp with flat ends | 06 |
| **static_assert** | A check done by the compiler | 03 |
| **Streaming** | Loading and unloading parts of the world as the player moves | 07 |
| **Texture array** | Many same-sized images in one texture, picked by a layer index | 05 |
| **Texture unit** | A numbered slot that a texture is bound to for shaders to read | 05 |
| **Time budget** | Limiting expensive work to a few milliseconds per frame | 07 |
| **Translation unit** | One `.cpp` after all its `#include`s are pasted in | 01 |
| **Tunnelling** | Passing through a thin wall because one step was too big | 09 |
| **UB (undefined behaviour)** | Code the C++ standard makes no promises about | 03 |
| **Uniform** | A shader input that is the same for a whole draw call, set from C++ | 05 |
| **unique_ptr** | A smart pointer with a single owner; deletes its object automatically | 03 |
| **UV / texture coordinates** | Which point of a texture maps to a vertex (0..1) | 05, 08 |
| **VAO** | Vertex array object: remembers how to read vertex buffers | 05 |
| **VBO** | Vertex buffer object: vertex data in GPU memory | 05 |
| **Vertex / vertex shader** | A corner of a triangle / the GPU program that positions it | 05 |
| **Viewport** | The window rectangle that NDC -1..1 is mapped onto | 06 |
| **Virtual-key code** | Windows' number for a key, by its label (`'W'`, `VK_SPACE`) | 04 |
| **V-sync** | Waiting for the monitor's refresh before showing a frame | 04 |
| **Voronoi** | Dividing space by "nearest of several points"; the cobblestone texture | 08 |
| **Voxel** | "Volume pixel": a value on a 3D grid; here, a block | 07 |
| **WGL** | The Windows-specific functions that connect OpenGL to windows (`wglCreateContext` ...) | 05 |
| **Winding order** | Whether a triangle's corners go clockwise or counter-clockwise on screen | 05, 08 |
| **Window procedure** | The function Windows calls with every message for a window | 04 |
| **X-macro** | One list macro expanded several times with different meanings | 03 |
| **Z-fighting** | Flicker when two surfaces have nearly equal depth | 06 |
