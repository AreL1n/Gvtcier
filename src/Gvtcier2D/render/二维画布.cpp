#include "Gvtcier.h"
#include <cmath>
#include <cstring>
#include <cstdio>
#include <algorithm>
#include <vector>

// 4x4 矩阵，按列主序存放：m[列*4 + 行]。整套 2D 变换都在这里手搓，
// 不依赖引擎的 Mat4，免得多绕一层，也让画布模块能单独看懂。
struct M4 { float m[16]; };

// 单位矩阵。齐次坐标下对角线全 1，其余 0，不做任何变换。
static M4 m4Identity() {
    M4 r = {};
    r.m[0] = r.m[5] = r.m[10] = r.m[15] = 1.0f;
    return r;
}

// 矩阵乘法，返回 a*b（先做 b 的变换再做 a 的变换）。
static M4 m4Mul(M4 a, M4 b) {
    M4 r = {};
    for (int c = 0; c < 4; ++c)
        for (int row = 0; row < 4; ++row) {
            float s = 0.0f;
            for (int k = 0; k < 4; ++k) s += a.m[k * 4 + row] * b.m[c * 4 + k];
            r.m[c * 4 + row] = s;
        }
    return r;
}

// 正交投影矩阵。2D 层自己把 y 轴翻过来：调用方传 b=height、t=0，
// 于是 (0,0) 落在屏幕左上角，x 向右、y 向下，跟窗口像素坐标一致。
// z 映射到 [-1,1]，因为 2D 不做深度排序，z 只用于生成 GL 的裁剪范围。
static M4 m4Ortho(float l, float r, float b, float t, float zn, float zf) {
    M4 m = m4Identity();
    m.m[0] = 2.0f / (r - l);
    m.m[5] = 2.0f / (t - b);
    m.m[10] = -2.0f / (zf - zn);
    m.m[12] = -(r + l) / (r - l);
    m.m[13] = -(t + b) / (t - b);
    m.m[14] = -(zf + zn) / (zf - zn);
    return m;
}

// 平移矩阵，位移写在列主序的第 4 列（m[12]、m[13]）。
static M4 m4Translate(float x, float y) {
    M4 m = m4Identity();
    m.m[12] = x; m.m[13] = y;
    return m;
}

// 绕 Z 轴旋转（角度制转弧度的工作由调用方完成），2D 只有这一个旋转轴。
static M4 m4RotateZ(float a) {
    M4 m = m4Identity();
    float c = std::cos(a), s = std::sin(a);
    m.m[0] = c; m.m[1] = s; m.m[4] = -s; m.m[5] = c;
    return m;
}

// 非等比缩放，专门给相机变焦与翻转用。
static M4 m4Scale(float x, float y) {
    M4 m = m4Identity();
    m.m[0] = x; m.m[5] = y;
    return m;
}

// 把 v 夹在 [a,b] 之间，本文件里用于防除零与进度归一。
static float clampf(float v, float a, float b) { return v < a ? a : (v > b ? b : v); }

// 一个待绘制的 2D 图元。所有图形最终都压成一个实例，交给同一个实例化着色器，
// 差别只在 d.k0 指定的图元类型（精灵/圆角矩形/圆/弧/线/组贴图）。
struct C2Item {
    C2Instance d;      // 真正的 GPU 实例数据：变换矩阵、颜色、uv、图元参数
    float z;           // 层内深度键，同层内 z 小的先画
    float ykey;        // 第二排序键，2.5D 场景按脚底 y 排序时用，普通 UI 恒为 0
    int layer;         // 第一大排序键，整数层号，用来区分背景/游戏/界面等大层
    int order;         // 提交顺序，最后的稳定排序依据，保证同键图元不乱序
    int groupRef;      // >=0 表示这是一张贴图合成项，指向 C2Group，-1 表示普通图元
    int premul;        // 1 表示颜色已预乘透明度，走 One/OneMinusSrcAlpha 混合
    C2Rect clip;       // 提交时生效的裁剪矩形，渲染时据此切同一批
    unsigned tex;      // 纹理 id，图元类型为矢量图形时为 0
};

// 半透明分组。整组先画到自己的离屏纹理，再带着组透明度一次性合成到父级，
// 这样组内元素互相重叠时不会出现双重混合导致的深色边。
struct C2Group {
    C2Rect rect;           // 组在屏幕上的矩形，同时决定离屏纹理尺寸
    float alpha;           // 整组的不透明度，合成时乘上去
    int layer;             // 合成项的排序键，与普通图元共用一套排序
    float z;
    int order;
    std::vector<C2Item> items;   // 组内收集到的子图元
    rhi::Fbo fbo;                // 离屏帧缓冲
    rhi::Texture tex;            // 离屏颜色纹理，同时也是合成时采样的贴图
    int fboW;                    // 当前离屏纹理宽，尺寸变了要重建
    int fboH;
};

// 画布全局状态。每帧由 BeginFrame 清空列表，绘制调用只往列表里塞实例，
// 真正上屏推迟到 EndFrame / RenderUI，这是典型的延迟批处理结构。
//
// 关于两个队列：二维画布本身只有 main 这一条队列；引擎 UI（设置界面、抬头显示、
// 主菜单等）和游戏 UI（脚本调 绘制_* 提交的东西）的分流不在这里做，而在
// Renderer::pushCanvasPrim 里按 writeEngine_ 标志决定塞进 engineItems_ 还是
// canvasItems_，两条队列各自排序、各自按层刷新，所以脚本画的 UI 永远盖不住引擎 UI。
struct C2State {
    std::vector<C2Item> main;       // 顶层图元，未进入任何分组时都落在这里
    std::vector<int> stack;         // 分组嵌套栈，栈顶是当前收流的组下标
    std::vector<int> freeGroups;    // 空闲组下标池，避免每帧反复分配离屏资源
    C2Rect curClip;                 // 当前裁剪矩形；注意这里只有一份，不随分组嵌套保存
    bool hasClip;
    int order;                      // 每帧递增的提交序号
    int width;                      // 逻辑画布宽，用于构造正交投影
    int height;
    float camX, camY, camZoom, camRot;  // 2D 相机参数，camRot 已转成弧度
    bool camEnabled;                // false 时按纯屏幕像素坐标出图（RenderUI 走这条路）

