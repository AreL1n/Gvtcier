#pragma once
#include <cstdint>
#include <vector>
#include <utility>

template<typename T>
struct GvtId {
    uint32_t index = 0xFFFFFFFFu;
    uint32_t generation = 0u;

    bool valid() const { return index != 0xFFFFFFFFu; }
    bool operator==(const GvtId& o) const { return index == o.index && generation == o.generation; }
    bool operator!=(const GvtId& o) const { return !(*this == o); }
};

template<typename T>
class GvtRegistry {
public:
    static constexpr uint32_t kInvalid = 0xFFFFFFFFu;

    struct Slot {
        T value;
        uint32_t generation = 0u;
        bool alive = false;
    };

    GvtId<T> Create(T value) {
        uint32_t idx;
        if (!freeList_.empty()) {
            idx = freeList_.back();
            freeList_.pop_back();
        } else {
            idx = (uint32_t)slots_.size();
            slots_.emplace_back();
        }
        Slot& s = slots_[idx];
        s.value = std::move(value);
        s.alive = true;
        return GvtId<T>{ idx, s.generation };
    }

    T* Get(GvtId<T> id) {
        if (id.index >= slots_.size()) return nullptr;
        Slot& s = slots_[id.index];
        if (!s.alive || s.generation != id.generation) return nullptr;
        return &s.value;
    }

    const T* Get(GvtId<T> id) const {
        if (id.index >= slots_.size()) return nullptr;
        const Slot& s = slots_[id.index];
        if (!s.alive || s.generation != id.generation) return nullptr;
        return &s.value;
    }

    bool IsAlive(GvtId<T> id) const { return Get(id) != nullptr; }

    bool Destroy(GvtId<T> id) {
        if (id.index >= slots_.size()) return false;
        Slot& s = slots_[id.index];
        if (!s.alive || s.generation != id.generation) return false;
        s.alive = false;
        ++s.generation;
        s.value = T();
        freeList_.push_back(id.index);
        return true;
    }

    size_t Capacity() const { return slots_.size(); }
    size_t AliveCount() const { return slots_.size() - freeList_.size(); }

    template<typename Fn>
    void ForEachAlive(Fn fn) {
        for (uint32_t i = 0; i < (uint32_t)slots_.size(); ++i) {
            if (slots_[i].alive) fn(slots_[i].value, GvtId<T>{ i, slots_[i].generation });
        }
    }

    template<typename Fn>
    void ForEachAlive(Fn fn) const {
        for (uint32_t i = 0; i < (uint32_t)slots_.size(); ++i) {
            if (slots_[i].alive) fn(slots_[i].value, GvtId<T>{ i, slots_[i].generation });
        }
    }

    void Clear() {
        slots_.clear();
        freeList_.clear();
    }

    const std::vector<Slot>& slots() const { return slots_; }

private:
    std::vector<Slot> slots_;
    std::vector<uint32_t> freeList_;
};

template<typename T>
class Gvtcier_Object {
public:
    GvtId<T> Gvtcier_Object_Register(T value) {
        return registry_.Create(std::move(value));
    }

    T* Gvtcier_Object_Get(GvtId<T> id) {
        return registry_.Get(id);
    }

    const T* Gvtcier_Object_Get(GvtId<T> id) const {
        return registry_.Get(id);
    }

    bool Gvtcier_Object_Alive(GvtId<T> id) const {
        return registry_.IsAlive(id);
    }

    bool Gvtcier_Object_Destroy(GvtId<T> id) {
        return registry_.Destroy(id);
    }

    size_t Gvtcier_Object_Count() const {
        return registry_.AliveCount();
    }

    void Gvtcier_Object_Clear() {
        registry_.Clear();
    }

    template<typename Fn>
    void ForEachAlive(Fn fn) {
        registry_.ForEachAlive(fn);
    }

    template<typename Fn>
    void ForEachAlive(Fn fn) const {
        registry_.ForEachAlive(fn);
    }

private:
    GvtRegistry<T> registry_;
};
