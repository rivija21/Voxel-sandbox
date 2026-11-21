# 06 - 3D maths (`mathlib.h`, `renderer.cpp: makeCamera`, `player.cpp`)

You need surprisingly little maths to make a 3D game: vectors, one kind of matrix, and a handful
of formulas. This chapter derives each one used in the code and ends with a worked example that
follows one point from the world all the way to a pixel.

---

## 1. The coordinate system

```
            +Y (up)
             │
             │      -Z (north: where yaw = 0 looks)
             │    ↗
             │  ╱
             │╱
  ───────────┼────────── +X (east)
           ╱ │
         ╱   │
     +Z (south)
```

- **Right-handed**: point your right hand's fingers along +X, curl them toward +Y, and your thumb
  points along +Z. OpenGL uses this convention.
- **Y is up.** Block (x, y, z) occupies the cube from (x, y, z) to (x+1, y+1, z+1).
- **The camera looks down -Z** by default, so "north" (yaw 0) is -Z. F3 shows which way you
  face.

---

## 2. Vectors (`Vec3`)

A vector is three numbers: a **position** (a point in the world) or a **direction** (an arrow).

| Operation | Formula | Used for |
|---|---|---|
| add / subtract | `(a.x + b.x, ...)` | moving: `position + velocity * dt`; "from A to B" is `B - A` |
| scale | `(s * v.x, ...)` | speeds, distances |
| length | `sqrt(x² + y² + z²)` | distance (fog), "is the wish vector longer than 1?" |
| normalise | `v / length(v)` | a direction of length 1 (a **unit vector**) |
| dot product | `a.x*b.x + a.y*b.y + a.z*b.z` | `= |a| |b| cos θ` |
| cross product | `(a.y*b.z - a.z*b.y, a.z*b.x - a.x*b.z, a.x*b.y - a.y*b.x)` | a vector perpendicular to both |

**The dot product** measures how much two directions agree. For unit vectors it is `cos θ`:
1 means same direction, 0 means perpendicular, -1 means opposite. In this code:

- *Sun glow* (sky shader): `dot(dir, uSunDir)` is close to 1 when looking toward the sun, so
  `pow(dot, 12)` makes a soft glow and `smoothstep(0.9990, 0.9994, dot)` makes a sharp disc.
- *Frustum culling* (section 7): `dot(normal, point) + d` says which side of a plane a point is on.

**The cross product** of two directions gives a third, perpendicular to both. Its length is
`|a| |b| sin θ`, and its direction follows the right-hand rule. `makeCamera` uses it to build
the camera's sideways and up axes from its forward direction:

```cpp
c.right = normalize(cross(forward, worldUp));   // perpendicular to forward, and horizontal
c.up    = cross(c.right, forward);              // perpendicular to both: the camera's "up"
```

---

## 3. From mouse angles to a look direction

The player's view is two angles: **yaw** (turning left/right) and **pitch** (looking up/down).
Converting them to a direction is the spherical-to-Cartesian formula (`Player::lookDirection`):

```cpp
forward = ( cos(pitch) * sin(yaw),
            sin(pitch),
           -cos(pitch) * cos(yaw) );
```

Check it: yaw = 0, pitch = 0 gives (0, 0, -1), north. Yaw = 90° gives (1, 0, 0), east. Pitch
= 90° gives (0, 1, 0), straight up. `cos(pitch)` shrinks the horizontal part as you look up, so
the vector always has length 1.

For **walking**, pitch is ignored (looking at the sky should not make you walk slower), so only
the horizontal forward and right vectors are used:

```cpp
forward = ( sin(yaw), 0, -cos(yaw) );
right   = ( cos(yaw), 0,  sin(yaw) );   // forward turned 90° clockwise seen from above
```

**Why pitch is clamped to ±89°.** At exactly ±90°, `forward` is parallel to `worldUp`, so
`cross(forward, worldUp)` is the zero vector. Normalising zero is undefined and the camera
flips wildly. Stopping one degree short avoids it.

---

## 4. Matrices and homogeneous coordinates

A 4x4 **matrix** describes a transformation (move, rotate, scale, project). Multiplying it by a
point applies that transformation:

```
| m00 m01 m02 m03 |   | x |   | m00*x + m01*y + m02*z + m03*w |
| m10 m11 m12 m13 | * | y | = | m10*x + m11*y + m12*z + m13*w |
| m20 m21 m22 m23 |   | z |   | ...                           |
| m30 m31 m32 m33 |   | w |   | ...                           |
```

