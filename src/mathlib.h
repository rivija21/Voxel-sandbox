// mathlib.h - The small amount of 3D maths a voxel game needs.
// (Not called "math.h" so it can never be confused with the C library header.)
//
// Conventions used everywhere in this project:
//   * Right-handed coordinates: +X = east, +Y = up, +Z = south, so north is -Z.
//   * Matrices are 4x4, stored COLUMN-major (the layout OpenGL expects):
//     element (row r, column c) lives at m[c * 4 + r].
//   * Angles are in radians unless a name says "deg".
#pragma once

#include <cmath>

constexpr float PI = 3.14159265358979f;

inline float radians(float degrees) { return degrees * (PI / 180.0f); }
inline float degrees(float rad) { return rad * (180.0f / PI); }
inline float clampf(float v, float lo, float hi) { return v < lo ? lo : (v > hi ? hi : v); }
inline float lerpf(float a, float b, float t) { return a + (b - a) * t; }

// Smooth 0..1 ramp between edge0 and edge1 (same as GLSL smoothstep).
inline float smoothstep(float edge0, float edge1, float x) {
    float t = clampf((x - edge0) / (edge1 - edge0), 0.0f, 1.0f);
    return t * t * (3.0f - 2.0f * t);
}

// std::floor then convert: -0.5 must become -1, not 0 (which plain (int) gives).
inline int floorToInt(float v) { return static_cast<int>(std::floor(v)); }

// Integer division that rounds toward minus infinity: floorDiv(-1, 16) == -1.
// C++'s "/" rounds toward zero (-1 / 16 == 0), which is wrong for chunk maths.
inline int floorDiv(int a, int b) { return (a >= 0) ? a / b : -((-a + b - 1) / b); }
// Matching remainder, always in [0, b): floorMod(-1, 16) == 15.
inline int floorMod(int a, int b) { int m = a % b; return m < 0 ? m + b : m; }

// ---------------------------------------------------------------------------
struct Vec3 {
    float x = 0, y = 0, z = 0;

    Vec3() = default;
    constexpr Vec3(float x_, float y_, float z_) : x(x_), y(y_), z(z_) {}

    // Access by axis number (0 = x, 1 = y, 2 = z). Handy in loops over axes.
    float& operator[](int axis) { return axis == 0 ? x : (axis == 1 ? y : z); }
    float operator[](int axis) const { return axis == 0 ? x : (axis == 1 ? y : z); }

    Vec3 operator+(const Vec3& o) const { return {x + o.x, y + o.y, z + o.z}; }
    Vec3 operator-(const Vec3& o) const { return {x - o.x, y - o.y, z - o.z}; }
    Vec3 operator-() const { return {-x, -y, -z}; }
    Vec3 operator*(float s) const { return {x * s, y * s, z * s}; }
    Vec3 operator/(float s) const { return {x / s, y / s, z / s}; }
    Vec3& operator+=(const Vec3& o) { x += o.x; y += o.y; z += o.z; return *this; }
    Vec3& operator-=(const Vec3& o) { x -= o.x; y -= o.y; z -= o.z; return *this; }
    Vec3& operator*=(float s) { x *= s; y *= s; z *= s; return *this; }
};

inline Vec3 operator*(float s, const Vec3& v) { return v * s; }
inline float dot(const Vec3& a, const Vec3& b) { return a.x * b.x + a.y * b.y + a.z * b.z; }
inline Vec3 cross(const Vec3& a, const Vec3& b) {
    return {a.y * b.z - a.z * b.y, a.z * b.x - a.x * b.z, a.x * b.y - a.y * b.x};
}
inline float length(const Vec3& v) { return std::sqrt(dot(v, v)); }
inline Vec3 normalize(const Vec3& v) {
    float len = length(v);
    return len > 0.0f ? v / len : Vec3{0, 0, 0};
}
inline Vec3 lerp(const Vec3& a, const Vec3& b, float t) { return a + (b - a) * t; }

// Integer 3D position: identifies one block in the world.
struct IVec3 {
    int x = 0, y = 0, z = 0;
    int& operator[](int axis) { return axis == 0 ? x : (axis == 1 ? y : z); }
    int operator[](int axis) const { return axis == 0 ? x : (axis == 1 ? y : z); }
    IVec3 operator+(const IVec3& o) const { return {x + o.x, y + o.y, z + o.z}; }
    bool operator==(const IVec3& o) const { return x == o.x && y == o.y && z == o.z; }
    bool operator!=(const IVec3& o) const { return !(*this == o); }
};

