#include "Gvtcier.h"
#include "shader_ubo.gen.h"
#include <cstdio>
#include <cstring>
#include <string>
#include <unordered_map>
#include <vector>

// 桌面 OpenGL 后端的 RHI 实现。
// 本文件把 rhi.h 里的统一枚举翻译成真实 GL 枚举，并实现全部 t_Gvtcier_Rhi_* 函数；
// 函数名前的 t_ 表示 "table 条目"，最终被填进 RhiTable 供上层转发调用。
// 额外自带一套全局 UBO 机制：着色器里 GvU 常量块的成员不走 glUniform，
// 而是先在 CPU 影子缓冲里累积，绘制前一次性上传，减少 uniform 调用次数。
namespace rhi {
namespace {

// 能力开关枚举 → GL 的 glEnable/glDisable 目标。
// 返回 0 表示未知值，调用 glEnable(0) 会产生 GL_INVALID_ENUM（当前实现未做防护）。
GLenum toGlCap(unsigned v) {
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
GLenum toGlFace(unsigned v) {
    switch (v) {
        case FrontAndBack: return 0x0408;  // GL_FRONT_AND_BACK
        case Front: return 0x0404;         // GL_FRONT
        case Back: return 0x0405;          // GL_BACK
    }
    return 0x0408;
}

// 正面绕序 → GL_CCW / GL_CW。
GLenum toGlWinding(unsigned v) {
    switch (v) {
        case Ccw: return 0x0901;  // GL_CCW 逆时针为正面
        case Cw: return 0x0900;   // GL_CW 顺时针为正面
    }
    return 0x0901;
}

// 深度比较函数 → GL 比较枚举，默认回落到 GL_LEQUAL。
// 反向 Z 场景下上层会显式传 Greater（0x0204），这里只做机械翻译。
GLenum toGlDepthFunc(unsigned v) {
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

// 光栅化模式 → GL_POINT / GL_LINE / GL_FILL。
GLenum toGlPolygonMode(unsigned v) {
    switch (v) {
        case Point: return 0x1B00;  // GL_POINT
        case Line: return 0x1B01;   // GL_LINE
        case Fill: return 0x1B02;   // GL_FILL
    }
    return 0x1B02;
}

// 混合因子 → GL 混合系数，默认 GL_ONE。
GLenum toGlBlendFactor(unsigned v) {
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

// 图元类型 → GL 绘图 begin mode，默认三角形。
GLenum toGlPrimitive(unsigned v) {
    switch (v) {
        case Points: return 0x0000;              // GL_POINTS
        case Lines: return 0x0001;               // GL_LINES
        case LineLoop: return 0x0002;            // GL_LINE_LOOP
        case LineStrip: return 0x0003;           // GL_LINE_STRIP
        case Triangles: return 0x0004;           // GL_TRIANGLES
        case TriangleStrip: return 0x0005;       // GL_TRIANGLE_STRIP
        case TriangleFan: return 0x0006;         // GL_TRIANGLE_FAN
    }
    return 0x0004;
}

// 缓冲目标 → GL 缓冲绑定目标。
// 0x88EB 是 GL_PIXEL_PACK_BUFFER（异步回读用），0x8A11 是 GL_UNIFORM_BUFFER。
GLenum toGlBufferTarget(unsigned v) {
    switch (v) {
        case ArrayBuffer: return 0x8892;           // GL_ARRAY_BUFFER
        case ElementArrayBuffer: return 0x8893;    // GL_ELEMENT_ARRAY_BUFFER
        case PixelPackBuffer: return 0x88EB;       // GL_PIXEL_PACK_BUFFER
        case ShaderStorageBuffer: return 0x90D2;   // GL_SHADER_STORAGE_BUFFER
        case UniformBuffer: return 0x8A11;         // GL_UNIFORM_BUFFER
    }
    return 0;
}

// uniform 块查询参数 → GL_ACTIVE_UNIFORM_BLOCK_* 系列枚举。
GLenum toGlBlockPname(unsigned v) {
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
GLenum toGlUniformPname(unsigned v) {
    switch (v) {
        case UniformType: return 0x8A37;         // GL_UNIFORM_TYPE
        case UniformOffset: return 0x8A3B;       // GL_UNIFORM_OFFSET
        case UniformArrayStride: return 0x8A3C;  // GL_UNIFORM_ARRAY_STRIDE
        case UniformMatrixStride: return 0x8A3D; // GL_UNIFORM_MATRIX_STRIDE
        case UniformArraySize: return 0x8A36;    // GL_UNIFORM_SIZE
    }
    return 0;
}

// 缓冲用途 → GL_STATIC_DRAW / GL_STREAM_READ / GL_DYNAMIC_DRAW / GL_STREAM_DRAW。
GLenum toGlUsage(unsigned v) {
    switch (v) {
        case StaticDraw: return 0x88E4;   // GL_STATIC_DRAW
        case StreamRead: return 0x88E5;   // GL_STREAM_READ
        case DynamicDraw: return 0x88E8;  // GL_DYNAMIC_DRAW
        case StreamDraw: return 0x88E0;   // GL_STREAM_DRAW
    }
    return 0x88E4;
}

// 标量/索引数据类型 → GL 类型枚举。
// 0x84FA 是 GL_UNSIGNED_INT_24_8（打包深度模板）。
GLenum toGlScalarType(unsigned v) {
    switch (v) {
        case Byte: return 0x1400;           // GL_BYTE
        case UnsignedByte: return 0x1401;   // GL_UNSIGNED_BYTE
        case Short: return 0x1402;          // GL_SHORT
        case UnsignedShort: return 0x1403;  // GL_UNSIGNED_SHORT
        case Int: return 0x1404;            // GL_INT
        case UnsignedInt248: return 0x84FA; // GL_UNSIGNED_INT_24_8
        case UnsignedInt: return 0x1405;    // GL_UNSIGNED_INT
        case Float: return 0x1406;          // GL_FLOAT
        case Double: return 0x140A;         // GL_DOUBLE
        case HalfFloat: return 0x140B;      // GL_HALF_FLOAT
    }
    return 0x1406;
}

// 像素外部格式 → GL 通道格式。
GLenum toGlPixelFormat(unsigned v) {
    switch (v) {
        case Red: return 0x1903;                 // GL_RED
        case Rg: return 0x8227;                  // GL_RG
        case Rgb: return 0x1907;                 // GL_RGB
        case Bgra: return 0x80E1;                // GL_BGRA
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
GLenum toGlInternalFormat(unsigned v) {
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
GLenum toGlTexTarget(unsigned v) {
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
// 0x84FE / 0x84FF 是各向异性扩展的等级与上限。
GLenum toGlTexParam(unsigned v) {
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
// 返回 0 表示不是这两类取值（例如各向异性等级整数），调用处会回退使用原值。
GLenum toGlTexValue(unsigned v) {
    switch (v) {
        case Nearest: return 0x2600;               // GL_NEAREST
        case Linear: return 0x2601;                // GL_LINEAR
        case NearestMipmapNearest: return 0x2700;  // GL_NEAREST_MIPMAP_NEAREST
        case LinearMipmapNearest: return 0x2701;   // GL_LINEAR_MIPMAP_NEAREST
        case NearestMipmapLinear: return 0x2702;   // GL_NEAREST_MIPMAP_LINEAR
        case LinearMipmapLinear: return 0x2703;    // GL_LINEAR_MIPMAP_LINEAR
        case ClampToEdge: return 0x812F;           // GL_CLAMP_TO_EDGE
        case ClampToBorder: return 0x812D;         // GL_CLAMP_TO_BORDER
        case MirroredRepeat: return 0x8370;        // GL_MIRRORED_REPEAT
        case Repeat: return 0x2901;                // GL_REPEAT
    }
    return 0x2601;
}

// 着色器阶段 → GL 阶段枚举（几何着色器为 GL_GEOMETRY_SHADER）。
GLenum toGlShaderStage(unsigned v) {
    switch (v) {
        case VertexShader: return 0x8B31;    // GL_VERTEX_SHADER
        case FragmentShader: return 0x8B30;  // GL_FRAGMENT_SHADER
        case GeometryShader: return 0x8DD9;  // GL_GEOMETRY_SHADER
        case ComputeShader: return 0x91B9;   // GL_COMPUTE_SHADER
    }
    return 0;
}

// 着色器/程序查询参数 → GL_COMPILE_STATUS / GL_LINK_STATUS / GL_INFO_LOG_LENGTH。
GLenum toGlShaderPname(unsigned v) {
    switch (v) {
        case CompileStatus: return 0x8B81;  // GL_COMPILE_STATUS
        case LinkStatus: return 0x8B82;     // GL_LINK_STATUS
        case InfoLogLength: return 0x8B84;  // GL_INFO_LOG_LENGTH
    }
    return 0;
}

// 帧缓冲类目标 → GL_FRAMEBUFFER / GL_RENDERBUFFER。
GLenum toGlFboTarget(unsigned v) {
    switch (v) {
        case Framebuffer: return 0x8D40;   // GL_FRAMEBUFFER
        case Renderbuffer: return 0x8D41;  // GL_RENDERBUFFER
    }
    return 0x8D40;
}

// 附件点 → GL_COLOR_ATTACHMENTn / GL_DEPTH_ATTACHMENT 等。
// 0x8D20 为 GL_STENCIL_ATTACHMENT，0x84F9 为 GL_DEPTH_STENCIL_ATTACHMENT。
GLenum toGlAttachment(unsigned v) {
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

// 内存屏障位 → GL 屏障位；未列出的值原样返回（便于上层直接传 GL 位）。
GLenum toGlBarrier(unsigned v) {
    switch (v) {
        case ShaderStorageBarrierBit: return 0x00002000;  // GL_SHADER_STORAGE_BARRIER_BIT
        case AllBarrierBits: return 0xFFFFFFFF;           // GL_ALL_BARRIER_BITS
    }
    return v;
}

// 驱动信息查询名 → GL 查询枚举。
GLenum toGlParamName(unsigned v) {
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

const char* t_Gvtcier_Rhi_GetString(unsigned name) { return (const char*)gl.glGetString(toGlParamName(name)); }
void t_Gvtcier_Rhi_GetIntegerv(unsigned pname, int* data) { gl.glGetIntegerv(toGlParamName(pname), (GLint*)data); }
unsigned t_Gvtcier_Rhi_GetError() { return (unsigned)gl.glGetError(); }

void t_Gvtcier_Rhi_ClearColor(float r, float g, float b, float a) { gl.glClearColor(r, g, b, a); }
// 设置深度清屏值。反向 Z 布局下远平面为 0，所以主通道与阴影通道调用方都传 0.0。
void t_Gvtcier_Rhi_ClearDepth(double d) { gl.glClearDepth(d); }
// 把 RHI 清屏位翻译成 GL 位域：0x4000 颜色、0x100 深度、0x400 模板。
void t_Gvtcier_Rhi_Clear(unsigned mask) {
    unsigned m = 0;
    if (mask & ColorBit) m |= 0x00004000;
    if (mask & DepthBit) m |= 0x00000100;
    if (mask & StencilBit) m |= 0x00000400;
    gl.glClear((GLbitfield)m);
}
void t_Gvtcier_Rhi_Viewport(int x, int y, int w, int h) { gl.glViewport(x, y, w, h); }
// 把 RasterState 全量套到 GL 状态机上。
// s 为空时直接返回；这里不做状态缓存/去冗余，每次调用都会把每个状态重新下发一遍，
// 因此上层频繁切换相同状态时会产生多余的 GL 调用（功能正确但有开销）。
void t_Gvtcier_Rhi_SetRasterState(const RasterState* s) {
    if (!s) return;
    // 深度测试开关及其比较函数。反向 Z 时调用方会把 depthFunc 设为 Greater，
    // 配合深度清 0.0 才能让更近的片元覆盖更远的片元。
    if (s->depthTest) gl.glEnable(toGlCap(DepthTest));
    else gl.glDisable(toGlCap(DepthTest));
    gl.glDepthMask(s->depthWrite ? GL_TRUE : GL_FALSE);
    gl.glDepthFunc(toGlDepthFunc(s->depthFunc));
    // 面剔除：关闭时只关开关，保留上次的剔除面设置。
    if (s->cull) {
        gl.glEnable(toGlCap(CullFace));
        gl.glCullFace(toGlFace(s->cullMode));
    } else {
        gl.glDisable(toGlCap(CullFace));
    }
    gl.glFrontFace(toGlWinding(s->frontFace));
    // 混合：开启时才设置混合因子。
    if (s->blend) {
        gl.glEnable(toGlCap(Blend));
        gl.glBlendFunc(toGlBlendFactor(s->blendSrc), toGlBlendFactor(s->blendDst));
    } else {
        gl.glDisable(toGlCap(Blend));
    }
    if (s->scissor) {
        gl.glEnable(toGlCap(ScissorTest));
        gl.glScissor(s->scissorX, s->scissorY, s->scissorW, s->scissorH);
    } else {
        gl.glDisable(toGlCap(ScissorTest));
    }
    gl.glPolygonMode(toGlFace(FrontAndBack), toGlPolygonMode(s->polygonMode));
    // 多边形偏移：两个系数都为 0 视为关闭；0x8038 是 GL_POLYGON_OFFSET_FILL，
    // 常用于阴影贴图或贴花，抵消深度精度误差造成的 Z 冲突。
    if (s->polyFactor == 0.0f && s->polyUnit == 0.0f) {
        gl.glDisable(0x8038);
    } else {
        gl.glEnable(0x8038);
        gl.glPolygonOffset(s->polyFactor, s->polyUnit);
    }
    gl.glColorMask(s->colorWrite[0] ? GL_TRUE : GL_FALSE, s->colorWrite[1] ? GL_TRUE : GL_FALSE,
                   s->colorWrite[2] ? GL_TRUE : GL_FALSE, s->colorWrite[3] ? GL_TRUE : GL_FALSE);
}
void t_Gvtcier_Rhi_DrawBuffer(unsigned buf) { gl.glDrawBuffer(toGlAttachment(buf)); }
// 指定多绘制目标，最多 8 个颜色附件（超过部分被截断）。
void t_Gvtcier_Rhi_DrawBuffers(int n, const unsigned* bufs) {
    GLenum tmp[8];
    if (n > 8) n = 8;
    for (int i = 0; i < n; ++i) tmp[i] = toGlAttachment(bufs[i]);
    gl.glDrawBuffers((GLsizei)n, tmp);
}
void t_Gvtcier_Rhi_ReadBuffer(unsigned buf) { gl.glReadBuffer(toGlAttachment(buf)); }
void t_Gvtcier_Rhi_ReadPixels(int x, int y, int w, int h, unsigned format, unsigned type, void* data) {
    gl.glReadPixels(x, y, w, h, toGlPixelFormat(format), toGlScalarType(type), data);
}
// 把当前读取缓冲的视口区域拷贝进纹理 tex。
// 0x0BA2 是 GL_VIEWPORT，0x8CA8 是 GL_READ_FRAMEBUFFER：
// 在 4.6 核心 profile 里 glReadBuffer 只对 READ_FRAMEBUFFER 目标生效，
// 而 bind 帧缓冲时用的是 GL_FRAMEBUFFER（同时绑定读/写），这里再显式切一次读目标。
// 只拷贝 2D 纹理；format/type 参数在此实现中被忽略。
void t_Gvtcier_Rhi_ReadBufferTex(unsigned format, unsigned type, unsigned tex) {
    GLint vp[4];
    gl.glGetIntegerv(0x0BA2, vp);
    gl.glReadBuffer(0x8CA8);
    gl.glBindTexture(0x0DE1, (GLuint)tex);
    gl.glCopyTexSubImage2D(0x0DE1, 0, 0, 0, vp[0], vp[1], vp[2], vp[3]);
    (void)format;
    (void)type;
}

void t_Gvtcier_Rhi_GenTextures(int n, Texture* out) { gl.glGenTextures(n, (GLuint*)out); }
void t_Gvtcier_Rhi_BindTexture(unsigned target, Texture t) { gl.glBindTexture(toGlTexTarget(target), (GLuint)t); }
void t_Gvtcier_Rhi_DeleteTextures(int n, const Texture* t) { gl.glDeleteTextures(n, (const GLuint*)t); }
// 分配 2D 纹理存储（border 固定 0，核心 profile 要求）。
void t_Gvtcier_Rhi_TexImage2D(unsigned target, int level, int internalFormat, int w, int h,
                  unsigned format, unsigned type, const void* data) {
    gl.glTexImage2D(toGlTexTarget(target), level, (int)toGlInternalFormat((unsigned)internalFormat), w, h, 0,
                    toGlPixelFormat(format), toGlScalarType(type), data);
}
void t_Gvtcier_Rhi_TexSubImage2D(unsigned target, int level, int x, int y, int w, int h,
                     unsigned format, unsigned type, const void* data) {
    gl.glTexSubImage2D(toGlTexTarget(target), level, x, y, w, h, toGlPixelFormat(format), toGlScalarType(type), data);
}
void t_Gvtcier_Rhi_TexImage3D(unsigned target, int level, int internalFormat, int w, int h, int d,
                  unsigned format, unsigned type, const void* data) {
    gl.glTexImage3D(toGlTexTarget(target), level, (int)toGlInternalFormat((unsigned)internalFormat), w, h, d, 0,
                    toGlPixelFormat(format), toGlScalarType(type), data);
}
void t_Gvtcier_Rhi_TexSubImage3D(unsigned target, int level, int x, int y, int z, int w, int h, int dd,
                     unsigned format, unsigned type, const void* data) {
    gl.glTexSubImage3D(toGlTexTarget(target), level, x, y, z, w, h, dd, toGlPixelFormat(format), toGlScalarType(type), data);
}
void t_Gvtcier_Rhi_GenerateMipmap(unsigned target) { gl.glGenerateMipmap(toGlTexTarget(target)); }
// 设置纹理整数参数：value 先尝试按过滤/寻址枚举翻译，
// 翻译结果非 0 说明命中这两类，否则视为普通整数（如各向异性等级）原样下发。
void t_Gvtcier_Rhi_TexParameteri(unsigned target, unsigned pname, int value) {
    GLenum mapped = toGlTexValue((unsigned)value);
    gl.glTexParameteri(toGlTexTarget(target), toGlTexParam(pname), mapped ? (int)mapped : value);
}
void t_Gvtcier_Rhi_TexParameterf(unsigned target, unsigned pname, float value) {
    gl.glTexParameterf(toGlTexTarget(target), toGlTexParam(pname), value);
}
// 全局各向异性等级，默认 16，夹取到 [1,16] 后保存，由上层读取后再下发到纹理。
static int g_anisoLevel = 16;
void t_Gvtcier_Rhi_SetAnisotropy(int level) {
    if (level < 1) level = 1;
    if (level > 16) level = 16;
    g_anisoLevel = level;
}
int t_Gvtcier_Rhi_Anisotropy() { return g_anisoLevel; }
// 激活纹理单元 unit（GL_TEXTURE0 = 0x84C0 加偏移）并在其上绑定纹理。
void t_Gvtcier_Rhi_BindTextureUnit(int unit, unsigned target, Texture t) {
    gl.glActiveTexture((GLenum)(0x84C0 + unit));
    gl.glBindTexture(toGlTexTarget(target), (GLuint)t);
}

void t_Gvtcier_Rhi_GenVertexArrays(int n, Vao* out) { gl.glGenVertexArrays(n, (GLuint*)out); }
void t_Gvtcier_Rhi_BindVertexArray(Vao v) { gl.glBindVertexArray((GLuint)v); }
void t_Gvtcier_Rhi_DeleteVertexArrays(int n, const Vao* v) { gl.glDeleteVertexArrays(n, (const GLuint*)v); }
void t_Gvtcier_Rhi_GenBuffers(int n, Buffer* out) { gl.glGenBuffers(n, (GLuint*)out); }
void t_Gvtcier_Rhi_BindBuffer(unsigned target, Buffer b) { gl.glBindBuffer(toGlBufferTarget(target), (GLuint)b); }
void t_Gvtcier_Rhi_BufferData(unsigned target, size_t size, const void* data, unsigned usage) {
    gl.glBufferData(toGlBufferTarget(target), (GLsizeiptr)size, data, toGlUsage(usage));
}
void t_Gvtcier_Rhi_BufferSubData(unsigned target, ptrdiff_t offset, size_t size, const void* data) {
    gl.glBufferSubData(toGlBufferTarget(target), (GLintptr)offset, (GLsizeiptr)size, data);
}
// 把缓冲绑定到 UBO/SSBO 的绑定点：绑定后程序里对应 binding 的块即可直接读取。
void t_Gvtcier_Rhi_BindBufferBase(unsigned target, unsigned index, Buffer b) {
    gl.glBindBufferBase(toGlBufferTarget(target), index, (GLuint)b);
}
unsigned t_Gvtcier_Rhi_GetUniformBlockIndex(Program p, const char* name) {
    if (!gl.glGetUniformBlockIndex) return InvalidIndex;
    return (unsigned)gl.glGetUniformBlockIndex((GLuint)p, name);
}
void t_Gvtcier_Rhi_UniformBlockBinding(Program p, unsigned blockIndex, unsigned binding) {
    if (gl.glUniformBlockBinding) gl.glUniformBlockBinding((GLuint)p, blockIndex, binding);
}

// 全局 UBO 子系统。
// 动机：着色器里 GvU 这个 uniform 块被所有程序共享，如果每个成员都走 glUniform
// 会产生大量小调用；改为单个 UBO 后，CPU 端先写影子缓冲，绘制前一次上传整块。
namespace {

// 伪位置标签：位于 Location 的最高位（bit 28）。
// 上层拿到的 uniform 位置如果带这个位，就表示该名字属于 GvU 块，
// 低 28 位是它在 UBO 内的字节偏移，而不是真实的 uniform location。
const int kUboTag = 1 << 28;
// GvU 块使用的绑定点号，所有程序统一绑到 0 号，便于共享同一个缓冲。
const unsigned kUboBinding = 0;
// GL_UNIFORM_BUFFER，绑定 UBO 时使用。
const unsigned kUboTarget = 0x8A11;

// 单个程序对应的 UBO 运行时状态。
struct GlUbo {
    Program prog = 0;                          // 所属着色器程序
    GLuint buf = 0;                            // GPU 侧 UBO 对象，0 表示未建立
    int size = 0;                              // 缓冲字节数（等于 kGvUboSize）
    bool ready = false;                        // 是否已尝试过建立（无论成功与否）
    bool dirty = false;                        // 影子缓冲有未上传的改动
    int writeCnt = 0;                          // 累计 CPU 写入次数（诊断用）
    int flushCnt = 0;                          // 累计上传次数（诊断用）
    std::vector<unsigned char> shadow;         // CPU 影子缓冲，大小与 GPU 缓冲一致
};

// 每个程序一份 UBO 状态；程序删除时同步移除。
std::vector<GlUbo> g_ubos;
// 最近一次 UseProgram 激活的程序，UBO 写入需要知道它属于哪个程序。
Program g_curProg = 0;

// 根据成员名在编译期生成的布局表 kGvUboMembers 中查字节偏移。
// 支持 "uFoo" 直接匹配，也支持 "uFoo[3]" 这种数组元素访问；
// 返回 -1 表示该名字不属于 GvU 块（或数组下标越界），调用方应回退到普通 uniform。
int uboLayoutOffset(const char* name) {
    if (!name) return -1;
    for (int i = 0; i < kGvUboMemberCount; ++i) {
        const char* n = kGvUboMembers[i].name;
        size_t j = 0;
        // 前缀逐字符比对，n 先结束时才可能是完整匹配。
        while (n[j] && name[j] && n[j] == name[j]) ++j;
        if (n[j] != '\0') continue;
        if (name[j] == '\0') return kGvUboMembers[i].offset;
        if (name[j] != '[') continue;
        // 解析 [数字] 下标，注意注释符号禁用，这里只能靠逐字符数字判断。
        int idx = 0;
        size_t k = j + 1;
        if (name[k] < '0' || name[k] > '9') continue;
        while (name[k] >= '0' && name[k] <= '9') {
            idx = idx * 10 + (name[k] - '0');
            ++k;
        }
        if (name[k] != ']') continue;
        int cnt = kGvUboMembers[i].count;
        int elem = kGvUboMembers[i].elemSize;
        if (cnt <= 1 || elem <= 0 || idx < 0 || idx >= cnt) continue;
        return kGvUboMembers[i].offset + idx * elem;
    }
    return -1;
}

// 按程序查找 UBO 状态；create 为 true 时找不到就新建一条。
// 注意返回的是 vector 内部元素地址，后续 push_back 会使它失效，
// 调用方都是即取即用，不跨帧保存。
GlUbo* uboOf(Program p, bool create) {
    for (size_t i = 0; i < g_ubos.size(); ++i)
        if (g_ubos[i].prog == p) return &g_ubos[i];
    if (!create) return nullptr;
    GlUbo u;
    u.prog = p;
    g_ubos.push_back(u);
    return &g_ubos.back();
}

// 为程序建立 UBO：查 GvU 块索引、分配缓冲、初始化全 0 数据，
// 并把块的 binding 点与 0 号绑定点绑定起来。
// 若驱动不支持 uniform 块接口或程序里没有 GvU 块，则 ready 置 true 但 buf 保持 0，
// 之后所有 UBO 写入会被静默忽略（值根本不会进着色器）。
void buildUbo(GlUbo* u) {
    u->ready = true;
    if (!gl.glGetUniformBlockIndex || !gl.glUniformBlockBinding) return;
    unsigned bi = (unsigned)gl.glGetUniformBlockIndex((GLuint)u->prog, "GvU");
    if (bi == 0xFFFFFFFFu) return;
    u->size = kGvUboSize;
    u->shadow.assign((size_t)u->size, 0);
    gl.glGenBuffers(1, &u->buf);
    gl.glBindBuffer(kUboTarget, u->buf);
    // 0x88E8 = GL_DYNAMIC_DRAW，UBO 每帧会反复更新，动态用途更合适。
    gl.glBufferData(kUboTarget, (GLsizeiptr)u->size, u->shadow.data(), 0x88E8);
    gl.glBindBuffer(kUboTarget, 0);
    gl.glUniformBlockBinding((GLuint)u->prog, bi, kUboBinding);
    gl.glBindBufferBase(kUboTarget, kUboBinding, u->buf);
    gl.glBindBuffer(kUboTarget, 0);
    {
        // 一次性诊断日志：把引擎假定的块大小与驱动上报的块大小对比，
        // 不一致说明 GLSL 的 std140 对齐与 CPU 布局存在偏差，均匀布局会错位。
        char key[64];
        snprintf(key, sizeof(key), "glubo_build%u", u->prog);
        int driverSize = -1;
        if (gl.glGetActiveUniformBlockiv)
            gl.glGetActiveUniformBlockiv((GLuint)u->prog, bi, 0x8A40, &driverSize);
        rlog::once(key, "[glubo] build prog=%u block=%u size=%d driverSize=%d match=%d\n",
                   u->prog, bi, u->size, driverSize, (int)(driverSize == u->size));
    }
}

// 绘制前上传：仅当影子缓冲被改过时才整块 glBufferSubData，然后重新绑定 0 号绑定点。
// 每次都 BindBufferBase 是为了防止期间其他绑定操作把绑定点改到别的缓冲。
void uboFlush(GlUbo* u) {
    if (!u || u->buf == 0) return;
    if (u->dirty) {
        u->dirty = false;
        gl.glBindBuffer(kUboTarget, u->buf);
        gl.glBufferSubData(kUboTarget, 0, (GLsizeiptr)u->size, u->shadow.data());
        gl.glBindBuffer(kUboTarget, 0);
        ++u->flushCnt;
        // 第 30 与 300 次上传各打一条一次性日志，抽查关键矩阵是否被正确写入。
        if (u->flushCnt == 30 || u->flushCnt == 300) {
            const float* m = (const float*)u->shadow.data();
            rlog::once(u->flushCnt == 30 ? "glubo_f30" : "glubo_f300",
                       "[glubo] prog=%u flush=%d writes=%d uModel0=%.3f uView0=%.3f uProj0=%.3f uProj5=%.3f uProj10=%.3f\n",
                       u->prog, u->flushCnt, u->writeCnt, m[0], m[16], m[32], m[37], m[42]);
        }
    }
    gl.glBindBufferBase(kUboTarget, kUboBinding, u->buf);
}

// 把数据写进影子缓冲，只标记脏位，真正上传推迟到下一次绘制。
// off 为 UBO 内字节偏移，越界写入直接丢弃（防止破坏相邻成员）。
void uboWrite(Program p, int off, const void* data, size_t size) {
    GlUbo* u = uboOf(p, false);
    if (!u || u->buf == 0 || off < 0) return;
    if ((size_t)off + size > (size_t)u->size) return;
    memcpy(u->shadow.data() + off, data, size);
    u->dirty = true;
    ++u->writeCnt;
}

// 查询某个名字在 UBO 中的偏移；首次查询时顺便惰性建立 UBO。
// 返回 -1 表示不是 GvU 成员或 UBO 建立失败。
int uboOffsetOf(Program p, const char* name) {
    GlUbo* u = uboOf(p, true);
    if (!u) return -1;
    if (!u->ready) buildUbo(u);
    if (u->buf == 0) return -1;
    return uboLayoutOffset(name);
}

}

void t_Gvtcier_Rhi_GetActiveUniformBlockiv(Program p, unsigned blockIndex, unsigned pname, int* params) {
    if (gl.glGetActiveUniformBlockiv)
        gl.glGetActiveUniformBlockiv((GLuint)p, blockIndex, toGlBlockPname(pname), (GLint*)params);
}
void t_Gvtcier_Rhi_GetUniformIndices(Program p, int count, const char* const* names, unsigned* indices) {
    if (gl.glGetUniformIndices)
        gl.glGetUniformIndices((GLuint)p, count, names, (GLuint*)indices);
}
void t_Gvtcier_Rhi_GetActiveUniformsiv(Program p, int count, const unsigned* indices,
                                       unsigned pname, int* params) {
    if (gl.glGetActiveUniformsiv)
        gl.glGetActiveUniformsiv((GLuint)p, count, (const GLuint*)indices, toGlUniformPname(pname), (GLint*)params);
}
void t_Gvtcier_Rhi_GetActiveUniformName(Program p, unsigned index, int bufSize, int* length, char* name) {
    if (gl.glGetActiveUniformName)
        gl.glGetActiveUniformName((GLuint)p, index, bufSize, (GLsizei*)length, name);
    else if (name && bufSize > 0)
        name[0] = '\0';
}
void t_Gvtcier_Rhi_DispatchCompute(unsigned x, unsigned y, unsigned z) {
    if (gl.glDispatchCompute) gl.glDispatchCompute(x, y, z);
}
// 计算能力探测：派发与屏障两个入口都非空才算支持。
bool t_Gvtcier_Rhi_HasCompute() {
    return gl.glDispatchCompute != nullptr && gl.glMemoryBarrier != nullptr;
}
// 实例化能力探测：实例绘制与属性分频入口都非空才算支持。
bool t_Gvtcier_Rhi_HasInstancing() {
    return gl.glDrawElementsInstanced != nullptr && gl.glVertexAttribDivisor != nullptr;
}
void t_Gvtcier_Rhi_MemoryBarrier(unsigned barriers) {
    if (gl.glMemoryBarrier) gl.glMemoryBarrier(toGlBarrier(barriers));
}
void t_Gvtcier_Rhi_DeleteBuffers(int n, const Buffer* b) { gl.glDeleteBuffers(n, (const GLuint*)b); }
void t_Gvtcier_Rhi_VertexAttribPointer(unsigned index, int size, unsigned type, bool normalized,
                           int stride, const void* ptr) {
    gl.glVertexAttribPointer(index, size, toGlScalarType(type), normalized ? GL_TRUE : GL_FALSE,
                             stride, ptr);
}
void t_Gvtcier_Rhi_EnableAttrib(unsigned index) { gl.glEnableVertexAttribArray(index); }
void t_Gvtcier_Rhi_VertexAttribDivisor(unsigned index, unsigned divisor) {
    if (gl.glVertexAttribDivisor) gl.glVertexAttribDivisor(index, divisor);
}

Stage t_Gvtcier_Rhi_CreateShader(unsigned type) { return (Stage)gl.glCreateShader(toGlShaderStage(type)); }
void t_Gvtcier_Rhi_ShaderSource(Stage s, int count, const char* const* str, const int* len) {
    gl.glShaderSource((GLuint)s, count, str, (const GLint*)len);
}
// 二进制着色器（SPIR-V）只有 Vulkan 后端能消费，GL 后端仅打印一行提示后忽略。
void t_Gvtcier_Rhi_ShaderSourceBinary(Stage s, const void* data, int len) {
    (void)s; (void)data; (void)len;
    printf("[gl] ShaderSourceBinary 在当前后端不支持（需 Vulkan）\n");
}
void t_Gvtcier_Rhi_CompileShader(Stage s) { gl.glCompileShader((GLuint)s); }
void t_Gvtcier_Rhi_GetShaderiv(Stage s, unsigned pname, int* params) {
    gl.glGetShaderiv((GLuint)s, toGlShaderPname(pname), (GLint*)params);
}
void t_Gvtcier_Rhi_GetShaderInfoLog(Stage s, int bufSize, int* len, char* log) {
    gl.glGetShaderInfoLog((GLuint)s, bufSize, (GLsizei*)len, (GLchar*)log);
}
void t_Gvtcier_Rhi_DeleteShader(Stage s) { gl.glDeleteShader((GLuint)s); }
Program t_Gvtcier_Rhi_CreateProgram() { return (Program)gl.glCreateProgram(); }
void t_Gvtcier_Rhi_AttachShader(Program p, Stage s) { gl.glAttachShader((GLuint)p, (GLuint)s); }
void t_Gvtcier_Rhi_LinkProgram(Program p) { gl.glLinkProgram((GLuint)p); }
void t_Gvtcier_Rhi_GetProgramiv(Program p, unsigned pname, int* params) {
    gl.glGetProgramiv((GLuint)p, toGlShaderPname(pname), (GLint*)params);
}
void t_Gvtcier_Rhi_GetProgramInfoLog(Program p, int bufSize, int* len, char* log) {
    gl.glGetProgramInfoLog((GLuint)p, bufSize, (GLsizei*)len, (GLchar*)log);
}
// 删除程序时必须先释放它名下的 UBO，否则 GPU 缓冲泄漏。
void t_Gvtcier_Rhi_DeleteProgram(Program p) {
    for (size_t i = 0; i < g_ubos.size(); ++i) {
        if (g_ubos[i].prog == p) {
            if (g_ubos[i].buf) gl.glDeleteBuffers(1, &g_ubos[i].buf);
            g_ubos.erase(g_ubos.begin() + (ptrdiff_t)i);
            break;
        }
    }
    gl.glDeleteProgram((GLuint)p);
}
// 激活程序并记下当前程序，UBO 写入要靠它定位影子缓冲。
void t_Gvtcier_Rhi_UseProgram(Program p) {
    gl.glUseProgram((GLuint)p);
    g_curProg = p;
}
// 查询 uniform 位置。
// 先查引擎自己的 GvU 布局表：命中则返回“标签 | 偏移”的伪位置，
// 后续 Uniform* 上传会走影子缓冲；未命中才回到真正的 glGetUniformLocation。
// 副作用：首次查询会为当前程序惰性创建 UBO。
Location t_Gvtcier_Rhi_UniformLocation(Program p, const char* name) {
    int off = uboOffsetOf(p, name);
    if (off >= 0) return (Location)(kUboTag | off);
    return (Location)gl.glGetUniformLocation((GLuint)p, name);
}
// 以下 Uniform 上传统一规则：位置带 kUboTag 时写影子缓冲，否则走原生 glUniform。
// 刻意不用 glGetUniformLocation 的返回值判断，因为伪位置可能为负数且带标签位。
void t_Gvtcier_Rhi_Uniform1i(Location l, int v) {
    if (l & kUboTag) { uboWrite(g_curProg, l & (kUboTag - 1), &v, 4); return; }
    gl.glUniform1i(l, v);
}
void t_Gvtcier_Rhi_Uniform1f(Location l, float v) {
    if (l & kUboTag) { uboWrite(g_curProg, l & (kUboTag - 1), &v, 4); return; }
    gl.glUniform1f(l, v);
}
void t_Gvtcier_Rhi_Uniform2f(Location l, float x, float y) {
    if (l & kUboTag) { float t[2] = { x, y }; uboWrite(g_curProg, l & (kUboTag - 1), t, 8); return; }
    gl.glUniform2f(l, x, y);
}
// vec3 只写 12 字节：std140 里 vec3 之后通常会跟隐式填充，剩余 4 字节保持原值。
void t_Gvtcier_Rhi_Uniform3f(Location l, float x, float y, float z) {
    if (l & kUboTag) { float t[4] = { x, y, z, 0.0f }; uboWrite(g_curProg, l & (kUboTag - 1), t, 12); return; }
    gl.glUniform3f(l, x, y, z);
}
void t_Gvtcier_Rhi_Uniform4f(Location l, float x, float y, float z, float w) {
    if (l & kUboTag) { float t[4] = { x, y, z, w }; uboWrite(g_curProg, l & (kUboTag - 1), t, 16); return; }
    gl.glUniform4f(l, x, y, z, w);
}
// 矩阵上传：每个 mat4 占 64 字节，count 个连续写入。
// UBO 路径忽略 transpose，因为 std140 布局假定 CPU 端已按列主序写好的数据；
// 若上层传 transpose=true，UBO 里存进去的就是未转置内容。
void t_Gvtcier_Rhi_UniformMatrix4fv(Location l, int count, bool transpose, const float* v) {
    if (l & kUboTag) {
        uboWrite(g_curProg, l & (kUboTag - 1), v, (size_t)count * 64);
        return;
    }
    gl.glUniformMatrix4fv(l, count, transpose ? GL_TRUE : GL_FALSE, v);
}

// 三种绘制入口都在真正下命令之前先 flush 当前程序的 UBO，
// 保证这一批 uniform 改动在同一次绘制内全部生效。
void t_Gvtcier_Rhi_DrawArrays(unsigned mode, int first, int count) {
    uboFlush(uboOf(g_curProg, false));
    gl.glDrawArrays(toGlPrimitive(mode), first, count);
}
void t_Gvtcier_Rhi_DrawElements(unsigned mode, int count, unsigned type, const void* indices) {
    uboFlush(uboOf(g_curProg, false));
    gl.glDrawElements(toGlPrimitive(mode), count, toGlScalarType(type), indices);
}
void t_Gvtcier_Rhi_DrawElementsInstanced(unsigned mode, int count, unsigned type,
                                         const void* indices, int instanceCount) {
    uboFlush(uboOf(g_curProg, false));
    if (gl.glDrawElementsInstanced)
        gl.glDrawElementsInstanced(toGlPrimitive(mode), count, toGlScalarType(type), indices, instanceCount);
}

void t_Gvtcier_Rhi_GenFramebuffers(int n, Fbo* out) { gl.glGenFramebuffers(n, (GLuint*)out); }
void t_Gvtcier_Rhi_BindFramebuffer(unsigned target, Fbo f) { gl.glBindFramebuffer(toGlFboTarget(target), (GLuint)f); }
void t_Gvtcier_Rhi_DeleteFramebuffers(int n, const Fbo* f) { gl.glDeleteFramebuffers(n, (const GLuint*)f); }
void t_Gvtcier_Rhi_FramebufferTexture2D(unsigned target, unsigned attachment, unsigned textarget,
                            Texture t, int level) {
    gl.glFramebufferTexture2D(toGlFboTarget(target), toGlAttachment(attachment), toGlTexTarget(textarget), (GLuint)t, level);
}
void t_Gvtcier_Rhi_FramebufferTextureLayer(unsigned target, unsigned attachment, Texture t,
                               int level, int layer) {
    gl.glFramebufferTextureLayer(toGlFboTarget(target), toGlAttachment(attachment), (GLuint)t, level, layer);
}
// 检查帧缓冲完整性。0x8CD5 是 GL_FRAMEBUFFER_COMPLETE；
// 完整时统一映射为 RHI 的 FramebufferComplete，不完整时把驱动原始错误码透传给上层。
unsigned t_Gvtcier_Rhi_CheckFramebufferStatus(unsigned target) {
    unsigned s = (unsigned)gl.glCheckFramebufferStatus(toGlFboTarget(target));
    return s == 0x8CD5 ? FramebufferComplete : s;
}
void t_Gvtcier_Rhi_GenRenderbuffers(int n, Rbo* out) { gl.glGenRenderbuffers(n, (GLuint*)out); }
void t_Gvtcier_Rhi_BindRenderbuffer(unsigned target, Rbo r) { gl.glBindRenderbuffer(toGlFboTarget(target), (GLuint)r); }
void t_Gvtcier_Rhi_DeleteRenderbuffers(int n, const Rbo* r) { gl.glDeleteRenderbuffers(n, (const GLuint*)r); }
void t_Gvtcier_Rhi_RenderbufferStorage(unsigned target, unsigned internalformat, int w, int h) {
    gl.glRenderbufferStorage(toGlFboTarget(target), toGlInternalFormat(internalformat), w, h);
}
void t_Gvtcier_Rhi_FramebufferRenderbuffer(unsigned target, unsigned attachment, unsigned rendertarget, Rbo r) {
    gl.glFramebufferRenderbuffer(toGlFboTarget(target), toGlAttachment(attachment), toGlFboTarget(rendertarget), (GLuint)r);
}

// 固定管线支持的三件套：创建 / 切换到兼容上下文 / 切回核心上下文。
bool t_Gvtcier_Rhi_LegacyCreate() { return glLegacyCreate(); }
bool t_Gvtcier_Rhi_LegacyBind() { return glLegacyBind(); }
bool t_Gvtcier_Rhi_CoreBind() { return glCoreBind(); }

// GL 后端的函数表实例：前四项是生命周期入口，其余由 RHI_TABLE_ENTRIES 展开成 t_ 函数指针。
RhiTable g_glTable = {
    glPresent,
    glSetVsync,
    glResize,
    glShutdown,

#define RHI_TABLE_ENTRY(ret, name, params, args) t_##name,
    RHI_TABLE_ENTRIES(RHI_TABLE_ENTRY)
#undef RHI_TABLE_ENTRY
};

}

// 供上层取表：先初始化 GL 上下文与函数地址，失败返回 nullptr 让调用方另择后端。
RhiTable* glTable(HWND hwnd) {
    if (!glInit(hwnd)) return nullptr;
    return &g_glTable;
}

}

