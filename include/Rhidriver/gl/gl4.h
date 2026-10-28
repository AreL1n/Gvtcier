#pragma once
// 桌面 OpenGL 的自家声明头：不依赖系统 glext.h，手工声明用到的类型、常量与函数指针。
// 函数地址在运行时由 图形4.cpp 从 opengl32.dll（1.1）与 wglGetProcAddress（1.2 以上）取回，
// 全部存进全局结构体 gl，调用处写成 gl.glXxx()。
#include <windows.h>
#include <cstddef>

#ifndef GLAPIENTRY
#define GLAPIENTRY APIENTRY
#endif

// GL 基础类型声明。
typedef unsigned int  GLenum;      // 枚举常量
typedef unsigned int  GLbitfield;  // 位掩码
typedef unsigned int  GLuint;      // 对象名
typedef int           GLint;       // 有符号整型
typedef int           GLsizei;     // 尺寸/数量
typedef unsigned char GLboolean;   // 布尔
typedef unsigned char GLubyte;     // 无符号字节
typedef float         GLfloat;     // 单精度浮点
typedef double        GLdouble;    // 双精度浮点
typedef char          GLchar;      // 字符串/日志字符
typedef ptrdiff_t     GLintptr;    // 缓冲内字节偏移
typedef ptrdiff_t     GLsizeiptr;  // 缓冲字节大小

