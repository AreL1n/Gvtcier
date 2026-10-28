#include "Gvtcier.h"

// 脚本绑定：数学。脚本里可直接用的数学函数（绝对值、取整、三角、向量运算等）。
// 这些函数都是纯计算，不碰引擎状态，也没有副作用。

static int g_mathAbs(gvts_State* L) { gvts_pushnumber(L, std::fabs((double)gvtsG_checknumber(L, 1))); return 1; }

static int g_mathCeil(gvts_State* L) { gvts_pushnumber(L, std::ceil((double)gvtsG_checknumber(L, 1))); return 1; }

static int g_mathFloor(gvts_State* L) { gvts_pushnumber(L, std::floor((double)gvtsG_checknumber(L, 1))); return 1; }

static int g_mathSqrt(gvts_State* L) { gvts_pushnumber(L, std::sqrt((double)gvtsG_checknumber(L, 1))); return 1; }

static int g_mathSin(gvts_State* L) { gvts_pushnumber(L, std::sin((double)gvtsG_checknumber(L, 1))); return 1; }

static int g_mathCos(gvts_State* L) { gvts_pushnumber(L, std::cos((double)gvtsG_checknumber(L, 1))); return 1; }

static int g_mathTan(gvts_State* L) { gvts_pushnumber(L, std::tan((double)gvtsG_checknumber(L, 1))); return 1; }

static int g_mathAsin(gvts_State* L) { gvts_pushnumber(L, std::asin((double)gvtsG_checknumber(L, 1))); return 1; }

static int g_mathAcos(gvts_State* L) { gvts_pushnumber(L, std::acos((double)gvtsG_checknumber(L, 1))); return 1; }

static int g_mathAtan(gvts_State* L) {
    double y = (double)gvtsG_checknumber(L, 1);
    double x = (double)gvtsG_optnumber(L, 2, 1.0);
    gvts_pushnumber(L, std::atan2(y, x));
    return 1;
}

static int g_mathAtan2(gvts_State* L) {
    double y = (double)gvtsG_checknumber(L, 1);
    double x = (double)gvtsG_checknumber(L, 2);
    gvts_pushnumber(L, std::atan2(y, x));
    return 1;
}

static int g_mathDeg(gvts_State* L) { gvts_pushnumber(L, (double)gvtsG_checknumber(L, 1) * (180.0 / 3.14159265358979323846)); return 1; }

static int g_mathRad(gvts_State* L) { gvts_pushnumber(L, (double)gvtsG_checknumber(L, 1) * (3.14159265358979323846 / 180.0)); return 1; }

static int g_mathExp(gvts_State* L) { gvts_pushnumber(L, std::exp((double)gvtsG_checknumber(L, 1))); return 1; }

static int g_mathLog(gvts_State* L) {
    double x = (double)gvtsG_checknumber(L, 1);
    double b = (double)gvtsG_optnumber(L, 2, 2.71828182845904523536);
    gvts_pushnumber(L, std::log(x) / std::log(b));
    return 1;
}

static int g_mathPow(gvts_State* L) {
    double a = (double)gvtsG_checknumber(L, 1);
    double b = (double)gvtsG_checknumber(L, 2);
    gvts_pushnumber(L, std::pow(a, b));
    return 1;
}

static int g_mathMin(gvts_State* L) {
    int n = gvts_gettop(L);
    double m = (double)gvtsG_checknumber(L, 1);
    for (int i = 2; i <= n; ++i) { double v = (double)gvtsG_checknumber(L, i); if (v < m) m = v; }
    gvts_pushnumber(L, m);
    return 1;
}

static int g_mathMax(gvts_State* L) {
    int n = gvts_gettop(L);
    double m = (double)gvtsG_checknumber(L, 1);
    for (int i = 2; i <= n; ++i) { double v = (double)gvtsG_checknumber(L, i); if (v > m) m = v; }
    gvts_pushnumber(L, m);
    return 1;
}

static int g_mathMod(gvts_State* L) {
    double a = (double)gvtsG_checknumber(L, 1);
    double b = (double)gvtsG_checknumber(L, 2);
    gvts_pushnumber(L, std::fmod(a, b));
    return 1;
}

static int g_mathClamp(gvts_State* L) {
    double v = (double)gvtsG_checknumber(L, 1);
    double lo = (double)gvtsG_checknumber(L, 2);
    double hi = (double)gvtsG_checknumber(L, 3);
    if (v < lo) v = lo;
    if (v > hi) v = hi;
    gvts_pushnumber(L, v);
    return 1;
}

