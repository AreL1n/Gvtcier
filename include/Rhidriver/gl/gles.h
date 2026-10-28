#pragma once
// OpenGL ES 后端的自家声明头：类型、带 GLES_ 前缀的常量、函数指针类型与函数地址结构体。
// 与 gl4.h 刻意使用 GLES_ 前缀，避免与桌面 GL 的同名宏/类型在同一编译单元里冲突；
// 函数地址由 GLES后端.cpp 通过 GvW_GL_Proc 取回，存入 rhi::gles。

#include <cstddef>

// GLES 基础类型。
typedef unsigned int  GLenum;      // 枚举常量
typedef unsigned int  GLbitfield;  // 位掩码
typedef unsigned int  GLuint;      // 对象名
typedef int           GLint;       // 有符号整型
typedef int           GLsizei;     // 尺寸/数量
typedef unsigned char GLboolean;   // 布尔
typedef unsigned char GLubyte;     // 无符号字节
typedef float         GLfloat;     // 单精度浮点
typedef char          GLchar;      // 字符串/日志字符
typedef ptrdiff_t     GLintptr;    // 缓冲内字节偏移
typedef ptrdiff_t     GLsizeiptr;  // 缓冲字节大小

#ifndef GL_FALSE
#define GL_FALSE 0  // 布尔假
#endif
#ifndef GL_TRUE
#define GL_TRUE 1   // 布尔真
#endif

