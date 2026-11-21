// raycast.h - Which block is the crosshair pointing at?
#pragma once

#include "mathlib.h"

class World;

struct RayHit {
    bool hit = false;
    IVec3 block;        // the block that was hit
    IVec3 normal;       // which face was hit, e.g. (0, 1, 0) = its top. block + normal = where a new block goes
    float distance = 0.0f;
};

// Walks along the ray one block at a time (the "DDA" / Amanatides-Woo method)
// and returns the first solid block within maxDistance. Water and air are skipped.
RayHit raycastBlocks(const World& world, const Vec3& origin, const Vec3& direction, float maxDistance);