static int g_mathLerp(gvts_State* L) {
    double a = (double)gvtsG_checknumber(L, 1);
    double b = (double)gvtsG_checknumber(L, 2);
    double t = (double)gvtsG_checknumber(L, 3);
    gvts_pushnumber(L, a + (b - a) * t);
    return 1;
}

static int g_mathSign(gvts_State* L) {
    double v = (double)gvtsG_checknumber(L, 1);
    gvts_pushnumber(L, v > 0.0 ? 1.0 : (v < 0.0 ? -1.0 : 0.0));
    return 1;
}

static const gvtsG_Reg g_mathLib[] = {
    { "绝对值", g_mathAbs },
    { "向上取整", g_mathCeil },
    { "向下取整", g_mathFloor },
    { "开方", g_mathSqrt },
    { "正弦", g_mathSin },
    { "余弦", g_mathCos },
    { "正切", g_mathTan },
    { "反正弦", g_mathAsin },
    { "反余弦", g_mathAcos },
    { "反正切", g_mathAtan },
    { "方位角", g_mathAtan2 },
    { "转角度", g_mathDeg },
    { "转弧度", g_mathRad },
    { "指数", g_mathExp },
    { "对数", g_mathLog },
    { "幂", g_mathPow },
    { "最小", g_mathMin },
    { "最大", g_mathMax },
    { "取余", g_mathMod },
    { "限制", g_mathClamp },
    { "插值", g_mathLerp },
    { "符号", g_mathSign },
    { NULL, NULL }
};

void openMathLibCN(gvts_State* L) {
    gvtsG_newlib(L, g_mathLib);
    gvts_setglobal(L, "数学");
}

static int g_gvVec(gvts_State* L) {
    float x = (float)gvtsG_optnumber(L, 1, 0.0);
    float y = (float)gvtsG_optnumber(L, 2, 0.0);
    float z = (float)gvtsG_optnumber(L, 3, 0.0);
    pushVec3(L, Vec3(x, y, z));
    return 1;
}

static int g_gvVecAdd(gvts_State* L) {
    Vec3 a = readVec3(L, 1);
    Vec3 b = readVec3(L, 1 + vec3Slots(L, 1));
    pushVec3(L, a + b);
    return 1;
}

static int g_gvVecSub(gvts_State* L) {
    Vec3 a = readVec3(L, 1);
    Vec3 b = readVec3(L, 1 + vec3Slots(L, 1));
    pushVec3(L, a - b);
    return 1;
}

static int g_gvVecMul(gvts_State* L) {
    Vec3 a = readVec3(L, 1);
    float s = (float)gvtsG_checknumber(L, 1 + vec3Slots(L, 1));
    pushVec3(L, a * s);
    return 1;
}

static int g_gvVecDiv(gvts_State* L) {
    Vec3 a = readVec3(L, 1);
    float s = (float)gvtsG_checknumber(L, 1 + vec3Slots(L, 1));
    if (std::fabs(s) < 1e-8f) { pushVec3(L, Vec3(0, 0, 0)); return 1; }
    pushVec3(L, a / s);
    return 1;
}

static int g_gvVecDot(gvts_State* L) {
    Vec3 a = readVec3(L, 1);
    Vec3 b = readVec3(L, 1 + vec3Slots(L, 1));
    gvts_pushnumber(L, a.dot(b));
    return 1;
}

static int g_gvVecCross(gvts_State* L) {
    Vec3 a = readVec3(L, 1);
    Vec3 b = readVec3(L, 1 + vec3Slots(L, 1));
    pushVec3(L, a.cross(b));
    return 1;
}

static int g_gvVecLen(gvts_State* L) {
    gvts_pushnumber(L, readVec3(L, 1).length());
    return 1;
}

static int g_gvVecNormalize(gvts_State* L) {
    pushVec3(L, readVec3(L, 1).normalized());
    return 1;
}

static int g_gvVecDistance(gvts_State* L) {
    Vec3 a = readVec3(L, 1);
    Vec3 b = readVec3(L, 1 + vec3Slots(L, 1));
    gvts_pushnumber(L, (a - b).length());
    return 1;
}

static int g_gvVecLerp(gvts_State* L) {
    Vec3 a = readVec3(L, 1);
    int bi = 1 + vec3Slots(L, 1);
    Vec3 b = readVec3(L, bi);
    float t = (float)gvtsG_checknumber(L, bi + vec3Slots(L, bi));
    pushVec3(L, a + (b - a) * t);
    return 1;
}

