#include "Gvtcier.h"
#include <algorithm>

// 注册一个控件样式。styles 是两层字典：先按类型（Container/Card/Button/Label），再按状态名
// （normal/hover/pressed/bg）。同名直接覆盖，所以可以在运行时热改主题。
void IfTheme::Gvtcier_UI_SetStyle(const char* type, const char* name, const IfStyleBox& sb) {
    styles[type][name] = sb;
}

// 查样式，找不到返回 nullptr，调用方要自己判空（绘制时判空即跳过）。
// 返回的是内部对象指针，主题被改后指针内容会变，不要跨帧保存。
const IfStyleBox* IfTheme::Gvtcier_UI_GetStyle(const char* type, const char* name) const {
    auto it = styles.find(type);
    if (it != styles.end()) {
        auto it2 = it->second.find(name);
        if (it2 != it->second.end()) return &it2->second;
    }
    return nullptr;
}

// 非 const 版本，转调 const 版本再是掉 const。
IfStyleBox* IfTheme::Gvtcier_UI_GetStyle(const char* type, const char* name) {
    return const_cast<IfStyleBox*>(const_cast<const IfTheme*>(this)->Gvtcier_UI_GetStyle(type, name));
}

// 注册一个颜色项，同样按类型 + 名字两层存储。
void IfTheme::Gvtcier_UI_SetColor(const char* type, const char* name, COLORREF c) {
    colors[type][name] = c;
}

// 查颜色，找不到返回 fallback。带兜底值是为了让主题里可以只覆盖一部分颜色。
COLORREF IfTheme::Gvtcier_UI_GetColor(const char* type, const char* name, COLORREF fallback) const {
    auto it = colors.find(type);
    if (it != colors.end()) {
        auto it2 = it->second.find(name);
        if (it2 != it->second.end()) return it2->second;
    }
    return fallback;
}

// 灌入默认主题：容器、卡片、按钮三态、窗口按钮三态，外加按钮/标签的文字色。
// 卡片圆角 8 带 6 像素投影；按钮是直角（radius=0），按下时用强调蓝。
void IfTheme::Gvtcier_UI_InitDefault() {
    IfStyleBox cont;
    cont.bg = RGB(0x1E, 0x21, 0x27);
    cont.borderW = 0;
    cont.drawCenter = true;
    Gvtcier_UI_SetStyle("Container", "bg", cont);

    IfStyleBox card;
    card.bg = RGB(0x2D, 0x30, 0x38);
    card.border = RGB(0x3A, 0x3D, 0x44);
    card.borderW = 1;
    card.radius = 8;
    card.shadowSize = 6;
    card.shadowColor = RGB(0x00, 0x00, 0x00);
    Gvtcier_UI_SetStyle("Card", "bg", card);

    IfStyleBox btnN;
    btnN.bg = RGB(0x2D, 0x30, 0x38);
    btnN.border = RGB(0x3A, 0x3D, 0x44);
    btnN.borderW = 1;
    btnN.radius = 0;
    Gvtcier_UI_SetStyle("Button", "normal", btnN);
    IfStyleBox btnH;
    btnH.bg = RGB(0x3A, 0x3F, 0x49);
    btnH.border = RGB(0x4E, 0x52, 0x5A);
    btnH.borderW = 1;
    btnH.radius = 0;
    Gvtcier_UI_SetStyle("Button", "hover", btnH);
    IfStyleBox btnP;
    btnP.bg = RGB(0x4A, 0x6C, 0xF7);
    btnP.border = RGB(0x35, 0x57, 0xD8);
    btnP.borderW = 1;
    btnP.radius = 0;
    Gvtcier_UI_SetStyle("Button", "pressed", btnP);

    IfStyleBox wbN = btnN;
    wbN.radius = 0;
    Gvtcier_UI_SetStyle("WinButton", "normal", wbN);
    IfStyleBox wbH = btnH;
    wbH.radius = 0;
    Gvtcier_UI_SetStyle("WinButton", "hover", wbH);
    IfStyleBox wbP = btnP;
    wbP.radius = 0;
    Gvtcier_UI_SetStyle("WinButton", "pressed", wbP);

    Gvtcier_UI_SetColor("Button", "text", RGB(0xE6, 0xE8, 0xEB));
    Gvtcier_UI_SetColor("Button", "text_pressed", RGB(0xFF, 0xFF, 0xFF));
    Gvtcier_UI_SetColor("Label", "text", RGB(0xE6, 0xE8, 0xEB));
    Gvtcier_UI_SetColor("Label", "text_muted", RGB(0x9D, 0xA0, 0xA6));
}