    C2State() : curClip({ 0, 0, 0, 0 }), hasClip(false), order(0),
                width(gv::kDefaultWindowWidth), height(gv::kDefaultWindowHeight), camX(0), camY(0), camZoom(1), camRot(0), camEnabled(false) {}

    // 当前应该接收新图元的列表：不在组里就是 main，在组里就是组自己的 items。
    std::vector<C2Item>* tgt() { return stack.empty() ? &main : &groups[stack.back()].items; }

    // 压入裁剪矩形，与已有裁剪求交集，保证裁剪只会越收越紧。
    void pushClip(C2Rect r) {
        if (hasClip) {
            float x0 = std::max(curClip.x, r.x);
            float y0 = std::max(curClip.y, r.y);
            float x1 = std::min(curClip.x + curClip.w, r.x + r.w);
            float y1 = std::min(curClip.y + curClip.h, r.y + r.h);
            curClip = { x0, y0, std::max(0.0f, x1 - x0), std::max(0.0f, y1 - y0) };
        } else {
            curClip = r;
            hasClip = true;
        }
    }

    // 弹出裁剪：这里直接清成"无裁剪"，不做栈回退，所以 绘制_裁剪 必须严格配对使用，
    // 嵌套裁剪时只能整体退回到没有裁剪的状态。
    void popClip() { hasClip = false; curClip = { 0, 0, 0, 0 }; }

    // 开始一个半透明分组，返回组下标。优先复用 freeGroups 里的空闲组，
    // 这样离屏纹理不会每帧重新分配。组内容会被暂存，等 EndGroup 才变成一张合成项。
    int beginGroup(C2Rect r, float alpha, int layer, float z) {
        int idx;
        if (!freeGroups.empty()) {
            idx = freeGroups.back();
            freeGroups.pop_back();
            C2Group& g = groups[idx];
            g.rect = r;
            g.alpha = alpha;
            g.layer = layer;
            g.z = z;
            g.order = order++;
            g.items.clear();
        } else {
            C2Group g = {};
            g.rect = r;
            g.alpha = alpha;
            g.layer = layer;
            g.z = z;
            g.order = order++;
            groups.push_back(g);
            idx = (int)groups.size() - 1;
        }
        stack.push_back(idx);
        return idx;
    }

    // 结束分组：把组包装成一个 C2_GROUP 图元塞进父级列表。
    // 变换取组矩形中心，半长取矩形半宽高，着色器里就是拿整块离屏纹理贴这个矩形。
    // v=1、vh=-1 是竖直翻转，因为离屏纹理的行序与屏幕相反。
    void endGroup() {
        if (stack.empty()) return;
        int idx = stack.back();
        stack.pop_back();
        C2Group& g = groups[idx];
        C2Item it = {};
        it.d.k0 = (float)C2_GROUP;
        it.d.r = it.d.g = it.d.b = 1.0f;
        it.d.a = g.alpha;
        setTransform(it.d, g.rect.x + g.rect.w * 0.5f, g.rect.y + g.rect.h * 0.5f, 0.0f,
                     1.0f, 1.0f, g.rect.w * 0.5f, g.rect.h * 0.5f);
        it.d.u = 0.0f;
        it.d.v = 1.0f;
        it.d.uw = 1.0f;
        it.d.vh = -1.0f;
        it.z = g.z;
        it.ykey = 0.0f;
        it.layer = g.layer;
        it.order = g.order;
        it.groupRef = idx;
        it.premul = 1;
        it.clip = curClip;
        it.tex = 0;
        tgt()->push_back(it);
    }

    // 组装实例的 2D 仿射部分：先按 sx/sy 缩放，再绕 Z 轴旋转 rot（弧度），
    // 最后平移到 (x,y)。hw/hh 是半宽半高，着色器用它把单位四边形撑开。
    static void setTransform(C2Instance& d, float x, float y, float rot, float sx, float sy, float hw, float hh) {
        float c = std::cos(rot), s = std::sin(rot);
        d.m00 = c * sx;
        d.m01 = s * sx;
        d.m10 = -s * sy;
        d.m11 = c * sy;
        d.tx = x;
        d.ty = y;
        d.hw = hw;
        d.hh = hh;
    }

    // 通用入队：补上排序键、提交序号、当前裁剪与纹理，写进当前目标列表。
    void addItem(const C2Instance& d, unsigned tex, int layer, float z, float ykey, int premul) {
        C2Item it = {};
        it.d = d;
        it.z = z;
        it.ykey = ykey;
        it.layer = layer;
        it.order = order++;
        it.groupRef = -1;
        it.premul = premul;
        it.clip = curClip;
        it.tex = tex;
        tgt()->push_back(it);
    }

    std::vector<C2Group> groups;
};

// 进程内唯一的画布状态，这个模块是全局单例写法，画布资源全挂在文件级静态变量上。
static C2State g_c2;
// 顶点数组 + 四个缓冲：单位四边形顶点、六索引（两个三角形）、实例流数据
static rhi::Vao g_c2Vao = 0;
static rhi::Buffer g_c2QuadVbo = 0;
static rhi::Buffer g_c2IdxVbo = 0;
static rhi::Buffer g_c2InstVbo = 0;
// 实例缓冲的当前容量（以实例个数计），满了翻倍重建，避免每帧重分配
static size_t g_c2InstCap = 0;
static rhi::Program g_c2Prog = 0;
// 1x1 全白纹理，纯色矢量图元与"无贴图"情况都绑它，省掉着色器分支
static rhi::Texture g_c2WhiteTex = 0;
// 上传实例数据用的暂存数组，避免每帧新分配
static std::vector<C2Instance> g_c2Stage;
// uniform 位置缓存，链接后查一次，之后直接复用
static int g_c2LocView = -1;
static int g_c2LocPremul = -1;
static int g_c2LocT0 = -1;
static int g_c2LocT2 = -1;