static int g_gvVecScale(gvts_State* L) {
    Vec3 a = readVec3(L, 1);
    Vec3 b = readVec3(L, 1 + vec3Slots(L, 1));
    pushVec3(L, Vec3(a.x * b.x, a.y * b.y, a.z * b.z));
    return 1;
}

static int g_gvMatIdentity(gvts_State* L) {
    pushMat4(L, Mat4::identity());
    return 1;
}

static int g_gvMatMul(gvts_State* L) {
    Mat4 a = readMat4(L, 1);
    Mat4 b = readMat4(L, 2);
    pushMat4(L, a * b);
    return 1;
}

static int g_gvMatInverse(gvts_State* L) {
    pushMat4(L, readMat4(L, 1).inverse());
    return 1;
}

static int g_gvMatTranslate(gvts_State* L) {
    pushMat4(L, Mat4::translate(readVec3(L, 1)));
    return 1;
}

static int g_gvMatRotateX(gvts_State* L) {
    pushMat4(L, Mat4::rotateX((float)gvtsG_checknumber(L, 1)));
    return 1;
}

static int g_gvMatRotateY(gvts_State* L) {
    pushMat4(L, Mat4::rotateY((float)gvtsG_checknumber(L, 1)));
    return 1;
}

static int g_gvMatRotateZ(gvts_State* L) {
    pushMat4(L, Mat4::rotateZ((float)gvtsG_checknumber(L, 1)));
    return 1;
}

static int g_gvMatScale(gvts_State* L) {
    pushMat4(L, Mat4::scale(readVec3(L, 1)));
    return 1;
}

static int g_gvMatLookAt(gvts_State* L) {
    Vec3 eye = readVec3(L, 1);
    int i2 = 1 + vec3Slots(L, 1);
    Vec3 center = readVec3(L, i2);
    int i3 = i2 + vec3Slots(L, i2);
    Vec3 up = AxisUp;
    if (gvts_gettop(L) >= i3) up = readVec3(L, i3);
    pushMat4(L, Mat4::lookAt(eye, center, up));
    return 1;
}

static int g_gvMatPerspective(gvts_State* L) {
    float fov = (float)gvtsG_checknumber(L, 1);
    float aspect = (float)gvtsG_checknumber(L, 2);
    float zn = (float)gvtsG_checknumber(L, 3);
    float zf = (float)gvtsG_checknumber(L, 4);
    pushMat4(L, Mat4::perspective(fov, aspect, zn, zf));
    return 1;
}

static int g_gvMatMulPoint(gvts_State* L) {
    Mat4 m = readMat4(L, 1);
    Vec3 v = readVec3(L, 2);
    Vec4 r = m * Vec4(v.x, v.y, v.z, 1.0f);
    float inv = (std::fabs(r.w) > 1e-8f) ? (1.0f / r.w) : 1.0f;
    pushVec3(L, Vec3(r.x * inv, r.y * inv, r.z * inv));
    return 1;
}

static int g_gvVecGet(gvts_State* L) {
    Vec3 a = readVec3(L, 1);
    const char* c = gvtsG_checkstring(L, 1 + vec3Slots(L, 1));
    if (strcmp(c, "x") == 0 || strcmp(c, "X") == 0) gvts_pushnumber(L, a.x);
    else if (strcmp(c, "y") == 0 || strcmp(c, "Y") == 0) gvts_pushnumber(L, a.y);
    else if (strcmp(c, "z") == 0 || strcmp(c, "Z") == 0) gvts_pushnumber(L, a.z);
    else gvts_pushnumber(L, 0.0);
    return 1;
}

static int g_gvQuatIdentity(gvts_State* L) {
    pushQuat(L, Quat::identity());
    return 1;
}

static int g_gvQuatFromAxis(gvts_State* L) {
    Vec3 axis = readVec3(L, 1);
    float rad = (float)gvtsG_checknumber(L, 1 + vec3Slots(L, 1));
    pushQuat(L, Quat::fromAxisAngle(axis, rad));
    return 1;
}

static int g_gvQuatFromEuler(gvts_State* L) {
    float p = (float)gvtsG_optnumber(L, 1, 0.0);
    float y = (float)gvtsG_optnumber(L, 2, 0.0);
    float r = (float)gvtsG_optnumber(L, 3, 0.0);
    pushQuat(L, Quat::fromEulerDeg(p, y, r));
    return 1;
}

