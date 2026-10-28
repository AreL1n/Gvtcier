#pragma once

namespace gv {

inline constexpr int kDefaultWindowWidth = 1280;
inline constexpr int kDefaultWindowHeight = 720;
inline constexpr float kDefaultFov = 60.0f;
inline constexpr int kShadowMapSize = 2048;
inline constexpr int kCascadeCount = 4;
inline constexpr int kZCanvasImage = 50;

enum class DepthRange
{
    NegativeOneToOne,
    ZeroToOne,
};

enum class MatrixMajor
{
    Column,
    Row,
};

enum class Handedness
{
    RightHanded,
    LeftHanded,
};

enum class FrontWinding
{
    CounterClockwise,
    Clockwise,
};

enum class ClipY
{
    Up,
    Down,
};

struct Conventions
{
    DepthRange depth = DepthRange::NegativeOneToOne;
    MatrixMajor major = MatrixMajor::Column;
    Handedness handed = Handedness::RightHanded;
    FrontWinding winding = FrontWinding::CounterClockwise;
    ClipY clipY = ClipY::Up;

    static const Conventions& active()
    {
        static Conventions c;
        return c;
    }

    static Conventions& mutableActive()
    {
        static Conventions c;
        return c;
    }

    bool zeroToOneDepth() const { return depth == DepthRange::ZeroToOne; }
    bool rowMajor() const { return major == MatrixMajor::Row; }
};

inline float depthFromNdc(float ndcZ, const Conventions& c)
{
    if (c.depth == DepthRange::ZeroToOne)
        return ndcZ;
    return ndcZ * 0.5f + 0.5f;
}

inline float ndcFromDepth(float d, const Conventions& c)
{
    if (c.depth == DepthRange::ZeroToOne)
        return d;
    return d * 2.0f - 1.0f;
}

inline float clipAZeroToOne(float zn, float zf)
{
    return zf / (zn - zf);
}

inline float clipBZeroToOne(float zn, float zf)
{
    return (zn * zf) / (zn - zf);
}

}
