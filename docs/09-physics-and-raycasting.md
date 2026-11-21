# 09 - Physics and raycasting

Files: `player.h/.cpp`, `raycast.h/.cpp`, `game.cpp` (`handleBlockActions`),
`tests/gameplay_tests.cpp`.

This chapter covers how the player moves, falls and bumps into blocks without ever passing
through them, and how the game works out which block you are pointing at.

---

## 1. The player is a box

```cpp
static constexpr float WIDTH = 0.6f;       // blocks
static constexpr float HEIGHT = 1.8f;
static constexpr float EYE_HEIGHT = 1.62f;
Vec3 position;   // the centre of the soles of the feet
```

For collisions the player is an **axis-aligned bounding box (AABB)**: a box whose edges are
parallel to the X, Y and Z axes, and which never rotates, even when you turn. It spans:

```
min = (position.x - 0.3, position.y,       position.z - 0.3)
max = (position.x + 0.3, position.y + 1.8, position.z + 0.3)
```

At 0.6 wide it fits through a 1-block gap, and at 1.8 tall through a 2-block-high doorway. The
camera sits at `position + (0, 1.62, 0)`.

Blocks are AABBs too (1 x 1 x 1 at whole-number positions), so every collision question is
"do two AABBs overlap?". Two boxes overlap exactly when they overlap on all three axes at once.

---

## 2. From keys to velocity

`Player::step` first turns the keys into a **wish direction** on the ground:

```cpp
const Vec3 forward{std::sin(yaw), 0, -std::cos(yaw)};
const Vec3 right{std::cos(yaw), 0, std::sin(yaw)};
Vec3 wish = forward * input.forward + right * input.strafe;   // W/S = ±forward, A/D = ±strafe
if (length(wish) > 1.0f) wish = normalize(wish);
```

The normalisation matters: W+D together gives (1, 1), with length 1.41. Without it, walking
diagonally would be 41% faster (the famous "strafe-running" bug of 1990s shooters).

Then velocity **moves smoothly toward** `wish * speed`:

```cpp
const float blend = 1.0f - std::exp(-grip * dt);
velocity.x += (wish.x * speed - velocity.x) * blend;
```

| Situation | `grip` | Feel |
|---|---|---|
| on the ground | 16 | starts and stops in ~0.1-0.2 s |
| in the air | 6 | some air control, but momentum carries |
| in water | 8 | sluggish |
| flying | 10 | |

### Why `1 - exp(-k * dt)` and not just `0.2`?

A fixed `velocity += (target - velocity) * 0.2f` per frame closes 20% of the gap *per frame*:
fast at 144 fps, slow at 30 fps. The game would play differently on every PC.

With `1 - e^(-k·dt)`, the remaining gap after time T is `e^(-k·T)` **however that time is cut
into frames**, because `e^(-k·a) · e^(-k·b) = e^(-k·(a+b))`. One step of 0.1 s gives exactly the
same result as ten steps of 0.01 s. This is the standard frame-rate-independent way to "move
toward a target", and `Game::updatePlaying` uses the same trick for the field-of-view zoom when
sprinting.

---

## 3. Gravity and jumping

Every step: `velocity.y -= GRAVITY * dt`. Jumping sets `velocity.y = JUMP_SPEED`, if
`onGround`.

**How high is a jump?** Physics class: a ball thrown up at speed v rises until gravity has used up
its speed, reaching height `h = v² / (2g)`.

```
GRAVITY = 28 blocks/s², JUMP_SPEED = 8.6 blocks/s
h = 8.6² / (2 * 28) = 73.96 / 56 = 1.32 blocks       time in the air: 2v / g = 0.61 s
```

1.32 is enough to get onto a 1-block step (the feet must rise above 1.0) but not onto 2 blocks,
which is the rule of thumb voxel games are built around. Gravity is about 3x Earth's (9.8):
with real gravity a 1.3-block jump would take 1 s and feel floaty. Games almost always use
stronger-than-real gravity.