static int g_gvQuatMul(gvts_State* L) {
    Quat a = readQuat(L, 1);
    Quat b = readQuat(L, 2);
    pushQuat(L, a * b);
    return 1;
}

static int g_gvQuatRotate(gvts_State* L) {
    Quat q = readQuat(L, 1);
    Vec3 v = readVec3(L, 2);
    pushVec3(L, q * v);
    return 1;
}

static int g_gvQuatSlerp(gvts_State* L) {
    Quat a = readQuat(L, 1);
    Quat b = readQuat(L, 2);
    float t = (float)gvtsG_checknumber(L, 3);
    pushQuat(L, a.slerp(b, t));
    return 1;
}

static int g_gvQuatNormalize(gvts_State* L) {
    pushQuat(L, readQuat(L, 1).normalized());
    return 1;
}

static int g_gvQuatToMat(gvts_State* L) {
    pushMat4(L, readQuat(L, 1).toMat4());
    return 1;
}

static int g_gvQuatToEuler(gvts_State* L) {
    Vec3 e = readQuat(L, 1).toEulerDeg();
    gvts_pushnumber(L, e.x);
    gvts_pushnumber(L, e.y);
    gvts_pushnumber(L, e.z);
    return 3;
}

static int g_gvRaySphere(gvts_State* L) {
    Vec3 o = readVec3(L, 1);
    int i2 = 1 + vec3Slots(L, 1);
    Vec3 d = readVec3(L, i2);
    int i3 = i2 + vec3Slots(L, i2);
    Vec3 c = readVec3(L, i3);
    float r = (float)gvtsG_checknumber(L, i3 + vec3Slots(L, i3));
    Vec3 oc = o - c;
    float a = d.dot(d);
    float b = 2.0f * oc.dot(d);
    float cc = oc.dot(oc) - r * r;
    float disc = b * b - 4.0f * a * cc;
    if (disc < 0.0f || a < 1e-8f) {
        gvts_pushboolean(L, 0);
        return 1;
    }
    float sq = std::sqrt(disc);
    float t0 = (-b - sq) / (2.0f * a);
    float t1 = (-b + sq) / (2.0f * a);
    float t = (t0 >= 0.0f) ? t0 : t1;
    if (t < 0.0f) {
        gvts_pushboolean(L, 0);
        return 1;
    }
    gvts_pushboolean(L, 1);
    gvts_pushnumber(L, t);
    pushVec3(L, o + d * t);
    return 3;
}

static int g_gvRayPlane(gvts_State* L) {
    Vec3 o = readVec3(L, 1);
    int i2 = 1 + vec3Slots(L, 1);
    Vec3 d = readVec3(L, i2);
    int i3 = i2 + vec3Slots(L, i2);
    Vec3 pn = readVec3(L, i3);
    float pd = (float)gvtsG_checknumber(L, i3 + vec3Slots(L, i3));
    float denom = pn.dot(d);
    if (std::fabs(denom) < 1e-8f) {
        gvts_pushboolean(L, 0);
        return 1;
    }
    float t = (pd - pn.dot(o)) / denom;
    if (t < 0.0f) {
        gvts_pushboolean(L, 0);
        return 1;
    }
    gvts_pushboolean(L, 1);
    gvts_pushnumber(L, t);
    pushVec3(L, o + d * t);
    return 3;
}

static int g_gvRayBox(gvts_State* L) {
    Vec3 o = readVec3(L, 1);
    int i2 = 1 + vec3Slots(L, 1);
    Vec3 d = readVec3(L, i2);
    int i3 = i2 + vec3Slots(L, i2);
    Vec3 bmin = readVec3(L, i3);
    int i4 = i3 + vec3Slots(L, i3);
    Vec3 bmax = readVec3(L, i4);
    float tmin = -1e30f, tmax = 1e30f;
    float od[3] = { o.x, o.y, o.z };
    float dd[3] = { d.x, d.y, d.z };
    float lo[3] = { bmin.x, bmin.y, bmin.z };
    float hi[3] = { bmax.x, bmax.y, bmax.z };
    for (int i = 0; i < 3; ++i) {
        if (std::fabs(dd[i]) < 1e-8f) {
            if (od[i] < lo[i] || od[i] > hi[i]) { gvts_pushboolean(L, 0); return 1; }
            continue;
        }
        float inv = 1.0f / dd[i];
        float t1 = (lo[i] - od[i]) * inv;
        float t2 = (hi[i] - od[i]) * inv;
        if (t1 > t2) { float tmp = t1; t1 = t2; t2 = tmp; }
        if (t1 > tmin) tmin = t1;
        if (t2 < tmax) tmax = t2;
        if (tmin > tmax) { gvts_pushboolean(L, 0); return 1; }
    }
    if (tmax < 0.0f) { gvts_pushboolean(L, 0); return 1; }
    float t = (tmin >= 0.0f) ? tmin : tmax;
    gvts_pushboolean(L, 1);
    gvts_pushnumber(L, t);
    pushVec3(L, o + d * t);
    return 3;
}