// ---- 常量：统一加 GLES_ 前缀 ----
#define GLES_GL_COLOR_BUFFER_BIT      0x00004000  // 清颜色缓冲
#define GLES_GL_DEPTH_BUFFER_BIT      0x00000100  // 清深度缓冲
#define GLES_GL_STENCIL_BUFFER_BIT    0x00000400  // 清模板缓冲
#define GLES_GL_DEPTH_TEST            0x0B71      // 深度测试开关
#define GLES_GL_CULL_FACE             0x0B44      // 面剔除开关
#define GLES_GL_SCISSOR_TEST          0x0C11      // 裁剪矩形开关
#define GLES_GL_POLYGON_OFFSET_FILL   0x8038      // 多边形偏移（GLES 只有填充模式）
#define GLES_GL_FRONT_AND_BACK        0x0408      // 正反两面
#define GLES_GL_BACK                  0x0405      // 背面
#define GLES_GL_CCW                   0x0901      // 逆时针为正面
#define GLES_GL_BLEND                 0x0BE2      // 混合开关
#define GLES_GL_SRC_ALPHA             0x0302      // 源 alpha 混合因子
#define GLES_GL_ONE_MINUS_SRC_ALPHA   0x0303      // 1-源 alpha 混合因子
#define GLES_GL_ONE                   1           // 混合因子 1
#define GLES_GL_TRIANGLES             0x0004      // 独立三角形
#define GLES_GL_LINES                 0x0001      // 线段
#define GLES_GL_VERSION               0x1F02      // 版本字符串
#define GLES_GL_RENDERER              0x1F01      // 渲染器字符串
#define GLES_GL_ARRAY_BUFFER          0x8892      // 顶点属性缓冲
#define GLES_GL_ELEMENT_ARRAY_BUFFER  0x8893      // 索引缓冲
#define GLES_GL_SHADER_STORAGE_BUFFER 0x90D2      // 着色器可读写缓冲（GLES 3.1 起）
#define GLES_GL_STATIC_DRAW           0x88E4      // 一次上传多次使用
#define GLES_GL_DYNAMIC_DRAW          0x88E8      // 频繁更新
#define GLES_GL_STREAM_DRAW           0x88E0      // 每帧重传
#define GLES_GL_STREAM_READ           0x88E5      // 每帧回读
#define GLES_GL_FLOAT                 0x1406      // 32 位浮点
#define GLES_GL_UNSIGNED_INT          0x1405      // 32 位无符号整数
#define GLES_GL_UNSIGNED_BYTE         0x1401      // 8 位无符号字节
#define GLES_GL_UNSIGNED_SHORT        0x1403      // 16 位无符号整数
#define GLES_GL_TEXTURE_2D            0x0DE1      // 二维纹理目标
#define GLES_GL_TEXTURE_2D_ARRAY      0x8DC1      // 二维纹理数组目标
#define GLES_GL_TEXTURE_3D            0x806F      // 三维纹理目标
#define GLES_GL_TEXTURE0              0x84C0      // 纹理单元 0 基址
#define GLES_GL_RGBA                  0x1908      // 四通道
#define GLES_GL_RGBA8                 0x8058      // 8 位四通道内部格式
#define GLES_GL_RGBA16F               0x881A      // 16 位浮点四通道内部格式
#define GLES_GL_RGBA32F               0x8814      // 32 位浮点四通道内部格式
#define GLES_GL_DEPTH_COMPONENT       0x1902      // 深度格式
#define GLES_GL_DEPTH_COMPONENT24     0x81A6      // 24 位深度内部格式
#define GLES_GL_TEXTURE_MIN_FILTER    0x2801      // 缩小过滤参数
#define GLES_GL_TEXTURE_MAG_FILTER    0x2800      // 放大过滤参数
#define GLES_GL_TEXTURE_WRAP_S        0x2802      // S 方向寻址参数
#define GLES_GL_TEXTURE_WRAP_T        0x2803      // T 方向寻址参数
#define GLES_GL_TEXTURE_WRAP_R        0x8072      // R 方向寻址参数
#define GLES_GL_NEAREST               0x2600      // 最近点采样
#define GLES_GL_LINEAR                0x2601      // 线性采样
#define GLES_GL_CLAMP_TO_EDGE         0x812F      // 夹取边缘
#define GLES_GL_REPEAT                0x2901      // 平铺重复
#define GLES_GL_VERTEX_SHADER         0x8B31      // 顶点着色器阶段
#define GLES_GL_FRAGMENT_SHADER       0x8B30      // 片元着色器阶段
#define GLES_GL_COMPUTE_SHADER        0x91B9      // 计算着色器阶段
#define GLES_GL_COMPILE_STATUS        0x8B81      // 编译状态查询
#define GLES_GL_LINK_STATUS           0x8B82      // 链接状态查询
#define GLES_GL_INFO_LOG_LENGTH       0x8B84      // 日志长度查询
#define GLES_GL_FRAMEBUFFER           0x8D40      // 帧缓冲绑定目标
#define GLES_GL_FRAMEBUFFER_COMPLETE  0x8CD5      // 帧缓冲完整
#define GLES_GL_DEPTH_ATTACHMENT      0x8D00      // 深度附件点
#define GLES_GL_RENDERBUFFER          0x8D41      // 渲染缓冲绑定目标
#define GLES_GL_COLOR_ATTACHMENT0     0x8CE0      // 颜色附件 0
#define GLES_GL_MAP_READ_BIT          0x0001      // 映射缓冲可读
#define GLES_GL_MAP_WRITE_BIT         0x0002      // 映射缓冲可写
#define GLES_GL_SHADER_STORAGE_BARRIER_BIT 0x00002000  // SSBO 写入屏障

// 批量声明函数指针类型：GLES_DECL(ret, glFoo, (参数)) 展开为 typedef ret (*glFooFn)(参数)。
// GLES 接口在 Windows 上由外部窗口库转发，调用约定与桌面 GL 的 GLAPIENTRY 无关，故不写调用约定。
#define GLES_DECL(ret, name, params) typedef ret (*name##Fn) params;