// 桌面走 GL 3.3 核心，Android 走 GLES 3.2，两者只是版本头不同，正文共用。

#ifdef GVWCIER_USE_GLES
#define C2_VS_HEADER "#version 320 es\nprecision highp float;\n"
#define C2_FS_HEADER "#version 320 es\nprecision highp float;\n"
#else
#define C2_VS_HEADER "#version 330 core\n"
#define C2_FS_HEADER "#version 330 core\n"
#endif

// 顶点着色器：一次绘制画出成百上千个四边形，靠实例化 + 每个实例自带仿射矩阵完成
// 位置、旋转、缩放；a_corner 是单位四边形的四个角 (-1..1)，v_local 是变换前的局部坐标，
// 片元阶段算 SDF 要用它。uv 由 a_uv 的偏移与跨度线性插值得到。
static const char* kC2VS = C2_VS_HEADER R"(
layout(location = 0) in vec2 a_corner;
layout(location = 1) in vec4 a_xf0;
layout(location = 2) in vec4 a_xf1;
layout(location = 3) in vec4 a_color;
layout(location = 4) in vec4 a_uv;
layout(location = 5) in vec4 a_k;
layout(location = 6) in vec4 a_p;
uniform mat4 u_view;
out vec2 v_local;
out vec2 v_half;
out vec2 v_uv;
out vec4 v_color;
out vec4 v_k;
out vec4 v_p;
void main() {
    v_half = a_xf1.zw;
    v_local = a_corner * v_half;
    vec2 wp = vec2(a_xf0.x * v_local.x + a_xf0.z * v_local.y + a_xf1.x,
                   a_xf0.y * v_local.x + a_xf0.w * v_local.y + a_xf1.y);
    gl_Position = u_view * vec4(wp, 0.0, 1.0);
    v_uv = a_uv.xy + a_uv.zw * (a_corner * 0.5 + 0.5);
    v_color = a_color;
    v_k = a_k;
    v_p = a_p;
}
)";

// 片元着色器：一个着色器覆盖所有 2D 图元，按 v_k.x 的整数类型分派。
// 0 精灵（直接采样贴图）；1 圆角矩形；2 圆；3 圆弧；4 线段；5 分组合成贴图。
// 矢量图形用有符号距离场 + 屏幕空间导数做抗锯齿，所以不需要额外的 MSAA。
static const char* kC2FS = C2_FS_HEADER R"(
in vec2 v_local;
in vec2 v_half;
in vec2 v_uv;
in vec4 v_color;
in vec4 v_k;
in vec4 v_p;
uniform sampler2D u_tex0;
uniform sampler2D u_tex2;
uniform int u_premul;
out vec4 frag;

// 圆角矩形 SDF：b 为半宽半高，r 为圆角半径，返回值 <0 在内部。
float sdRoundBox(vec2 p, vec2 b, float r) {
    vec2 q = abs(p) - b + vec2(r);
    return min(max(q.x, q.y), 0.0) + length(max(q, vec2(0.0))) - r;
}

// 线段 SDF：点到线段 a-b 的最近距离。
float sdSegment(vec2 p, vec2 a, vec2 b) {
    vec2 pa = p - a;
    vec2 ba = b - a;
    float h = clamp(dot(pa, ba) / max(dot(ba, ba), 1e-8), 0.0, 1.0);
    return length(pa - ba * h);
}

// 圆弧 SDF：角度落在 [a0,a1] 时按到圆周的距离算，否则取到两个端点的最近距离，
// 这样弧的两头是圆的。th 为线宽，所以最后统一减半个线宽。
float sdArc(vec2 p, float r, float th, float a0, float a1) {
    float ang = atan(p.y, p.x);
    if (ang >= a0 && ang <= a1) return abs(length(p) - r) - th * 0.5;
    vec2 d0 = vec2(cos(a0), sin(a0)) * r;
    vec2 d1 = vec2(cos(a1), sin(a1)) * r;
    return min(length(p - d0), length(p - d1)) - th * 0.5;
}

// 把 SDF 距离换算成覆盖率。fwidth 取该像素上的距离变化量，正好是一个像素宽度的渐变带，
// 1e-5 的下限防止退化图元（距离恒定）除零。
float aa(float d) {
    float w = max(fwidth(d), 1e-5);
    return clamp(0.5 - d / w, 0.0, 1.0);
}

void main() {
    int kind = int(v_k.x + 0.5);
    vec4 col = v_color;
    float a = 1.0;

    if (kind == 0) {
        vec4 t = texture(u_tex0, v_uv);
        col *= t;
        a = col.a;
        col.a = 1.0;
    } else if (kind == 1) {
        float d = sdRoundBox(v_local, v_half, v_k.y);
        if (v_k.z > 0.0) d = abs(d) - v_k.z * 0.5;
        a = aa(d);
    } else if (kind == 2) {
        float d = length(v_local) - v_k.y;
        if (v_k.z > 0.0) d = abs(d) - v_k.z * 0.5;
        a = aa(d);
    } else if (kind == 3) {
        a = aa(sdArc(v_local, v_k.y, v_k.z, v_k.w, v_p.x));
    } else if (kind == 4) {
        a = aa(sdSegment(v_local, vec2(-v_half.x, 0.0), vec2(v_half.x, 0.0)) - v_half.y);
    } else if (kind == 5) {
        vec4 t = texture(u_tex2, v_uv);
        float ga = v_color.a;
        frag = vec4(t.rgb * ga, t.a * ga);
        return;
    } else {
        a = 0.0;
    }

    a *= col.a;
    col.a = a;
    if (u_premul == 1) col.rgb *= a;
    frag = col;
}
)";

