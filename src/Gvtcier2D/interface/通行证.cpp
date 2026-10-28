#include "Gvtcier.h"

// 创建（或更新）一个界面节点，返回节点下标，找不到/越界用 -1。
// identity 是点分路径，例如 "主菜单.设置.通用"，父节点会自动按最后一个点之前的部分去找，
// 所以节点的创建顺序最好是父在子前；子先创建时会挂不到父上（parent 保持 -1）。
// 同一 identity 重复创建不会新建，只更新它的 uiId 与 hwnd，便于重开界面时复用身份。
int GvtcierPassportTree::GvtcierPassport_Create(const char* identity, GvtId<UiObject> uiId, HWND hwnd) {
    int ex = GvtcierPassport_Find(identity);
    if (ex >= 0) {
        nodes[ex].uiId = uiId;
        nodes[ex].hwnd = hwnd;
        return ex;
    }
    GvtcierPassport n;
    n.identity = identity ? identity : "";
    n.uiId = uiId;
    n.hwnd = hwnd;
    n.parent = -1;
    size_t dot = n.identity.rfind('.');
    if (dot != std::string::npos) {
        n.parent = GvtcierPassport_Find(n.identity.substr(0, dot).c_str());
    }
    nodes.push_back(n);
    return (int)nodes.size() - 1;
}

// 按 identity 线性查找节点下标，节点数量本来就少，没必要建索引表。
int GvtcierPassportTree::GvtcierPassport_Find(const char* identity) const {
    for (size_t i = 0; i < nodes.size(); ++i) {
        if (nodes[i].identity == identity) return (int)i;
    }
    return -1;
}

// 给节点挂一个"绑定"标识，通常是它对应的脚本回调名或数据键。节点不存在时静默忽略。
void GvtcierPassportTree::GvtcierPassport_SetBind(const char* identity, const char* bind) {
    int i = GvtcierPassport_Find(identity);
    if (i >= 0) nodes[i].bind = bind ? bind : "";
}

// 手动指定父子关系，并继承父级的滚动归属与安全区设置。
// 与 Create 时的自动推断相比，这个接口可以建立不按点分路径命名的层级。
void GvtcierPassportTree::GvtcierPassport_Inherit(const char* child, const char* parent) {
    int c = GvtcierPassport_Find(child);
    int p = GvtcierPassport_Find(parent);
    if (c >= 0 && p >= 0) {
        nodes[c].parent = p;
        nodes[c].scrollOwner = nodes[p].scrollOwner;
        nodes[c].safeArea = nodes[p].safeArea;
    }
}

// 开/关一个节点，并递归作用于它的所有子节点。
// 同时做三件事：记状态、设 UI 对象可见性（需要 canvas 指针有效）、显示/隐藏原生窗口。
// 递归用 identity 字符串重新查找子节点，所以子节点的 identity 必须是完整路径且能精确匹配。
void GvtcierPassportTree::GvtcierPassport_SetActive(const char* identity, bool active) {
    int i = GvtcierPassport_Find(identity);
    if (i < 0) return;
    nodes[i].active = active;
    if (nodes[i].uiId.valid() && canvas) {
        canvas->Gvtcier_UI_SetVisible(nodes[i].uiId, active);
    }
    if (nodes[i].hwnd) {
        ShowWindow(nodes[i].hwnd, active ? SW_SHOW : SW_HIDE);
    }
    for (size_t k = 0; k < nodes.size(); ++k) {
        if (nodes[k].parent == i) GvtcierPassport_SetActive(nodes[k].identity.c_str(), active);
    }
}

// 名字叫删除，实际只是隐藏：节点仍留在树里，之后还能重新激活。
// 真要彻底移除节点只有 Clear。
void GvtcierPassportTree::GvtcierPassport_Erase(const char* identity) {
    GvtcierPassport_SetActive(identity, false);
}

// 清空整棵树。不会顺带销毁 UI 对象或窗口，清之前要由调用方自己收尾。
void GvtcierPassportTree::GvtcierPassport_Clear() {
    nodes.clear();
}
