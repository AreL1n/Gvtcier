#pragma once
#include "bicu/core/math.h"
#include "bicu/core/conventions.h"

constexpr float kPlayerTotalHeight = 1.8f;
constexpr float kPlayerRadius = 0.35f;
constexpr float kPlayerCylHalf = 0.55f;
constexpr float kPlayerTotalHalf = kPlayerCylHalf + kPlayerRadius;
constexpr float kPlayerWalkSpeed = 2.5f;
constexpr float kPlayerRunSpeed = 4.5f;
constexpr float kLockEps = 0.0001f;
constexpr float kAbsF(float v) { return v < 0.0f ? -v : v; }
static_assert(kAbsF(kPlayerTotalHeight - 1.8f) < kLockEps, "player total height is locked to 1.8");
static_assert(kAbsF(kPlayerRadius - 0.35f) < kLockEps, "player radius is locked to 0.35");
static_assert(kAbsF(kPlayerCylHalf - 0.55f) < kLockEps, "player capsule half height is locked to 0.55");
static_assert(kAbsF(kPlayerTotalHalf * 2.0f - kPlayerTotalHeight) < kLockEps, "player half height must match total height");
static_assert(kPlayerRadius > 0.0f, "player radius must be positive");
static_assert(kPlayerCylHalf > 0.0f, "player capsule half height must be positive");

struct PlayerSettings {
    float mass = 80.0f;
    float accel = 20.0f;
    float decel = 16.0f;
    float jumpSpeed = 9.0f;
    float speed = 0.0f;
    float walkSpeed = kPlayerWalkSpeed;
    float runSpeed = kPlayerRunSpeed;
    bool runEnabled = true;
    float eyeHeight = 1.62f;
    float pushFactor = 0.1f;
    float airControl = 0.12f;
    float fov = gv::kDefaultFov;
};