// 着色器编译，失败时把 info log 打到控制台并返回 0，tag 用来区分是哪份着色器出的错。
static rhi::Stage c2CompileShader(unsigned type, const char* src, const char* tag) {
    rhi::Stage sh = rhi::Gvtcier_Rhi_CreateShader(type);
    rhi::Gvtcier_Rhi_ShaderSource(sh, 1, &src, nullptr);
    rhi::Gvtcier_Rhi_CompileShader(sh);
    int ok = 0;
    rhi::Gvtcier_Rhi_GetShaderiv(sh, rhi::CompileStatus, &ok);
    if (!ok) {
        int len = 0;
        rhi::Gvtcier_Rhi_GetShaderiv(sh, rhi::InfoLogLength, &len);
        std::vector<char> buf((size_t)(len > 1 ? len : 1));
        int got = 0;
        rhi::Gvtcier_Rhi_GetShaderInfoLog(sh, (int)buf.size(), &got, buf.data());
        printf("[c2d] %s failed:\n%s\n", tag, buf.data());
        rhi::Gvtcier_Rhi_DeleteShader(sh);
        return 0;
    }
    return sh;
}

// 编译顶点/片元并链接成程序。成功后着色器对象就可以删了，链接失败则连程序一起释放。
static rhi::Program c2LinkProgram(const char* vs, const char* fs, const char* tag) {
    rhi::Stage v = c2CompileShader(rhi::VertexShader, vs, tag);
    rhi::Stage f = c2CompileShader(rhi::FragmentShader, fs, tag);
    if (!v || !f) return 0;
    rhi::Program p = rhi::Gvtcier_Rhi_CreateProgram();
    rhi::Gvtcier_Rhi_AttachShader(p, v);
    rhi::Gvtcier_Rhi_AttachShader(p, f);
    rhi::Gvtcier_Rhi_LinkProgram(p);
    int ok = 0;
    rhi::Gvtcier_Rhi_GetProgramiv(p, rhi::LinkStatus, &ok);
    if (!ok) {
        int len = 0;
        rhi::Gvtcier_Rhi_GetProgramiv(p, rhi::InfoLogLength, &len);
        std::vector<char> buf((size_t)(len > 1 ? len : 1));
        int got = 0;
        rhi::Gvtcier_Rhi_GetProgramInfoLog(p, (int)buf.size(), &got, buf.data());
        printf("[c2d] %s link failed:\n%s\n", tag, buf.data());
        rhi::Gvtcier_Rhi_DeleteProgram(p);
        return 0;
    }
    rhi::Gvtcier_Rhi_DeleteShader(v);
    rhi::Gvtcier_Rhi_DeleteShader(f);
    return p;
}

// 生成 1x1 全白纹理。向量图形不需要贴图，但采样器不能为空，
// 绑白色就等于"乘 1"，颜色原样输出。这里借用 15 号纹理单元，避免踩到常规纹理槽。
static rhi::Texture c2WhiteTexture() {
    rhi::Texture tex = 0;
    unsigned char px[4] = { 255, 255, 255, 255 };
    rhi::Gvtcier_Rhi_GenTextures(1, &tex);
    rhi::Gvtcier_Rhi_BindTextureUnit(15, rhi::Tex2D, tex);
    rhi::Gvtcier_Rhi_TexImage2D(rhi::Tex2D, 0, rhi::Rgba8, 1, 1, rhi::Rgba, rhi::UnsignedByte, px);
    rhi::Gvtcier_Rhi_TexParameteri(rhi::Tex2D, rhi::TexMinFilter, rhi::Linear);
    rhi::Gvtcier_Rhi_TexParameteri(rhi::Tex2D, rhi::TexMagFilter, rhi::Linear);
    rhi::Gvtcier_Rhi_TexParameteri(rhi::Tex2D, rhi::TexWrapS, rhi::ClampToEdge);
    rhi::Gvtcier_Rhi_TexParameteri(rhi::Tex2D, rhi::TexWrapT, rhi::ClampToEdge);
    return tex;
}

// 保证实例缓冲能装下 n 个实例。容量不足时按 2 的幂翻倍重建（最小 4096），
// 只增不减，宁可多占一点显存也不要在稳定帧率时反复重分配。
static void c2EnsureInstCapacity(size_t n) {
    if (n <= g_c2InstCap) return;
    size_t cap = g_c2InstCap ? g_c2InstCap : 4096;
    while (cap < n) cap *= 2;
    rhi::Gvtcier_Rhi_BindBuffer(rhi::ArrayBuffer, g_c2InstVbo);
    rhi::Gvtcier_Rhi_BufferData(rhi::ArrayBuffer, cap * sizeof(C2Instance), nullptr, rhi::StreamDraw);
    g_c2InstCap = cap;
}

// 浮点矩形逐字段比较，用来把相邻的"同裁剪、同混合模式"图元并成一次绘制。
static bool c2RectEq(const C2Rect& a, const C2Rect& b) {
    return a.x == b.x && a.y == b.y && a.w == b.w && a.h == b.h;
}

// 图元排序规则：先比层号（大层），再比 z（层内深度），再比 ykey（2.5D 前后关系），
// 最后比提交序号保证同一批同键图元严格保持脚本/UI 的书写顺序。用 std::sort 不是稳定排序，
// 所以必须带 order 这一级兜底。
static bool c2ItemLess(const C2Item& a, const C2Item& b) {
    if (a.layer != b.layer) return a.layer < b.layer;
    if (a.z != b.z) return a.z < b.z;
    if (a.ykey != b.ykey) return a.ykey < b.ykey;
    return a.order < b.order;
}

// 2D 专用光栅状态，跟 3D 状态分开维护，避免互相污染
static rhi::RasterState g_c2Rs;
// 把当前光栅状态推给 RHI，改动任何一个字段后都要调一次
static void c2RsFlush() { rhi::Gvtcier_Rhi_SetRasterState(&g_c2Rs); }

// 应用裁剪矩形。GL 的裁剪原点在左下角，而画布坐标原点在左上角，
// 所以 y 要换算成 viewportH - (y + h)。矩形宽高为 0 时直接关掉裁剪，
// 这样"裁剪到空"和"不裁剪"就区分不开了，靠调用方保证别传无效矩形。
static void c2ApplyScissor(const C2Rect& clip, int viewportH) {
    if (clip.w <= 0.0f || clip.h <= 0.0f) {
        g_c2Rs.scissor = false;
        c2RsFlush();
        return;
    }
    g_c2Rs.scissor = true;
    int x = (int)std::floor(clip.x);
    int y = (int)std::floor(clip.y);
    int w = (int)std::ceil(clip.w);
    int h = (int)std::ceil(clip.h);
    if (w < 0) w = 0;
    if (h < 0) h = 0;
    g_c2Rs.scissorX = x;
    g_c2Rs.scissorY = viewportH - (y + h);
    g_c2Rs.scissorW = w;
    g_c2Rs.scissorH = h;
    c2RsFlush();
}