// ---------------------------------------------------------------------------
struct Mat4 {
    float m[16] = {};  // column-major, see top of file

    float& at(int row, int col) { return m[col * 4 + row]; }
    float at(int row, int col) const { return m[col * 4 + row]; }

    static Mat4 identity() {
        Mat4 r;
        r.at(0, 0) = r.at(1, 1) = r.at(2, 2) = r.at(3, 3) = 1.0f;
        return r;
    }
};

// Matrix product: (a * b) applied to a point means "first b, then a".
inline Mat4 operator*(const Mat4& a, const Mat4& b) {
    Mat4 r;
    for (int row = 0; row < 4; ++row)
        for (int col = 0; col < 4; ++col) {
            float sum = 0.0f;
            for (int k = 0; k < 4; ++k) sum += a.at(row, k) * b.at(k, col);
            r.at(row, col) = sum;
        }
    return r;
}

// Perspective projection: things further away look smaller.
// Maps the visible "frustum" to the cube -1..1 that OpenGL draws (clip space).
inline Mat4 perspective(float fovY, float aspect, float zNear, float zFar) {
    const float f = 1.0f / std::tan(fovY * 0.5f);  // cot(fov/2): bigger fov -> smaller f
    Mat4 r;
    r.at(0, 0) = f / aspect;
    r.at(1, 1) = f;
    r.at(2, 2) = (zFar + zNear) / (zNear - zFar);
    r.at(2, 3) = (2.0f * zFar * zNear) / (zNear - zFar);
    r.at(3, 2) = -1.0f;  // copies -z into w, so the GPU divides by distance
    return r;
}

// Orthographic projection: no perspective, used for the 2D HUD.
inline Mat4 ortho(float left, float right, float bottom, float top, float zNear, float zFar) {
    Mat4 r = Mat4::identity();
    r.at(0, 0) = 2.0f / (right - left);
    r.at(1, 1) = 2.0f / (top - bottom);
    r.at(2, 2) = -2.0f / (zFar - zNear);
    r.at(0, 3) = -(right + left) / (right - left);
    r.at(1, 3) = -(top + bottom) / (top - bottom);
    r.at(2, 3) = -(zFar + zNear) / (zFar - zNear);
    return r;
}

// View matrix: moves the whole world so the camera sits at the origin looking down -Z.
inline Mat4 lookAt(const Vec3& eye, const Vec3& target, const Vec3& up) {
    const Vec3 f = normalize(target - eye);  // forward
    const Vec3 s = normalize(cross(f, up));  // side (right)
    const Vec3 u = cross(s, f);              // true up
    Mat4 r = Mat4::identity();
    r.at(0, 0) = s.x;  r.at(0, 1) = s.y;  r.at(0, 2) = s.z;  r.at(0, 3) = -dot(s, eye);
    r.at(1, 0) = u.x;  r.at(1, 1) = u.y;  r.at(1, 2) = u.z;  r.at(1, 3) = -dot(u, eye);
    r.at(2, 0) = -f.x; r.at(2, 1) = -f.y; r.at(2, 2) = -f.z; r.at(2, 3) = dot(f, eye);
    return r;
}

// ---------------------------------------------------------------------------
// Frustum culling: the 6 planes of the camera's view volume. A chunk whose box is
// completely outside any plane cannot be seen, so we skip drawing it.
struct Frustum {
    float planes[6][4];  // each plane: a*x + b*y + c*z + d >= 0 means "inside"

    // Gribb & Hartmann method: the planes fall straight out of the rows of
    // the combined projection * view matrix.
    static Frustum fromMatrix(const Mat4& vp) {
        Frustum fr;
        for (int i = 0; i < 3; ++i) {
            for (int j = 0; j < 4; ++j) {
                fr.planes[i * 2 + 0][j] = vp.at(3, j) + vp.at(i, j);  // left, bottom, near
                fr.planes[i * 2 + 1][j] = vp.at(3, j) - vp.at(i, j);  // right, top, far
            }
        }
        return fr;
    }

    // True if the axis-aligned box [mn, mx] is at least partly inside.
    bool boxVisible(const Vec3& mn, const Vec3& mx) const {
        for (const auto& p : planes) {
            // The box corner furthest along the plane normal ("positive vertex").
            const float x = p[0] >= 0 ? mx.x : mn.x;
            const float y = p[1] >= 0 ? mx.y : mn.y;
            const float z = p[2] >= 0 ? mx.z : mn.z;
            if (p[0] * x + p[1] * y + p[2] * z + p[3] < 0) return false;
        }
        return true;
    }
};