// ---- 常量：与本文件下方函数指针配套使用，数值为标准 GL 枚举值 ----
#define GL_COLOR_BUFFER_BIT      0x00004000  // 清颜色缓冲
#define GL_DEPTH_BUFFER_BIT      0x00000100  // 清深度缓冲
#define GL_DEPTH_TEST            0x0B71      // 深度测试开关
#define GL_CULL_FACE             0x0B44      // 面剔除开关
#define GL_SCISSOR_TEST          0x0C11      // 裁剪矩形开关
#define GL_FRONT_AND_BACK        0x0408      // 正反两面
#define GL_BACK                  0x0405      // 背面
#define GL_CCW                   0x0901      // 逆时针为正面
#define GL_LEQUAL                0x0203      // 深度小于等于通过
#define GL_LINE                  0x1B01      // 线框模式
#define GL_FILL                  0x1B02      // 实心填充
#define GL_BLEND                 0x0BE2      // 混合开关
#define GL_SRC_ALPHA             0x0302      // 源 alpha 混合因子
#define GL_ONE_MINUS_SRC_ALPHA   0x0303      // 1-源 alpha 混合因子
#define GL_ONE                   1           // 混合因子 1
#define GL_TRIANGLES             0x0004      // 独立三角形
#define GL_LINES                 0x0001      // 线段
#define GL_NONE                  0           // 无（绘制/读取缓冲为空）
#define GL_FALSE                 0           // 布尔假
#define GL_TRUE                  1           // 布尔真
#define GL_VERSION               0x1F02      // 版本字符串
#define GL_RENDERER              0x1F01      // 渲染器字符串
#define GL_ARRAY_BUFFER          0x8892      // 顶点属性缓冲
#define GL_ELEMENT_ARRAY_BUFFER  0x8893      // 索引缓冲
#define GL_SHADER_STORAGE_BUFFER 0x90D2      // 着色器可读写缓冲
#define GL_STATIC_DRAW           0x88E4      // 一次上传多次使用
#define GL_DYNAMIC_DRAW          0x88E8      // 频繁更新
#define GL_READ_WRITE            0x88BA      // 映射缓冲可读写
#define GL_FLOAT                 0x1406      // 32 位浮点
#define GL_RED                   0x1903      // 单通道
#define GL_R8                    0x8229      // 8 位单通道内部格式
#define GL_UNSIGNED_INT          0x1405      // 32 位无符号整数
#define GL_UNSIGNED_BYTE         0x1401      // 8 位无符号字节
#define GL_TEXTURE_2D            0x0DE1      // 二维纹理目标
// 纹理单元 0/1/2，后续单元为基址加偏移
#define GL_TEXTURE0              0x84C0      // 纹理单元 0
#define GL_TEXTURE1              0x84C1      // 纹理单元 1
#define GL_TEXTURE2              0x84C2      // 纹理单元 2
#define GL_RGBA                  0x1908      // 四通道
#define GL_RGBA8                 0x8058      // 8 位四通道内部格式
#define GL_DEPTH_COMPONENT       0x1902      // 深度格式
#define GL_DEPTH_COMPONENT24     0x81A6      // 24 位深度内部格式
#define GL_TEXTURE_MIN_FILTER    0x2801      // 缩小过滤参数
#define GL_TEXTURE_MAG_FILTER    0x2800      // 放大过滤参数
#define GL_TEXTURE_WRAP_S        0x2802      // S 方向寻址参数
#define GL_TEXTURE_WRAP_T        0x2803      // T 方向寻址参数
#define GL_TEXTURE_WRAP_R        0x8072      // R 方向寻址参数
#define GL_TEXTURE_MAX_ANISOTROPY_EXT      0x84FE  // 各向异性等级（扩展）
#define GL_MAX_TEXTURE_MAX_ANISOTROPY_EXT  0x84FF  // 各向异性上限（扩展）
#define GL_NEAREST               0x2600      // 最近点采样
#define GL_LINEAR                0x2601      // 线性采样
#define GL_NEAREST_MIPMAP_NEAREST 0x2700     // mip 内最近 + mip 间最近
#define GL_LINEAR_MIPMAP_NEAREST 0x2701      // mip 内线性 + mip 间最近
#define GL_NEAREST_MIPMAP_LINEAR 0x2702      // mip 内最近 + mip 间线性
#define GL_LINEAR_MIPMAP_LINEAR  0x2703      // 三线性过滤
#define GL_CLAMP_TO_EDGE         0x812F      // 夹取边缘
#define GL_REPEAT                0x2901      // 平铺重复
#define GL_VERTEX_SHADER         0x8B31      // 顶点着色器阶段
#define GL_FRAGMENT_SHADER       0x8B30      // 片元着色器阶段
#define GL_COMPUTE_SHADER        0x91B9      // 计算着色器阶段
#define GL_SHADER_STORAGE_BARRIER_BIT 0x00002000  // SSBO 写入屏障
#define GL_ALL_BARRIER_BITS      0xFFFFFFFF  // 所有屏障
#define GL_MAX_COMPUTE_WORK_GROUP_COUNT 0x91BE    // 每组数量上限查询
#define GL_MAX_COMPUTE_WORK_GROUP_SIZE  0x91BF    // 组尺寸上限查询
#define GL_COMPILE_STATUS        0x8B81      // 编译状态查询
#define GL_LINK_STATUS           0x8B82      // 链接状态查询
#define GL_INFO_LOG_LENGTH       0x8B84      // 日志长度查询
#define GL_FRAMEBUFFER           0x8D40      // 帧缓冲绑定目标
#define GL_FRAMEBUFFER_COMPLETE  0x8CD5      // 帧缓冲完整
#define GL_DEPTH_ATTACHMENT      0x8D00      // 深度附件点
#define GL_RENDERBUFFER          0x8D41      // 渲染缓冲绑定目标
#define GL_COLOR_ATTACHMENT0     0x8CE0      // 颜色附件 0
// 立方体贴图及其六个面
#define GL_TEXTURE_CUBE_MAP            0x8513  // 立方体贴图目标
#define GL_TEXTURE_CUBE_MAP_POSITIVE_X 0x8515  // +X 面
#define GL_TEXTURE_CUBE_MAP_NEGATIVE_X 0x8516  // -X 面
#define GL_TEXTURE_CUBE_MAP_POSITIVE_Y 0x8517  // +Y 面
#define GL_TEXTURE_CUBE_MAP_NEGATIVE_Y 0x8518  // -Y 面
#define GL_TEXTURE_CUBE_MAP_POSITIVE_Z 0x8519  // +Z 面
#define GL_TEXTURE_CUBE_MAP_NEGATIVE_Z 0x851A  // -Z 面
#define GL_RGBA32F               0x8814      // 32 位四通道浮点内部格式
#define GL_STREAM_DRAW           0x88E0      // 每帧重传
#define GL_STREAM_READ           0x88E5      // 每帧回读
#define GL_READ_ONLY             0x88B8      // 映射缓冲只读
#define GL_VERTEX_ATTRIB_ARRAY_BARRIER_BIT 0x00000001  // 顶点属性数组屏障
#define GL_UNSIGNED_SHORT        0x1403      // 16 位无符号整数