// 清屏、视口与状态开关
GLES_DECL(void, glClear, (GLbitfield mask))
GLES_DECL(void, glClearColor, (GLfloat r, GLfloat g, GLfloat b, GLfloat a))
GLES_DECL(void, glClearDepthf, (GLfloat depth))
GLES_DECL(void, glViewport, (GLint x, GLint y, GLsizei w, GLsizei h))
GLES_DECL(void, glEnable, (GLenum cap))
GLES_DECL(void, glDisable, (GLenum cap))
GLES_DECL(void, glDepthFunc, (GLenum func))
GLES_DECL(void, glDepthMask, (GLboolean flag))
GLES_DECL(void, glStencilFunc, (GLenum func, GLint ref, GLuint mask))
GLES_DECL(void, glStencilOp, (GLenum sfail, GLenum dpfail, GLenum dppass))
GLES_DECL(void, glStencilMask, (GLuint mask))
GLES_DECL(void, glClearStencil, (GLint s))
GLES_DECL(void, glColorMask, (GLboolean r, GLboolean g, GLboolean b, GLboolean a))
GLES_DECL(void, glCullFace, (GLenum mode))
GLES_DECL(void, glFrontFace, (GLenum mode))
GLES_DECL(void, glScissor, (GLint x, GLint y, GLsizei w, GLsizei h))
GLES_DECL(void, glPolygonOffset, (GLfloat factor, GLfloat units))
// 绘制/读取目标与像素操作
GLES_DECL(void, glDrawBuffers, (GLsizei n, const GLenum* bufs))
GLES_DECL(void, glReadBuffer, (GLenum buf))
GLES_DECL(void, glReadPixels, (GLint x, GLint y, GLsizei w, GLsizei h, GLenum format, GLenum type, void* data))
GLES_DECL(void, glBlitFramebuffer, (GLint srcX0, GLint srcY0, GLint srcX1, GLint srcY1, GLint dstX0, GLint dstY0, GLint dstX1, GLint dstY1, GLbitfield mask, GLenum filter))
GLES_DECL(void, glCopyTexSubImage2D, (GLenum target, GLint level, GLint xoffset, GLint yoffset, GLint x, GLint y, GLsizei w, GLsizei h))
GLES_DECL(void*, glMapBufferRange, (GLenum target, GLintptr offset, GLsizeiptr length, GLbitfield access))
GLES_DECL(GLboolean, glUnmapBuffer, (GLenum target))
// 纹理
GLES_DECL(void, glGenTextures, (GLsizei n, GLuint* tex))
GLES_DECL(void, glBindTexture, (GLenum target, GLuint tex))
GLES_DECL(void, glDeleteTextures, (GLsizei n, const GLuint* tex))
GLES_DECL(void, glTexImage2D, (GLenum target, GLint level, GLint internalFormat, GLsizei w, GLsizei h, GLint border, GLenum format, GLenum type, const void* data))
GLES_DECL(void, glTexImage3D, (GLenum target, GLint level, GLint internalFormat, GLsizei w, GLsizei h, GLsizei d, GLint border, GLenum format, GLenum type, const void* data))
GLES_DECL(void, glTexSubImage3D, (GLenum target, GLint level, GLint xoffset, GLint yoffset, GLint zoffset, GLsizei w, GLsizei h, GLsizei d, GLenum format, GLenum type, const void* data))
GLES_DECL(void, glTexSubImage2D, (GLenum target, GLint level, GLint xoffset, GLint yoffset, GLsizei w, GLsizei h, GLenum format, GLenum type, const void* data))
GLES_DECL(void, glGenerateMipmap, (GLenum target))
GLES_DECL(void, glTexParameteri, (GLenum target, GLenum pname, GLint param))
GLES_DECL(void, glTexParameterf, (GLenum target, GLenum pname, GLfloat param))
// 查询
GLES_DECL(const GLubyte*, glGetString, (GLenum name))
GLES_DECL(void, glGetIntegerv, (GLenum pname, GLint* data))
GLES_DECL(GLenum, glGetError, (void))
GLES_DECL(void, glActiveTexture, (GLenum texture))
// 顶点数组与缓冲
GLES_DECL(void, glGenVertexArrays, (GLsizei n, GLuint* arrays))
GLES_DECL(void, glBindVertexArray, (GLuint array))
GLES_DECL(void, glDeleteVertexArrays, (GLsizei n, const GLuint* arrays))
GLES_DECL(void, glGenBuffers, (GLsizei n, GLuint* buffers))
GLES_DECL(void, glBindBuffer, (GLenum target, GLuint buffer))
GLES_DECL(void, glBufferData, (GLenum target, GLsizeiptr size, const void* data, GLenum usage))
GLES_DECL(void, glBufferSubData, (GLenum target, GLintptr offset, GLsizeiptr size, const void* data))
GLES_DECL(void, glBindBufferBase, (GLenum target, GLuint index, GLuint buffer))
GLES_DECL(GLuint, glGetUniformBlockIndex, (GLuint program, const GLchar* uniformBlockName))
GLES_DECL(void, glUniformBlockBinding, (GLuint program, GLuint uniformBlockIndex, GLuint uniformBlockBinding))
GLES_DECL(void, glGetActiveUniformBlockiv, (GLuint program, GLuint uniformBlockIndex, GLenum pname, GLint* params))
GLES_DECL(void, glGetUniformIndices, (GLuint program, GLsizei uniformCount, const GLchar* const* uniformNames, GLuint* uniformIndices))
GLES_DECL(void, glGetActiveUniformsiv, (GLuint program, GLsizei uniformCount, const GLuint* uniformIndices, GLenum pname, GLint* params))
GLES_DECL(void, glGetActiveUniformName, (GLuint program, GLuint uniformIndex, GLsizei bufSize, GLsizei* length, GLchar* uniformName))
GLES_DECL(void, glDispatchCompute, (GLuint x, GLuint y, GLuint z))
GLES_DECL(void, glMemoryBarrier, (GLbitfield barriers))
GLES_DECL(void, glDeleteBuffers, (GLsizei n, const GLuint* buffers))
GLES_DECL(void, glVertexAttribPointer, (GLuint index, GLint size, GLenum type, GLboolean normalized, GLsizei stride, const void* pointer))
GLES_DECL(void, glEnableVertexAttribArray, (GLuint index))
// 着色器
GLES_DECL(GLuint, glCreateShader, (GLenum type))
GLES_DECL(void, glShaderSource, (GLuint shader, GLsizei count, const GLchar* const* string, const GLint* length))
GLES_DECL(void, glCompileShader, (GLuint shader))
GLES_DECL(void, glGetShaderiv, (GLuint shader, GLenum pname, GLint* params))
GLES_DECL(void, glGetShaderInfoLog, (GLuint shader, GLsizei bufSize, GLsizei* length, GLchar* infoLog))
GLES_DECL(void, glDeleteShader, (GLuint shader))
GLES_DECL(GLuint, glCreateProgram, (void))
GLES_DECL(void, glAttachShader, (GLuint program, GLuint shader))
GLES_DECL(void, glLinkProgram, (GLuint program))
GLES_DECL(void, glGetProgramiv, (GLuint program, GLenum pname, GLint* params))
GLES_DECL(void, glGetProgramInfoLog, (GLuint program, GLsizei bufSize, GLsizei* length, GLchar* infoLog))
GLES_DECL(void, glDeleteProgram, (GLuint program))
GLES_DECL(void, glUseProgram, (GLuint program))
GLES_DECL(GLint, glGetUniformLocation, (GLuint program, const GLchar* name))
GLES_DECL(void, glUniform1i, (GLint location, GLint v0))
GLES_DECL(void, glUniform1f, (GLint location, GLfloat v0))
GLES_DECL(void, glUniform2f, (GLint location, GLfloat v0, GLfloat v1))
GLES_DECL(void, glUniform3f, (GLint location, GLfloat v0, GLfloat v1, GLfloat v2))
GLES_DECL(void, glUniform4f, (GLint location, GLfloat v0, GLfloat v1, GLfloat v2, GLfloat v3))
GLES_DECL(void, glUniformMatrix4fv, (GLint location, GLsizei count, GLboolean transpose, const GLfloat* value))
// 绘制
GLES_DECL(void, glDrawArrays, (GLenum mode, GLint first, GLsizei count))
GLES_DECL(void, glDrawElements, (GLenum mode, GLsizei count, GLenum type, const void* indices))
GLES_DECL(void, glDrawElementsInstanced, (GLenum mode, GLsizei count, GLenum type, const void* indices, GLsizei instanceCount))
GLES_DECL(void, glVertexAttribDivisor, (GLuint index, GLuint divisor))
// 帧缓冲与渲染缓冲
GLES_DECL(void, glGenFramebuffers, (GLsizei n, GLuint* framebuffers))
GLES_DECL(void, glBindFramebuffer, (GLenum target, GLuint framebuffer))
GLES_DECL(void, glDeleteFramebuffers, (GLsizei n, const GLuint* framebuffers))
GLES_DECL(void, glFramebufferTexture2D, (GLenum target, GLenum attachment, GLenum textarget, GLuint texture, GLint level))
GLES_DECL(void, glFramebufferTextureLayer, (GLenum target, GLenum attachment, GLuint texture, GLint level, GLint layer))
GLES_DECL(GLenum, glCheckFramebufferStatus, (GLenum target))
GLES_DECL(void, glBlendFunc, (GLenum sfactor, GLenum dfactor))
GLES_DECL(void, glGenRenderbuffers, (GLsizei n, GLuint* renderbuffers))
GLES_DECL(void, glBindRenderbuffer, (GLenum target, GLuint renderbuffer))
GLES_DECL(void, glDeleteRenderbuffers, (GLsizei n, const GLuint* renderbuffers))
GLES_DECL(void, glRenderbufferStorage, (GLenum target, GLenum internalformat, GLsizei width, GLsizei height))
GLES_DECL(void, glFramebufferRenderbuffer, (GLenum target, GLenum attachment, GLenum rendertarget, GLuint renderbuffer))

