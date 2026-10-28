#include "Gvtcier.h"
#include <cstdio>

// 固定管线（OpenGL 1.x）函数装载。
// 这些函数只在兼容上下文中有效，因此本文件只负责从 opengl32.dll 取地址，
// 上下文切换由 图形4.cpp 的 glLegacyCreate/glLegacyBind/glCoreBind 负责。
namespace gl1 {

Gl1 g = {};

#ifndef _WIN32

// 非 Windows 平台没有可用的固定管线导出，直接报告不可用。
bool Gvtcier_Gl1_LoadFunctions() {
    printf("[gl1] 平台不支持固定管线\n");
    return false;
}

bool Gvtcier_Gl1_Available() {
    return false;
}

#else

// 是否已完成加载，避免重复枚举导出表。
static bool g_loaded = false;

// 从 opengl32.dll 取单个函数地址，取不到就打印日志并置 ok = false。
#define LOAD_GL1(name) do { \
    g.name = (PFN_##name)GetProcAddress(dll, #name); \
    if (!g.name) { printf("[gl1] missing GL 1.1 function: %s\n", #name); ok = false; } \
} while (0)

// 加载全部固定管线函数地址。已加载成功过则直接返回 true；全部成功才算成功。
bool Gvtcier_Gl1_LoadFunctions() {
    if (g_loaded) return true;
    HMODULE dll = GetModuleHandleA("opengl32.dll");
    if (!dll) dll = LoadLibraryA("opengl32.dll");
    if (!dll) {
        printf("[gl1] cannot load opengl32.dll\n");
        return false;
    }
    bool ok = true;
    LOAD_GL1(glBegin);
    LOAD_GL1(glEnd);
    LOAD_GL1(glVertex2f);
    LOAD_GL1(glVertex3f);
    LOAD_GL1(glColor3f);
    LOAD_GL1(glColor4f);
    LOAD_GL1(glNormal3f);
    LOAD_GL1(glTexCoord2f);
    LOAD_GL1(glMatrixMode);
    LOAD_GL1(glLoadIdentity);
    LOAD_GL1(glPushMatrix);
    LOAD_GL1(glPopMatrix);
    LOAD_GL1(glOrtho);
    LOAD_GL1(glFrustum);
    LOAD_GL1(glTranslatef);
    LOAD_GL1(glRotatef);
    LOAD_GL1(glScalef);
    LOAD_GL1(glShadeModel);
    LOAD_GL1(glGetFloatv);
    LOAD_GL1(glLineWidth);
    LOAD_GL1(glPointSize);
    LOAD_GL1(glLightfv);
    LOAD_GL1(glMaterialfv);
    LOAD_GL1(glDepthRange);
    LOAD_GL1(glMultMatrixf);
    LOAD_GL1(glLoadMatrixf);
#undef LOAD_GL1
    g_loaded = ok;
    if (ok) printf("[gl1] 固定管线函数加载完成\n");
    return ok;
}

// 是否可用：已加载成功，且绘制必需的 glBegin/glEnd/glVertex3f 都已就位。
bool Gvtcier_Gl1_Available() {
    return g_loaded && g.glBegin && g.glEnd && g.glVertex3f;
}

#endif

}