// 控件类型到主题类型名的映射。Label 是兜底分支，所以 kUiLabel 与任何未列出的类型都画成标签。
static const char* kindType(UiKind k) {
    switch (k) {
        case kUiContainer: return "Container";
        case kUiCard: return "Card";
        case kUiButton: return "Button";
        default: return "Label";
    }
}

// 以"像素矩形"方式加一个 UI 对象：锚点四个角都设为 Begin(0) 加上偏移，等价于左上角定位。
// id 由内部自增计数器分配，parent 为空表示挂在根节点下。返回新对象 ID，可选地写回 idOut。
// 注意这里用的是引擎 ID 系统，不返回裸指针。
GvtId<UiObject> UiCanvas::Gvtcier_UI_Add(UiKind kind, const char* text, GvtId<UiObject> parent, int z,
                  int x, int y, int w, int h, GvtId<UiObject>* idOut) {
    UiObject o;
    o.id = ++nextId_;
    o.kind = kind;
    o.parent = parent;
    o.z = z;
    o.text = text ? text : "";
    o.anchor[0] = kUiAnchorBegin;
    o.anchor[1] = kUiAnchorBegin;
    o.anchor[2] = kUiAnchorBegin;
    o.anchor[3] = kUiAnchorBegin;
    o.offset[0] = x;
    o.offset[1] = y;
    o.offset[2] = x + w;
    o.offset[3] = y + h;
    GvtId<UiObject> id = objs_.Gvtcier_Object_Register(o);
    if (idOut) *idOut = id;
    return id;
}

// 以"锚点 + 偏移"方式加一个 UI 对象，做自适应布局用。
// 锚点用千分数表示父矩形上的相对位置：0 左/上，500 中心，1000 右/下；
// 偏移再按像素叠加。四个角各自有锚点与偏移，所以可以做"左边固定、右边留 20""四边留边"这类效果。
GvtId<UiObject> UiCanvas::Gvtcier_UI_AddAnchor(UiKind kind, const char* text, GvtId<UiObject> parent, int z,
                        int aL, int aT, int aR, int aB,
                        int oL, int oT, int oR, int oB, GvtId<UiObject>* idOut) {
    UiObject o;
    o.id = ++nextId_;
    o.kind = kind;
    o.parent = parent;
    o.z = z;
    o.text = text ? text : "";
    o.anchor[0] = aL;
    o.anchor[1] = aT;
    o.anchor[2] = aR;
    o.anchor[3] = aB;
    o.offset[0] = oL;
    o.offset[1] = oT;
    o.offset[2] = oR;
    o.offset[3] = oB;
    GvtId<UiObject> id = objs_.Gvtcier_Object_Register(o);
    if (idOut) *idOut = id;
    return id;
}

// 按 ID 取对象，ID 失效（对象已销毁或世代号变了）返回空指针。
UiObject* UiCanvas::Gvtcier_UI_Find(GvtId<UiObject> id) {
    return objs_.Gvtcier_Object_Get(id);
}

const UiObject* UiCanvas::Gvtcier_UI_Find(GvtId<UiObject> id) const {
    return objs_.Gvtcier_Object_Get(id);
}

// 销毁一个 UI 对象。走 ID 系统的 Destroy，旧 ID 会自动失效。
void UiCanvas::Gvtcier_UI_Remove(GvtId<UiObject> id) {
    objs_.Gvtcier_Object_Destroy(id);
}

// 清空全部 UI 对象并把自增 id 归零。因为每次重建界面都会重新分配同名对象，
// 自减回去可以让 id 保持稳定，方便调试时对照。
void UiCanvas::Gvtcier_UI_Clear() {
    objs_.Gvtcier_Object_Clear();
    nextId_ = 0;
}

