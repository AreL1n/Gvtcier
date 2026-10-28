#pragma once
#include "bicu/core/math.h"
#include "bicu/core/conventions.h"
#include <string>

struct Player {
    Vec3 pos = Vec3(0, 0.0f, 0.0f);
    float yawDeg = 180.0f;
    float pitchDeg = 0.0f;
    float eyeHeight = 1.7f;
    float speed = 0.0f;
    float fov = gv::kDefaultFov;
    Vec3 wishDir;
    bool jumpRequested = false;
    bool running = false;
    Vec3 predictError;
    Vec3 predictErrorRot;
    uint32_t predictSeq = 0;

    Vec3 forward() const { return dirForward(yawDeg, pitchDeg); }

    void turn(float ddx, float ddy) {
        yawDeg -= ddx * 0.12f;
        pitchDeg -= ddy * 0.12f;
        if (pitchDeg > 89.0f) pitchDeg = 89.0f;
        if (pitchDeg < -89.0f) pitchDeg = -89.0f;
    }

    void move(bool fwd, bool back, bool left, bool right) {
        Vec3 f = dirForwardFlat(yawDeg);
        Vec3 r = dirRightFlat(yawDeg);
        Vec3 wish;
        if (fwd) wish += f;
        if (back) wish -= f;
        if (right) wish += r;
        if (left) wish -= r;
        wishDir = (wish.length() > 1e-6f) ? wish.normalized() : Vec3(0, 0, 0);
    }

    Mat4 viewMatrix() const {
        Vec3 eye = pos + Vec3(0, eyeHeight, 0);
        return Mat4::lookAt(eye, eye + forward(), Vec3(0, 1, 0));
    }
    Mat4 projMatrix(float aspect) const { return Mat4::perspective(fov, aspect, 0.05f, 2000.0f); }
};

struct Camera {
    Vec3 pos;
    float yawDeg = 180.0f;
    float pitchDeg = 0.0f;
    float fov = gv::kDefaultFov;
    float nearPlane = 0.05f;
    float farPlane = 2000.0f;
    std::string target;
    Vec3 offset;

    int mode = 0;
    float flySpeed = 12.0f;
    float rollRad = 0.0f;
    float steerRoll = 0.0f;
    float wobblePhase = 0.0f;
    float wobbleGain = 0.0f;
    Vec3 swayVel;

    Vec3 forward() const { return dirForward(yawDeg, pitchDeg); }
    void turn(float ddx, float ddy) {
        yawDeg -= ddx * 0.12f;
        pitchDeg -= ddy * 0.12f;
        if (pitchDeg > 89.0f) pitchDeg = 89.0f;
        if (pitchDeg < -89.0f) pitchDeg = -89.0f;
    }
    Mat4 viewMatrix() const {
        Vec3 r, u;
        Vec3 f = dirFromYawPitch(yawDeg, pitchDeg, r, u);
        if (std::fabs(rollRad) > 1e-6f) {
            float c = std::cos(rollRad), s = std::sin(rollRad);
            u = u * c + r * s;
        }
        return Mat4::lookAt(pos, pos + f, u);
    }
    Mat4 projMatrix(float aspect) const { return Mat4::perspective(fov, aspect, nearPlane, farPlane); }
};

inline void cameraMotionSway(Camera& cam, const Vec3& velocity, float dt, bool grounded) {
    if (dt > 0.1f) dt = 0.1f;
    if (dt < 0.0f) dt = 0.0f;

    float vk = std::fmin(1.0f, 10.0f * dt);
    cam.swayVel = cam.swayVel + (velocity - cam.swayVel) * vk;
    const Vec3& vel = cam.swayVel;

    const float kSteerMax = 4.5f;
    const float kRefSpeed = 6.0f;
    const float kSteerSmooth = 8.0f;
    const float kWobbleMax = 1.1f;
    const float kWobbleSmooth = 6.0f;

    Vec3 rF = dirRightFlat(cam.yawDeg);
    Vec3 fF = dirForwardFlat(cam.yawDeg);

    float lateral = vel.dot(rF);
    float forward = vel.dot(fF);

    float lateralN = lateral / kRefSpeed;
    if (lateralN > 1.0f) lateralN = 1.0f;
    if (lateralN < -1.0f) lateralN = -1.0f;

    float steerTarget = lateralN * deg2rad(kSteerMax);
    cam.steerRoll += (steerTarget - cam.steerRoll) * (kSteerSmooth * dt);

    float speedN = std::sqrt(vel.x * vel.x + vel.z * vel.z) / kRefSpeed;
    if (speedN > 1.0f) speedN = 1.0f;
    float fwdN = std::fabs(forward) / kRefSpeed;
    if (fwdN > 1.0f) fwdN = 1.0f;

    float gainTarget = (speedN > 0.03f && grounded) ? 1.0f : 0.0f;
    cam.wobbleGain += (gainTarget - cam.wobbleGain) * (kWobbleSmooth * dt);

    float wobble = 0.0f;
    if (cam.wobbleGain > 1e-3f) {
        cam.wobblePhase += dt * (5.2f + 4.0f * fwdN);
        wobble = std::sin(cam.wobblePhase) * deg2rad(kWobbleMax) * speedN * cam.wobbleGain;
    }
    if (cam.wobblePhase > 6.2831853f) cam.wobblePhase -= 6.2831853f;

    cam.rollRad = cam.steerRoll + wobble;
}
