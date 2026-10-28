#include "Gvtcier.h"
#include <cstdio>
#include <cstring>

#ifdef __ANDROID__
#include <dlfcn.h>
#endif

// OpenGL ES 后端的 RHI 实现。
// 与 GL 后端功能一一对应，差异集中在三处：
//   1. 不提供 glPolygonMode（GLES 只有填充光栅化），所以 RasterState::polygonMode 被忽略；
//   2. 清深度用 glClearDepthf（浮点版本），没有 glClearDepth 双精度接口；
//   3. 不支持二进制着色器与固定管线兼容上下文，也没有全局 UBO 合并机制，
//      所有 uniform 直接走 glUniform，因此这里没有 GvU 影子缓冲那一套逻辑。
namespace rhi {

GLES gles;

// 设备是否支持各向异性过滤扩展（GL_EXT_texture_filter_anisotropic）。
static bool g_glesAnisoExt = false;
// 累计绘制调用次数，供性能统计接口导出。
static unsigned long long g_glesDrawCalls = 0;

namespace {

#ifdef __ANDROID__
// Android 上优先从窗口库给的取址函数取，取不到再退回 dlsym 全局限定查找。
static void* glesSym(const char* name, void* (*proc)(const char*)) {
    void* p = proc(name);
    if (!p) p = dlsym(RTLD_DEFAULT, name);
    return p;
}
#else
static void* glesSym(const char* name, void* (*proc)(const char*)) { return proc(name); }
#endif

// 批量取全部 GLES 函数地址，存入 gles 结构体。
// 取不到的项保留空指针，由调用处判空或能力探测函数处理。
void glesLoad(void* (*proc)(const char*)) {
#define GLOAD(name) gles.name = (name##Fn)glesSym(#name, proc);

    GLOAD(glClear);
    GLOAD(glClearColor);
    GLOAD(glClearDepthf);
    GLOAD(glViewport);
    GLOAD(glEnable);
    GLOAD(glDisable);
    GLOAD(glDepthFunc);
    GLOAD(glDepthMask);
    GLOAD(glStencilFunc);
    GLOAD(glStencilOp);
    GLOAD(glStencilMask);
    GLOAD(glClearStencil);
    GLOAD(glColorMask);
    GLOAD(glCullFace);
    GLOAD(glFrontFace);
    GLOAD(glScissor);
    GLOAD(glPolygonOffset);
    GLOAD(glDrawBuffers);
    GLOAD(glReadBuffer);
    GLOAD(glReadPixels);
    GLOAD(glCopyTexSubImage2D);
    GLOAD(glMapBufferRange);
    GLOAD(glUnmapBuffer);
    GLOAD(glGenTextures);
    GLOAD(glBindTexture);
    GLOAD(glDeleteTextures);
    GLOAD(glTexImage2D);
    GLOAD(glTexImage3D);
    GLOAD(glTexSubImage3D);
    GLOAD(glTexSubImage2D);
    GLOAD(glGenerateMipmap);
    GLOAD(glTexParameteri);
    GLOAD(glTexParameterf);
    GLOAD(glGetString);
    GLOAD(glGetIntegerv);
    GLOAD(glGetError);
    GLOAD(glActiveTexture);
    GLOAD(glGenVertexArrays);
    GLOAD(glBindVertexArray);
    GLOAD(glDeleteVertexArrays);
    GLOAD(glGenBuffers);
    GLOAD(glBindBuffer);
    GLOAD(glBufferData);
    GLOAD(glBufferSubData);
    GLOAD(glBindBufferBase);
    GLOAD(glGetUniformBlockIndex);
    GLOAD(glUniformBlockBinding);
    GLOAD(glGetActiveUniformBlockiv);
    GLOAD(glGetUniformIndices);
    GLOAD(glGetActiveUniformsiv);
    GLOAD(glGetActiveUniformName);
    GLOAD(glDispatchCompute);
    GLOAD(glMemoryBarrier);
    GLOAD(glDeleteBuffers);
    GLOAD(glVertexAttribPointer);
    GLOAD(glEnableVertexAttribArray);
    GLOAD(glCreateShader);
    GLOAD(glShaderSource);
    GLOAD(glCompileShader);
    GLOAD(glGetShaderiv);
    GLOAD(glGetShaderInfoLog);
    GLOAD(glDeleteShader);
    GLOAD(glCreateProgram);
    GLOAD(glAttachShader);
    GLOAD(glLinkProgram);
    GLOAD(glGetProgramiv);
    GLOAD(glGetProgramInfoLog);
    GLOAD(glDeleteProgram);
    GLOAD(glUseProgram);
    GLOAD(glGetUniformLocation);
    GLOAD(glUniform1i);
    GLOAD(glUniform1f);
    GLOAD(glUniform2f);
    GLOAD(glUniform3f);
    GLOAD(glUniform4f);
    GLOAD(glUniformMatrix4fv);
    GLOAD(glDrawArrays);
    GLOAD(glDrawElements);
    GLOAD(glDrawElementsInstanced);
    GLOAD(glVertexAttribDivisor);
    GLOAD(glGenFramebuffers);
    GLOAD(glBindFramebuffer);
    GLOAD(glDeleteFramebuffers);
    GLOAD(glFramebufferTexture2D);
    GLOAD(glFramebufferTextureLayer);
    GLOAD(glBlitFramebuffer);
    GLOAD(glCheckFramebufferStatus);
    GLOAD(glBlendFunc);
    GLOAD(glGenRenderbuffers);
    GLOAD(glBindRenderbuffer);
    GLOAD(glDeleteRenderbuffers);
    GLOAD(glRenderbufferStorage);
    GLOAD(glFramebufferRenderbuffer);

#undef GLOAD
#undef GLOAD_FALLBACK
}

GLenum toGlesCap(unsigned v) {
    switch (v) {
        case DepthTest: return 0x0B71;    // GL_DEPTH_TEST
        case CullFace: return 0x0B44;     // GL_CULL_FACE
        case ScissorTest: return 0x0C11;  // GL_SCISSOR_TEST
        case Blend: return 0x0BE2;        // GL_BLEND
        case StencilTest: return 0x0B90;  // GL_STENCIL_TEST
    }
    return 0;
}

// 面枚举 → GL_FRONT_AND_BACK / GL_FRONT / GL_BACK。
GLenum toGlesFace(unsigned v) {
    switch (v) {
        case FrontAndBack: return 0x0408;  // GL_FRONT_AND_BACK
        case Front: return 0x0404;         // GL_FRONT
        case Back: return 0x0405;          // GL_BACK
    }
    return 0x0408;
}

// 正面绕序 → GL_CCW / GL_CW。
GLenum toGlesWinding(unsigned v) {
    switch (v) {
        case Ccw: return 0x0901;  // GL_CCW
        case Cw: return 0x0900;   // GL_CW
    }
    return 0x0901;
}

// 深度比较函数 → GL 比较枚举，默认 GL_LEQUAL。
// 反向 Z 时上层传 Greater，配合深度清 0.0 使用。
GLenum toGlesDepthFunc(unsigned v) {
    switch (v) {
        case Never: return 0x0200;     // GL_NEVER
        case Less: return 0x0201;      // GL_LESS
        case EqualD: return 0x0202;    // GL_EQUAL
        case Lequal: return 0x0203;    // GL_LEQUAL
        case Greater: return 0x0204;   // GL_GREATER
        case Notequal: return 0x0205;  // GL_NOTEQUAL
        case Gequal: return 0x0206;    // GL_GEQUAL
        case Always: return 0x0207;    // GL_ALWAYS
    }
    return 0x0203;
}

// 混合因子 → GL 混合系数，默认 GL_ONE。
GLenum toGlesBlendFactor(unsigned v) {
    switch (v) {
        case Zero: return 0x0000;              // GL_ZERO
        case One: return 0x0001;               // GL_ONE
        case SrcColor: return 0x0300;          // GL_SRC_COLOR
        case OneMinusSrcColor: return 0x0301;  // GL_ONE_MINUS_SRC_COLOR
        case SrcAlpha: return 0x0302;          // GL_SRC_ALPHA
        case OneMinusSrcAlpha: return 0x0303;  // GL_ONE_MINUS_SRC_ALPHA
        case DstAlpha: return 0x0304;          // GL_DST_ALPHA
        case OneMinusDstAlpha: return 0x0305;  // GL_ONE_MINUS_DST_ALPHA
    }
    return 0x0001;
}

// 图元类型 → GL 绘图模式，默认三角形。
GLenum toGlesPrimitive(unsigned v) {
    switch (v) {
        case Points: return 0x0000;         // GL_POINTS
        case Lines: return 0x0001;          // GL_LINES
        case LineLoop: return 0x0002;       // GL_LINE_LOOP
        case LineStrip: return 0x0003;      // GL_LINE_STRIP
        case Triangles: return 0x0004;      // GL_TRIANGLES
        case TriangleStrip: return 0x0005;  // GL_TRIANGLE_STRIP
        case TriangleFan: return 0x0006;    // GL_TRIANGLE_FAN
    }
    return 0x0004;
}

// 缓冲目标 → GL 缓冲绑定目标。0x88EB 为 GL_PIXEL_PACK_BUFFER，0x8A11 为 GL_UNIFORM_BUFFER。
GLenum toGlesBufferTarget(unsigned v) {
    switch (v) {
        case ArrayBuffer: return 0x8892;           // GL_ARRAY_BUFFER
        case ElementArrayBuffer: return 0x8893;    // GL_ELEMENT_ARRAY_BUFFER
        case PixelPackBuffer: return 0x88EB;       // GL_PIXEL_PACK_BUFFER
        case ShaderStorageBuffer: return 0x90D2;   // GL_SHADER_STORAGE_BUFFER
        case UniformBuffer: return 0x8A11;         // GL_UNIFORM_BUFFER
    }
    return 0;
}

// uniform 块查询参数 → GL_UNIFORM_BLOCK_* 枚举。
GLenum toGlesBlockPname(unsigned v) {
    switch (v) {
        case UniformBlockBinding: return 0x8A3F;               // GL_UNIFORM_BLOCK_BINDING
        case UniformBlockDataSize: return 0x8A40;              // GL_UNIFORM_BLOCK_DATA_SIZE
        case UniformBlockNameLength: return 0x8A41;            // GL_UNIFORM_BLOCK_NAME_LENGTH
        case UniformBlockActiveUniforms: return 0x8A42;        // GL_UNIFORM_BLOCK_ACTIVE_UNIFORMS
        case UniformBlockActiveUniformIndices: return 0x8A43;  // GL_UNIFORM_BLOCK_ACTIVE_UNIFORM_INDICES
    }
    return 0;
}

// uniform 成员查询参数 → GL_UNIFORM_* 枚举。
GLenum toGlesUniformPname(unsigned v) {
    switch (v) {
        case UniformType: return 0x8A37;         // GL_UNIFORM_TYPE
        case UniformOffset: return 0x8A3B;       // GL_UNIFORM_OFFSET
        case UniformArrayStride: return 0x8A3C;  // GL_UNIFORM_ARRAY_STRIDE
        case UniformMatrixStride: return 0x8A3D; // GL_UNIFORM_MATRIX_STRIDE
        case UniformArraySize: return 0x8A36;    // GL_UNIFORM_SIZE
    }
    return 0;
}

// 缓冲用途 → GL 用途枚举。
GLenum toGlesUsage(unsigned v) {
    switch (v) {
        case StaticDraw: return 0x88E4;   // GL_STATIC_DRAW
        case StreamRead: return 0x88E5;   // GL_STREAM_READ
        case DynamicDraw: return 0x88E8;  // GL_DYNAMIC_DRAW
        case StreamDraw: return 0x88E0;   // GL_STREAM_DRAW
    }
    return 0x88E4;
}

// 标量/索引数据类型 → GL 类型枚举。
// 注意 Double 在 GLES 里没有对应类型，这里映射为 GL_FLOAT（精度会下降）；
// 0x84FA 为 GL_UNSIGNED_INT_24_8。
GLenum toGlesScalarType(unsigned v) {
    switch (v) {
        case Byte: return 0x1400;            // GL_BYTE
        case UnsignedByte: return 0x1401;    // GL_UNSIGNED_BYTE
        case Short: return 0x1402;           // GL_SHORT
        case UnsignedShort: return 0x1403;   // GL_UNSIGNED_SHORT
        case Int: return 0x1404;             // GL_INT
        case UnsignedInt248: return 0x84FA;  // GL_UNSIGNED_INT_24_8
        case UnsignedInt: return 0x1405;     // GL_UNSIGNED_INT
        case Float: return 0x1406;           // GL_FLOAT
        case Double: return 0x1406;          // GLES 无 GL_DOUBLE，退化为 GL_FLOAT
        case HalfFloat: return 0x140B;       // GL_HALF_FLOAT
    }
    return 0x1406;
}

// 像素外部格式 → GL 通道格式。
GLenum toGlesPixelFormat(unsigned v) {
    switch (v) {
        case Red: return 0x1903;                 // GL_RED
        case Rg: return 0x8227;                  // GL_RG
        case Rgb: return 0x1907;                 // GL_RGB
        case Bgra: return 0x80E1;                // GL_BGRA_EXT
        case Rgba: return 0x1908;                // GL_RGBA
        case RedInteger: return 0x8D94;          // GL_RED_INTEGER
        case RgInteger: return 0x8228;           // GL_RG_INTEGER
        case RgbInteger: return 0x8D98;          // GL_RGB_INTEGER
        case RgbaInteger: return 0x8D99;         // GL_RGBA_INTEGER
        case DepthStencilFormat: return 0x84F9;  // GL_DEPTH_STENCIL
        case StencilIndex: return 0x1901;        // GL_STENCIL_INDEX
        case DepthComponent: return 0x1902;      // GL_DEPTH_COMPONENT
    }
    return 0x1908;
}

// 纹理内部格式 → GL 内部格式枚举，默认 GL_RGBA8。
GLenum toGlesInternalFormat(unsigned v) {
    switch (v) {
        case R8: return 0x8229;                  // GL_R8
        case R16: return 0x822A;                 // GL_R16
        case R16F: return 0x822D;                // GL_R16F
        case R32F: return 0x822E;                // GL_R32F
        case Rg8: return 0x822B;                 // GL_RG8
        case Rg16F: return 0x822F;               // GL_RG16F
        case Rg32F: return 0x8230;               // GL_RG32F
        case Rgb8: return 0x8051;                // GL_RGB8
        case Rgb16F: return 0x881B;              // GL_RGB16F
        case Rgba: return 0x1908;                // GL_RGBA
        case Rgba8: return 0x8058;               // GL_RGBA8
        case Rgba16: return 0x805B;              // GL_RGBA16
        case Rgba16F: return 0x881A;             // GL_RGBA16F
        case Rgba32F: return 0x8814;             // GL_RGBA32F
        case Srgb8Alpha8: return 0x8C43;         // GL_SRGB8_ALPHA8
        case DepthComponent: return 0x1902;      // GL_DEPTH_COMPONENT
        case DepthComponent16: return 0x81A5;    // GL_DEPTH_COMPONENT16
        case DepthComponent24: return 0x81A6;    // GL_DEPTH_COMPONENT24
        case DepthComponent32F: return 0x8CAC;   // GL_DEPTH_COMPONENT32F
        case Depth24Stencil8: return 0x88F0;     // GL_DEPTH24_STENCIL8
        case R11G11B10F: return 0x8C3A;          // GL_R11F_G11F_B10F
    }
    return 0x8058;
}

// 纹理目标 → GL_TEXTURE_* 枚举。
GLenum toGlesTexTarget(unsigned v) {
    switch (v) {
        case Tex2D: return 0x0DE1;          // GL_TEXTURE_2D
        case Tex2DArray: return 0x8C1A;     // GL_TEXTURE_2D_ARRAY
        case Tex3D: return 0x806F;          // GL_TEXTURE_3D
        case TexCube: return 0x8513;        // GL_TEXTURE_CUBE_MAP
        case TexCubePosX: return 0x8515;    // GL_TEXTURE_CUBE_MAP_POSITIVE_X
        case TexCubeNegX: return 0x8516;    // GL_TEXTURE_CUBE_MAP_NEGATIVE_X
        case TexCubePosY: return 0x8517;    // GL_TEXTURE_CUBE_MAP_POSITIVE_Y
        case TexCubeNegY: return 0x8518;    // GL_TEXTURE_CUBE_MAP_NEGATIVE_Y
        case TexCubePosZ: return 0x8519;    // GL_TEXTURE_CUBE_MAP_POSITIVE_Z
        case TexCubeNegZ: return 0x851A;    // GL_TEXTURE_CUBE_MAP_NEGATIVE_Z
    }
    return 0x0DE1;
}

// 纹理参数名 → GL_TEXTURE_* 参数枚举。
GLenum toGlesTexParam(unsigned v) {
    switch (v) {
        case TexMinFilter: return 0x2801;  // GL_TEXTURE_MIN_FILTER
        case TexMagFilter: return 0x2800;  // GL_TEXTURE_MAG_FILTER
        case TexWrapS: return 0x2802;      // GL_TEXTURE_WRAP_S
        case TexWrapT: return 0x2803;      // GL_TEXTURE_WRAP_T
        case TexWrapR: return 0x8072;      // GL_TEXTURE_WRAP_R
        case TexMaxAniso: return 0x84FE;   // GL_TEXTURE_MAX_ANISOTROPY_EXT
        case MaxTexAniso: return 0x84FF;   // GL_MAX_TEXTURE_MAX_ANISOTROPY_EXT
    }
    return 0;
}

// 过滤/寻址取值 → GL 取值，默认 GL_LINEAR。
// GLES 没有 GL_CLAMP_TO_BORDER，越界夹取统一用 GL_CLAMP_TO_EDGE 代替；
// 返回 0 表示不属于这两类取值，调用处会回退使用原值。
GLenum toGlesTexValue(unsigned v) {
    switch (v) {
        case Nearest: return 0x2600;               // GL_NEAREST
        case Linear: return 0x2601;                // GL_LINEAR
        case NearestMipmapNearest: return 0x2700;  // GL_NEAREST_MIPMAP_NEAREST
        case LinearMipmapNearest: return 0x2701;   // GL_LINEAR_MIPMAP_NEAREST
        case NearestMipmapLinear: return 0x2702;   // GL_NEAREST_MIPMAP_LINEAR
        case LinearMipmapLinear: return 0x2703;    // GL_LINEAR_MIPMAP_LINEAR
        case ClampToEdge: return 0x812F;           // GL_CLAMP_TO_EDGE
        case ClampToBorder: return 0x812F;          // GLES 无边框夹取，等价为 GL_CLAMP_TO_EDGE
        case MirroredRepeat: return 0x8370;        // GL_MIRRORED_REPEAT
        case Repeat: return 0x2901;                // GL_REPEAT
    }
    return 0x2601;
}

// 着色器阶段 → GL 阶段枚举。GLES 没有几何着色器，返回 0 使 glCreateShader 报错。
GLenum toGlesShaderStage(unsigned v) {
    switch (v) {
        case VertexShader: return 0x8B31;    // GL_VERTEX_SHADER
        case FragmentShader: return 0x8B30;  // GL_FRAGMENT_SHADER
        case GeometryShader: return 0;
        case ComputeShader: return 0x91B9;   // GL_COMPUTE_SHADER（GLES 3.1 起）
    }
    return 0;
}

// 着色器/程序查询参数 → GL 状态枚举。
GLenum toGlesShaderPname(unsigned v) {
    switch (v) {
        case CompileStatus: return 0x8B81;  // GL_COMPILE_STATUS
        case LinkStatus: return 0x8B82;     // GL_LINK_STATUS
        case InfoLogLength: return 0x8B84;  // GL_INFO_LOG_LENGTH
    }
    return 0;
}

// 帧缓冲类目标 → GL_FRAMEBUFFER / GL_RENDERBUFFER。
GLenum toGlesFboTarget(unsigned v) {
    switch (v) {
        case Framebuffer: return 0x8D40;   // GL_FRAMEBUFFER
        case Renderbuffer: return 0x8D41;  // GL_RENDERBUFFER
    }
    return 0x8D40;
}

// 附件点 → GL 附件枚举，0x8D20 为 GL_STENCIL_ATTACHMENT，0x84F9 为 GL_DEPTH_STENCIL_ATTACHMENT。
GLenum toGlesAttachment(unsigned v) {
    switch (v) {
        case DepthAttachment: return 0x8D00;          // GL_DEPTH_ATTACHMENT
        case StencilAttachment: return 0x8D20;        // GL_STENCIL_ATTACHMENT
        case DepthStencilAttachment: return 0x84F9;   // GL_DEPTH_STENCIL_ATTACHMENT
        case ColorAttachment0: return 0x8CE0;         // GL_COLOR_ATTACHMENT0
        case ColorAttachment1: return 0x8CE1;         // GL_COLOR_ATTACHMENT1
        case ColorAttachment2: return 0x8CE2;         // GL_COLOR_ATTACHMENT2
        case ColorAttachment3: return 0x8CE3;         // GL_COLOR_ATTACHMENT3
        case ColorAttachment4: return 0x8CE4;         // GL_COLOR_ATTACHMENT4
        case ColorAttachment5: return 0x8CE5;         // GL_COLOR_ATTACHMENT5
        case ColorAttachment6: return 0x8CE6;         // GL_COLOR_ATTACHMENT6
        case ColorAttachment7: return 0x8CE7;         // GL_COLOR_ATTACHMENT7
    }
    return 0x8CE0;
}

// 内存屏障位 → GL 屏障位，未列出的值原样返回。
GLenum toGlesBarrier(unsigned v) {
    switch (v) {
        case ShaderStorageBarrierBit: return 0x00002000;  // GL_SHADER_STORAGE_BARRIER_BIT
        case AllBarrierBits: return 0xFFFFFFFF;           // GL_ALL_BARRIER_BITS
    }
    return v;
}

// 驱动信息查询名 → GL 查询枚举。
GLenum toGlesParamName(unsigned v) {
    switch (v) {
        case Version: return 0x1F02;                  // GL_VERSION
        case RendererName: return 0x1F01;             // GL_RENDERER
        case VendorName: return 0x1F00;               // GL_VENDOR
        case ShadingLanguageVersion: return 0x8B8C;   // GL_SHADING_LANGUAGE_VERSION
        case MajorVersion: return 0x821B;             // GL_MAJOR_VERSION
        case MinorVersion: return 0x821C;             // GL_MINOR_VERSION
        case MaxTextureSize: return 0x0D33;           // GL_MAX_TEXTURE_SIZE
        case MaxSamples: return 0x8D57;               // GL_MAX_SAMPLES
    }
    return 0;
}

// 当前 GLES 绘制表面（由 glesTable 记录，Present/Shutdown 需要它）。
GvWWindow* g_glesWin = nullptr;

const char* t_Gvtcier_Rhi_GetString(unsigned name) { return (const char*)gles.glGetString(toGlesParamName(name)); }
// 查询整型值；MaxTexAniso 是特例：设备没有各向异性扩展时直接返回 1，
// 避免查询未定义的枚举而触发 GL_INVALID_ENUM。
void t_Gvtcier_Rhi_GetIntegerv(unsigned pname, int* data) {
    if (!data) return;
    if (pname == MaxTexAniso) {
        if (!g_glesAnisoExt) { *data = 1; return; }
        gles.glGetIntegerv(0x84FF, (GLint*)data);
        return;
    }
    gles.glGetIntegerv(toGlesParamName(pname), (GLint*)data);
}
unsigned t_Gvtcier_Rhi_GetError() { return (unsigned)gles.glGetError(); }

void t_Gvtcier_Rhi_ClearColor(float r, float g, float b, float a) { gles.glClearColor(r, g, b, a); }
// GLES 只有 glClearDepthf（float 参数），双精度值在此被截断。
// 反向 Z 下调用方传 0.0，与 GL 后端语义一致。
void t_Gvtcier_Rhi_ClearDepth(double d) { gles.glClearDepthf((GLfloat)d); }
// RHI 清屏位 → GLES 位域：0x4000 颜色、0x100 深度、0x400 模板。
void t_Gvtcier_Rhi_Clear(unsigned mask) {
    unsigned m = 0;
    if (mask & ColorBit) m |= 0x00004000;
    if (mask & DepthBit) m |= 0x00000100;
    if (mask & StencilBit) m |= 0x00000400;
    gles.glClear((GLbitfield)m);
}
void t_Gvtcier_Rhi_Viewport(int x, int y, int w, int h) { gles.glViewport(x, y, w, h); }
// 把 RasterState 套到 GLES 状态机。
// 与 GL 后端的差别：没有 glPolygonMode，s->polygonMode 被忽略（GLES 只支持填充）。
// 同样没有状态缓存，每次调用都会全量下发。
void t_Gvtcier_Rhi_SetRasterState(const RasterState* s) {
    if (!s) return;
    if (s->depthTest) gles.glEnable(toGlesCap(DepthTest));
    else gles.glDisable(toGlesCap(DepthTest));
    gles.glDepthMask(s->depthWrite ? GL_TRUE : GL_FALSE);
    gles.glDepthFunc(toGlesDepthFunc(s->depthFunc));
    if (s->cull) {
        gles.glEnable(toGlesCap(CullFace));
        gles.glCullFace(toGlesFace(s->cullMode));
    } else {
        gles.glDisable(toGlesCap(CullFace));
    }
    gles.glFrontFace(toGlesWinding(s->frontFace));
    if (s->blend) {
        gles.glEnable(toGlesCap(Blend));
        gles.glBlendFunc(toGlesBlendFactor(s->blendSrc), toGlesBlendFactor(s->blendDst));
    } else {
        gles.glDisable(toGlesCap(Blend));
    }
    if (s->scissor) {
        gles.glEnable(toGlesCap(ScissorTest));
        gles.glScissor(s->scissorX, s->scissorY, s->scissorW, s->scissorH);
    } else {
        gles.glDisable(toGlesCap(ScissorTest));
    }
    // 多边形偏移：GLES 只有 GL_POLYGON_OFFSET_FILL 这一种模式。
    if (s->polyFactor == 0.0f && s->polyUnit == 0.0f) {
        gles.glDisable(GLES_GL_POLYGON_OFFSET_FILL);
    } else {
        gles.glEnable(GLES_GL_POLYGON_OFFSET_FILL);
        gles.glPolygonOffset(s->polyFactor, s->polyUnit);
    }
    gles.glColorMask(s->colorWrite[0] ? GL_TRUE : GL_FALSE, s->colorWrite[1] ? GL_TRUE : GL_FALSE,
                     s->colorWrite[2] ? GL_TRUE : GL_FALSE, s->colorWrite[3] ? GL_TRUE : GL_FALSE);
}
// GLES 3.x 没有 glDrawBuffer（单缓冲接口），单个目标也用 glDrawBuffers 数组形式实现。
void t_Gvtcier_Rhi_DrawBuffer(unsigned buf) {
    GLenum tmp[1] = { toGlesAttachment(buf) };
    gles.glDrawBuffers(1, tmp);
}
// 多绘制目标，最多 8 个颜色附件。
void t_Gvtcier_Rhi_DrawBuffers(int n, const unsigned* bufs) {
    GLenum tmp[8];
    if (n > 8) n = 8;
    for (int i = 0; i < n; ++i) tmp[i] = toGlesAttachment(bufs[i]);
    gles.glDrawBuffers((GLsizei)n, tmp);
}
// 指定读取缓冲；glReadBuffer 属于可选扩展入口，缺失时静默跳过。
void t_Gvtcier_Rhi_ReadBuffer(unsigned buf) {
    if (gles.glReadBuffer) gles.glReadBuffer(toGlesAttachment(buf));
}
// GLES 下该功能整段为空实现：参数被丢弃，纹理不会被更新。
void t_Gvtcier_Rhi_ReadBufferTex(unsigned format, unsigned type, unsigned tex) {
    (void)format;
    (void)type;
    (void)tex;
}
void t_Gvtcier_Rhi_ReadPixels(int x, int y, int w, int h, unsigned format, unsigned type, void* data) {
    gles.glReadPixels(x, y, w, h, toGlesPixelFormat(format), toGlesScalarType(type), data);
}

void t_Gvtcier_Rhi_GenTextures(int n, Texture* out) { gles.glGenTextures(n, (GLuint*)out); }
void t_Gvtcier_Rhi_BindTexture(unsigned target, Texture t) { gles.glBindTexture(toGlesTexTarget(target), (GLuint)t); }
void t_Gvtcier_Rhi_DeleteTextures(int n, const Texture* t) { gles.glDeleteTextures(n, (const GLuint*)t); }
void t_Gvtcier_Rhi_TexImage2D(unsigned target, int level, int internalFormat, int w, int h,
                  unsigned format, unsigned type, const void* data) {
    gles.glTexImage2D(toGlesTexTarget(target), level, (int)toGlesInternalFormat((unsigned)internalFormat), w, h, 0,
                      toGlesPixelFormat(format), toGlesScalarType(type), data);
}
void t_Gvtcier_Rhi_TexSubImage2D(unsigned target, int level, int x, int y, int w, int h,
                     unsigned format, unsigned type, const void* data) {
    gles.glTexSubImage2D(toGlesTexTarget(target), level, x, y, w, h, toGlesPixelFormat(format), toGlesScalarType(type), data);
}
void t_Gvtcier_Rhi_TexImage3D(unsigned target, int level, int internalFormat, int w, int h, int d,
                  unsigned format, unsigned type, const void* data) {
    gles.glTexImage3D(toGlesTexTarget(target), level, (int)toGlesInternalFormat((unsigned)internalFormat), w, h, d, 0,
                      toGlesPixelFormat(format), toGlesScalarType(type), data);
}
void t_Gvtcier_Rhi_TexSubImage3D(unsigned target, int level, int x, int y, int z, int w, int h, int dd,
                     unsigned format, unsigned type, const void* data) {
    gles.glTexSubImage3D(toGlesTexTarget(target), level, x, y, z, w, h, dd, toGlesPixelFormat(format), toGlesScalarType(type), data);
}
void t_Gvtcier_Rhi_GenerateMipmap(unsigned target) { gles.glGenerateMipmap(toGlesTexTarget(target)); }
// 纹理整数参数：value 先尝试按过滤/寻址枚举翻译，命中则用翻译值，否则原样下发。
void t_Gvtcier_Rhi_TexParameteri(unsigned target, unsigned pname, int value) {
    GLenum mapped = toGlesTexValue((unsigned)value);
    gles.glTexParameteri(toGlesTexTarget(target), toGlesTexParam(pname), mapped ? (int)mapped : value);
}
void t_Gvtcier_Rhi_TexParameterf(unsigned target, unsigned pname, float value) {
    gles.glTexParameterf(toGlesTexTarget(target), toGlesTexParam(pname), value);
}
// GLES 各向异性默认等级为 1（关闭），因为多数移动 GPU 需要显式扩展支持。
static int g_glesAnisoLevel = 1;
void t_Gvtcier_Rhi_SetAnisotropy(int level) {
    if (level < 1) level = 1;
    if (level > 16) level = 16;
    g_glesAnisoLevel = level;
}
int t_Gvtcier_Rhi_Anisotropy() { return g_glesAnisoLevel; }
// 激活纹理单元（GL_TEXTURE0 = 0x84C0 加偏移）并绑定纹理。
void t_Gvtcier_Rhi_BindTextureUnit(int unit, unsigned target, Texture t) {
    gles.glActiveTexture((GLenum)(0x84C0 + unit));
    gles.glBindTexture(toGlesTexTarget(target), (GLuint)t);
}

void t_Gvtcier_Rhi_GenVertexArrays(int n, Vao* out) { gles.glGenVertexArrays(n, (GLuint*)out); }
void t_Gvtcier_Rhi_BindVertexArray(Vao v) { gles.glBindVertexArray((GLuint)v); }
void t_Gvtcier_Rhi_DeleteVertexArrays(int n, const Vao* v) { gles.glDeleteVertexArrays(n, (const GLuint*)v); }
void t_Gvtcier_Rhi_GenBuffers(int n, Buffer* out) { gles.glGenBuffers(n, (GLuint*)out); }
void t_Gvtcier_Rhi_BindBuffer(unsigned target, Buffer b) { gles.glBindBuffer(toGlesBufferTarget(target), (GLuint)b); }
void t_Gvtcier_Rhi_BufferData(unsigned target, size_t size, const void* data, unsigned usage) {
    gles.glBufferData(toGlesBufferTarget(target), (GLsizeiptr)size, data, toGlesUsage(usage));
}
void t_Gvtcier_Rhi_BufferSubData(unsigned target, ptrdiff_t offset, size_t size, const void* data) {
    gles.glBufferSubData(toGlesBufferTarget(target), (GLintptr)offset, (GLsizeiptr)size, data);
}
void t_Gvtcier_Rhi_BindBufferBase(unsigned target, unsigned index, Buffer b) {
    gles.glBindBufferBase(toGlesBufferTarget(target), index, (GLuint)b);
}
unsigned t_Gvtcier_Rhi_GetUniformBlockIndex(Program p, const char* name) {
    if (!gles.glGetUniformBlockIndex) return InvalidIndex;
    return (unsigned)gles.glGetUniformBlockIndex((GLuint)p, name);
}
void t_Gvtcier_Rhi_UniformBlockBinding(Program p, unsigned blockIndex, unsigned binding) {
    if (gles.glUniformBlockBinding) gles.glUniformBlockBinding((GLuint)p, blockIndex, binding);
}
void t_Gvtcier_Rhi_GetActiveUniformBlockiv(Program p, unsigned blockIndex, unsigned pname, int* params) {
    if (gles.glGetActiveUniformBlockiv)
        gles.glGetActiveUniformBlockiv((GLuint)p, blockIndex, toGlesBlockPname(pname), (GLint*)params);
}
void t_Gvtcier_Rhi_GetUniformIndices(Program p, int count, const char* const* names, unsigned* indices) {
    if (gles.glGetUniformIndices)
        gles.glGetUniformIndices((GLuint)p, count, names, (GLuint*)indices);
}
void t_Gvtcier_Rhi_GetActiveUniformsiv(Program p, int count, const unsigned* indices,
                                       unsigned pname, int* params) {
    if (gles.glGetActiveUniformsiv)
        gles.glGetActiveUniformsiv((GLuint)p, count, (const GLuint*)indices, toGlesUniformPname(pname), (GLint*)params);
}
// 成员名反查；驱动不支持时输出空串避免上层读到脏数据。
void t_Gvtcier_Rhi_GetActiveUniformName(Program p, unsigned index, int bufSize, int* length, char* name) {
    if (gles.glGetActiveUniformName)
        gles.glGetActiveUniformName((GLuint)p, index, bufSize, (GLsizei*)length, name);
    else if (name && bufSize > 0)
        name[0] = '\0';
}
void t_Gvtcier_Rhi_DispatchCompute(unsigned x, unsigned y, unsigned z) {
    if (gles.glDispatchCompute) gles.glDispatchCompute(x, y, z);
}
// 计算能力探测：需要 glDispatchCompute 与 glMemoryBarrier 都存在（GLES 3.1 起）。
bool t_Gvtcier_Rhi_HasCompute() {
    return gles.glDispatchCompute != nullptr && gles.glMemoryBarrier != nullptr;
}
// 实例化能力探测（GLES 3.0 起）。
bool t_Gvtcier_Rhi_HasInstancing() {
    return gles.glDrawElementsInstanced != nullptr && gles.glVertexAttribDivisor != nullptr;
}
void t_Gvtcier_Rhi_MemoryBarrier(unsigned barriers) {
    if (gles.glMemoryBarrier) gles.glMemoryBarrier(toGlesBarrier(barriers));
}
void t_Gvtcier_Rhi_DeleteBuffers(int n, const Buffer* b) { gles.glDeleteBuffers(n, (const GLuint*)b); }
void t_Gvtcier_Rhi_VertexAttribPointer(unsigned index, int size, unsigned type, bool normalized,
                           int stride, const void* ptr) {
    gles.glVertexAttribPointer(index, size, toGlesScalarType(type), normalized ? GL_TRUE : GL_FALSE,
                               stride, ptr);
}
void t_Gvtcier_Rhi_EnableAttrib(unsigned index) { gles.glEnableVertexAttribArray(index); }
void t_Gvtcier_Rhi_VertexAttribDivisor(unsigned index, unsigned divisor) {
    if (gles.glVertexAttribDivisor) gles.glVertexAttribDivisor(index, divisor);
}

Stage t_Gvtcier_Rhi_CreateShader(unsigned type) { return (Stage)gles.glCreateShader(toGlesShaderStage(type)); }
void t_Gvtcier_Rhi_ShaderSource(Stage s, int count, const char* const* str, const int* len) {
    gles.glShaderSource((GLuint)s, count, str, (const GLint*)len);
}
// GLES 后端不支持二进制着色器，静默忽略。
void t_Gvtcier_Rhi_ShaderSourceBinary(Stage s, const void* data, int len) {
    (void)s; (void)data; (void)len;
}
void t_Gvtcier_Rhi_CompileShader(Stage s) { gles.glCompileShader((GLuint)s); }
void t_Gvtcier_Rhi_GetShaderiv(Stage s, unsigned pname, int* params) {
    gles.glGetShaderiv((GLuint)s, toGlesShaderPname(pname), (GLint*)params);
}
void t_Gvtcier_Rhi_GetShaderInfoLog(Stage s, int bufSize, int* len, char* log) {
    gles.glGetShaderInfoLog((GLuint)s, bufSize, (GLsizei*)len, (GLchar*)log);
}
void t_Gvtcier_Rhi_DeleteShader(Stage s) { gles.glDeleteShader((GLuint)s); }
Program t_Gvtcier_Rhi_CreateProgram() { return (Program)gles.glCreateProgram(); }
void t_Gvtcier_Rhi_AttachShader(Program p, Stage s) { gles.glAttachShader((GLuint)p, (GLuint)s); }
void t_Gvtcier_Rhi_LinkProgram(Program p) { gles.glLinkProgram((GLuint)p); }
void t_Gvtcier_Rhi_GetProgramiv(Program p, unsigned pname, int* params) {
    gles.glGetProgramiv((GLuint)p, toGlesShaderPname(pname), (GLint*)params);
}
void t_Gvtcier_Rhi_GetProgramInfoLog(Program p, int bufSize, int* len, char* log) {
    gles.glGetProgramInfoLog((GLuint)p, bufSize, (GLsizei*)len, (GLchar*)log);
}
void t_Gvtcier_Rhi_DeleteProgram(Program p) { gles.glDeleteProgram((GLuint)p); }
void t_Gvtcier_Rhi_UseProgram(Program p) { gles.glUseProgram((GLuint)p); }
// GLES 后端不做 UBO 合并，uniform 位置就是真实 location。
Location t_Gvtcier_Rhi_UniformLocation(Program p, const char* name) {
    return (Location)gles.glGetUniformLocation((GLuint)p, name);
}
void t_Gvtcier_Rhi_Uniform1i(Location l, int v) { gles.glUniform1i(l, v); }
void t_Gvtcier_Rhi_Uniform1f(Location l, float v) { gles.glUniform1f(l, v); }
void t_Gvtcier_Rhi_Uniform2f(Location l, float x, float y) { gles.glUniform2f(l, x, y); }
void t_Gvtcier_Rhi_Uniform3f(Location l, float x, float y, float z) { gles.glUniform3f(l, x, y, z); }
void t_Gvtcier_Rhi_Uniform4f(Location l, float x, float y, float z, float w) { gles.glUniform4f(l, x, y, z, w); }
void t_Gvtcier_Rhi_UniformMatrix4fv(Location l, int count, bool transpose, const float* v) {
    gles.glUniformMatrix4fv(l, count, transpose ? GL_TRUE : GL_FALSE, v);
}

// 三种绘制入口都累加绘制定数（供 Gvtcier_Gles_DrawCalls 导出），无需 flush UBO。
void t_Gvtcier_Rhi_DrawArrays(unsigned mode, int first, int count) {
    ++g_glesDrawCalls;
    gles.glDrawArrays(toGlesPrimitive(mode), first, count);
}
void t_Gvtcier_Rhi_DrawElements(unsigned mode, int count, unsigned type, const void* indices) {
    ++g_glesDrawCalls;
    gles.glDrawElements(toGlesPrimitive(mode), count, toGlesScalarType(type), indices);
}
void t_Gvtcier_Rhi_DrawElementsInstanced(unsigned mode, int count, unsigned type,
                                         const void* indices, int instanceCount) {
    ++g_glesDrawCalls;
    if (gles.glDrawElementsInstanced)
        gles.glDrawElementsInstanced(toGlesPrimitive(mode), count, toGlesScalarType(type), indices, instanceCount);
}

void t_Gvtcier_Rhi_GenFramebuffers(int n, Fbo* out) { gles.glGenFramebuffers(n, (GLuint*)out); }
void t_Gvtcier_Rhi_BindFramebuffer(unsigned target, Fbo f) { gles.glBindFramebuffer(toGlesFboTarget(target), (GLuint)f); }
void t_Gvtcier_Rhi_DeleteFramebuffers(int n, const Fbo* f) { gles.glDeleteFramebuffers(n, (const GLuint*)f); }
void t_Gvtcier_Rhi_FramebufferTexture2D(unsigned target, unsigned attachment, unsigned textarget,
                            Texture t, int level) {
    gles.glFramebufferTexture2D(toGlesFboTarget(target), toGlesAttachment(attachment), toGlesTexTarget(textarget), (GLuint)t, level);
}
void t_Gvtcier_Rhi_FramebufferTextureLayer(unsigned target, unsigned attachment, Texture t,
                               int level, int layer) {
    gles.glFramebufferTextureLayer(toGlesFboTarget(target), toGlesAttachment(attachment), (GLuint)t, level, layer);
}
// 帧缓冲完整性检查：0x8CD5 = GL_FRAMEBUFFER_COMPLETE，完整时映射为 FramebufferComplete。
unsigned t_Gvtcier_Rhi_CheckFramebufferStatus(unsigned target) {
    unsigned s = (unsigned)gles.glCheckFramebufferStatus(toGlesFboTarget(target));
    return s == 0x8CD5 ? FramebufferComplete : s;
}
void t_Gvtcier_Rhi_GenRenderbuffers(int n, Rbo* out) { gles.glGenRenderbuffers(n, (GLuint*)out); }
void t_Gvtcier_Rhi_BindRenderbuffer(unsigned target, Rbo r) { gles.glBindRenderbuffer(toGlesFboTarget(target), (GLuint)r); }
void t_Gvtcier_Rhi_DeleteRenderbuffers(int n, const Rbo* r) { gles.glDeleteRenderbuffers(n, (const GLuint*)r); }
void t_Gvtcier_Rhi_RenderbufferStorage(unsigned target, unsigned internalformat, int w, int h) {
    gles.glRenderbufferStorage(toGlesFboTarget(target), toGlesInternalFormat(internalformat), w, h);
}
void t_Gvtcier_Rhi_FramebufferRenderbuffer(unsigned target, unsigned attachment, unsigned rendertarget, Rbo r) {
    gles.glFramebufferRenderbuffer(toGlesFboTarget(target), toGlesAttachment(attachment), toGlesFboTarget(rendertarget), (GLuint)r);
}

// GLES 没有固定管线兼容上下文：创建恒失败，绑定/切回核心上下文都是空操作并报告成功，
// 这样上层的“尝试兼容上下文、失败则忽略”的流程可以正常工作。
bool t_Gvtcier_Rhi_LegacyCreate() { return false; }
bool t_Gvtcier_Rhi_LegacyBind() { return true; }
bool t_Gvtcier_Rhi_CoreBind() { return true; }

// 生命周期入口，转交给平台窗口层（GvW_*）实现。
void glesPresent() { GvW_SwapBuffers(g_glesWin); }
void glesSetVsync(bool vsync) { GvW_GL_SetVsync(vsync); }
// GLES 表面尺寸由系统窗口回调管理，这里无需处理。
void glesResize(int w, int h) { (void)w; (void)h; }
void glesShutdown() { GvW_GL_Shutdown(g_glesWin); }

// GLES 后端的函数表实例。
RhiTable g_glesTable = {
    glesPresent,
    glesSetVsync,
    glesResize,
    glesShutdown,

#define RHI_TABLE_ENTRY(ret, name, params, args) t_##name,
    RHI_TABLE_ENTRIES(RHI_TABLE_ENTRY)
#undef RHI_TABLE_ENTRY
};

}

// 供上层取表：校验窗口、初始化 GLES 上下文、加载函数地址并探测各向异性扩展。
// 任一步失败返回 nullptr。
RhiTable* glesTable(void* gvwWindow) {
    GvWWindow* w = (GvWWindow*)gvwWindow;
    if (!w) return nullptr;
    if (!GvW_GL_Init(w)) return nullptr;
    glesLoad((void* (*)(const char*))GvW_GL_Proc);
    g_glesWin = w;
    // 0x1F03 是 GL_EXTENSIONS，检查是否具备各向异性过滤扩展。
    const char* ext = (const char*)gles.glGetString(0x1F03);
    g_glesAnisoExt = ext && strstr(ext, "GL_EXT_texture_filter_anisotropic") != nullptr;
    return &g_glesTable;
}

}

// 导出给性能统计使用的绘制调用计数，GLES 后端专用。
extern "C" unsigned long long Gvtcier_Gles_DrawCalls() { return rhi::g_glesDrawCalls; }
extern "C" void Gvtcier_Gles_ResetDrawCalls() { rhi::g_glesDrawCalls = 0; }