// 改文本。传空指针按空串处理，避免后续绘制时解引用空指针。
void UiCanvas::Gvtcier_UI_SetText(GvtId<UiObject> id, const char* text) {
    if (UiObject* o = Gvtcier_UI_Find(id)) o->text = text ? text : "";
}

// 直接设定像素矩形（左上角 + 宽高）。由于锚点还是原来的值，下一次 Layout 会按锚点重算并覆盖它，
// 所以这个接口适合锚点全为 Begin 的对象。
void UiCanvas::Gvtcier_UI_SetRect(GvtId<UiObject> id, int x, int y, int w, int h) {
    if (UiObject* o = Gvtcier_UI_Find(id)) {
        o->offset[0] = x;
        o->offset[1] = y;
        o->offset[2] = x + w;
        o->offset[3] = y + h;
    }
}

// 按像素平移对象。改的是四个角的偏移，所以锚点布局下一次生效时依然保留这个位移。
void UiCanvas::Gvtcier_UI_Move(GvtId<UiObject> id, int dx, int dy) {
    if (UiObject* o = Gvtcier_UI_Find(id)) {
        o->offset[0] += dx;
        o->offset[1] += dy;
        o->offset[2] += dx;
        o->offset[3] += dy;
    }
}

// 显隐控制。隐藏的对象在 Layout、Draw、Hit 三处都会被跳过，等于完全不参与。
void UiCanvas::Gvtcier_UI_SetVisible(GvtId<UiObject> id, bool v) {
    if (UiObject* o = Gvtcier_UI_Find(id)) o->visible = v;
}

// 给单个对象套自定义样式，并置上 customStyle 标记，绘制时就优先用它而不是主题。
void UiCanvas::Gvtcier_UI_SetStyle(GvtId<UiObject> id, const IfStyleBox& sb) {
    if (UiObject* o = Gvtcier_UI_Find(id)) {
        o->style = sb;
        o->customStyle = true;
    }
}

// 按锚点重算所有对象的矩形。因为子对象依赖父对象的矩形，父级先算完子级才对，
// 而遍历顺序不保证父在子前，所以最多迭代 6 轮，直到某一轮没有任何矩形发生变化才提前收工。
// root 是根矩形（通常是窗口客户区），没有父级的对象都以它为参照。
// 最后再调 fitChildren 处理"均分子级"的布局。
void UiCanvas::Gvtcier_UI_Layout(const RECT& root) {
    rootRect_ = root;
    for (int pass = 0; pass < 6; ++pass) {
        bool changed = false;
        objs_.ForEachAlive([&](UiObject& o, GvtId<UiObject>) {
            if (!o.visible) return;
            RECT pr = root;
            if (o.parent.valid()) {
                const UiObject* p = Gvtcier_UI_Find(o.parent);
                if (!p || !p->visible) return;
                pr = p->rect;
            }
            int pw = pr.right - pr.left;
            int ph = pr.bottom - pr.top;
            RECT nr;
            nr.left = pr.left + pw * o.anchor[0] / 1000 + o.offset[0];
            nr.top = pr.top + ph * o.anchor[1] / 1000 + o.offset[1];
            nr.right = pr.left + pw * o.anchor[2] / 1000 + o.offset[2];
            nr.bottom = pr.top + ph * o.anchor[3] / 1000 + o.offset[3];
            if (nr.left != o.rect.left || nr.top != o.rect.top ||
                nr.right != o.rect.right || nr.bottom != o.rect.bottom) {
                changed = true;
                o.rect = nr;
            }
        });
        if (!changed) break;
    }
    fitChildren();
}