static int g_gvClosestPoint(gvts_State* L) {
    Vec3 p = readVec3(L, 1);
    int i2 = 1 + vec3Slots(L, 1);
    Vec3 a = readVec3(L, i2);
    Vec3 b = readVec3(L, i2 + vec3Slots(L, i2));
    Vec3 ab = b - a;
    float len2 = ab.dot(ab);
    if (len2 < 1e-8f) { pushVec3(L, a); return 1; }
    float t = (p - a).dot(ab) / len2;
    if (t < 0.0f) t = 0.0f;
    if (t > 1.0f) t = 1.0f;
    pushVec3(L, a + ab * t);
    return 1;
}

static int g_gvPointPlaneSide(gvts_State* L) {
    Vec3 p = readVec3(L, 1);
    int i2 = 1 + vec3Slots(L, 1);
    Vec3 pn = readVec3(L, i2);
    float pd = (float)gvtsG_checknumber(L, i2 + vec3Slots(L, i2));
    gvts_pushnumber(L, pn.dot(p) - pd);
    return 1;
}

static int g_gvProjectOnPlane(gvts_State* L) {
    Vec3 p = readVec3(L, 1);
    int i2 = 1 + vec3Slots(L, 1);
    Vec3 pn = readVec3(L, i2);
    float pd = (float)gvtsG_checknumber(L, i2 + vec3Slots(L, i2));
    Vec3 n = pn.normalized();
    pushVec3(L, p - n * (n.dot(p) - pd));
    return 1;
}

static const gvtsG_Reg g_mathGvLib[] = {
    { "向量", g_gvVec },
    { "向量加", g_gvVecAdd },
    { "向量减", g_gvVecSub },
    { "向量乘", g_gvVecMul },
    { "向量除", g_gvVecDiv },
    { "点乘", g_gvVecDot },
    { "叉乘", g_gvVecCross },
    { "长度", g_gvVecLen },
    { "归一化", g_gvVecNormalize },
    { "距离", g_gvVecDistance },
    { "向量插值", g_gvVecLerp },
    { "逐分量乘", g_gvVecScale },
    { "取分量", g_gvVecGet },

    { "单位矩阵", g_gvMatIdentity },
    { "矩阵乘", g_gvMatMul },
    { "矩阵逆", g_gvMatInverse },
    { "平移矩阵", g_gvMatTranslate },
    { "绕X轴旋转", g_gvMatRotateX },
    { "绕Y轴旋转", g_gvMatRotateY },
    { "绕Z轴旋转", g_gvMatRotateZ },
    { "缩放矩阵", g_gvMatScale },
    { "观察矩阵", g_gvMatLookAt },
    { "透视矩阵", g_gvMatPerspective },
    { "变换点", g_gvMatMulPoint },

    { "单位四元数", g_gvQuatIdentity },
    { "轴角四元数", g_gvQuatFromAxis },
    { "欧拉四元数", g_gvQuatFromEuler },
    { "四元数乘", g_gvQuatMul },
    { "四元数旋转", g_gvQuatRotate },
    { "球面插值", g_gvQuatSlerp },
    { "四元数归一", g_gvQuatNormalize },
    { "四元数转矩阵", g_gvQuatToMat },
    { "四元数转欧拉", g_gvQuatToEuler },

    { "射线与球求交", g_gvRaySphere },
    { "射线与平面求交", g_gvRayPlane },
    { "射线与盒求交", g_gvRayBox },
    { "线段最近点", g_gvClosestPoint },
    { "点在平面哪侧", g_gvPointPlaneSide },
    { "投影到平面", g_gvProjectOnPlane },
    { NULL, NULL }
};

void openMathGvLib(gvts_State* L) {
    gvtsG_newlib(L, g_mathGvLib);
    gvts_setglobal(L, "MathGv");
}

void Gvtcier_Bind_Math(gvts_State* L, GvtsEngineCtx* ctx) {
}
