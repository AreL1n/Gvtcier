#include "Gvtcier.h"

namespace {

//当前图形模式。Gvtio_None 表示脚本尚未声明，此时引擎按 3D 处理。
GvtioMode g_mode = Gvtio_None;

}

//脚本声明要使用 2D 绘制模式。切换后主循环走 2D 分支，不做 3D 场景渲染。
void Gvtcier_Gvtio_Use2D() { g_mode = Gvtio_2D; }

//脚本声明要使用 3D 绘制模式。
void Gvtcier_Gvtio_Use3D() { g_mode = Gvtio_3D; }

//是否处于 2D 模式（未声明时为假，即默认按 3D 处理）。
bool Gvtcier_Gvtio_Is2D() { return g_mode == Gvtio_2D; }

//是否显式处于 3D 模式。注意未声明时此函数返回假，与 Is2D 不等价。
bool Gvtcier_Gvtio_Is3D() { return g_mode == Gvtio_3D; }

//当前模式的整型值，便于脚本层透传。
int Gvtcier_Gvtio_Mode() { return (int)g_mode; }