Falling speed is capped at 50 blocks/s ("terminal velocity"), which also keeps each physics
step small (section 5).

**Water** changes the rules (`inWater`, tested at the middle of the body):

- weaker gravity (10) and drag (`velocity.y *= exp(-2 * dt)`), so you sink slowly;
- holding Space pushes up (`SWIM_UP_ACCEL = 22`);
- swimming into a bank with Space held gives an upward boost, so you can climb out.

**Flying** (F) has no gravity. Space/Shift set a target vertical speed, smoothed like walking.

---

## 4. Collision: move one axis at a time

The heart of `player.cpp`:

```cpp
onGround = false;
moveAxis(1, velocity.y * dt, world);                       // Y first
const bool hitX = moveAxis(0, velocity.x * dt, world);
const bool hitZ = moveAxis(2, velocity.z * dt, world);
```

`moveAxis(axis, amount)`:

1. Move along that one axis: `position[axis] += amount`.
2. Find every block the box now overlaps (the range of block coordinates covered by min..max).
3. If any is solid, push the player back so they **just touch** it:
   - moving +: our max side stops at the block's min side, so `position = blockMin - extent`
   - moving -: our min side stops at the block's max side, so `position = blockMin + 1 + extent`
4. Zero the velocity on that axis. If it was Y moving down, we have landed: `onGround = true`.

**Why one axis at a time?** If you walk diagonally into a wall, a single combined move would
detect "collision!" and stop you dead. Resolving X and Z separately stops only the blocked axis
and keeps the other, so you **slide along the wall**. The test "slides north along the wall"
checks exactly this.

**Touching is not overlapping.** Standing on the ground, your feet are at exactly y = 11.000,
which is the top of block 10. Is block 10 "overlapped"? It must not be, or every step would
register a collision with the floor under you. The block range shrinks the box by a tiny `EPS`:

```cpp
const int y0 = floorToInt(mn.y + EPS), y1 = floorToInt(mx.y - EPS);   // EPS = 0.001
// mn.y = 11.000 -> floor(11.001) = 11: block 10 is not in the range. Good.
```

This also makes snapping exact (`position = blockMin - extent`) safe against floating-point
rounding.

**How `onGround` stays true while standing still:** gravity pulls you down a tiny bit every
step, the Y move collides with the floor, you are snapped back to exactly the top, and
`onGround` is set. Jumping checks the value from the previous step.

---

## 5. Tunnelling, and why physics takes small steps

If something moves further than a block in one step, it can jump from one side of a thin wall
to the other without ever overlapping it. This is called **tunnelling**. A slow frame (say a
0.1 s hiccup) at falling speed 50 would move 5 blocks in one go.

`Player::update` therefore cuts every frame into **sub-steps of at most 1/120 s**:

```cpp
const int steps = std::max(1, static_cast<int>(std::ceil(dt / MAX_STEP)));
for (int i = 0; i < steps; ++i) step(dt / steps, input, world);
```

At the maximum speed of 50 blocks/s, one sub-step moves 50 / 120 = 0.42 blocks, less than one
block, so no wall can be skipped. At 60 fps that is 2 sub-steps per frame: cheap.

---

## 6. The edges of the world

- **Unloaded chunks report `Bedrock`** from `World::getBlock`. To the physics they are solid
  walls, so you can never walk or fall into land that does not exist yet.
