#pragma once
#include "bicu/core/export.h"
#include "Gvtcier2D/interface/ui.h"
#include <string>
#include <vector>

// 一个界面节点的"通行证"：记录它的身份路径、绑定标识、对应的 UI 对象与原生窗口，
// 以及滚动归属、安全区这些需要从父级继承的属性。
struct GVTCIER_API GvtcierPassport {
    std::string identity;    // 点分路径，如 "主菜单.设置"，既是名字也是层级依据
    std::string bind;        // 绑定的脚本/数据标识
    GvtId<UiObject> uiId;    // 对应的 UI 画布对象，可为空（纯原生窗口节点）
    HWND hwnd = nullptr;     // 对应的原生窗口，可为空（纯 UI 节点）
    bool active = false;     // 当前是否显示
    std::string scrollOwner; // 滚动由哪个节点负责，从父级继承
    std::string safeArea;    // 安全区设置，从父级继承
    int parent = -1;         // 父节点在 nodes 里的下标，-1 表示根
};

// 界面节点树。按 identity 维护层级，支持整棵子树显隐与属性继承。
class GVTCIER_API GvtcierPassportTree {
public:
    UiCanvas* canvas = nullptr;          // 用于控制 UI 对象可见性，可为空
    std::vector<GvtcierPassport> nodes;  // 所有节点，按创建顺序存放

    // 创建或更新节点，返回下标；identity 为空等情况返回 -1。
    int GvtcierPassport_Create(const char* identity, GvtId<UiObject> uiId, HWND hwnd = nullptr);
    int GvtcierPassport_Find(const char* identity) const;
    void GvtcierPassport_SetBind(const char* identity, const char* bind);
    void GvtcierPassport_Inherit(const char* child, const char* parent);
    // 整棵子树开/关，同时同步 UI 可见性与原生窗口显隐。
    void GvtcierPassport_SetActive(const char* identity, bool active);
    // 实为隐藏，不移除节点。
    void GvtcierPassport_Erase(const char* identity);
    void GvtcierPassport_Clear();
};
