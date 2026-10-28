#pragma once
#include <cmath>
#include "bicu/core/conventions.h"

using gv::Conventions;
using gv::DepthRange;

struct Vec2 {
    double x, y;
    Vec2() : x(0), y(0) {}
    Vec2(double x_, double y_) : x(x_), y(y_) {}
    Vec2 operator+(const Vec2& o) const { return Vec2(x + o.x, y + o.y); }
    Vec2 operator-(const Vec2& o) const { return Vec2(x - o.x, y - o.y); }
    Vec2 operator*(double s) const { return Vec2(x * s, y * s); }
    double length() const { return std::sqrt(x * x + y * y); }
    double dot(const Vec2& o) const { return x * o.x + y * o.y; }
};

struct Vec3 {
    double x, y, z;
    Vec3() : x(0), y(0), z(0) {}
    Vec3(double x_, double y_, double z_) : x(x_), y(y_), z(z_) {}
    Vec3 operator+(const Vec3& o) const { return Vec3(x + o.x, y + o.y, z + o.z); }
    Vec3 operator-(const Vec3& o) const { return Vec3(x - o.x, y - o.y, z - o.z); }
    Vec3 operator-() const { return Vec3(-x, -y, -z); }
    Vec3 operator*(double s) const { return Vec3(x * s, y * s, z * s); }
    Vec3 operator/(double s) const { return Vec3(x / s, y / s, z / s); }
    Vec3& operator+=(const Vec3& o) { x += o.x; y += o.y; z += o.z; return *this; }
    Vec3& operator-=(const Vec3& o) { x -= o.x; y -= o.y; z -= o.z; return *this; }
    Vec3& operator*=(double s) { x *= s; y *= s; z *= s; return *this; }
    double length() const { return std::sqrt(x * x + y * y + z * z); }
    Vec3 normalized() const {
        double l = length();
        return l > 1e-8 ? Vec3(x / l, y / l, z / l) : Vec3(0, 0, 0);
    }
    double dot(const Vec3& o) const { return x * o.x + y * o.y + z * o.z; }
    Vec3 cross(const Vec3& o) const {
        return Vec3(y * o.z - z * o.y, z * o.x - x * o.z, x * o.y - y * o.x);
    }
};

struct Vec4 {
    double x, y, z, w;
    Vec4() : x(0), y(0), z(0), w(1) {}
    Vec4(double x_, double y_, double z_, double w_) : x(x_), y(y_), z(z_), w(w_) {}
    double dot(const Vec4& o) const { return x * o.x + y * o.y + z * o.z + w * o.w; }
    Vec4 operator*(double s) const { return Vec4(x * s, y * s, z * s, w * s); }
};

inline double deg2rad(double d) { return d * 3.14159265358979323846 / 180.0; }
inline double rad2deg(double r) { return r * 180.0 / 3.14159265358979323846; }

inline const Vec3 AxisRight(1.0, 0.0, 0.0);
inline const Vec3 AxisUp(0.0, 1.0, 0.0);
inline const Vec3 AxisForward(0.0, 0.0, 1.0);

inline Vec3 dirFromYawPitch(double yawDeg, double pitchDeg, Vec3& outRight, Vec3& outUp) {
    double y = deg2rad(yawDeg), p = deg2rad(pitchDeg);
    double cp = std::cos(p);
    Vec3 f(cp * std::sin(y), std::sin(p), cp * std::cos(y));
    outRight = f.cross(AxisUp).normalized();
    if (outRight.length() < 1e-6) outRight = AxisRight;
    outUp = outRight.cross(f).normalized();
    return f;
}

inline Vec3 dirForward(double yawDeg, double pitchDeg) {
    Vec3 r, u;
    return dirFromYawPitch(yawDeg, pitchDeg, r, u);
}

inline Vec3 dirRight(double yawDeg, double pitchDeg) {
    Vec3 r, u;
    dirFromYawPitch(yawDeg, pitchDeg, r, u);
    return r;
}