// 处理"均分容器"布局：把 fitDir>0 的对象按父级分组，同组内水平或垂直均分父矩形。
// 分组键把父 ID 的 index 与 generation 拼成 64 位，没有父级的用全 1 当作"根组"。
// 方向取组内第一个子对象的 fitDir==1 判定为水平，否则垂直；
// 最后一个子对象直接顶到父级右/下边界，除不尽的余数落在最后一块，不会留缝。
void UiCanvas::fitChildren() {
    std::unordered_map<uint64_t, std::vector<GvtId<UiObject>>> groups;
    objs_.ForEachAlive([&](UiObject& o, GvtId<UiObject> id) {
        if (o.fitDir > 0 && o.visible) {
            uint64_t key = o.parent.valid()
                ? ((uint64_t)o.parent.index << 32) | o.parent.generation
                : 0xFFFFFFFFFFFFFFFFull;
            groups[key].push_back(id);
        }
    });
    for (auto& kv : groups) {
        auto& ch = kv.second;
        RECT pr;
        if (kv.first == 0xFFFFFFFFFFFFFFFFull) {
            pr = rootRect_;
        } else {
            GvtId<UiObject> pid;
            pid.index = (uint32_t)(kv.first >> 32);
            pid.generation = (uint32_t)(kv.first & 0xFFFFFFFFu);
            const UiObject* p = Gvtcier_UI_Find(pid);
            if (!p) continue;
            pr = p->rect;
        }
        int pw = pr.right - pr.left;
        int ph = pr.bottom - pr.top;
        int n = (int)ch.size();
        if (n <= 0 || pw <= 0 || ph <= 0) continue;
        const UiObject* first = Gvtcier_UI_Find(ch[0]);
        if (!first) continue;
        bool h = first->fitDir == 1;
        for (int i = 0; i < n; ++i) {
            UiObject* c = Gvtcier_UI_Find(ch[i]);
            if (!c) continue;
            RECT nr;
            if (h) {
                int cw = pw / n;
                int cx0 = pr.left + cw * i;
                int cx1 = (i == n - 1) ? pr.right : cx0 + cw;
                nr = { cx0, pr.top, cx1, pr.bottom };
            } else {
                int chh = ph / n;
                int cy0 = pr.top + chh * i;
                int cy1 = (i == n - 1) ? pr.bottom : cy0 + chh;
                nr = { pr.left, cy0, pr.right, cy1 };
            }
            c->rect = nr;
        }
    }
}

// 画一层柔和投影，思路与接口层的同名函数一样，只是这里的透明度峰值取 50 而不是 60，
// 让卡片投影在深色主题下更含蓄。
static void drawShadow(HDC hdc, const RECT& rc, int size, COLORREF color, int radius) {
    if (size <= 0) return;
    for (int i = size; i > 0; --i) {
        int a = 50 * (size - i + 1) / size;
        COLORREF c = RGB((GetRValue(color) * a) / 255, (GetGValue(color) * a) / 255, (GetBValue(color) * a) / 255);
        RECT sr = { rc.left + i, rc.top + i, rc.right + i, rc.bottom + i };
        HPEN pn = CreatePen(PS_SOLID, 1, c);
        HPEN op = (HPEN)SelectObject(hdc, pn);
        HBRUSH ob = (HBRUSH)SelectObject(hdc, GetStockObject(NULL_BRUSH));
        RoundRect(hdc, sr.left, sr.top, sr.right, sr.bottom, radius, radius);
        SelectObject(hdc, op);
        SelectObject(hdc, ob);
        DeleteObject(pn);
    }
}

// 按样式画一个盒子：先投影、再圆角填充、最后描边。
// drawCenter=false 的样式只描边不填底，用于透明容器。
static void drawStyleBox(HDC hdc, const RECT& rc, const IfStyleBox& sb) {
    if (sb.shadowSize > 0) drawShadow(hdc, rc, sb.shadowSize, sb.shadowColor, sb.radius);
    if (sb.drawCenter) Gvtcier_UI_FillRound(hdc, rc, sb.bg, sb.radius);
    if (sb.borderW > 0) Gvtcier_UI_RoundRect(hdc, rc, sb.border, sb.borderW, sb.radius);
}

