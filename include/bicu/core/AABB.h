#pragma once
#include "bicu/core/math.h"
#include <cmath>

struct AABB3 {
    Vec3 center;
    Vec3 half;
};

struct Plane3 {
    Vec3 normal;
    double d;
};

inline AABB3 makeAABB(const Vec3& center, const Vec3& half) {
    return { center, half };
}

inline Vec3 aabbMin(const AABB3& b) {
    return Vec3(b.center.x - b.half.x, b.center.y - b.half.y, b.center.z - b.half.z);
}

inline Vec3 aabbMax(const AABB3& b) {
    return Vec3(b.center.x + b.half.x, b.center.y + b.half.y, b.center.z + b.half.z);
}

inline Vec3 aabbExtents(const AABB3& b) {
    return b.half * 2.0;
}

inline Vec3 aabbClosestPoint(const AABB3& b, const Vec3& p) {
    return Vec3(std::fmax(b.center.x - b.half.x, std::fmin(b.center.x + b.half.x, p.x)),
                std::fmax(b.center.y - b.half.y, std::fmin(b.center.y + b.half.y, p.y)),
                std::fmax(b.center.z - b.half.z, std::fmin(b.center.z + b.half.z, p.z)));
}

inline bool aabbContains(const AABB3& b, const Vec3& p) {
    return std::fabs(p.x - b.center.x) <= b.half.x &&
           std::fabs(p.y - b.center.y) <= b.half.y &&
           std::fabs(p.z - b.center.z) <= b.half.z;
}

inline bool aabbOverlap(const AABB3& a, const AABB3& b) {
    return std::fabs(a.center.x - b.center.x) <= a.half.x + b.half.x &&
           std::fabs(a.center.y - b.center.y) <= a.half.y + b.half.y &&
           std::fabs(a.center.z - b.center.z) <= a.half.z + b.half.z;
}

inline AABB3 unionAABB(const AABB3& a, const AABB3& b) {
    Vec3 mn(std::fmin(a.center.x - a.half.x, b.center.x - b.half.x),
            std::fmin(a.center.y - a.half.y, b.center.y - b.half.y),
            std::fmin(a.center.z - a.half.z, b.center.z - b.half.z));
    Vec3 mx(std::fmax(a.center.x + a.half.x, b.center.x + b.half.x),
            std::fmax(a.center.y + a.half.y, b.center.y + b.half.y),
            std::fmax(a.center.z + a.half.z, b.center.z + b.half.z));
    return { (mn + mx) * 0.5, (mx - mn) * 0.5 };
}

inline AABB3 expandAABB(const AABB3& b, double margin) {
    return { b.center, b.half + Vec3(margin, margin, margin) };
}

inline AABB3 scaleAABB(const AABB3& b, double s) {
    return { b.center, b.half * s };
}

inline double aabbSurfaceArea(const AABB3& b) {
    double x = b.half.x * 2.0;
    double y = b.half.y * 2.0;
    double z = b.half.z * 2.0;
    return 2.0 * (x * y + y * z + z * x);
}

inline double planeDistanceTo(const Plane3& p, const Vec3& v) {
    return p.normal.x * v.x + p.normal.y * v.y + p.normal.z * v.z - p.d;
}

inline bool planeIsPointOver(const Plane3& p, const Vec3& v) {
    return planeDistanceTo(p, v) > 0.0;
}

inline bool aabbIntersectsConvexShape(const AABB3& b, const Plane3* planes, int count) {
    Vec3 half = b.half;
    Vec3 ofs = b.center;
    for (int i = 0; i < count; ++i) {
        const Plane3& p = planes[i];
        Vec3 point(
            (p.normal.x > 0.0) ? -half.x : half.x,
            (p.normal.y > 0.0) ? -half.y : half.y,
            (p.normal.z > 0.0) ? -half.z : half.z);
        point += ofs;
        if (planeIsPointOver(p, point)) return false;
    }
    return true;
}

inline bool aabbOutsideFrustum(const AABB3& b, const Plane3* planes, int n) {
    return !aabbIntersectsConvexShape(b, planes, n);
}