// 保证分组拥有与组矩形等大的离屏纹理（RGBA8）。尺寸没变就复用，变了就销毁重建。
// 纹理用线性过滤、边缘钳制，避免组边缘采样到外面的像素。
static void c2EnsureGroupFbo(C2Group& g) {
    int w = (int)std::ceil(g.rect.w);
    int h = (int)std::ceil(g.rect.h);
    if (w < 1) w = 1;
    if (h < 1) h = 1;
    if (g.fbo && g.fboW == w && g.fboH == h) return;
    if (g.fbo) {
        rhi::Gvtcier_Rhi_DeleteFramebuffers(1, &g.fbo);
        rhi::Gvtcier_Rhi_DeleteTextures(1, &g.tex);
        g.fbo = 0;
        g.tex = 0;
    }
    rhi::Gvtcier_Rhi_GenTextures(1, &g.tex);
    rhi::Gvtcier_Rhi_BindTextureUnit(15, rhi::Tex2D, g.tex);
    rhi::Gvtcier_Rhi_TexImage2D(rhi::Tex2D, 0, rhi::Rgba8, w, h, rhi::Rgba, rhi::UnsignedByte, nullptr);
    rhi::Gvtcier_Rhi_TexParameteri(rhi::Tex2D, rhi::TexMinFilter, rhi::Linear);
    rhi::Gvtcier_Rhi_TexParameteri(rhi::Tex2D, rhi::TexMagFilter, rhi::Linear);
    rhi::Gvtcier_Rhi_TexParameteri(rhi::Tex2D, rhi::TexWrapS, rhi::ClampToEdge);
    rhi::Gvtcier_Rhi_TexParameteri(rhi::Tex2D, rhi::TexWrapT, rhi::ClampToEdge);
    rhi::Gvtcier_Rhi_GenFramebuffers(1, &g.fbo);
    rhi::Gvtcier_Rhi_BindFramebuffer(rhi::Framebuffer, g.fbo);
    rhi::Gvtcier_Rhi_FramebufferTexture2D(rhi::Framebuffer, rhi::ColorAttachment0, rhi::Tex2D, g.tex, 0);
    if (rhi::Gvtcier_Rhi_CheckFramebufferStatus(rhi::Framebuffer) != rhi::FramebufferComplete)
        printf("[c2d] group framebuffer incomplete\n");
    rhi::Gvtcier_Rhi_BindFramebuffer(rhi::Framebuffer, 0);
    g.fboW = w;
    g.fboH = h;
}

// 渲染一个图元列表的前置声明，分组渲染时会递归调回自己
static void c2RenderList(std::vector<C2Item>& items, float view[16], rhi::Fbo targetFbo, int targetW, int targetH,
                         int forcePremul, C2Stats& st);

// 把某个分组画进它自己的离屏纹理：切换到组 FBO，清成全透明黑，
// 用组矩形做正交投影（注意 y 同样翻转，b 传下边、t 传上边），
// 组内一律强制预乘混合（forcePremul=1），这样合成时分组的 alpha 可以直接乘。
static void c2RenderGroup(int idx, rhi::Fbo targetFbo, int targetW, int targetH, C2Stats& st) {
    C2Group& g = g_c2.groups[idx];
    c2EnsureGroupFbo(g);
    rhi::Gvtcier_Rhi_BindFramebuffer(rhi::Framebuffer, g.fbo);
    rhi::Gvtcier_Rhi_Viewport(0, 0, g.fboW, g.fboH);
    g_c2Rs.scissor = false;
    c2RsFlush();
    rhi::Gvtcier_Rhi_ClearColor(0.0f, 0.0f, 0.0f, 0.0f);
    rhi::Gvtcier_Rhi_Clear(rhi::ColorBit);
    M4 v = m4Ortho(g.rect.x, g.rect.x + (float)g.fboW, g.rect.y + (float)g.fboH, g.rect.y, -1.0f, 1.0f);
    c2RenderList(g.items, v.m, g.fbo, g.fboW, g.fboH, 1, st);
    rhi::Gvtcier_Rhi_BindFramebuffer(rhi::Framebuffer, targetFbo);
    rhi::Gvtcier_Rhi_Viewport(0, 0, targetW, targetH);
    st.groups++;
}