inline Vec3 dirUp(double yawDeg, double pitchDeg) {
    Vec3 r, u;
    dirFromYawPitch(yawDeg, pitchDeg, r, u);
    return u;
}

inline Vec3 dirForwardFlat(double yawDeg) {
    double y = deg2rad(yawDeg);
    Vec3 f(std::sin(y), 0.0, std::cos(y));
    return f.normalized();
}

inline Vec3 dirRightFlat(double yawDeg) {
    Vec3 f = dirForwardFlat(yawDeg);
    Vec3 r = f.cross(AxisUp).normalized();
    if (r.length() < 1e-6) r = AxisRight;
    return r;
}

struct Mat4 {
    double m[16];

    Mat4() { setIdentity(); }

    void setIdentity() {
        for (int i = 0; i < 16; ++i) m[i] = (i % 5 == 0) ? 1.0 : 0.0;
    }
    static Mat4 identity() { return Mat4(); }

    double& operator[](int i) { return m[i]; }
    double operator[](int i) const { return m[i]; }

    Mat4 operator*(const Mat4& b) const {
        Mat4 r;
        for (int c = 0; c < 4; ++c)
            for (int row = 0; row < 4; ++row) {
                double s = 0;
                for (int k = 0; k < 4; ++k) s += m[k * 4 + row] * b.m[c * 4 + k];
                r.m[c * 4 + row] = s;
            }
        return r;
    }

    Vec4 operator*(const Vec4& v) const {
        return Vec4(
            m[0] * v.x + m[4] * v.y + m[8] * v.z + m[12] * v.w,
            m[1] * v.x + m[5] * v.y + m[9] * v.z + m[13] * v.w,
            m[2] * v.x + m[6] * v.y + m[10] * v.z + m[14] * v.w,
            m[3] * v.x + m[7] * v.y + m[11] * v.z + m[15] * v.w);
    }

    Vec3 operator*(const Vec3& v) const {
        Vec4 r = (*this) * Vec4(v.x, v.y, v.z, 1.0);
        double inv = (std::fabs(r.w) > 1e-8) ? (1.0 / r.w) : 1.0;
        return Vec3(r.x * inv, r.y * inv, r.z * inv);
    }

    Mat4 inverse() const {
        double a[16];
        for (int i = 0; i < 16; ++i) a[i] = m[i];
        double inv[16] = { 1,0,0,0, 0,1,0,0, 0,0,1,0, 0,0,0,1 };
        for (int col = 0; col < 4; ++col) {
            int piv = col;
            for (int r = col + 1; r < 4; ++r)
                if (std::fabs(a[r * 4 + col]) > std::fabs(a[piv * 4 + col])) piv = r;
            if (std::fabs(a[piv * 4 + col]) < 1e-12) return Mat4();
            if (piv != col) {
                for (int k = 0; k < 4; ++k) {
                    double t = a[piv * 4 + k]; a[piv * 4 + k] = a[col * 4 + k]; a[col * 4 + k] = t;
                    t = inv[piv * 4 + k]; inv[piv * 4 + k] = inv[col * 4 + k]; inv[col * 4 + k] = t;
                }
            }
            double d = a[col * 4 + col];
            for (int k = 0; k < 4; ++k) { a[col * 4 + k] /= d; inv[col * 4 + k] /= d; }
            for (int r = 0; r < 4; ++r) {
                if (r == col) continue;
                double f = a[r * 4 + col];
                if (std::fabs(f) < 1e-15) continue;
                for (int k = 0; k < 4; ++k) {
                    a[r * 4 + k] -= f * a[col * 4 + k];
                    inv[r * 4 + k] -= f * inv[col * 4 + k];
                }
            }
        }
        Mat4 r;
        for (int i = 0; i < 16; ++i) r.m[i] = inv[i];
        return r;
    }

    static Mat4 perspective(double fovyDeg, double aspect, double zn, double zf) {
        return perspective(fovyDeg, aspect, zn, zf, Conventions::active());
    }