// 用宏批量声明“函数指针类型 + 同名成员”，避免上百行重复代码：
// GL_DECL(ret, glFoo, (参数)) 展开为 typedef ret (GLAPIENTRY *glFooFn)(参数);
// 第二次定义同名宏后改为成员声明 glFooFn glFoo;，即结构体里的函数指针字段。
#define GL_DECL(ret, name, params) typedef ret (GLAPIENTRY *name##Fn) params;

// 清屏、视口与状态开关
GL_DECL(void, glClear, (GLbitfield mask))
GL_DECL(void, glClearColor, (GLfloat r, GLfloat g, GLfloat b, GLfloat a))
GL_DECL(void, glClearDepth, (GLdouble depth))
GL_DECL(void, glViewport, (GLint x, GLint y, GLsizei w, GLsizei h))
GL_DECL(void, glEnable, (GLenum cap))
GL_DECL(void, glDisable, (GLenum cap))
GL_DECL(void, glDepthFunc, (GLenum func))
GL_DECL(void, glDepthMask, (GLboolean flag))
GL_DECL(void, glStencilFunc, (GLenum func, GLint ref, GLuint mask))
GL_DECL(void, glStencilOp, (GLenum sfail, GLenum dpfail, GLenum dppass))
GL_DECL(void, glStencilMask, (GLuint mask))
GL_DECL(void, glClearStencil, (GLint s))
GL_DECL(void, glColorMask, (GLboolean r, GLboolean g, GLboolean b, GLboolean a))
GL_DECL(void, glCullFace, (GLenum mode))
GL_DECL(void, glFrontFace, (GLenum mode))
GL_DECL(void, glScissor, (GLint x, GLint y, GLsizei w, GLsizei h))
GL_DECL(void, glPolygonMode, (GLenum face, GLenum mode))
GL_DECL(void, glPolygonOffset, (GLfloat factor, GLfloat units))
// 绘制/读取目标选择
GL_DECL(void, glDrawBuffer, (GLenum buf))
GL_DECL(void, glDrawBuffers, (GLsizei n, const GLenum* bufs))
GL_DECL(void, glReadBuffer, (GLenum buf))
GL_DECL(void, glReadPixels, (GLint x, GLint y, GLsizei w, GLsizei h, GLenum format, GLenum type, void* data))
GL_DECL(void, glBlitFramebuffer, (GLint srcX0, GLint srcY0, GLint srcX1, GLint srcY1, GLint dstX0, GLint dstY0, GLint dstX1, GLint dstY1, GLbitfield mask, GLenum filter))
GL_DECL(void, glCopyTexSubImage2D, (GLenum target, GLint level, GLint xoffset, GLint yoffset, GLint x, GLint y, GLsizei w, GLsizei h))
GL_DECL(void*, glMapBuffer, (GLenum target, GLenum access))
GL_DECL(GLboolean, glUnmapBuffer, (GLenum target))
// 纹理
GL_DECL(void, glGenTextures, (GLsizei n, GLuint* tex))
GL_DECL(void, glBindTexture, (GLenum target, GLuint tex))
GL_DECL(void, glDeleteTextures, (GLsizei n, const GLuint* tex))
GL_DECL(void, glTexImage2D, (GLenum target, GLint level, GLint internalFormat, GLsizei w, GLsizei h, GLint border, GLenum format, GLenum type, const void* data))
GL_DECL(void, glTexImage3D, (GLenum target, GLint level, GLint internalFormat, GLsizei w, GLsizei h, GLsizei d, GLint border, GLenum format, GLenum type, const void* data))
GL_DECL(void, glTexSubImage3D, (GLenum target, GLint level, GLint xoffset, GLint yoffset, GLint zoffset, GLsizei w, GLsizei h, GLsizei d, GLenum format, GLenum type, const void* data))
GL_DECL(void, glTexSubImage2D, (GLenum target, GLint level, GLint xoffset, GLint yoffset, GLsizei w, GLsizei h, GLenum format, GLenum type, const void* data))
GL_DECL(void, glGenerateMipmap, (GLenum target))
GL_DECL(void, glTexParameteri, (GLenum target, GLenum pname, GLint param))
GL_DECL(void, glTexParameterf, (GLenum target, GLenum pname, GLfloat param))
// 查询
GL_DECL(const GLubyte*, glGetString, (GLenum name))
GL_DECL(void, glGetIntegerv, (GLenum pname, GLint* data))
GL_DECL(GLenum, glGetError, (void))
GL_DECL(void, glActiveTexture, (GLenum texture))
// 顶点数组与缓冲
GL_DECL(void, glGenVertexArrays, (GLsizei n, GLuint* arrays))
GL_DECL(void, glBindVertexArray, (GLuint array))
GL_DECL(void, glDeleteVertexArrays, (GLsizei n, const GLuint* arrays))
GL_DECL(void, glGenBuffers, (GLsizei n, GLuint* buffers))
GL_DECL(void, glBindBuffer, (GLenum target, GLuint buffer))
GL_DECL(void, glBufferData, (GLenum target, GLsizeiptr size, const void* data, GLenum usage))
GL_DECL(void, glBufferSubData, (GLenum target, GLintptr offset, GLsizeiptr size, const void* data))
GL_DECL(void, glBindBufferBase, (GLenum target, GLuint index, GLuint buffer))
GL_DECL(GLuint, glGetUniformBlockIndex, (GLuint program, const GLchar* uniformBlockName))
GL_DECL(void, glUniformBlockBinding, (GLuint program, GLuint uniformBlockIndex, GLuint uniformBlockBinding))
GL_DECL(void, glGetActiveUniformBlockiv, (GLuint program, GLuint uniformBlockIndex, GLenum pname, GLint* params))
GL_DECL(void, glGetUniformIndices, (GLuint program, GLsizei uniformCount, const GLchar* const* uniformNames, GLuint* uniformIndices))
GL_DECL(void, glGetActiveUniformsiv, (GLuint program, GLsizei uniformCount, const GLuint* uniformIndices, GLenum pname, GLint* params))
GL_DECL(void, glGetActiveUniformName, (GLuint program, GLuint uniformIndex, GLsizei bufSize, GLsizei* length, GLchar* uniformName))
GL_DECL(void, glDispatchCompute, (GLuint x, GLuint y, GLuint z))
GL_DECL(void, glMemoryBarrier, (GLbitfield barriers))
GL_DECL(void, glDeleteBuffers, (GLsizei n, const GLuint* buffers))
GL_DECL(void, glVertexAttribPointer, (GLuint index, GLint size, GLenum type, GLboolean normalized, GLsizei stride, const void* pointer))
GL_DECL(void, glEnableVertexAttribArray, (GLuint index))
// 着色器
GL_DECL(GLuint, glCreateShader, (GLenum type))
GL_DECL(void, glShaderSource, (GLuint shader, GLsizei count, const GLchar* const* string, const GLint* length))
GL_DECL(void, glCompileShader, (GLuint shader))
GL_DECL(void, glGetShaderiv, (GLuint shader, GLenum pname, GLint* params))
GL_DECL(void, glGetShaderInfoLog, (GLuint shader, GLsizei bufSize, GLsizei* length, GLchar* infoLog))
GL_DECL(void, glDeleteShader, (GLuint shader))
GL_DECL(GLuint, glCreateProgram, (void))
GL_DECL(void, glAttachShader, (GLuint program, GLuint shader))
GL_DECL(void, glLinkProgram, (GLuint program))
GL_DECL(void, glGetProgramiv, (GLuint program, GLenum pname, GLint* params))
GL_DECL(void, glGetProgramInfoLog, (GLuint program, GLsizei bufSize, GLsizei* length, GLchar* infoLog))
GL_DECL(void, glDeleteProgram, (GLuint program))
GL_DECL(void, glUseProgram, (GLuint program))
GL_DECL(GLint, glGetUniformLocation, (GLuint program, const GLchar* name))
GL_DECL(void, glUniform1i, (GLint location, GLint v0))
GL_DECL(void, glUniform1f, (GLint location, GLfloat v0))
GL_DECL(void, glUniform2f, (GLint location, GLfloat v0, GLfloat v1))
GL_DECL(void, glUniform3f, (GLint location, GLfloat v0, GLfloat v1, GLfloat v2))
GL_DECL(void, glUniform4f, (GLint location, GLfloat v0, GLfloat v1, GLfloat v2, GLfloat v3))
GL_DECL(void, glUniformMatrix4fv, (GLint location, GLsizei count, GLboolean transpose, const GLfloat* value))
// 绘制
GL_DECL(void, glDrawArrays, (GLenum mode, GLint first, GLsizei count))
GL_DECL(void, glDrawElements, (GLenum mode, GLsizei count, GLenum type, const void* indices))
GL_DECL(void, glDrawElementsInstanced, (GLenum mode, GLsizei count, GLenum type, const void* indices, GLsizei instanceCount))
GL_DECL(void, glVertexAttribDivisor, (GLuint index, GLuint divisor))
// 帧缓冲与渲染缓冲
GL_DECL(void, glGenFramebuffers, (GLsizei n, GLuint* framebuffers))
GL_DECL(void, glBindFramebuffer, (GLenum target, GLuint framebuffer))
GL_DECL(void, glDeleteFramebuffers, (GLsizei n, const GLuint* framebuffers))
GL_DECL(void, glFramebufferTexture2D, (GLenum target, GLenum attachment, GLenum textarget, GLuint texture, GLint level))
GL_DECL(void, glFramebufferTextureLayer, (GLenum target, GLenum attachment, GLuint texture, GLint level, GLint layer))
GL_DECL(GLenum, glCheckFramebufferStatus, (GLenum target))
GL_DECL(void, glBlendFunc, (GLenum sfactor, GLenum dfactor))
GL_DECL(void, glGenRenderbuffers, (GLsizei n, GLuint* renderbuffers))
GL_DECL(void, glBindRenderbuffer, (GLenum target, GLuint renderbuffer))
GL_DECL(void, glDeleteRenderbuffers, (GLsizei n, const GLuint* renderbuffers))
GL_DECL(void, glRenderbufferStorage, (GLenum target, GLenum internalformat, GLsizei width, GLsizei height))
GL_DECL(void, glFramebufferRenderbuffer, (GLenum target, GLenum attachment, GLenum rendertarget, GLuint renderbuffer))

