#pragma once
#include "bicu/core/AABB.h"
#include <cmath>

struct OBB3 {
    Vec3 center;
    Vec3 half;
    Vec3 axisX = Vec3(1, 0, 0);
    Vec3 axisY = Vec3(0, 1, 0);
    Vec3 axisZ = Vec3(0, 0, 1);
};

inline OBB3 makeOBB(const Vec3& center, const Vec3& half, const Quat& rot) {
    Mat4 m = rot.toMat4();
    OBB3 o;
    o.center = center;
    o.half = half;
    o.axisX = Vec3(m.m[0], m.m[1], m.m[2]);
    o.axisY = Vec3(m.m[4], m.m[5], m.m[6]);
    o.axisZ = Vec3(m.m[8], m.m[9], m.m[10]);
    return o;
}

inline OBB3 makeOBBEuler(const Vec3& center, const Vec3& half, const Vec3& rotDeg) {
    return makeOBB(center, half, Quat::fromEulerDeg(rotDeg.x, rotDeg.y, rotDeg.z));
}

inline OBB3 identityOBB(const Vec3& center, const Vec3& half) {
    return makeOBB(center, half, Quat());
}

inline AABB3 obbToAABB(const OBB3& o) {
    Vec3 bh(
        std::fabs(o.half.x * o.axisX.x) + std::fabs(o.half.y * o.axisY.x) + std::fabs(o.half.z * o.axisZ.x),
        std::fabs(o.half.x * o.axisX.y) + std::fabs(o.half.y * o.axisY.y) + std::fabs(o.half.z * o.axisZ.y),
        std::fabs(o.half.x * o.axisX.z) + std::fabs(o.half.y * o.axisY.z) + std::fabs(o.half.z * o.axisZ.z));
    return { o.center, bh };
}

inline AABB3 makeObbAABB(const Vec3& center, const Vec3& half, const Vec3& rotDeg) {
    return obbToAABB(makeOBBEuler(center, half, rotDeg));
}

inline Vec3 obbClosestPoint(const OBB3& o, const Vec3& p) {
    Vec3 d = p - o.center;
    Vec3 local(d.dot(o.axisX), d.dot(o.axisY), d.dot(o.axisZ));
    local.x = std::fmax(-o.half.x, std::fmin(o.half.x, local.x));
    local.y = std::fmax(-o.half.y, std::fmin(o.half.y, local.y));
    local.z = std::fmax(-o.half.z, std::fmin(o.half.z, local.z));
    return o.center + o.axisX * local.x + o.axisY * local.y + o.axisZ * local.z;
}

inline bool obbContains(const OBB3& o, const Vec3& p) {
    Vec3 d = p - o.center;
    return std::fabs(d.dot(o.axisX)) <= o.half.x &&
           std::fabs(d.dot(o.axisY)) <= o.half.y &&
           std::fabs(d.dot(o.axisZ)) <= o.half.z;
}

inline bool obbOutsideAABB(const OBB3& o, const AABB3& b) {
    return !aabbOverlap(obbToAABB(o), b);
}

inline float obbProjectRadius(const OBB3& o, const Vec3& n) {
    return std::fabs(o.half.x * n.dot(o.axisX)) +
           std::fabs(o.half.y * n.dot(o.axisY)) +
           std::fabs(o.half.z * n.dot(o.axisZ));
}