    static Mat4 perspective(double fovyDeg, double aspect, double zn, double zf, const Conventions& c) {
        Mat4 r;
        double f = 1.0 / std::tan(deg2rad(fovyDeg) * 0.5);
        r.m[0] = f / aspect;
        r.m[5] = f;
        r.m[11] = -1.0;
        r.m[15] = 0.0;
        if (c.depth == DepthRange::ZeroToOne) {
            r.m[10] = zf / (zn - zf);
            r.m[14] = (zn * zf) / (zn - zf);
        } else {
            r.m[10] = (zf + zn) / (zf - zn);
            r.m[14] = (2.0 * zf * zn) / (zf - zn);
        }
        return r;
    }

    static Mat4 ortho(double l, double r, double b, double t, double zn, double zf) {
        return ortho(l, r, b, t, zn, zf, Conventions::active());
    }

    static Mat4 ortho(double l, double r, double b, double t, double zn, double zf, const Conventions& c) {
        Mat4 o;
        o.m[0] = 2.0 / (r - l);
        o.m[5] = 2.0 / (t - b);
        o.m[12] = -(r + l) / (r - l);
        o.m[13] = -(t + b) / (t - b);
        if (c.depth == DepthRange::ZeroToOne) {
            o.m[10] = -1.0 / (zf - zn);
            o.m[14] = -zn / (zf - zn);
        } else {
            o.m[10] = 2.0 / (zf - zn);
            o.m[14] = (zf + zn) / (zf - zn);
        }
        return o;
    }

    static Mat4 lookAt(Vec3 eye, Vec3 center, Vec3 up) {
        Vec3 f = (center - eye).normalized();
        Vec3 s = f.cross(up).normalized();
        Vec3 u = s.cross(f);
        Mat4 m;
        m.m[0] = s.x;  m.m[4] = s.y;  m.m[8] = s.z;   m.m[12] = -s.dot(eye);
        m.m[1] = u.x;  m.m[5] = u.y;  m.m[9] = u.z;   m.m[13] = -u.dot(eye);
        m.m[2] = -f.x; m.m[6] = -f.y; m.m[10] = -f.z; m.m[14] = f.dot(eye);
        m.m[3] = 0;    m.m[7] = 0;    m.m[11] = 0;    m.m[15] = 1.0;
        return m;
    }

    static Mat4 translate(Vec3 t) {
        Mat4 r;
        r.m[12] = t.x; r.m[13] = t.y; r.m[14] = t.z;
        return r;
    }

    static Mat4 rotateX(double rad) {
        Mat4 r;
        double c = std::cos(rad), s = std::sin(rad);
        r.m[5] = c; r.m[6] = s; r.m[9] = -s; r.m[10] = c;
        return r;
    }

    static Mat4 rotateY(double rad) {
        Mat4 r;
        double c = std::cos(rad), s = std::sin(rad);
        r.m[0] = c; r.m[2] = -s; r.m[8] = s; r.m[10] = c;
        return r;
    }

    static Mat4 rotateZ(double rad) {
        Mat4 r;
        double c = std::cos(rad), s = std::sin(rad);
        r.m[0] = c; r.m[1] = s; r.m[4] = -s; r.m[5] = c;
        return r;
    }

    static Mat4 scale(Vec3 s) {
        Mat4 r;
        r.m[0] = s.x; r.m[5] = s.y; r.m[10] = s.z;
        return r;
    }

    void toFloat(float* out) const {
        for (int i = 0; i < 16; ++i) out[i] = (float)m[i];
    }
};

struct Quat {
    double x, y, z, w;

    Quat() : x(0), y(0), z(0), w(1) {}
    Quat(double x_, double y_, double z_, double w_) : x(x_), y(y_), z(z_), w(w_) {}

    static Quat identity() { return Quat(0, 0, 0, 1); }