// 核心渲染流程。
// 1) 先按 c2ItemLess 排序；
// 2) 把所有分组先单独渲染到各自离屏纹理（分组项的绘制顺序靠排在它前面的排序项体现，
//    这里并不严格按序，属于现有实现）；
// 3) 把全部实例一次性上传到实例缓冲；
// 4) 顺序扫描，遇到分组项单独一次绘制，遇到普通项则合并"同裁剪 + 同预乘模式"的连续段，
//    每段只切一次 scissor、设一次混合、发一次 DrawElementsInstanced。
static void c2RenderList(std::vector<C2Item>& items, float view[16], rhi::Fbo targetFbo, int targetW, int targetH,
                         int forcePremul, C2Stats& st) {
    if (items.empty()) return;

    std::sort(items.begin(), items.end(), c2ItemLess);

    for (size_t i = 0; i < items.size(); ++i)
        if (items[i].groupRef >= 0) c2RenderGroup(items[i].groupRef, targetFbo, targetW, targetH, st);

    rhi::Gvtcier_Rhi_BindFramebuffer(rhi::Framebuffer, targetFbo);
    rhi::Gvtcier_Rhi_Viewport(0, 0, targetW, targetH);
    g_c2Rs.depthTest = false;
    g_c2Rs.cull = false;
    g_c2Rs.depthWrite = true;
    c2RsFlush();
    g_c2Stage.resize(items.size());
    for (size_t i = 0; i < items.size(); ++i) g_c2Stage[i] = items[i].d;
    c2EnsureInstCapacity(items.size());
    rhi::Gvtcier_Rhi_BindBuffer(rhi::ArrayBuffer, g_c2InstVbo);
    rhi::Gvtcier_Rhi_BufferSubData(rhi::ArrayBuffer, 0, items.size() * sizeof(C2Instance), g_c2Stage.data());
    st.inst += (int)items.size();

    rhi::Gvtcier_Rhi_UseProgram(g_c2Prog);
    rhi::Gvtcier_Rhi_UniformMatrix4fv(g_c2LocView, 1, false, view);
    rhi::Gvtcier_Rhi_Uniform1i(g_c2LocT0, rhi::TexSlot::Scene);
    rhi::Gvtcier_Rhi_Uniform1i(g_c2LocT2, 2);

    rhi::Gvtcier_Rhi_BindVertexArray(g_c2Vao);
    g_c2Rs.blend = true;
    c2RsFlush();

    size_t i = 0;
    while (i < items.size()) {
        if (items[i].groupRef >= 0) {
            c2ApplyScissor(items[i].clip, targetH);
            g_c2Rs.blendSrc = rhi::One; g_c2Rs.blendDst = rhi::OneMinusSrcAlpha;
            c2RsFlush();
            rhi::Gvtcier_Rhi_Uniform1i(g_c2LocPremul, 1);
            rhi::Gvtcier_Rhi_BindTextureUnit(2, rhi::Tex2D, g_c2.groups[items[i].groupRef].tex);
            for (int a = 0; a < 6; ++a)
                rhi::Gvtcier_Rhi_VertexAttribPointer((unsigned)(a + 1), 4, rhi::Float, false, (int)sizeof(C2Instance),
                                                     (const void*)(size_t)(i * sizeof(C2Instance) + (size_t)a * 16));
            rhi::Gvtcier_Rhi_DrawElementsInstanced(rhi::Triangles, 6, rhi::UnsignedShort, nullptr, 1);
            st.batches++;
            ++i;
            continue;
        }

        C2Rect c = items[i].clip;
        int pm = forcePremul ? 1 : items[i].premul;
        size_t j = i + 1;
        while (j < items.size() && items[j].groupRef < 0 && c2RectEq(items[j].clip, c) &&
               (forcePremul ? 1 : items[j].premul) == pm)
            ++j;

        c2ApplyScissor(c, targetH);
        if (pm) { g_c2Rs.blendSrc = rhi::One; g_c2Rs.blendDst = rhi::OneMinusSrcAlpha; }
        else { g_c2Rs.blendSrc = rhi::SrcAlpha; g_c2Rs.blendDst = rhi::OneMinusSrcAlpha; }
        c2RsFlush();
        rhi::Gvtcier_Rhi_Uniform1i(g_c2LocPremul, pm);
        for (int a = 0; a < 6; ++a)
            rhi::Gvtcier_Rhi_VertexAttribPointer((unsigned)(a + 1), 4, rhi::Float, false, (int)sizeof(C2Instance),
                                                 (const void*)(size_t)(i * sizeof(C2Instance) + (size_t)a * 16));
        rhi::Gvtcier_Rhi_DrawElementsInstanced(rhi::Triangles, 6, rhi::UnsignedShort, nullptr, (int)(j - i));
        st.batches++;
        i = j;
    }

    rhi::Gvtcier_Rhi_BindVertexArray(0);
    g_c2Rs.scissor = false;
    c2RsFlush();
}

// 初始化画布：建 VAO、搭实例化顶点布局、编译着色器、建白纹理。
// 已经初始化过直接返回 true，重复调用是安全的。
// 顶点布局：location 0 是单位四边形角点，location 1~6 是实例的 6 个 vec4（每个占 16 字节步进），
// 都设置了 divisor=1，即每个实例前进一次而不是每个顶点一次。
bool Canvas2D::Gvtcier_Canvas2D_Init() {
    if (g_c2Prog) return true;

    const float quad[8] = { -1.0f, -1.0f, 1.0f, -1.0f, 1.0f, 1.0f, -1.0f, 1.0f };
    const unsigned short idx[6] = { 0, 1, 2, 0, 2, 3 };

    rhi::Gvtcier_Rhi_GenVertexArrays(1, &g_c2Vao);
    rhi::Gvtcier_Rhi_BindVertexArray(g_c2Vao);

    rhi::Gvtcier_Rhi_GenBuffers(1, &g_c2QuadVbo);
    rhi::Gvtcier_Rhi_BindBuffer(rhi::ArrayBuffer, g_c2QuadVbo);
    rhi::Gvtcier_Rhi_BufferData(rhi::ArrayBuffer, sizeof(quad), quad, rhi::StaticDraw);
    rhi::Gvtcier_Rhi_VertexAttribPointer(0, 2, rhi::Float, false, 0, nullptr);
    rhi::Gvtcier_Rhi_EnableAttrib(0);

    rhi::Gvtcier_Rhi_GenBuffers(1, &g_c2IdxVbo);
    rhi::Gvtcier_Rhi_BindBuffer(rhi::ElementArrayBuffer, g_c2IdxVbo);
    rhi::Gvtcier_Rhi_BufferData(rhi::ElementArrayBuffer, sizeof(idx), idx, rhi::StaticDraw);

    rhi::Gvtcier_Rhi_GenBuffers(1, &g_c2InstVbo);
    rhi::Gvtcier_Rhi_BindBuffer(rhi::ArrayBuffer, g_c2InstVbo);
    rhi::Gvtcier_Rhi_BufferData(rhi::ArrayBuffer, 4096 * sizeof(C2Instance), nullptr, rhi::StreamDraw);
    for (int a = 0; a < 6; ++a) {
        rhi::Gvtcier_Rhi_VertexAttribPointer((unsigned)(a + 1), 4, rhi::Float, false, (int)sizeof(C2Instance),
                                             (const void*)(size_t)(a * 16));
        rhi::Gvtcier_Rhi_EnableAttrib((unsigned)(a + 1));
        rhi::Gvtcier_Rhi_VertexAttribDivisor((unsigned)(a + 1), 1);
    }

    rhi::Gvtcier_Rhi_BindVertexArray(0);

    g_c2Prog = c2LinkProgram(kC2VS, kC2FS, "c2d");
    if (!g_c2Prog) return false;
    g_c2LocView = rhi::Gvtcier_Rhi_UniformLocation(g_c2Prog, "u_view");
    g_c2LocPremul = rhi::Gvtcier_Rhi_UniformLocation(g_c2Prog, "u_premul");
    g_c2LocT0 = rhi::Gvtcier_Rhi_UniformLocation(g_c2Prog, "u_tex0");
    g_c2LocT2 = rhi::Gvtcier_Rhi_UniformLocation(g_c2Prog, "u_tex2");
    g_c2WhiteTex = c2WhiteTexture();
    printf("[c2d] canvas2d ready\n");
    return true;
}

