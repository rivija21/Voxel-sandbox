// raycast.cpp - Voxel traversal (J. Amanatides & A. Woo, 1987).
#include "raycast.h"

#include "world.h"

#include <limits>

RayHit raycastBlocks(const World& world, const Vec3& origin, const Vec3& direction, float maxDistance) {
    constexpr float INF = std::numeric_limits<float>::infinity();

    IVec3 cell{floorToInt(origin.x), floorToInt(origin.y), floorToInt(origin.z)};
    int step[3];      // +1 or -1: which way we move along each axis
    float tMax[3];    // distance along the ray to the next block boundary on each axis
    float tDelta[3];  // distance along the ray between boundaries on each axis

    for (int a = 0; a < 3; ++a) {
        const float d = direction[a];
        if (d > 0.0f) {
            step[a] = 1;
            tMax[a] = (static_cast<float>(cell[a]) + 1.0f - origin[a]) / d;
            tDelta[a] = 1.0f / d;
        } else if (d < 0.0f) {
            step[a] = -1;
            tMax[a] = (origin[a] - static_cast<float>(cell[a])) / -d;
            tDelta[a] = 1.0f / -d;
        } else {  // the ray never crosses a boundary on this axis
            step[a] = 0;
            tMax[a] = INF;
            tDelta[a] = INF;
        }
    }

    IVec3 normal{0, 0, 0};
    float t = 0.0f;
    while (t <= maxDistance) {
        const BlockId id = world.getBlock(cell);
        if (id != Block::Air && id != Block::Water) {
            RayHit hit;
            hit.hit = true;
            hit.block = cell;
            hit.normal = normal;
            hit.distance = t;
            return hit;
        }
        // Step into the neighbouring block whose boundary the ray reaches first.
        int axis = 0;
        if (tMax[1] < tMax[axis]) axis = 1;
        if (tMax[2] < tMax[axis]) axis = 2;
        t = tMax[axis];
        cell[axis] += step[axis];
        tMax[axis] += tDelta[axis];
        normal = {0, 0, 0};
        normal[axis] = -step[axis];  // we entered through the face pointing back at us
    }
    return {};
}
