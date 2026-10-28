#pragma once
#include "Rhidriver/rhi.h"

struct HWND__;
typedef HWND__* HWND;

// 后端函数表条目清单，X 宏定义一次、展开多次：
// 每一项形如 X(返回类型, 函数名, 形参列表, 实参列表)。
// GL / GLES / Vulkan 三个后端各自实现同名的 t_Gvtcier_Rhi_* 函数，
// 这里只做声明，具体绑定在各自后端的 .cpp 里通过 RHI_TABLE_ENTRIES 展开生成。
#define RHI_TABLE_ENTRIES(X) \
    X(const char*, Gvtcier_Rhi_GetString, (unsigned name), (name)) \
    X(void, Gvtcier_Rhi_GetIntegerv, (unsigned pname, int* data), (pname, data)) \
    X(unsigned, Gvtcier_Rhi_GetError, (), ()) \
    X(void, Gvtcier_Rhi_ClearColor, (float r, float g, float b, float a), (r, g, b, a)) \
    X(void, Gvtcier_Rhi_ClearDepth, (double d), (d)) \
    X(void, Gvtcier_Rhi_Clear, (unsigned mask), (mask)) \
    X(void, Gvtcier_Rhi_Viewport, (int x, int y, int w, int h), (x, y, w, h)) \
    X(void, Gvtcier_Rhi_SetRasterState, (const RasterState* s), (s)) \
    X(void, Gvtcier_Rhi_DrawBuffer, (unsigned buf), (buf)) \
    X(void, Gvtcier_Rhi_DrawBuffers, (int n, const unsigned* bufs), (n, bufs)) \
    X(void, Gvtcier_Rhi_ReadBuffer, (unsigned buf), (buf)) \
    X(void, Gvtcier_Rhi_ReadPixels, (int x, int y, int w, int h, unsigned format, unsigned type, void* data), (x, y, w, h, format, type, data)) \
    X(void, Gvtcier_Rhi_GenTextures, (int n, Texture* out), (n, out)) \
    X(void, Gvtcier_Rhi_BindTexture, (unsigned target, Texture t), (target, t)) \
    X(void, Gvtcier_Rhi_BindTextureUnit, (int unit, unsigned target, Texture t), (unit, target, t)) \
    X(void, Gvtcier_Rhi_DeleteTextures, (int n, const Texture* t), (n, t)) \
    X(void, Gvtcier_Rhi_TexImage2D, (unsigned target, int level, int internalFormat, int w, int h, unsigned format, unsigned type, const void* data), (target, level, internalFormat, w, h, format, type, data)) \
    X(void, Gvtcier_Rhi_TexSubImage2D, (unsigned target, int level, int x, int y, int w, int h, unsigned format, unsigned type, const void* data), (target, level, x, y, w, h, format, type, data)) \
    X(void, Gvtcier_Rhi_ReadBufferTex, (unsigned format, unsigned type, unsigned tex), (format, type, tex)) \
    X(void, Gvtcier_Rhi_TexImage3D, (unsigned target, int level, int internalFormat, int w, int h, int d, unsigned format, unsigned type, const void* data), (target, level, internalFormat, w, h, d, format, type, data)) \
    X(void, Gvtcier_Rhi_TexSubImage3D, (unsigned target, int level, int x, int y, int z, int w, int h, int dd, unsigned format, unsigned type, const void* data), (target, level, x, y, z, w, h, dd, format, type, data)) \
    X(void, Gvtcier_Rhi_GenerateMipmap, (unsigned target), (target)) \
    X(void, Gvtcier_Rhi_TexParameteri, (unsigned target, unsigned pname, int value), (target, pname, value)) \
    X(void, Gvtcier_Rhi_TexParameterf, (unsigned target, unsigned pname, float value), (target, pname, value)) \
    X(void, Gvtcier_Rhi_SetAnisotropy, (int level), (level)) \
    X(int, Gvtcier_Rhi_Anisotropy, (), ()) \
    X(void, Gvtcier_Rhi_GenVertexArrays, (int n, Vao* out), (n, out)) \
    X(void, Gvtcier_Rhi_BindVertexArray, (Vao v), (v)) \
    X(void, Gvtcier_Rhi_DeleteVertexArrays, (int n, const Vao* v), (n, v)) \
    X(void, Gvtcier_Rhi_GenBuffers, (int n, Buffer* out), (n, out)) \
    X(void, Gvtcier_Rhi_BindBuffer, (unsigned target, Buffer b), (target, b)) \
    X(void, Gvtcier_Rhi_BufferData, (unsigned target, size_t size, const void* data, unsigned usage), (target, size, data, usage)) \
    X(void, Gvtcier_Rhi_BufferSubData, (unsigned target, ptrdiff_t offset, size_t size, const void* data), (target, offset, size, data)) \
    X(void, Gvtcier_Rhi_BindBufferBase, (unsigned target, unsigned index, Buffer b), (target, index, b)) \
    X(unsigned, Gvtcier_Rhi_GetUniformBlockIndex, (Program p, const char* name), (p, name)) \
    X(void, Gvtcier_Rhi_UniformBlockBinding, (Program p, unsigned blockIndex, unsigned binding), (p, blockIndex, binding)) \
    X(void, Gvtcier_Rhi_GetActiveUniformBlockiv, (Program p, unsigned blockIndex, unsigned pname, int* params), (p, blockIndex, pname, params)) \
    X(void, Gvtcier_Rhi_GetUniformIndices, (Program p, int count, const char* const* names, unsigned* indices), (p, count, names, indices)) \
    X(void, Gvtcier_Rhi_GetActiveUniformsiv, (Program p, int count, const unsigned* indices, unsigned pname, int* params), (p, count, indices, pname, params)) \
    X(void, Gvtcier_Rhi_GetActiveUniformName, (Program p, unsigned index, int bufSize, int* length, char* name), (p, index, bufSize, length, name)) \
    X(void, Gvtcier_Rhi_DispatchCompute, (unsigned x, unsigned y, unsigned z), (x, y, z)) \
    X(void, Gvtcier_Rhi_MemoryBarrier, (unsigned barriers), (barriers)) \
    X(bool, Gvtcier_Rhi_HasCompute, (), ()) \
    X(bool, Gvtcier_Rhi_HasInstancing, (), ()) \
    X(void, Gvtcier_Rhi_DeleteBuffers, (int n, const Buffer* b), (n, b)) \
    X(void, Gvtcier_Rhi_VertexAttribPointer, (unsigned index, int size, unsigned type, bool normalized, int stride, const void* ptr), (index, size, type, normalized, stride, ptr)) \
    X(void, Gvtcier_Rhi_EnableAttrib, (unsigned index), (index)) \
    X(void, Gvtcier_Rhi_VertexAttribDivisor, (unsigned index, unsigned divisor), (index, divisor)) \
    X(Stage, Gvtcier_Rhi_CreateShader, (unsigned type), (type)) \
    X(void, Gvtcier_Rhi_ShaderSource, (Stage s, int count, const char* const* str, const int* len), (s, count, str, len)) \
    X(void, Gvtcier_Rhi_ShaderSourceBinary, (Stage s, const void* data, int len), (s, data, len)) \
    X(void, Gvtcier_Rhi_CompileShader, (Stage s), (s)) \
    X(void, Gvtcier_Rhi_GetShaderiv, (Stage s, unsigned pname, int* params), (s, pname, params)) \
    X(void, Gvtcier_Rhi_GetShaderInfoLog, (Stage s, int bufSize, int* len, char* log), (s, bufSize, len, log)) \
    X(void, Gvtcier_Rhi_DeleteShader, (Stage s), (s)) \
    X(Program, Gvtcier_Rhi_CreateProgram, (), ()) \
    X(void, Gvtcier_Rhi_AttachShader, (Program p, Stage s), (p, s)) \
    X(void, Gvtcier_Rhi_LinkProgram, (Program p), (p)) \
    X(void, Gvtcier_Rhi_GetProgramiv, (Program p, unsigned pname, int* params), (p, pname, params)) \
    X(void, Gvtcier_Rhi_GetProgramInfoLog, (Program p, int bufSize, int* len, char* log), (p, bufSize, len, log)) \
    X(void, Gvtcier_Rhi_DeleteProgram, (Program p), (p)) \
    X(void, Gvtcier_Rhi_UseProgram, (Program p), (p)) \
    X(Location, Gvtcier_Rhi_UniformLocation, (Program p, const char* name), (p, name)) \
    X(void, Gvtcier_Rhi_Uniform1i, (Location l, int v), (l, v)) \
    X(void, Gvtcier_Rhi_Uniform1f, (Location l, float v), (l, v)) \
    X(void, Gvtcier_Rhi_Uniform2f, (Location l, float x, float y), (l, x, y)) \
    X(void, Gvtcier_Rhi_Uniform3f, (Location l, float x, float y, float z), (l, x, y, z)) \
    X(void, Gvtcier_Rhi_Uniform4f, (Location l, float x, float y, float z, float w), (l, x, y, z, w)) \
    X(void, Gvtcier_Rhi_UniformMatrix4fv, (Location l, int count, bool transpose, const float* v), (l, count, transpose, v)) \
    X(void, Gvtcier_Rhi_DrawArrays, (unsigned mode, int first, int count), (mode, first, count)) \
    X(void, Gvtcier_Rhi_DrawElements, (unsigned mode, int count, unsigned type, const void* indices), (mode, count, type, indices)) \
    X(void, Gvtcier_Rhi_DrawElementsInstanced, (unsigned mode, int count, unsigned type, const void* indices, int instanceCount), (mode, count, type, indices, instanceCount)) \
    X(void, Gvtcier_Rhi_GenFramebuffers, (int n, Fbo* out), (n, out)) \
    X(void, Gvtcier_Rhi_BindFramebuffer, (unsigned target, Fbo f), (target, f)) \
    X(void, Gvtcier_Rhi_DeleteFramebuffers, (int n, const Fbo* f), (n, f)) \
    X(void, Gvtcier_Rhi_FramebufferTexture2D, (unsigned target, unsigned attachment, unsigned textarget, Texture t, int level), (target, attachment, textarget, t, level)) \
    X(void, Gvtcier_Rhi_FramebufferTextureLayer, (unsigned target, unsigned attachment, Texture t, int level, int layer), (target, attachment, t, level, layer)) \
    X(unsigned, Gvtcier_Rhi_CheckFramebufferStatus, (unsigned target), (target)) \
    X(void, Gvtcier_Rhi_GenRenderbuffers, (int n, Rbo* out), (n, out)) \
    X(void, Gvtcier_Rhi_BindRenderbuffer, (unsigned target, Rbo r), (target, r)) \
    X(void, Gvtcier_Rhi_DeleteRenderbuffers, (int n, const Rbo* r), (n, r)) \
    X(void, Gvtcier_Rhi_RenderbufferStorage, (unsigned target, unsigned internalformat, int w, int h), (target, internalformat, w, h)) \
    X(void, Gvtcier_Rhi_FramebufferRenderbuffer, (unsigned target, unsigned attachment, unsigned rendertarget, Rbo r), (target, attachment, rendertarget, r)) \
    X(bool, Gvtcier_Rhi_LegacyCreate, (), ()) \
    X(bool, Gvtcier_Rhi_LegacyBind, (), ()) \
    X(bool, Gvtcier_Rhi_CoreBind, (), ())