// 释放画布持有的全部 GPU 资源：分组离屏纹理、白纹理、VAO/缓冲、着色器程序。
// 注意不清空 g_c2.groups 数组本身，下次 Init 时会复用同一批槽位重新分配。
void Canvas2D::Gvtcier_Canvas2D_Shutdown() {
    for (size_t i = 0; i < g_c2.groups.size(); ++i) {
        if (g_c2.groups[i].fbo) rhi::Gvtcier_Rhi_DeleteFramebuffers(1, &g_c2.groups[i].fbo);
        if (g_c2.groups[i].tex) rhi::Gvtcier_Rhi_DeleteTextures(1, &g_c2.groups[i].tex);
    }
    if (g_c2WhiteTex) { rhi::Gvtcier_Rhi_DeleteTextures(1, &g_c2WhiteTex); g_c2WhiteTex = 0; }
    if (g_c2Vao) { rhi::Gvtcier_Rhi_DeleteVertexArrays(1, &g_c2Vao); g_c2Vao = 0; }
    if (g_c2QuadVbo) { rhi::Gvtcier_Rhi_DeleteBuffers(1, &g_c2QuadVbo); g_c2QuadVbo = 0; }
    if (g_c2IdxVbo) { rhi::Gvtcier_Rhi_DeleteBuffers(1, &g_c2IdxVbo); g_c2IdxVbo = 0; }
    if (g_c2InstVbo) { rhi::Gvtcier_Rhi_DeleteBuffers(1, &g_c2InstVbo); g_c2InstVbo = 0; }
    if (g_c2Prog) { rhi::Gvtcier_Rhi_DeleteProgram(g_c2Prog); g_c2Prog = 0; }
    g_c2InstCap = 0;
}

// 开始一帧：清掉上一帧的图元，把已有分组全部回收进空闲池（离屏纹理保留复用），
// 重置排序序号与裁剪状态，并记下这一帧的相机参数。
// cam.rotDeg 是角度，这里换成弧度存着；camEnabled 置 true 表示这一帧走相机变换，
// 后续调 RenderUI 会把它改回 false。
void Canvas2D::Gvtcier_Canvas2D_BeginFrame(int w, int h, const C2Camera2D& cam) {
    g_c2.main.clear();
    g_c2.stack.clear();
    g_c2.freeGroups.clear();
    for (size_t i = 0; i < g_c2.groups.size(); ++i) {
        g_c2.groups[i].items.clear();
        g_c2.freeGroups.push_back((int)i);
    }
    g_c2.order = 0;
    g_c2.hasClip = false;
    g_c2.curClip = { 0, 0, 0, 0 };
    g_c2.width = w;
    g_c2.height = h;
    g_c2.camX = cam.x;
    g_c2.camY = cam.y;
    g_c2.camZoom = cam.zoom;
    g_c2.camRot = cam.rotDeg * 3.14159265f / 180.0f;
    g_c2.camEnabled = true;
}

// 出帧的公共实现。相机变换的顺序是：正交投影 -> 平移到屏幕中心 -> 旋转 -> 缩放 -> 平移到 -相机位置，
// 乘起来等价于"以相机世界坐标为原点、带旋转和变焦的视图矩阵"。
// 没开相机时只用正交投影，坐标就是纯屏幕像素。
// targetFbo 传 0 表示直接画到默认帧缓冲（窗口或引擎的离屏目标）。
static void c2EndFrameImpl(C2Stats* outStats) {
    C2Stats st;
    M4 v;
    if (g_c2.camEnabled) {
        v = m4Ortho(0.0f, (float)g_c2.width, (float)g_c2.height, 0.0f, -1.0f, 1.0f);
        v = m4Mul(v, m4Translate((float)g_c2.width * 0.5f, (float)g_c2.height * 0.5f));
        v = m4Mul(v, m4RotateZ(g_c2.camRot));
        v = m4Mul(v, m4Scale(g_c2.camZoom, g_c2.camZoom));
        v = m4Mul(v, m4Translate(-g_c2.camX, -g_c2.camY));
    } else {
        v = m4Ortho(0.0f, (float)g_c2.width, (float)g_c2.height, 0.0f, -1.0f, 1.0f);
    }
    rhi::Gvtcier_Rhi_BindTextureUnit(0, rhi::Tex2D, g_c2WhiteTex);
    rhi::Gvtcier_Rhi_BindTextureUnit(2, rhi::Tex2D, g_c2WhiteTex);
    c2RenderList(g_c2.main, v.m, 0, g_c2.width, g_c2.height, 0, st);
    if (outStats) *outStats = st;
}

// 结束一帧并上屏。outStats 非空时回填本帧的实例数、绘制批次数、分组数，供调试统计用。
void Canvas2D::Gvtcier_Canvas2D_EndFrame(C2Stats* outStats) {
    c2EndFrameImpl(outStats);
}

// 以纯屏幕坐标再出一遍图元，供引擎 UI 层使用：先关掉相机变换，
// 这样坐标 (0,0) 永远是左上角像素，界面不受游戏相机平移缩放影响。
void Canvas2D::Gvtcier_Canvas2D_RenderUI(C2Stats* outStats) {
    g_c2.camEnabled = false;
    c2EndFrameImpl(outStats);
}