#undef GLES_DECL

// GLES 函数地址集合。成员由 glesLoad 填充；
// 设备缺失的函数（例如 GLES 3.0 上的 glDispatchCompute）为空指针，
// 上层通过 Gvtcier_Rhi_HasCompute / HasInstancing 探测，调用前必须判空。
struct GLES {
#define GLES_DECL(ret, name, params) name##Fn name;
    GLES_DECL(void, glClear, (GLbitfield mask))
    GLES_DECL(void, glClearColor, (GLfloat r, GLfloat g, GLfloat b, GLfloat a))
    GLES_DECL(void, glClearDepthf, (GLfloat depth))
    GLES_DECL(void, glViewport, (GLint x, GLint y, GLsizei w, GLsizei h))
    GLES_DECL(void, glEnable, (GLenum cap))
    GLES_DECL(void, glDisable, (GLenum cap))
    GLES_DECL(void, glDepthFunc, (GLenum func))
    GLES_DECL(void, glDepthMask, (GLboolean flag))
    GLES_DECL(void, glStencilFunc, (GLenum func, GLint ref, GLuint mask))
    GLES_DECL(void, glStencilOp, (GLenum sfail, GLenum dpfail, GLenum dppass))
    GLES_DECL(void, glStencilMask, (GLuint mask))
    GLES_DECL(void, glClearStencil, (GLint s))
    GLES_DECL(void, glColorMask, (GLboolean r, GLboolean g, GLboolean b, GLboolean a))
    GLES_DECL(void, glCullFace, (GLenum mode))
    GLES_DECL(void, glFrontFace, (GLenum mode))
    GLES_DECL(void, glScissor, (GLint x, GLint y, GLsizei w, GLsizei h))
    GLES_DECL(void, glPolygonOffset, (GLfloat factor, GLfloat units))
    GLES_DECL(void, glDrawBuffers, (GLsizei n, const GLenum* bufs))
    GLES_DECL(void, glReadBuffer, (GLenum buf))
    GLES_DECL(void, glReadPixels, (GLint x, GLint y, GLsizei w, GLsizei h, GLenum format, GLenum type, void* data))
    GLES_DECL(void, glBlitFramebuffer, (GLint srcX0, GLint srcY0, GLint srcX1, GLint srcY1, GLint dstX0, GLint dstY0, GLint dstX1, GLint dstY1, GLbitfield mask, GLenum filter))
    GLES_DECL(void, glCopyTexSubImage2D, (GLenum target, GLint level, GLint xoffset, GLint yoffset, GLint x, GLint y, GLsizei w, GLsizei h))
    GLES_DECL(void*, glMapBufferRange, (GLenum target, GLintptr offset, GLsizeiptr length, GLbitfield access))
    GLES_DECL(GLboolean, glUnmapBuffer, (GLenum target))
    GLES_DECL(void, glGenTextures, (GLsizei n, GLuint* tex))
    GLES_DECL(void, glBindTexture, (GLenum target, GLuint tex))
    GLES_DECL(void, glDeleteTextures, (GLsizei n, const GLuint* tex))
    GLES_DECL(void, glTexImage2D, (GLenum target, GLint level, GLint internalFormat, GLsizei w, GLsizei h, GLint border, GLenum format, GLenum type, const void* data))
    GLES_DECL(void, glTexImage3D, (GLenum target, GLint level, GLint internalFormat, GLsizei w, GLsizei h, GLsizei d, GLint border, GLenum format, GLenum type, const void* data))
    GLES_DECL(void, glTexSubImage3D, (GLenum target, GLint level, GLint xoffset, GLint yoffset, GLint zoffset, GLsizei w, GLsizei h, GLsizei d, GLenum format, GLenum type, const void* data))
    GLES_DECL(void, glTexSubImage2D, (GLenum target, GLint level, GLint xoffset, GLint yoffset, GLsizei w, GLsizei h, GLenum format, GLenum type, const void* data))
    GLES_DECL(void, glGenerateMipmap, (GLenum target))
    GLES_DECL(void, glTexParameteri, (GLenum target, GLenum pname, GLint param))
    GLES_DECL(void, glTexParameterf, (GLenum target, GLenum pname, GLfloat param))
    GLES_DECL(const GLubyte*, glGetString, (GLenum name))
    GLES_DECL(void, glGetIntegerv, (GLenum pname, GLint* data))
    GLES_DECL(GLenum, glGetError, (void))
    GLES_DECL(void, glActiveTexture, (GLenum texture))
    GLES_DECL(void, glGenVertexArrays, (GLsizei n, GLuint* arrays))
    GLES_DECL(void, glBindVertexArray, (GLuint array))
    GLES_DECL(void, glDeleteVertexArrays, (GLsizei n, const GLuint* arrays))
    GLES_DECL(void, glGenBuffers, (GLsizei n, GLuint* buffers))
    GLES_DECL(void, glBindBuffer, (GLenum target, GLuint buffer))
    GLES_DECL(void, glBufferData, (GLenum target, GLsizeiptr size, const void* data, GLenum usage))
    GLES_DECL(void, glBufferSubData, (GLenum target, GLintptr offset, GLsizeiptr size, const void* data))
    GLES_DECL(void, glBindBufferBase, (GLenum target, GLuint index, GLuint buffer))
    GLES_DECL(GLuint, glGetUniformBlockIndex, (GLuint program, const GLchar* uniformBlockName))
    GLES_DECL(void, glUniformBlockBinding, (GLuint program, GLuint uniformBlockIndex, GLuint uniformBlockBinding))
    GLES_DECL(void, glGetActiveUniformBlockiv, (GLuint program, GLuint uniformBlockIndex, GLenum pname, GLint* params))
    GLES_DECL(void, glGetUniformIndices, (GLuint program, GLsizei uniformCount, const GLchar* const* uniformNames, GLuint* uniformIndices))
    GLES_DECL(void, glGetActiveUniformsiv, (GLuint program, GLsizei uniformCount, const GLuint* uniformIndices, GLenum pname, GLint* params))
    GLES_DECL(void, glGetActiveUniformName, (GLuint program, GLuint uniformIndex, GLsizei bufSize, GLsizei* length, GLchar* uniformName))
    GLES_DECL(void, glDispatchCompute, (GLuint x, GLuint y, GLuint z))
    GLES_DECL(void, glMemoryBarrier, (GLbitfield barriers))
    GLES_DECL(void, glDeleteBuffers, (GLsizei n, const GLuint* buffers))
    GLES_DECL(void, glVertexAttribPointer, (GLuint index, GLint size, GLenum type, GLboolean normalized, GLsizei stride, const void* pointer))
    GLES_DECL(void, glEnableVertexAttribArray, (GLuint index))
    GLES_DECL(GLuint, glCreateShader, (GLenum type))
    GLES_DECL(void, glShaderSource, (GLuint shader, GLsizei count, const GLchar* const* string, const GLint* length))
    GLES_DECL(void, glCompileShader, (GLuint shader))
    GLES_DECL(void, glGetShaderiv, (GLuint shader, GLenum pname, GLint* params))
    GLES_DECL(void, glGetShaderInfoLog, (GLuint shader, GLsizei bufSize, GLsizei* length, GLchar* infoLog))
    GLES_DECL(void, glDeleteShader, (GLuint shader))
    GLES_DECL(GLuint, glCreateProgram, (void))
    GLES_DECL(void, glAttachShader, (GLuint program, GLuint shader))
    GLES_DECL(void, glLinkProgram, (GLuint program))
    GLES_DECL(void, glGetProgramiv, (GLuint program, GLenum pname, GLint* params))
    GLES_DECL(void, glGetProgramInfoLog, (GLuint program, GLsizei bufSize, GLsizei* length, GLchar* infoLog))
    GLES_DECL(void, glDeleteProgram, (GLuint program))
    GLES_DECL(void, glUseProgram, (GLuint program))
    GLES_DECL(GLint, glGetUniformLocation, (GLuint program, const GLchar* name))
    GLES_DECL(void, glUniform1i, (GLint location, GLint v0))
    GLES_DECL(void, glUniform1f, (GLint location, GLfloat v0))
    GLES_DECL(void, glUniform2f, (GLint location, GLfloat v0, GLfloat v1))
    GLES_DECL(void, glUniform3f, (GLint location, GLfloat v0, GLfloat v1, GLfloat v2))
    GLES_DECL(void, glUniform4f, (GLint location, GLfloat v0, GLfloat v1, GLfloat v2, GLfloat v3))
    GLES_DECL(void, glUniformMatrix4fv, (GLint location, GLsizei count, GLboolean transpose, const GLfloat* value))
    GLES_DECL(void, glDrawArrays, (GLenum mode, GLint first, GLsizei count))
    GLES_DECL(void, glDrawElements, (GLenum mode, GLsizei count, GLenum type, const void* indices))
    GLES_DECL(void, glDrawElementsInstanced, (GLenum mode, GLsizei count, GLenum type, const void* indices, GLsizei instanceCount))
    GLES_DECL(void, glVertexAttribDivisor, (GLuint index, GLuint divisor))
    GLES_DECL(void, glGenFramebuffers, (GLsizei n, GLuint* framebuffers))
    GLES_DECL(void, glBindFramebuffer, (GLenum target, GLuint framebuffer))
    GLES_DECL(void, glDeleteFramebuffers, (GLsizei n, const GLuint* framebuffers))
    GLES_DECL(void, glFramebufferTexture2D, (GLenum target, GLenum attachment, GLenum textarget, GLuint texture, GLint level))
    GLES_DECL(void, glFramebufferTextureLayer, (GLenum target, GLenum attachment, GLuint texture, GLint level, GLint layer))
    GLES_DECL(GLenum, glCheckFramebufferStatus, (GLenum target))
    GLES_DECL(void, glBlendFunc, (GLenum sfactor, GLenum dfactor))
    GLES_DECL(void, glGenRenderbuffers, (GLsizei n, GLuint* renderbuffers))
    GLES_DECL(void, glBindRenderbuffer, (GLenum target, GLuint renderbuffer))
    GLES_DECL(void, glDeleteRenderbuffers, (GLsizei n, const GLuint* renderbuffers))
    GLES_DECL(void, glRenderbufferStorage, (GLenum target, GLenum internalformat, GLsizei width, GLsizei height))
    GLES_DECL(void, glFramebufferRenderbuffer, (GLenum target, GLenum attachment, GLenum rendertarget, GLuint renderbuffer))
#undef GLES_DECL
};

namespace rhi {

// 全局 GLES 函数地址表，glesTable 初始化成功后可用。
extern GLES gles;

}