// 绘制整个 UI 画布。先收集所有可见对象，按 z 稳定排序（同 z 保持创建顺序），再逐个画。
// 容器用主题的 Container/bg 直接纯色填充；卡片走完整样式（投影 + 圆角 + 边框）；
// 按钮按 disabled/pressed/hover 三态选样式名，按下的按钮文字用粗体强调；
// 其余都当标签，有自定义样式时用自定义文字色，否则查主题的颜色。
// root 参数当前没有使用（对象矩形在 Layout 时就算好了），保留是为了接口对称。
void UiCanvas::Gvtcier_UI_Draw(HDC hdc, const RECT& root) {
    std::vector<const UiObject*> sorted;
    objs_.ForEachAlive([&](const UiObject& o, GvtId<UiObject>) {
        if (o.visible) sorted.push_back(&o);
    });
    std::stable_sort(sorted.begin(), sorted.end(),
                     [](const UiObject* a, const UiObject* b) { return a->z < b->z; });
    for (const UiObject* o : sorted) {
        switch (o->kind) {
            case kUiContainer: {
                const IfStyleBox* sb = o->customStyle ? &o->style
                                    : theme.Gvtcier_UI_GetStyle("Container", "bg");
                if (sb) Gvtcier_UI_DrawContainer(hdc, o->rect, sb->bg);
                break;
            }
            case kUiCard: {
                const IfStyleBox* sb = o->customStyle ? &o->style
                                    : theme.Gvtcier_UI_GetStyle("Card", "bg");
                if (sb) drawStyleBox(hdc, o->rect, *sb);
                break;
            }
            case kUiButton: {
                const char* typeName = o->type ? o->type : "Button";
                const char* state = o->disabled ? "normal" : (o->pressed ? "pressed" : (o->hover ? "hover" : "normal"));
                const IfStyleBox* sb = o->customStyle ? &o->style
                                    : theme.Gvtcier_UI_GetStyle(typeName, state);
                if (sb) drawStyleBox(hdc, o->rect, *sb);
                COLORREF tc;
                if (o->pressed) tc = theme.Gvtcier_UI_GetColor("Button", "text_pressed", RGB(0xFF, 0xFF, 0xFF));
                else tc = theme.Gvtcier_UI_GetColor("Button", "text", RGB(0xE6, 0xE8, 0xEB));
                if (o->pressed) Gvtcier_UI_SelectFontBold(hdc);
                Gvtcier_UI_Text(hdc, o->rect, o->text.c_str(), o->textFmt, tc);
                Gvtcier_UI_SelectFont(hdc);
                break;
            }
            default: {
                COLORREF tc = o->customStyle ? o->textColor
                            : theme.Gvtcier_UI_GetColor("Label", "text", o->textColor);
                Gvtcier_UI_Text(hdc, o->rect, o->text.c_str(), o->textFmt, tc);
                break;
            }
        }
    }
}

// 命中测试，返回鼠标位置下 z 最大的可见对象，没有命中返回无效 ID。
// 注意这里没有沿父级裁剪，被父容器挡住或超出父级范围的对象仍然会被命中，
// 需要精确裁剪时得靠调用方自己判断。
GvtId<UiObject> UiCanvas::Gvtcier_UI_Hit(int x, int y) const {
    GvtId<UiObject> best;
    int bestZ = -1;
    objs_.ForEachAlive([&](const UiObject& o, GvtId<UiObject> id) {
        if (!o.visible) return;
        if (x >= o.rect.left && x <= o.rect.right && y >= o.rect.top && y <= o.rect.bottom) {
            if (o.z >= bestZ) {
                bestZ = o.z;
                best = id;
            }
        }
    });
    return best;
}

// 每个面板的滚动状态，按窗口句柄索引。之所以挂在全局字典而不是 GvPanel 成员上，
// 是为了让 GvPanel 保持无状态、可以随意拷贝。
struct PanelState {
    RECT rc = { 0, 0, 0, 0 };   // 最近一次拿到的客户区矩形
    int claimBottom = 0;        // 已声明内容的下边界，用于算总内容高
    bool any = false;           // 本轮是否已经初始化过
};
static std::unordered_map<HWND, PanelState> g_panelStates;

// 绑定面板到窗口句柄，后续所有操作都作用在这个窗口上。
void GvPanel::Gvtcier_UI_Panel_Attach(HWND hwnd) {
    wnd = hwnd;
}

// 开始一轮面板布局：刷新客户区尺寸并清空"已声明下边界"。
// 一次 Begin 到 End 之间，所有控件都要通过 Claim 或 Reserve 报出自己的高度。
void GvPanel::Gvtcier_UI_Panel_Begin() {
    if (!wnd) return;
    PanelState& st = g_panelStates[wnd];
    GetClientRect(wnd, &st.rc);
    st.claimBottom = 0;
    st.any = true;
}

