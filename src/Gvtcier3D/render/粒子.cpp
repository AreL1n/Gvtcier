#include "Gvtcier.h"
#include <cmath>
#include <cstdlib>

namespace {

// 把容器裁到上限 cap，从最老的一头开始删。
// 为了不每帧都 erase 一次，删除量取 cap/4 的批量：超出时才一次性砍掉一批，
// 这样超上限后粒子数会在 cap 和 cap 的 3/4 之间来回，避免逐个删除带来的抖动。
// cap <= 0 表示上限是 0，直接清空。
template <typename T>
void trimOldest(std::vector<T>& v, int cap) {
    if (cap <= 0) { v.clear(); return; }
    if ((int)v.size() <= cap) return;
    size_t drop = v.size() - (size_t)cap;
    size_t batch = (size_t)cap / 4;
    if (batch < 1) batch = 1;
    if (drop < batch) drop = batch;
    if (drop > v.size()) drop = v.size();
    v.erase(v.begin(), v.begin() + (ptrdiff_t)drop);
}

}

// 每帧推进所有粒子：寿命递减、按重力积分速度与位置、尺寸按 growth 生长。
// 寿命耗尽的粒子用“尾部覆盖法”删除——把最后一个元素搬到当前槽位再 pop_back，
// 这样删除是 O(1)，代价是粒子顺序会被打乱（对加色粒子无所谓，不需要排序）。
// dt 是帧间隔秒数。
void FxSystem::Gvtcier_Fx_Update(float dt) {
    for (size_t i = 0; i < particles_.size();) {
        FxParticle& p = particles_[i];
        p.life -= dt;
        if (p.life <= 0.0f) {
            particles_[i] = particles_.back();
            particles_.pop_back();
            continue;
        }
        // 速度按重力做显式欧拉积分，再拿速度推位置；size 按 growth 线性变大
        p.vel.y -= p.gravity * dt;
        p.pos += p.vel * dt;
        p.size += p.growth * dt;
        p.rotation += p.spin * dt;
        i++;
    }
}

// 清空这一帧的全部瞬时特效：粒子、光束、动态光源都会消失。
// 常驻光源 persistentLights_ 不清，由 Gvtcier_Fx_AddPersistentLight 自己维护。
void FxSystem::Gvtcier_Fx_Clear() {
    particles_.clear();
    beams_.clear();
    lights_.clear();
}

// 生成一颗粒子。数量接近上限时不硬截断，而是按平方衰减的概率丢弃，
// 让爆炸这类爆发场景的粒子密度平滑下降，不会出现突然少一半的跳变。
void FxSystem::Gvtcier_Fx_Emit(const FxParticle& p) {
    const int cap = Gvtcier_Limits().maxParticles;
    const int softCap = cap * 3 / 4;
    int n = (int)particles_.size();
    if (n >= cap) return;
    if (n >= softCap) {
        float t = (float)(cap - n) / (float)(cap - softCap);
        float chance = t * t;
        float r = (float)rand() / (float)RAND_MAX;
        if (r >= chance) return;
    }
    particles_.push_back(p);
    trimOldest(particles_, cap);
}

// 加一条从 a 到 b 的光束（激光、射线之类），宽度 width，颜色 color。
// 同样受 maxBeams 上限约束。
void FxSystem::Gvtcier_Fx_AddBeam(const Vec3& a, const Vec3& b, const FxColor& color, float width) {
    beams_.push_back({ a, b, width, color });
    trimOldest(beams_, Gvtcier_Limits().maxBeams);
}

// 添加一个只存活本帧的动态点光源，参与 tile 光照分配。
// castShadow 目前只是标记，本函数不做阴影贴图；intensity 是亮度系数。
void FxSystem::Gvtcier_Fx_AddLight(const Vec3& pos, const FxColor& color, float radius, bool castShadow, float intensity) {
    lights_.push_back({ pos, color, radius, castShadow, intensity });
    trimOldest(lights_, Gvtcier_Limits().maxDynamicLights);
}

// 添加常驻光源：跨帧存在的灯（火把、路灯等）。
// 用位置去重——距离小于 0.01 视为同一盏灯，直接覆盖它的颜色、半径等属性，
// 这样脚本每帧重复登记同一盏灯也不会把光源数量撑爆。
void FxSystem::Gvtcier_Fx_AddPersistentLight(const Vec3& pos, const FxColor& color, float radius, bool castShadow, float intensity) {
    for (auto& l : persistentLights_) {
        if ((l.pos - pos).length() < 0.01f) { l = { pos, color, radius, castShadow, intensity }; return; }
    }
    persistentLights_.push_back({ pos, color, radius, castShadow, intensity });
    trimOldest(persistentLights_, Gvtcier_Limits().maxPersistentLights);
}
