// player.h - First-person movement: walking, jumping, swimming, flying, collisions.
#pragma once

#include "mathlib.h"

class World;  // forward declaration: we only use World by reference here

// What the keyboard asks for this frame (filled in by the Game).
struct PlayerInput {
    float forward = 0.0f;  // +1 = W held, -1 = S held
    float strafe = 0.0f;   // +1 = D held, -1 = A held
    bool jump = false;     // Space
    bool descend = false;  // Shift (only used while flying)
    bool sprint = false;   // Ctrl
};

class Player {
public:
    // The player is a box 0.6 wide and 1.8 tall (an "axis-aligned bounding box").
    static constexpr float WIDTH = 0.6f;
    static constexpr float HEIGHT = 1.8f;
    static constexpr float EYE_HEIGHT = 1.62f;

    Vec3 position;  // centre of the soles of the feet
    Vec3 velocity;  // blocks per second
    float yaw = 0.0f;    // radians; 0 = facing north (-Z), increases turning right
    float pitch = 0.0f;  // radians; positive = looking up
    bool flying = false;
    bool onGround = false;
    bool inWater = false;

    // Mouse movement (in raw mouse counts) turns the head.
    void turn(float dx, float dy, float sensitivity);

    // Advances the simulation by dt seconds.
    void update(float dt, const PlayerInput& input, const World& world);

    Vec3 eyePosition() const { return position + Vec3{0.0f, EYE_HEIGHT, 0.0f}; }
    Vec3 lookDirection() const;  // unit vector the camera looks along

    // Does the player's box overlap block (x, y, z)? Used to forbid placing a block inside yourself.
    bool overlapsBlock(int x, int y, int z) const;
    // Is the player's box inside any solid block right now?
    bool insideSolid(const World& world) const;

private:
    void step(float dt, const PlayerInput& input, const World& world);
    bool moveAxis(int axis, float amount, const World& world);
};