    static Quat fromAxisAngle(Vec3 axis, double rad) {
        Vec3 a = axis.normalized();
        double s = std::sin(rad * 0.5);
        return Quat(a.x * s, a.y * s, a.z * s, std::cos(rad * 0.5));
    }

    static Quat fromEulerDeg(double pitchDeg, double yawDeg, double rollDeg) {
        double p = deg2rad(pitchDeg) * 0.5, y = deg2rad(yawDeg) * 0.5, r = deg2rad(rollDeg) * 0.5;
        double sp = std::sin(p), cp = std::cos(p);
        double sy = std::sin(y), cy = std::cos(y);
        double sr = std::sin(r), cr = std::cos(r);
        return Quat(
            cy * sp * cr + cp * sy * sr,
            cp * sy * cr - cy * sp * sr,
            cp * cy * sr + sp * sy * cr,
            cp * cy * cr - sp * sy * sr);
    }

    double length() const { return std::sqrt(x * x + y * y + z * z + w * w); }
    Quat normalized() const {
        double l = length();
        return l > 1e-8 ? Quat(x / l, y / l, z / l, w / l) : Quat();
    }
    Quat conjugate() const { return Quat(-x, -y, -z, w); }
    Quat inverse() const { return conjugate().normalized(); }
    double dot(const Quat& o) const { return x * o.x + y * o.y + z * o.z + w * o.w; }

    Quat operator*(const Quat& o) const {
        return Quat(
            w * o.x + x * o.w + y * o.z - z * o.y,
            w * o.y - x * o.z + y * o.w + z * o.x,
            w * o.z + x * o.y - y * o.x + z * o.w,
            w * o.w - x * o.x - y * o.y - z * o.z);
    }

    Vec3 operator*(const Vec3& v) const {
        Vec3 u(x, y, z);
        Vec3 t = u.cross(v) * 2.0;
        return v + t * w + u.cross(t);
    }

    Quat slerp(const Quat& target, double t) const {
        Quat a = normalized();
        Quat b = target.normalized();
        double d = a.dot(b);
        if (d < 0.0) { b = Quat(-b.x, -b.y, -b.z, -b.w); d = -d; }
        if (d > 0.9995) {
            Quat r = Quat(a.x + (b.x - a.x) * t, a.y + (b.y - a.y) * t,
                          a.z + (b.z - a.z) * t, a.w + (b.w - a.w) * t);
            return r.normalized();
        }
        double th = std::acos(d);
        double sa = std::sin(th);
        double s0 = std::sin((1.0 - t) * th) / sa;
        double s1 = std::sin(t * th) / sa;
        return Quat(a.x * s0 + b.x * s1, a.y * s0 + b.y * s1,
                    a.z * s0 + b.z * s1, a.w * s0 + b.w * s1).normalized();
    }

    Mat4 toMat4() const {
        Mat4 m;
        double xx = x * x, yy = y * y, zz = z * z;
        double xy = x * y, xz = x * z, yz = y * z;
        double wx = w * x, wy = w * y, wz = w * z;
        m.m[0] = 1.0 - 2.0 * (yy + zz);
        m.m[1] = 2.0 * (xy + wz);
        m.m[2] = 2.0 * (xz - wy);
        m.m[4] = 2.0 * (xy - wz);
        m.m[5] = 1.0 - 2.0 * (xx + zz);
        m.m[6] = 2.0 * (yz + wx);
        m.m[8] = 2.0 * (xz + wy);
        m.m[9] = 2.0 * (yz - wx);
        m.m[10] = 1.0 - 2.0 * (xx + yy);
        return m;
    }

    Vec3 toEulerDeg() const {
        Mat4 m = toMat4();
        double yaw = std::asin(std::fmin(1.0, std::fmax(-1.0, m.m[8])));
        double pitch = std::atan2(-m.m[9], m.m[10]);
        double roll = std::atan2(-m.m[4], m.m[0]);
        return Vec3(rad2deg(pitch), rad2deg(yaw), rad2deg(roll));
    }
};