**Why 4D for 3D maths?** A 3x3 matrix can rotate and scale, but cannot *move* things, since
moving is adding, not multiplying. Adding a 4th coordinate `w = 1` to points makes the last
column act as an addition: `m03*w` adds `m03` to x. So translation fits in a matrix too. These are
**homogeneous coordinates**. Directions use `w = 0`, so translations do not affect them (an arrow
does not change when you move it).

**Combining transforms** is matrix multiplication. `P * V * point` means "apply V first, then
P". The order is read **right to left**, and swapping it gives a different result.

**Memory layout: column-major.** `Mat4::m[16]` stores the matrix *column by column*, which is
what OpenGL expects:

```cpp
float& at(int row, int col) { return m[col * 4 + row]; }   // mathlib.h
```

`glUniformMatrix4fv(..., GL_FALSE, m)` then passes it unchanged (`GL_FALSE` = "do not
transpose"). If you ever see a scene that looks inside-out or skewed, row/column order is the
first suspect.

---

## 5. The transformation chain

Every vertex goes through these spaces:

```
 model space  ──(model matrix)──►  world space  ──(view matrix)──►  view (camera) space
                                                                          │
                                                               (projection matrix)
                                                                          ▼
 screen pixels ◄──(viewport)── normalised device coords ◄──(÷ w)── clip space
```

- **Model → world.** Chunk vertices are *already* stored in world coordinates (the mesher adds
  `chunk.cx * 16` itself), so the model matrix is the identity and is skipped.
- **World → view.** The **view matrix** moves and turns the whole world so that the camera sits
  at the origin looking down -Z.
- **View → clip.** The **projection matrix** applies perspective.
- **Clip → NDC.** The GPU divides x, y and z by w. Everything visible ends up in the cube
  -1..1 on every axis: *normalised device coordinates*.
- **NDC → pixels.** `glViewport(0, 0, w, h)` maps -1..1 to 0..width and 0..height.

The code multiplies projection and view **once per frame on the CPU** (`viewProjection =
projection * view` in `makeCamera`), so the GPU does a single matrix multiply per vertex.

---

## 6. The two important matrices

### The view matrix: `lookAt(eye, target, up)`

```cpp
f = normalize(target - eye);    // forward
s = normalize(cross(f, up));    // side (right)
u = cross(s, f);                // true up

| s.x  s.y  s.z  -dot(s, eye) |
| u.x  u.y  u.z  -dot(u, eye) |
|-f.x -f.y -f.z   dot(f, eye) |
|  0    0    0        1       |
```

The top-left 3x3 part *rotates*: its rows are the camera's axes, so multiplying a vector by it
gives the vector's components *along* the camera's right, up and backward directions. The last
column *moves* everything by `-eye`, so the camera ends up at the origin. Forward is stored
negated because the camera looks down **-Z** in view space.

### The perspective projection: `perspective(fovY, aspect, near, far)`

Perspective means far things look smaller: screen position = position / distance. A matrix
can only multiply and add, not divide. The trick is to copy the distance into `w`, and the GPU's
automatic divide by `w` does the rest:

```cpp
f = 1 / tan(fovY / 2)

| f/aspect  0        0                         0              |
| 0         f        0                         0              |
| 0         0   (far+near)/(near-far)   2*far*near/(near-far) |
| 0         0       -1                         0              |
```

- Row 4 is `w_clip = -z_view`, the distance in front of the camera (it is positive, since
  visible points have negative z in view space).
- `f` sets the field of view. With a 70° vertical FOV, `tan(35°) = 0.70`, so a point at height
  0.70 at distance 1 lands exactly on the top edge of the screen.
- Dividing x by `aspect` (width/height) keeps squares square on a wide window.
- Row 3 maps distance `near` to -1 and `far` to +1 after the divide. The depth buffer stores
  that value.

**Depth is not linear.** After the divide, depth behaves like `1/distance`. Most of the depth
buffer's precision goes to things very close to the camera (see the worked example). That is why
`near` should be as *large* as you can get away with. Here it is 0.05 blocks: just smaller than
the 0.3-block gap between the eye and a wall you stand against. Setting near to 0.0001 would make
distant faces flicker as they fight over equal depth values ("z-fighting").

### The orthographic projection (HUD)

`ortho(0, width, height, 0, -1, 1)` maps pixel coordinates straight to -1..1 with no
perspective. Passing `bottom = height` and `top = 0` flips Y, so (0, 0) is the **top-left**
corner and Y grows downward, the usual convention for 2D screen layout.

---

## 7. Frustum culling

The **frustum** is the visible volume: a pyramid with its tip cut off, bounded by 6 planes
(left, right, top, bottom, near, far). A chunk whose bounding box lies entirely outside any one
plane cannot be seen, so the renderer skips it. That removes 50-70% of the chunks every frame.

**A plane** is `a*x + b*y + c*z + d = 0`, where `(a, b, c)` is its normal. For any point,
`a*x + b*y + c*z + d` is positive on the normal's side ("inside") and negative on the other.

**Getting the planes from the matrix** (Gribb & Hartmann, 2001). A point is visible when its
clip coordinates satisfy `-w ≤ x ≤ w` (and the same for y, z). Write row i of the
view-projection matrix as `r_i`, so that `x_clip = r0 · p` and `w_clip = r3 · p`. Then:

```
-w ≤ x   ⇔   (r3 + r0) · p ≥ 0      left plane  = r3 + r0
 x ≤ w   ⇔   (r3 - r0) · p ≥ 0      right plane = r3 - r0
```

and the same with r1 (bottom, top) and r2 (near, far). Six planes from adding and subtracting
rows: that is all `Frustum::fromMatrix` does.

**Testing a box** (`Frustum::boxVisible`). For each plane, take the box corner *furthest along
the plane's normal* (the "positive vertex": pick max or min on each axis by the sign of the
normal's component). If even that corner is outside, the whole box is. This is conservative: it
never culls something visible. Occasionally it keeps a box that is actually invisible near a
frustum corner, which is harmless.

---

## 8. Interpolation and smoothing

| Function | Formula | Where |
|---|---|---|
| `lerp(a, b, t)` | `a + (b - a) * t` | everywhere: 0 gives a, 1 gives b |
| `smoothstep(e0, e1, x)` | `t²(3 - 2t)`, `t = clamp((x-e0)/(e1-e0))` | fog, mountains mask, sky gradient: an S-curve with flat ends |
| Perlin `fade(t)` | `6t⁵ - 15t⁴ + 10t³` | noise: even smoother than smoothstep ([07](07-world-generation.md)) |
| exponential smoothing | `v += (target - v) * (1 - e^(-k·dt))` | velocity, field of view ([09](09-physics-and-raycasting.md)) |

`smoothstep` was chosen for the sky gradient specifically. The first version used
`pow(height, 0.55)`, whose slope is infinite at 0, and that showed as a visible crease along the
horizon in the test screenshots. `smoothstep` starts flat, so there is no crease.

---

## 9. Worked example: one point, world to pixel

Camera at the origin looking north (yaw 0, pitch 0), FOV 70°, window 1600 x 900
(aspect 1.778), near 0.05, far 1000. Where does the block corner at **(1, 0, -10)** appear?

1. **View matrix.** The camera is at the origin looking down -Z, so the view matrix is the
   identity: view-space point = (1, 0, -10, 1).
2. **Projection.** `f = 1 / tan(35°) = 1.428`.
   - `x_clip = (f / aspect) * 1 = 1.428 / 1.778 = 0.803`
   - `y_clip = f * 0 = 0`
   - `z_clip = (1000.05 / -999.95) * (-10) + (2 * 1000 * 0.05 / -999.95) = 10.001 - 0.100 = 9.901`
   - `w_clip = -z = 10`
3. **Divide by w.** NDC = (0.0803, 0, 0.9901).
4. **Viewport.** `pixel x = (0.0803 + 1) / 2 * 1600 = 864`, and `pixel y = 450` (the middle).

So the corner appears 64 pixels right of the crosshair. Move it to (1, 0, -20) and NDC x halves
to 0.040: twice as far, half the offset. That halving *is* perspective.

Look at the depth: at 10 blocks, NDC z is already **0.990**, so 99.5% of the depth range
(-1..1) is used up by the first 10 blocks, and everything from 10 to 1000 blocks shares the last
0.5%. This is the non-linear depth from section 6, and the reason `near` matters so much.

---

## Try it

- Change the FOV in `game.cpp` (`targetFov`) to 30° (zoomed, flat-looking) and to 110° (fish-eye).
- In `makeCamera`, swap the multiplication order to `view * projection` and watch everything
  break, then put it back.
- Comment out the `if (!frustum.boxVisible(...)) continue;` line in `Renderer::drawWorld` and
  compare the "drawn" count in F3.