// 声明一块内容区域，把"已声明下边界"推到最底部。返回值恒为 true，
// 表示这个调用总是成功，调用方可以直接 if (!Claim(...)) 判断但实际不会失败。
bool GvPanel::Gvtcier_UI_Panel_Claim(int x, int y, int w, int h) {
    if (!wnd) return false;
    PanelState& st = g_panelStates[wnd];
    if (!st.any) {
        GetClientRect(wnd, &st.rc);
        st.claimBottom = 0;
        st.any = true;
    }
    int bottom = y + h;
    if (bottom > st.claimBottom) st.claimBottom = bottom;
    return true;
}

// 直接按高度追加内容，用于没有具体矩形的占位（间距、空行）。
void GvPanel::Gvtcier_UI_Panel_Reserve(int h) {
    if (!wnd) return;
    PanelState& st = g_panelStates[wnd];
    if (!st.any) {
        GetClientRect(wnd, &st.rc);
        st.claimBottom = 0;
        st.any = true;
    }
    st.claimBottom += h;
}

// 结束一轮布局：内容高取"已声明下边界"与调用方给的 contentBottom 的较大值，再补 8 像素余量。
// 然后算最大滚动量并把 scrollY 夹回合法范围，最后更新竖向滚动条。
// nMax 用的是 contentH 而不是 contentH-1，配合 nPage 正好表示"整块内容比可视区多出的部分"。
void GvPanel::Gvtcier_UI_Panel_End(int contentBottom) {
    if (!wnd) return;
    PanelState& st = g_panelStates[wnd];
    if (!st.any) {
        GetClientRect(wnd, &st.rc);
        st.claimBottom = 0;
        st.any = true;
    }
    int bottom = contentBottom > st.claimBottom ? contentBottom : st.claimBottom;
    contentH = bottom + 8;
    int viewH = st.rc.bottom - st.rc.top;
    int maxScroll = contentH > viewH ? contentH - viewH : 0;
    if (scrollY > maxScroll) scrollY = maxScroll;
    if (scrollY < 0) scrollY = 0;
    SCROLLINFO si;
    memset(&si, 0, sizeof(si));
    si.cbSize = sizeof(si);
    si.fMask = SIF_RANGE | SIF_PAGE | SIF_POS;
    si.nMin = 0;
    si.nMax = contentH;
    si.nPage = (UINT)(viewH > 0 ? viewH : 1);
    si.nPos = scrollY;
    SetScrollInfo(wnd, SB_VERT, &si, TRUE);
}

// 按增量滚动（滚轮通常是 ±120 的倍数），同样夹在 [0, 最大滚动量] 内。
void GvPanel::Gvtcier_UI_Panel_Scroll(int delta) {
    if (!wnd) return;
    PanelState& st = g_panelStates[wnd];
    if (!st.any) GetClientRect(wnd, &st.rc);
    int viewH = st.rc.bottom - st.rc.top;
    int maxScroll = contentH > viewH ? contentH - viewH : 0;
    scrollY += delta;
    if (scrollY > maxScroll) scrollY = maxScroll;
    if (scrollY < 0) scrollY = 0;
    SetScrollPos(wnd, SB_VERT, scrollY, TRUE);
}

// 批量重排控件时先关掉重绘，避免每个控件移动都触发一次闪烁。
void GvPanel::Gvtcier_UI_Panel_SuspendDraw() {
    if (!wnd) return;
    SendMessageA(wnd, WM_SETREDRAW, FALSE, 0);
}

// 恢复重绘并立刻整窗重画。RDW_UPDATENOW 让它同步完成，调用返回时画面已经是新的。
void GvPanel::Gvtcier_UI_Panel_ResumeDraw() {
    if (!wnd) return;
    SendMessageA(wnd, WM_SETREDRAW, TRUE, 0);
    RedrawWindow(wnd, NULL, NULL,
                 RDW_INVALIDATE | RDW_ERASE | RDW_FRAME | RDW_ALLCHILDREN | RDW_UPDATENOW);
}
