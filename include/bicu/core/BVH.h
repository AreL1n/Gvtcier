#pragma once
#include "bicu/core/AABB.h"
#include <algorithm>
#include <vector>

class Bvh {
public:
    void build(const std::vector<AABB3>& bounds);
    void queryFrustum(const Plane3* planes, int planeCount, std::vector<int>& out) const;
    void queryAABB(const AABB3& q, std::vector<int>& out) const;
    void queryAll(std::vector<int>& out) const;
    void clear();
    bool built() const { return !nodes_.empty(); }

private:
    int buildNode(int begin, int end);
    struct Node {
        AABB3 bounds;
        int left = -1, right = -1;
        int begin = 0, end = 0;
    };
    std::vector<Node> nodes_;
    std::vector<int> order_;
    std::vector<AABB3> bounds_;
    mutable std::vector<int> stack_;
};

inline void Bvh::clear() {
    nodes_.clear();
    order_.clear();
    bounds_.clear();
}

inline bool aabbListEqual(const std::vector<AABB3>& a, const std::vector<AABB3>& b) {
    if (a.size() != b.size()) return false;
    for (size_t i = 0; i < a.size(); ++i) {
        if (a[i].center.x != b[i].center.x || a[i].center.y != b[i].center.y ||
            a[i].center.z != b[i].center.z || a[i].half.x != b[i].half.x ||
            a[i].half.y != b[i].half.y || a[i].half.z != b[i].half.z) return false;
    }
    return true;
}

inline void Bvh::build(const std::vector<AABB3>& bounds) {
    if (built() && aabbListEqual(bounds, bounds_)) return;
    bounds_ = bounds;
    nodes_.clear();
    order_.clear();
    order_.resize(bounds_.size());
    for (size_t i = 0; i < order_.size(); ++i) order_[i] = (int)i;
    if (!bounds_.empty()) buildNode(0, (int)order_.size());
}

inline int Bvh::buildNode(int begin, int end) {
    int idx = (int)nodes_.size();
    nodes_.push_back(Node());
    AABB3 b = bounds_[order_[begin]];
    for (int i = begin + 1; i < end; ++i) b = unionAABB(b, bounds_[order_[i]]);
    nodes_[idx].bounds = b;
    int count = end - begin;
    if (count == 1) {
        nodes_[idx].begin = begin;
        nodes_[idx].end = end;
        return idx;
    }
    Vec3 e = b.half;
    int axis = (e.x >= e.y && e.x >= e.z) ? 0 : (e.y >= e.z ? 1 : 2);
    int mid = begin + count / 2;
    std::nth_element(order_.begin() + begin, order_.begin() + mid, order_.begin() + end,
        [&](int ai, int bi) {
            Vec3 ca = bounds_[ai].center;
            Vec3 cb = bounds_[bi].center;
            return axis == 0 ? (ca.x < cb.x) : (axis == 1 ? (ca.y < cb.y) : (ca.z < cb.z));
        });
    nodes_[idx].left = buildNode(begin, mid);
    nodes_[idx].right = buildNode(mid, end);
    return idx;
}

inline void Bvh::queryAABB(const AABB3& q, std::vector<int>& out) const {
    if (nodes_.empty()) return;
    stack_.clear();
    stack_.push_back(0);
    while (!stack_.empty()) {
        int idx = stack_.back();
        stack_.pop_back();
        const Node& n = nodes_[idx];
        if (!aabbOverlap(n.bounds, q)) continue;
        if (n.left < 0) {
            for (int i = n.begin; i < n.end; ++i) {
                if (aabbOverlap(bounds_[order_[i]], q)) out.push_back(order_[i]);
            }
        } else {
            stack_.push_back(n.left);
            stack_.push_back(n.right);
        }
    }
}

inline void Bvh::queryAll(std::vector<int>& out) const {
    for (size_t i = 0; i < order_.size(); ++i) out.push_back(order_[i]);
}

inline void Bvh::queryFrustum(const Plane3* planes, int planeCount, std::vector<int>& out) const {
    if (nodes_.empty()) return;
    stack_.clear();
    stack_.push_back(0);
    while (!stack_.empty()) {
        int idx = stack_.back();
        stack_.pop_back();
        const Node& n = nodes_[idx];
        if (aabbOutsideFrustum(n.bounds, planes, planeCount)) continue;
        if (n.left < 0) {
            for (int i = n.begin; i < n.end; ++i) out.push_back(order_[i]);
        } else {
            stack_.push_back(n.left);
            stack_.push_back(n.right);
        }
    }
}
