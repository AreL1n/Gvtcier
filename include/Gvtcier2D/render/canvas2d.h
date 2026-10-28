#pragma once
#include <vector>
#include "bicu/core/export.h"

// 2D 画布用到的几个基础结构。坐标系约定：原点在左上角，x 向右、y 向下，单位是像素。
struct C2Rect { float x, y, w, h; };
struct C2Color { float r, g, b, a; };
// 2D 相机。zoom 为放大倍数，rotDeg 是角度制旋转。
struct C2Camera2D { float x = 0.0f, y = 0.0f, zoom = 1.0f, rotDeg = 0.0f; };

// 图元类型，写入实例的 k0 字段，片元着色器据此走不同分支。
enum C2Kind {
    C2_SPRITE = 0,
    C2_RRECT = 1,
    C2_CIRCLE = 2,
    C2_ARC = 3,
    C2_LINE = 4,
    C2_GROUP = 5
};

// 单个图元的 GPU 实例数据，正好 6 个 vec4，对应顶点属性 location 1~6。
// k0~k3、p0~p3 是各图元类型的私有参数（半径、线宽、角度等），含义由 C2Kind 决定。
// 注意：这里不直接暴露给外部使用，外部只需通过 Canvas2D 的绘制接口提交。
struct C2Instance {
    float m00, m01, m10, m11;
    float tx, ty, hw, hh;
    float r, g, b, a;
    float u, v, uw, vh;
    float k0, k1, k2, k3;
    float p0, p1, p2, p3;
};

// 一帧的渲染统计，用于调试面板观察批处理效果。
struct C2Stats {
    int inst = 0;
    int batches = 0;
    int groups = 0;
};

// 2D 画布。延迟批处理式的即时接口：绘制函数只把图元塞进队列，EndFrame 时才统一排序、合批、上屏。
// 所有状态都放在实现文件的静态变量里，类本身不持有数据，可以随意复制构造。
class GVTCIER_API Canvas2D {
public:
    // 创建 GPU 资源（VAO、缓冲、着色器、白纹理）。重复调用安全，失败返回 false。
    bool Gvtcier_Canvas2D_Init();
    // 释放全部 GPU 资源。
    void Gvtcier_Canvas2D_Shutdown();
    // 开始一帧，清空队列并记录画布尺寸与相机。
    void Gvtcier_Canvas2D_BeginFrame(int w, int h, const C2Camera2D& cam);
    // 结束一帧，按相机变换出图。outStats 可为空。
    void Gvtcier_Canvas2D_EndFrame(C2Stats* outStats);
    // 关掉相机、按纯屏幕像素再出一遍，供引擎 UI 使用。
    void Gvtcier_Canvas2D_RenderUI(C2Stats* outStats);

    // 裁剪入栈（与已有裁剪求交），以及退出裁剪（直接清空，无回退）。
    void Gvtcier_Canvas2D_PushClip(const C2Rect& r);
    void Gvtcier_Canvas2D_PopClip();

    // 精灵：x/y 中心点，rotDeg 角度，sx/sy 缩放，hw/hh 半宽半高，u/v/uw/vh 图集 uv。
    void Gvtcier_Canvas2D_Sprite(unsigned tex, float x, float y, float rotDeg, float sx, float sy,
                                 float hw, float hh, float u, float v, float uw, float vh,
                                 const C2Color& c, int layer, float z, float ykey);
    // 圆角矩形：x/y 左上角，stroke>0 为描边。
    void Gvtcier_Canvas2D_RoundRect(float x, float y, float w, float h, float radius, float stroke,
                                    const C2Color& c, int layer, float z);
    // 圆：x/y 圆心，stroke>0 为描边。
    void Gvtcier_Canvas2D_Circle(float x, float y, float radius, float stroke,
                                 const C2Color& c, int layer, float z);
    // 圆弧：a0Deg/a1Deg 起止角度，thickness 线宽。
    void Gvtcier_Canvas2D_Arc(float x, float y, float radius, float thickness, float a0Deg, float a1Deg,
                              const C2Color& c, int layer, float z);
    // 线段：两端点 + 线宽。
    void Gvtcier_Canvas2D_Line(float x0, float y0, float x1, float y1, float thickness,
                               const C2Color& c, int layer, float z, float ykey);

    // 半透明分组：组内图元先画进离屏纹理，再按 alpha 整体合成，避免重叠处双重混合。
    void Gvtcier_Canvas2D_BeginGroup(const C2Rect& r, float alpha, int layer, float z);
    void Gvtcier_Canvas2D_EndGroup();

private:
    friend struct C2CanvasAccess;
    // 占位成员，真正的状态在实现文件的全局变量里。
    std::vector<unsigned char> impl_;
};