namespace rhi {

// 后端切换的核心：一张纯函数指针表。
// 上层的 Gvtcier_Rhi_* 只是转发器（见 渲染抽象.cpp），全部实际调用都经过这里，
// 所以“换后端”等价于“换这张表”。表是静态常量，不参与分配，也不持有指针对象。
struct RhiTable {
    void (*present)();            // 交换缓冲提交画面
    void (*setVsync)(bool vsync); // 开关垂直同步
    void (*resize)(int w, int h); // 记录/响应尺寸变化
    void (*shutdown)();           // 释放上下文

// 把条目清单展开成成员函数指针声明
#define RHI_TABLE_ENTRY(ret, name, params, args) ret (*name) params;
    RHI_TABLE_ENTRIES(RHI_TABLE_ENTRY)
#undef RHI_TABLE_ENTRY
};

// 取得各后端的函数表；返回 nullptr 表示该后端在此平台不可用（初始化失败）。
// glTable 需要 Win32 窗口句柄创建 WGL 上下文，glesTable 需要原生窗口（GvWWindow*），vkTable 需要 HWND。
RhiTable* glTable(HWND hwnd);
RhiTable* glesTable(void* gvwWindow);
RhiTable* vkTable(HWND hwnd);

// sokol_gfx 包装出来的后端表。跑的是 Vulkan，上层看到的仍是同一套 Gvtcier_Rhi_*。
RhiTable* sgTable(HWND hwnd);

}