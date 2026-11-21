// player.cpp - Movement physics and block collision.
#include "player.h"

#include "world.h"

#include <algorithm>
#include <cmath>

namespace {

// Units: blocks (about 1 metre) and seconds.
constexpr float GRAVITY = 28.0f;          // blocks/s^2 (stronger than Earth's 9.8: feels snappier)
constexpr float JUMP_SPEED = 8.6f;        // jump height = v^2 / (2g) = 1.32 blocks
constexpr float MAX_FALL_SPEED = 50.0f;
constexpr float WALK_SPEED = 4.3f;
constexpr float SPRINT_SPEED = 6.2f;
constexpr float SWIM_SPEED = 2.4f;
constexpr float FLY_SPEED = 11.0f;
constexpr float FLY_FAST_SPEED = 26.0f;
constexpr float WATER_GRAVITY = 10.0f;
constexpr float SWIM_UP_ACCEL = 22.0f;
constexpr float MAX_STEP = 1.0f / 120.0f;  // physics never takes a bigger time step than this
constexpr float EPS = 0.001f;              // "touching" is not "overlapping"

}  // namespace

void Player::turn(float dx, float dy, float sensitivity) {
    yaw += dx * sensitivity;
    pitch -= dy * sensitivity;  // mouse down (dy > 0) looks down
    // Stop just short of straight up/down, where "forward" and "up" would coincide.
    pitch = clampf(pitch, radians(-89.0f), radians(89.0f));
    yaw = std::fmod(yaw, 2.0f * PI);
    if (yaw < 0.0f) yaw += 2.0f * PI;
}

Vec3 Player::lookDirection() const {
    // Spherical -> Cartesian. yaw = 0, pitch = 0 gives (0, 0, -1): north.
    return {std::cos(pitch) * std::sin(yaw), std::sin(pitch), -std::cos(pitch) * std::cos(yaw)};
}

void Player::update(float dt, const PlayerInput& input, const World& world) {
    // A slow frame could move us further than a block in one step and skip
    // straight through a wall ("tunnelling"), so big steps are cut into small ones.
    const int steps = std::max(1, static_cast<int>(std::ceil(dt / MAX_STEP)));
    const float h = dt / static_cast<float>(steps);
    for (int i = 0; i < steps; ++i) step(h, input, world);
}

void Player::step(float dt, const PlayerInput& input, const World& world) {
    // In water if the middle of the body is in a water block.
    inWater = world.getBlock(floorToInt(position.x), floorToInt(position.y + 0.7f), floorToInt(position.z)) ==
              Block::Water;

    // Turn the keys into a direction on the ground, relative to where we look.
    const Vec3 forward{std::sin(yaw), 0.0f, -std::cos(yaw)};
    const Vec3 right{std::cos(yaw), 0.0f, std::sin(yaw)};
    Vec3 wish = forward * input.forward + right * input.strafe;
    if (length(wish) > 1.0f) wish = normalize(wish);  // diagonal is not faster

    float speed = input.sprint ? SPRINT_SPEED : WALK_SPEED;
    if (inWater) speed = SWIM_SPEED;
    if (flying) speed = input.sprint ? FLY_FAST_SPEED : FLY_SPEED;

    // Move the velocity smoothly toward the wanted velocity. Using exp() makes
    // the result the same whatever the frame rate. Less grip in the air.
    const float grip = flying ? 10.0f : (onGround ? 16.0f : (inWater ? 8.0f : 6.0f));
    const float blend = 1.0f - std::exp(-grip * dt);
    velocity.x += (wish.x * speed - velocity.x) * blend;
    velocity.z += (wish.z * speed - velocity.z) * blend;

    if (flying) {
        const float target = (input.jump ? speed : 0.0f) - (input.descend ? speed : 0.0f);
        velocity.y += (target - velocity.y) * blend;
    } else if (inWater) {
        velocity.y -= WATER_GRAVITY * dt;
        if (input.jump) velocity.y += SWIM_UP_ACCEL * dt;
        velocity.y *= std::exp(-2.0f * dt);  // water drag
        velocity.y = clampf(velocity.y, -4.0f, 4.0f);
    } else {
        velocity.y -= GRAVITY * dt;
        if (input.jump && onGround) velocity.y = JUMP_SPEED;
        velocity.y = std::max(velocity.y, -MAX_FALL_SPEED);
    }

    // Move one axis at a time. Resolving X, Y and Z separately is what lets
    // you slide along a wall instead of sticking to it.
    onGround = false;
    moveAxis(1, velocity.y * dt, world);
    const bool hitX = moveAxis(0, velocity.x * dt, world);
    const bool hitZ = moveAxis(2, velocity.z * dt, world);

    // Swimming into a bank with jump held: climb out.
    if (inWater && input.jump && (hitX || hitZ)) velocity.y = 5.0f;
}

bool Player::moveAxis(int axis, float amount, const World& world) {
    if (amount == 0.0f) return false;
    position[axis] += amount;

    const float half = WIDTH * 0.5f;
    const Vec3 mn{position.x - half, position.y, position.z - half};
    const Vec3 mx{position.x + half, position.y + HEIGHT, position.z + half};

    // Every block the box now overlaps. EPS shrinks the box a hair, so standing
    // exactly ON a block (feet at y = 64.000) does not count as being IN it.
    const int x0 = floorToInt(mn.x + EPS), x1 = floorToInt(mx.x - EPS);
    const int y0 = floorToInt(mn.y + EPS), y1 = floorToInt(mx.y - EPS);
    const int z0 = floorToInt(mn.z + EPS), z1 = floorToInt(mx.z - EPS);

    bool hit = false;
    float limit = position[axis];
    for (int y = y0; y <= y1; ++y) {
        for (int z = z0; z <= z1; ++z) {
            for (int x = x0; x <= x1; ++x) {
                if (!isSolid(world.getBlock(x, y, z))) continue;
                hit = true;
                const float blockMin = static_cast<float>(axis == 0 ? x : (axis == 1 ? y : z));
                if (amount > 0.0f) {
                    // Moving in + direction: our far side stops at the block's near side.
                    const float extent = (axis == 1) ? HEIGHT : half;
                    limit = std::min(limit, blockMin - extent);
                } else {
                    // Moving in - direction: our near side stops at the block's far side.
                    const float extent = (axis == 1) ? 0.0f : half;
                    limit = std::max(limit, blockMin + 1.0f + extent);
                }
            }
        }
    }

    if (hit) {
        position[axis] = limit;
        velocity[axis] = 0.0f;
        if (axis == 1 && amount < 0.0f) onGround = true;  // landed on something
    }
    return hit;
}

bool Player::overlapsBlock(int x, int y, int z) const {
    const float half = WIDTH * 0.5f;
    return position.x - half < x + 1 - EPS && position.x + half > x + EPS &&
           position.y < y + 1 - EPS && position.y + HEIGHT > y + EPS &&
           position.z - half < z + 1 - EPS && position.z + half > z + EPS;
}

bool Player::insideSolid(const World& world) const {
    const float half = WIDTH * 0.5f;
    for (int y = floorToInt(position.y + EPS); y <= floorToInt(position.y + HEIGHT - EPS); ++y)
        for (int z = floorToInt(position.z - half + EPS); z <= floorToInt(position.z + half - EPS); ++z)
            for (int x = floorToInt(position.x - half + EPS); x <= floorToInt(position.x + half - EPS); ++x)
                if (isSolid(world.getBlock(x, y, z))) return true;
    return false;
}