- The physics only runs when the player's own chunk is loaded (`world_->isLoadedAt`).
- Below y = 0 is bedrock as well; above y = 127 is air (you can fly over the world's ceiling).
- Falling below y = -40 somehow (it should be impossible) teleports you back to the spawn point,
  just in case.

---

## 7. The tests

`tests/gameplay_tests.cpp` checks this chapter with numbers, no window needed:

| Test | Checks |
|---|---|
| falling and landing | lands at exactly y = 11.0000 and sets `onGround` |
| walking into a wall | stops at x = 11.7 (wall at 12 minus half the width) without drifting sideways |
| sliding along a wall | walking diagonally into the wall slides along it |
| jumping onto a step | walk up to a 1-block step, jump, end up standing on it |
| overlap check | the player occupies its two blocks but not the touching neighbours |
| raycasts | hits the right block and face, respects reach, works at negative coordinates |

One test first failed: from a **standstill 1.2 blocks away**, the jump arc came down 5 cm short
of the step. The test was unrealistic (a player walks up to the step first), so it was changed,
and air control was raised from 4 to 6 so the jump feels responsive. Tests catch both bugs and
bad game feel.

---

## 8. Raycasting: what is under the crosshair?

The crosshair is the centre of the screen. The block it points at is the first solid block along
a **ray** from the eye in the look direction. Reach is limited to 6 blocks.

**The naive way** samples points along the ray every 0.1 blocks. It is slow, and it can miss a
block when the ray just clips its corner between two samples.

**The exact way: Amanatides & Woo (1987)**, also called voxel DDA. The ray passes through the
grid one block at a time, visiting **exactly** the blocks it crosses, in order:

```
        ┌───┬───┬───┬───┐
        │   │   │   │ ▓ │   ▓ = the block that is hit
        ├───┼───┼───┼──/┤
        │   │   │ 4 │5/ │   The ray crosses a vertical or a horizontal
        ├───┼───┼──/┼───┤   grid line at each step. Each step moves to the
        │   │ 2 │3/ │   │   neighbour on the other side of whichever line
        ├───┼──/┼───┼───┤   the ray reaches FIRST.
        │ 0 │1/ │   │   │
        └───┴/──┴───┴───┘
           eye
```

Per axis, the algorithm keeps two numbers, both measured as distance *along the ray*:

- `tMax[a]`: how far until the ray crosses the next grid line on axis a.
- `tDelta[a]`: how far between consecutive grid lines on axis a (= `1 / |direction[a]|`).

Each step, pick the axis with the smallest `tMax`, move one block along it, and add `tDelta` to
that `tMax`:

```cpp
int axis = 0;
if (tMax[1] < tMax[axis]) axis = 1;
if (tMax[2] < tMax[axis]) axis = 2;
t = tMax[axis];               // distance travelled so far
cell[axis] += step[axis];     // into the neighbouring block
tMax[axis] += tDelta[axis];
normal = {0, 0, 0};
normal[axis] = -step[axis];   // we came in through the face pointing back at us
```

That **normal** tells which face of the block was hit, for example (0, 1, 0) for its top. It is
what makes building work: the new block goes at `block + normal`, the empty space in front of
the face you are looking at.

An axis the ray runs parallel to gets `tMax = tDelta = infinity`, so it is never chosen. No
special case is needed. Air and water are passed through, so you can't select water, but you
can build on the sea floor through it.

---

## 9. Breaking and placing (`Game::handleBlockActions`)

| Action | Rule |
|---|---|
| **Break** (left click) | Set the target to Air. If water touches the hole (sides or above), set it to Water instead, so digging underwater does not leave air bubbles. Bedrock cannot be broken |
| **Place** (right click) | At `target.block + target.normal`, only if that space is Air or Water, inside the world's height, and **not overlapping the player** (`Player::overlapsBlock`), or you could entomb yourself. Leaves and glass count as solid for this |
| **Pick** (middle click) | Copy the target block's type into the selected hotbar slot |
| **Hold to repeat** | A click acts at once; holding repeats every 0.22 s (`actionCooldown_`) |

The click that resumes the game from the pause screen sets a 0.3 s cooldown, so it does not also
break whatever is in front of you.

---

## Try it

- Moon gravity: `GRAVITY = 5`, `JUMP_SPEED = 4`. Work out the jump height first, then test it.
- Make the player 0.9 tall (crawling) and walk through 1-block gaps. What else must change?
  (Hint: `EYE_HEIGHT`.)
- Add **auto-step**: when a horizontal move hits a wall exactly 1 block high and there is room
  above, lift the player onto it. The test "jumping onto a step" can become "walking onto a
  step".
- Draw the ray: add a debug mode that draws the visited cells' outlines (reuse `drawOutline`).