// 压入裁剪矩形，供脚本 绘制_裁剪 使用，后续绘制全部被限制在矩形内。
void Canvas2D::Gvtcier_Canvas2D_PushClip(const C2Rect& r) {
    g_c2.pushClip(r);
}

// 结束最近一次裁剪。由于没有裁剪栈，调用一次就回到"无裁剪"。
void Canvas2D::Gvtcier_Canvas2D_PopClip() {
    g_c2.popClip();
}

// 绘制一张带旋转缩放的精灵。
// tex 是已加载的纹理 id；x/y 是中心点像素坐标；rotDeg 角度制；sx/sy 额外缩放；
// hw/hh 是未缩放时的半宽半高；u/v/uw/vh 是图集内 uv 原点与跨度（整张图填 0,0,1,1）；
// c 是叠乘颜色；layer/z/ykey 是排序键（见 c2ItemLess）。
// 精灵不预乘（premul=0），走常规 SrcAlpha 混合。
void Canvas2D::Gvtcier_Canvas2D_Sprite(unsigned tex, float x, float y, float rotDeg, float sx, float sy,
                                       float hw, float hh, float u, float v, float uw, float vh,
                                       const C2Color& c, int layer, float z, float ykey) {
    C2Instance d = {};
    C2State::setTransform(d, x, y, rotDeg * 3.14159265f / 180.0f, sx, sy, hw, hh);
    d.u = u; d.v = v; d.uw = uw; d.vh = vh;
    d.r = c.r; d.g = c.g; d.b = c.b; d.a = c.a;
    d.k0 = (float)C2_SPRITE;
    g_c2.addItem(d, tex, layer, z, ykey, 0);
}

// 绘制圆角矩形，x/y 是左上角、w/h 是宽高。radius 会被夹到短边的一半，
// 否则半径大于半宽时 SDF 会算出自相交的怪形状。stroke>0 时变成描边而不是填充。
void Canvas2D::Gvtcier_Canvas2D_RoundRect(float x, float y, float w, float h, float radius, float stroke,
                                          const C2Color& c, int layer, float z) {
    C2Instance d = {};
    C2State::setTransform(d, x + w * 0.5f, y + h * 0.5f, 0.0f, 1.0f, 1.0f, w * 0.5f, h * 0.5f);
    d.r = c.r; d.g = c.g; d.b = c.b; d.a = c.a;
    d.k0 = (float)C2_RRECT;
    d.k1 = std::min(radius, std::min(w, h) * 0.5f);
    d.k2 = stroke;
    g_c2.addItem(d, 0, layer, z, 0.0f, 0);
}

// 绘制圆（x/y 是圆心）。四边形向外多扩出半个线宽再加 1 像素，
// 给抗锯齿留边，否则描边外缘会被裁掉一圈。
void Canvas2D::Gvtcier_Canvas2D_Circle(float x, float y, float radius, float stroke,
                                       const C2Color& c, int layer, float z) {
    C2Instance d = {};
    float ext = radius + stroke * 0.5f + 1.0f;
    C2State::setTransform(d, x, y, 0.0f, 1.0f, 1.0f, ext, ext);
    d.r = c.r; d.g = c.g; d.b = c.b; d.a = c.a;
    d.k0 = (float)C2_CIRCLE;
    d.k1 = radius;
    d.k2 = stroke;
    g_c2.addItem(d, 0, layer, z, 0.0f, 0);
}

// 绘制圆弧。a0Deg/a1Deg 是起止角（度数，0 度指向 +x，逆时针增大），
// thickness 是线宽，半径同样要外扩半个线宽 + 1 像素留抗锯齿余量。
void Canvas2D::Gvtcier_Canvas2D_Arc(float x, float y, float radius, float thickness, float a0Deg, float a1Deg,
                                    const C2Color& c, int layer, float z) {
    C2Instance d = {};
    float a0 = a0Deg * 3.14159265f / 180.0f;
    float a1 = a1Deg * 3.14159265f / 180.0f;
    float ext = radius + thickness * 0.5f + 1.0f;
    C2State::setTransform(d, x, y, 0.0f, 1.0f, 1.0f, ext, ext);
    d.r = c.r; d.g = c.g; d.b = c.b; d.a = c.a;
    d.k0 = (float)C2_ARC;
    d.k1 = radius;
    d.k2 = thickness;
    d.k3 = a0;
    d.p0 = a1;
    g_c2.addItem(d, 0, layer, z, 0.0f, 0);
}

// 绘制线段。做法是把线当作"长条矩形"：位置取中点，旋转角取线段方向 atan2(dy,dx)，
// 半宽取长度一半、半高取厚度一半，片元里再按半高做一次 SDF 收边，两头是平的。
void Canvas2D::Gvtcier_Canvas2D_Line(float x0, float y0, float x1, float y1, float thickness,
                                     const C2Color& c, int layer, float z, float ykey) {
    C2Instance d = {};
    float dx = x1 - x0, dy = y1 - y0;
    float len = std::sqrt(dx * dx + dy * dy);
    C2State::setTransform(d, (x0 + x1) * 0.5f, (y0 + y1) * 0.5f, std::atan2(dy, dx), 1.0f, 1.0f,
                          len * 0.5f, thickness * 0.5f);
    d.r = c.r; d.g = c.g; d.b = c.b; d.a = c.a;
    d.k0 = (float)C2_LINE;
    g_c2.addItem(d, 0, layer, z, ykey, 0);
}

// 开始半透明分组。r 是组的屏幕矩形，alpha 是整组不透明度。
// 用法上必须与 EndGroup 成对出现，中间画的图元都进离屏纹理，不直接落屏。
void Canvas2D::Gvtcier_Canvas2D_BeginGroup(const C2Rect& r, float alpha, int layer, float z) {
    g_c2.beginGroup(r, alpha, layer, z);
}

// 结束当前分组，把它合成为一张贴图塞回父级列表。没有正在进行的组时什么也不做。
void Canvas2D::Gvtcier_Canvas2D_EndGroup() {
    g_c2.endGroup();
}