#undef GL_DECL

// 重新定义宏，这次把条目展开成结构体成员：每项是一个已加载的函数指针。
#define GL_DECL(ret, name, params) name##Fn name;

// GL 函数地址集合。所有调用都写成 gl.glXxx()；
// 如果某个函数在当前驱动上不存在，对应成员为空指针，可据此做特性探测。
struct GL {
    GL_DECL(void, glClear, (GLbitfield mask))
    GL_DECL(void, glClearColor, (GLfloat r, GLfloat g, GLfloat b, GLfloat a))
    GL_DECL(void, glClearDepth, (GLdouble depth))
    GL_DECL(void, glViewport, (GLint x, GLint y, GLsizei w, GLsizei h))
    GL_DECL(void, glEnable, (GLenum cap))
    GL_DECL(void, glDisable, (GLenum cap))
    GL_DECL(void, glDepthFunc, (GLenum func))
    GL_DECL(void, glDepthMask, (GLboolean flag))
    GL_DECL(void, glStencilFunc, (GLenum func, GLint ref, GLuint mask))
    GL_DECL(void, glStencilOp, (GLenum sfail, GLenum dpfail, GLenum dppass))
    GL_DECL(void, glStencilMask, (GLuint mask))
    GL_DECL(void, glClearStencil, (GLint s))
    GL_DECL(void, glColorMask, (GLboolean r, GLboolean g, GLboolean b, GLboolean a))
    GL_DECL(void, glBlendFunc, (GLenum sfactor, GLenum dfactor))
    GL_DECL(void, glCullFace, (GLenum mode))
    GL_DECL(void, glFrontFace, (GLenum mode))
    GL_DECL(void, glScissor, (GLint x, GLint y, GLsizei w, GLsizei h))
    GL_DECL(void, glPolygonMode, (GLenum face, GLenum mode))
    GL_DECL(void, glPolygonOffset, (GLfloat factor, GLfloat units))
    GL_DECL(void, glDrawBuffer, (GLenum buf))
    GL_DECL(void, glDrawBuffers, (GLsizei n, const GLenum* bufs))
    GL_DECL(void, glReadBuffer, (GLenum buf))
    GL_DECL(void, glReadPixels, (GLint x, GLint y, GLsizei w, GLsizei h, GLenum format, GLenum type, void* data))
    GL_DECL(void, glBlitFramebuffer, (GLint srcX0, GLint srcY0, GLint srcX1, GLint srcY1, GLint dstX0, GLint dstY0, GLint dstX1, GLint dstY1, GLbitfield mask, GLenum filter))
    GL_DECL(void, glCopyTexSubImage2D, (GLenum target, GLint level, GLint xoffset, GLint yoffset, GLint x, GLint y, GLsizei w, GLsizei h))
    GL_DECL(void*, glMapBuffer, (GLenum target, GLenum access))
GL_DECL(GLboolean, glUnmapBuffer, (GLenum target))
    GL_DECL(void, glGenTextures, (GLsizei n, GLuint* tex))
    GL_DECL(void, glBindTexture, (GLenum target, GLuint tex))
    GL_DECL(void, glDeleteTextures, (GLsizei n, const GLuint* tex))
    GL_DECL(void, glTexImage2D, (GLenum target, GLint level, GLint internalFormat, GLsizei w, GLsizei h, GLint border, GLenum format, GLenum type, const void* data))
GL_DECL(void, glTexImage3D, (GLenum target, GLint level, GLint internalFormat, GLsizei w, GLsizei h, GLsizei d, GLint border, GLenum format, GLenum type, const void* data))
GL_DECL(void, glTexSubImage3D, (GLenum target, GLint level, GLint xoffset, GLint yoffset, GLint zoffset, GLsizei w, GLsizei h, GLsizei d, GLenum format, GLenum type, const void* data))
    GL_DECL(void, glTexSubImage2D, (GLenum target, GLint level, GLint xoffset, GLint yoffset, GLsizei w, GLsizei h, GLenum format, GLenum type, const void* data))
    GL_DECL(void, glGenerateMipmap, (GLenum target))
    GL_DECL(void, glTexParameteri, (GLenum target, GLenum pname, GLint param))
    GL_DECL(void, glTexParameterf, (GLenum target, GLenum pname, GLfloat param))
    GL_DECL(const GLubyte*, glGetString, (GLenum name))
    GL_DECL(void, glGetIntegerv, (GLenum pname, GLint* data))
    GL_DECL(GLenum, glGetError, (void))
    GL_DECL(void, glActiveTexture, (GLenum texture))
    GL_DECL(void, glGenVertexArrays, (GLsizei n, GLuint* arrays))
    GL_DECL(void, glBindVertexArray, (GLuint array))
    GL_DECL(void, glDeleteVertexArrays, (GLsizei n, const GLuint* arrays))
    GL_DECL(void, glGenBuffers, (GLsizei n, GLuint* buffers))
    GL_DECL(void, glBindBuffer, (GLenum target, GLuint buffer))
    GL_DECL(void, glBufferData, (GLenum target, GLsizeiptr size, const void* data, GLenum usage))
    GL_DECL(void, glBufferSubData, (GLenum target, GLintptr offset, GLsizeiptr size, const void* data))
    GL_DECL(void, glBindBufferBase, (GLenum target, GLuint index, GLuint buffer))
    GL_DECL(GLuint, glGetUniformBlockIndex, (GLuint program, const GLchar* uniformBlockName))
    GL_DECL(void, glUniformBlockBinding, (GLuint program, GLuint uniformBlockIndex, GLuint uniformBlockBinding))
    GL_DECL(void, glGetActiveUniformBlockiv, (GLuint program, GLuint uniformBlockIndex, GLenum pname, GLint* params))
    GL_DECL(void, glGetUniformIndices, (GLuint program, GLsizei uniformCount, const GLchar* const* uniformNames, GLuint* uniformIndices))
    GL_DECL(void, glGetActiveUniformsiv, (GLuint program, GLsizei uniformCount, const GLuint* uniformIndices, GLenum pname, GLint* params))
    GL_DECL(void, glGetActiveUniformName, (GLuint program, GLuint uniformIndex, GLsizei bufSize, GLsizei* length, GLchar* uniformName))
    GL_DECL(void, glDispatchCompute, (GLuint x, GLuint y, GLuint z))
    GL_DECL(void, glMemoryBarrier, (GLbitfield barriers))
    GL_DECL(void, glDeleteBuffers, (GLsizei n, const GLuint* buffers))
    GL_DECL(void, glVertexAttribPointer, (GLuint index, GLint size, GLenum type, GLboolean normalized, GLsizei stride, const void* pointer))
    GL_DECL(void, glEnableVertexAttribArray, (GLuint index))
    GL_DECL(GLuint, glCreateShader, (GLenum type))
    GL_DECL(void, glShaderSource, (GLuint shader, GLsizei count, const GLchar* const* string, const GLint* length))
    GL_DECL(void, glCompileShader, (GLuint shader))
    GL_DECL(void, glGetShaderiv, (GLuint shader, GLenum pname, GLint* params))
    GL_DECL(void, glGetShaderInfoLog, (GLuint shader, GLsizei bufSize, GLsizei* length, GLchar* infoLog))
    GL_DECL(void, glDeleteShader, (GLuint shader))
    GL_DECL(GLuint, glCreateProgram, (void))
    GL_DECL(void, glAttachShader, (GLuint program, GLuint shader))
    GL_DECL(void, glLinkProgram, (GLuint program))
    GL_DECL(void, glGetProgramiv, (GLuint program, GLenum pname, GLint* params))
    GL_DECL(void, glGetProgramInfoLog, (GLuint program, GLsizei bufSize, GLsizei* length, GLchar* infoLog))
    GL_DECL(void, glDeleteProgram, (GLuint program))
    GL_DECL(void, glUseProgram, (GLuint program))
    GL_DECL(GLint, glGetUniformLocation, (GLuint program, const GLchar* name))
    GL_DECL(void, glUniform1i, (GLint location, GLint v0))
    GL_DECL(void, glUniform1f, (GLint location, GLfloat v0))
    GL_DECL(void, glUniform2f, (GLint location, GLfloat v0, GLfloat v1))
    GL_DECL(void, glUniform3f, (GLint location, GLfloat v0, GLfloat v1, GLfloat v2))
    GL_DECL(void, glUniform4f, (GLint location, GLfloat v0, GLfloat v1, GLfloat v2, GLfloat v3))
    GL_DECL(void, glUniformMatrix4fv, (GLint location, GLsizei count, GLboolean transpose, const GLfloat* value))
    GL_DECL(void, glDrawArrays, (GLenum mode, GLint first, GLsizei count))
    GL_DECL(void, glDrawElements, (GLenum mode, GLsizei count, GLenum type, const void* indices))
    GL_DECL(void, glDrawElementsInstanced, (GLenum mode, GLsizei count, GLenum type, const void* indices, GLsizei instanceCount))
    GL_DECL(void, glVertexAttribDivisor, (GLuint index, GLuint divisor))
    GL_DECL(void, glGenFramebuffers, (GLsizei n, GLuint* framebuffers))
    GL_DECL(void, glBindFramebuffer, (GLenum target, GLuint framebuffer))
    GL_DECL(void, glDeleteFramebuffers, (GLsizei n, const GLuint* framebuffers))
    GL_DECL(void, glFramebufferTexture2D, (GLenum target, GLenum attachment, GLenum textarget, GLuint texture, GLint level))
GL_DECL(void, glFramebufferTextureLayer, (GLenum target, GLenum attachment, GLuint texture, GLint level, GLint layer))
    GL_DECL(GLenum, glCheckFramebufferStatus, (GLenum target))
    GL_DECL(void, glGenRenderbuffers, (GLsizei n, GLuint* renderbuffers))
    GL_DECL(void, glBindRenderbuffer, (GLenum target, GLuint renderbuffer))
    GL_DECL(void, glDeleteRenderbuffers, (GLsizei n, const GLuint* renderbuffers))
    GL_DECL(void, glRenderbufferStorage, (GLenum target, GLenum internalformat, GLsizei width, GLsizei height))
    GL_DECL(void, glFramebufferRenderbuffer, (GLenum target, GLenum attachment, GLenum rendertarget, GLuint renderbuffer))
};

#undef GL_DECL

// 全局 GL 函数地址表，实现见 图形4.cpp。
extern GL gl;
// 创建窗口像素格式与 4.6 核心上下文，并加载全部 GL 函数地址；任一步失败返回 false。
bool glInit(HWND hwnd);
// 交换缓冲（SwapBuffers），把本帧结果显示到窗口。
void glPresent();
// 通过 wglSwapIntervalEXT 设置垂直同步；驱动不支持该扩展时静默忽略。
void glSetVsync(bool vsync);
// 记录当前绘制尺寸，供后续查询使用。
void glResize(int w, int h);
// 销毁上下文并释放 DC。
void glShutdown();

// 兼容上下文管理：核心 profile 下创建额外的 2.1 兼容上下文，用于固定管线绘制。
bool glLegacyCreate();
// 把兼容上下文设为当前上下文。
bool glLegacyBind();
// 把核心上下文重新设为当前上下文。
bool glCoreBind();
// 销毁兼容上下文。
bool glLegacyDestroy();
