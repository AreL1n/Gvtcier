#pragma once
#include <cstddef>
#include "bicu/core/export.h"

struct HWND__;
typedef HWND__* HWND;

// RHI = Render Hardware Interface，渲染硬件接口层。
// 上层渲染代码只依赖本文件里的 Gvtcier_Rhi_* 函数与这套自造的枚举编号，
// 不直接接触任何 GL / GLES / Vulkan 的符号。三种后端各自实现同一张函数表
// （见 backend.h 的 RhiTable），由 渲染抽象.cpp 在初始化时按平台挑选并统一转发，
// 于是切换后端只需要换一张表，调用方代码零改动。
namespace rhi {

// 当前生效的图形后端：桌面 OpenGL 4.6 核心、OpenGL ES（移动/嵌入式）、Vulkan。
enum class Backend { Gl, Gles, Vulkan };

// 以下句柄类型统一用 GL 的对象命名（GLuint）表示，Vulkan 后端内部自行映射。
using Buffer = unsigned int;   // 缓冲对象（顶点/索引/UBO/SSBO/像素打包）
using Texture = unsigned int;  // 纹理对象
using Vao = unsigned int;      // 顶点数组对象，记录顶点属性的绑定状态
using Fbo = unsigned int;      // 帧缓冲对象，渲染目标集合
using Rbo = unsigned int;      // 渲染缓冲对象，不可采样的渲染目标（常用于深度）
using Program = unsigned int;  // 已链接的着色器程序
using Stage = unsigned int;    // 单个着色器阶段（顶点/片元等）对象
using Location = int;          // uniform 位置；负数表示无效，GL 后端中高 4 位被借用为标签

// Gvtcier_Rhi_Clear 的掩码位，后端负责翻译成各自的位域。
constexpr unsigned ColorBit = 1u << 0;    // 清颜色
constexpr unsigned DepthBit = 1u << 1;    // 清深度
constexpr unsigned StencilBit = 1u << 2;  // 清模板

// 可开关的渲染能力，对应 GL 的 glEnable/glDisable 目标。
constexpr unsigned DepthTest = 100;    // GL_DEPTH_TEST 深度测试
constexpr unsigned CullFace = 101;     // GL_CULL_FACE 面剔除
constexpr unsigned ScissorTest = 102;  // GL_SCISSOR_TEST 裁剪矩形
constexpr unsigned Blend = 103;        // GL_BLEND 颜色混合
constexpr unsigned StencilTest = 104;  // GL_STENCIL_TEST 模板测试

// 剔除面与正面绕序。
constexpr unsigned FrontAndBack = 110;  // 正反两面都操作（GL_FRONT_AND_BACK）
constexpr unsigned Front = 111;         // 仅正面（GL_FRONT）
constexpr unsigned Back = 112;          // 仅背面（GL_BACK）
constexpr unsigned Ccw = 113;           // 逆时针顶点顺序视为正面（GL_CCW）
constexpr unsigned Cw = 114;            // 顺时针顶点顺序视为正面（GL_CW）

// 深度/模板比较函数。
// Greater 是本引擎主渲染通道使用的比较方式：投影矩阵采用反向 Z，
// 近平面映射到 1、远平面映射到 0，因此“更近”意味着深度值更大，
// 必须用 Greater 才能正确保留最近片元，同时把深度清成 0.0 而非 1.0。
constexpr unsigned Never = 120;     // GL_NEVER 永不通过
constexpr unsigned Less = 121;      // GL_LESS 小于则通过
constexpr unsigned EqualD = 122;    // GL_EQUAL 等于则通过（命名避开 Equal 关键字）
constexpr unsigned Lequal = 123;    // GL_LEQUAL 小于等于则通过（RasterState 默认值）
constexpr unsigned Greater = 124;   // GL_GREATER 大于则通过（反向 Z 正向通道）
constexpr unsigned Notequal = 125;  // GL_NOTEQUAL 不等于则通过
constexpr unsigned Gequal = 126;    // GL_GEQUAL 大于等于则通过（反向 Z 阴影通道）
constexpr unsigned Always = 127;    // GL_ALWAYS 总通过
// 模板操作（glStencilOp 的三个动作）。
constexpr unsigned Keep = 128;       // GL_KEEP 保持原值
constexpr unsigned ReplaceOp = 129;  // GL_REPLACE 用参考值替换
constexpr unsigned Incr = 130;       // GL_INCR 饱和自增
constexpr unsigned Decr = 131;       // GL_DECR 饱和自减
constexpr unsigned Invert = 132;     // GL_INVERT 按位取反
constexpr unsigned IncrWrap = 133;   // GL_INCR_WRAP 回绕自增
constexpr unsigned DecrWrap = 134;   // GL_DECR_WRAP 回绕自减

// 多边形光栅化模式（glPolygonMode）。
// 注意这三个编号与上面的 Incr/Decr/Invert 数值重叠，靠上下文区分，不要混用。
constexpr unsigned Point = 130;  // GL_POINT 只画顶点
constexpr unsigned Line = 131;   // GL_LINE 画线框
constexpr unsigned Fill = 132;   // GL_FILL 实心填充（默认）

// 混合因子（glBlendFunc 的源/目标系数）。
constexpr unsigned Zero = 140;                // GL_ZERO 取 0
constexpr unsigned One = 141;                 // GL_ONE 取 1
constexpr unsigned SrcAlpha = 142;            // GL_SRC_ALPHA 源 alpha
constexpr unsigned OneMinusSrcAlpha = 143;    // GL_ONE_MINUS_SRC_ALPHA 1-源 alpha（常规透明混合）
constexpr unsigned SrcColor = 144;            // GL_SRC_COLOR 源颜色
constexpr unsigned OneMinusSrcColor = 145;    // GL_ONE_MINUS_SRC_COLOR 1-源颜色
constexpr unsigned DstAlpha = 146;            // GL_DST_ALPHA 目标 alpha
constexpr unsigned OneMinusDstAlpha = 147;    // GL_ONE_MINUS_DST_ALPHA 1-目标 alpha
constexpr unsigned None = 0;                  // 无绑定（如绘制缓冲为 GL_NONE）

// 一次绘制所需的光栅状态快照。
// 调用方每帧填写后交给 Gvtcier_Rhi_SetRasterState，后端负责把它套到当前 GL 状态机上。
// 这份表是“全量状态”而不是差异：每个字段都必须填对，未赋值的字段会保留结构体默认值。
struct RasterState {
    bool depthTest = true;              // 是否开启深度测试
    bool depthWrite = true;             // 是否写入深度缓冲（透明物体应关掉）
    unsigned depthFunc = Lequal;        // 深度比较函数，主通道用 Greater 配合反向 Z
    bool cull = true;                   // 是否开启背面剔除
    unsigned cullMode = Back;           // 剔除哪一面：Front/Back/FrontAndBack
    unsigned frontFace = Ccw;           // 正面绕序：Ccw/Cw，用于判断哪面算正面
    bool blend = false;                 // 是否开启颜色混合
    unsigned blendSrc = One;            // 混合源因子
    unsigned blendDst = Zero;           // 混合目标因子
    bool scissor = false;               // 是否开启裁剪矩形
    int scissorX = 0, scissorY = 0, scissorW = 0, scissorH = 0;  // 裁剪矩形（左下角原点，像素）
    unsigned polygonMode = Fill;        // 光栅化模式：Point/Line/Fill
    float polyFactor = 0.0f, polyUnit = 0.0f;  // 多边形偏移；两者全 0 时视为关闭偏移
    bool colorWrite[4] = {true, true, true, true};  // 颜色通道写掩码 R/G/B/A
};

// 图元装配方式，对应 glDrawArrays/glDrawElements 的 mode。
constexpr unsigned Points = 150;         // GL_POINTS 独立点
constexpr unsigned Lines = 151;          // GL_LINES 每两点一条线段
constexpr unsigned LineStrip = 152;      // GL_LINE_STRIP 折线
constexpr unsigned LineLoop = 153;       // GL_LINE_LOOP 闭合折线
constexpr unsigned Triangles = 154;      // GL_TRIANGLES 独立三角形
constexpr unsigned TriangleStrip = 155;  // GL_TRIANGLE_STRIP 三角带
constexpr unsigned TriangleFan = 156;    // GL_TRIANGLE_FAN 三角扇

// 缓冲绑定目标。
constexpr unsigned ArrayBuffer = 160;           // GL_ARRAY_BUFFER 顶点属性数据
constexpr unsigned ElementArrayBuffer = 161;    // GL_ELEMENT_ARRAY_BUFFER 索引数据
constexpr unsigned ShaderStorageBuffer = 162;   // GL_SHADER_STORAGE_BUFFER 计算/图形可读写缓冲
constexpr unsigned UniformBuffer = 163;         // GL_UNIFORM_BUFFER 常量缓冲，引擎全局 ubo 用它
constexpr unsigned PixelPackBuffer = 164;       // GL_PIXEL_PACK_BUFFER 异步回读的目标缓冲

// uniform 块查询参数（glGetActiveUniformBlockiv 的 pname）。
constexpr unsigned UniformBlockBinding = 170;               // 块绑定的 binding 点号
constexpr unsigned UniformBlockDataSize = 171;              // 块数据字节数，用于与 CPU 端布局比对
constexpr unsigned UniformBlockNameLength = 172;            // 块名长度
constexpr unsigned UniformBlockActiveUniforms = 173;        // 块内活跃成员数量
constexpr unsigned UniformBlockActiveUniformIndices = 174;  // 块内活跃成员的索引数组

// 单个 uniform 成员的查询参数（glGetActiveUniformsiv 的 pname）。
constexpr unsigned UniformType = 175;           // 成员类型（TypeFloat* 等）
constexpr unsigned UniformOffset = 176;         // 成员在块内的字节偏移
constexpr unsigned UniformArrayStride = 177;    // 数组元素之间的跨距
constexpr unsigned UniformMatrixStride = 178;   // 矩阵一列的跨距
constexpr unsigned UniformArraySize = 179;      // 数组长度

// uniform 块索引查询失败时 GL 返回的保留值（GL_INVALID_INDEX）。
constexpr unsigned InvalidIndex = 0xFFFFFFFFu;

// 着色器 uniform 类型，用于引擎自建的 UBO 布局反射。
constexpr unsigned TypeFloat = 180;      // 标量 float
constexpr unsigned TypeFloatVec2 = 181;  // vec2
constexpr unsigned TypeFloatVec3 = 182;  // vec3
constexpr unsigned TypeFloatVec4 = 183;  // vec4
constexpr unsigned TypeInt = 184;        // int
constexpr unsigned TypeBool = 185;       // bool
constexpr unsigned TypeFloatMat2 = 186;  // mat2
constexpr unsigned TypeFloatMat3 = 187;  // mat3
constexpr unsigned TypeFloatMat4 = 188;  // mat4

// 缓冲用途（glBufferData 的 usage）。
constexpr unsigned StaticDraw = 190;   // 一次上传多次绘制
constexpr unsigned DynamicDraw = 191;  // 频繁更新（引擎的 UBO 使用它）
constexpr unsigned StreamDraw = 192;   // 每帧重传
constexpr unsigned StreamRead = 193;   // 每帧回读

// 顶点属性/索引的元素数据类型。
constexpr unsigned Byte = 200;           // GL_BYTE
constexpr unsigned UnsignedByte = 201;   // GL_UNSIGNED_BYTE
constexpr unsigned Short = 202;          // GL_SHORT
constexpr unsigned UnsignedShort = 203;  // GL_UNSIGNED_SHORT，常用索引类型
constexpr unsigned Int = 204;            // GL_INT
constexpr unsigned UnsignedInt = 205;    // GL_UNSIGNED_INT，大网格索引类型
constexpr unsigned Float = 206;          // GL_FLOAT
constexpr unsigned Double = 207;         // GL_DOUBLE
constexpr unsigned HalfFloat = 208;      // GL_HALF_FLOAT 压缩顶点
constexpr unsigned UnsignedInt248 = 209; // GL_UNSIGNED_INT_24_8 打包深度模板

// 像素数据的外部通道格式（glTexImage/glReadPixels 的 format）。
constexpr unsigned Red = 210;               // GL_RED 单通道
constexpr unsigned Rg = 211;                // GL_RG 双通道
constexpr unsigned Rgb = 212;               // GL_RGB 三通道
constexpr unsigned Bgra = 213;              // GL_BGRA Windows 位图常规顺序
constexpr unsigned Rgba = 214;              // GL_RGBA 四通道
constexpr unsigned RedInteger = 215;        // GL_RED_INTEGER 整数单通道
constexpr unsigned RgInteger = 216;         // GL_RG_INTEGER
constexpr unsigned RgbInteger = 217;        // GL_RGB_INTEGER
constexpr unsigned RgbaInteger = 218;       // GL_RGBA_INTEGER
constexpr unsigned DepthStencilFormat = 219;// GL_DEPTH_STENCIL 打包深度模板
constexpr unsigned StencilIndex = 220;      // GL_STENCIL_INDEX 仅模板

// 纹理内部格式。数值即 glTexImage 的 internalFormat 参数入口。
constexpr unsigned R8 = 230;                 // GL_R8 8 位单通道，噪声/掩码
constexpr unsigned R16 = 231;                // GL_R16
constexpr unsigned R16F = 232;               // GL_R16F 半精度单通道
constexpr unsigned R32F = 233;               // GL_R32F 单精度单通道
constexpr unsigned Rg8 = 234;                // GL_RG8 双通道，常用存法线 xy
constexpr unsigned Rg16F = 235;              // GL_RG16F
constexpr unsigned Rg32F = 236;              // GL_RG32F
constexpr unsigned Rgb8 = 237;               // GL_RGB8
constexpr unsigned Rgb16F = 238;             // GL_RGB16F HDR 三通道
constexpr unsigned Rgba8 = 239;              // GL_RGBA8 常规 8 位颜色目标
constexpr unsigned Rgba16 = 240;             // GL_RGBA16
constexpr unsigned Rgba16F = 241;            // GL_RGBA16F 半精度 HDR 中间目标
constexpr unsigned Rgba32F = 242;            // GL_RGBA32F 全精度浮点目标
constexpr unsigned Rgba32f = 242;            // Rgba32F 的别名
constexpr unsigned Srgb8Alpha8 = 243;        // GL_SRGB8_ALPHA8 sRGB 颜色目标
constexpr unsigned DepthComponent = 244;     // GL_DEPTH_COMPONENT 深度格式
constexpr unsigned DepthComponent16 = 245;   // GL_DEPTH_COMPONENT16
constexpr unsigned DepthComponent24 = 246;   // GL_DEPTH_COMPONENT24 深度 24 位
constexpr unsigned DepthComponent32F = 247;  // GL_DEPTH_COMPONENT32F 浮点深度，反向 Z 保精度首选
constexpr unsigned Depth24 = 246;            // DepthComponent24 的别名
constexpr unsigned Depth24Stencil8 = 248;    // GL_DEPTH24_STENCIL8 打包深度模板
constexpr unsigned R11G11B10F = 249;         // GL_R11G11B10F 无 alpha 的紧凑 HDR
constexpr unsigned RgbaFloat = 242;          // Rgba32F 的别名

// 纹理目标类型；TexCubeXxx 是立方体贴图六个面，创建时按面分别绑定。
constexpr unsigned Tex2D = 250;         // GL_TEXTURE_2D
constexpr unsigned Tex2DArray = 251;    // GL_TEXTURE_2D_ARRAY 分层阴影/级联贴图
constexpr unsigned Tex3D = 252;         // GL_TEXTURE_3D 体纹理
constexpr unsigned TexCube = 253;       // GL_TEXTURE_CUBE_MAP 点光源阴影立方体贴图
constexpr unsigned TexCubePosX = 254;   // GL_TEXTURE_CUBE_MAP_POSITIVE_X
constexpr unsigned TexCubeNegX = 255;   // GL_TEXTURE_CUBE_MAP_NEGATIVE_X
constexpr unsigned TexCubePosY = 256;   // GL_TEXTURE_CUBE_MAP_POSITIVE_Y
constexpr unsigned TexCubeNegY = 257;   // GL_TEXTURE_CUBE_MAP_NEGATIVE_Y
constexpr unsigned TexCubePosZ = 258;   // GL_TEXTURE_CUBE_MAP_POSITIVE_Z
constexpr unsigned TexCubeNegZ = 259;   // GL_TEXTURE_CUBE_MAP_NEGATIVE_Z

// 纹理单元编号。GL 的单元是连续的枚举（GL_TEXTURE0 + n），
// 所以这里用 0 起始的序号表示，绑定后端会加上基址。
constexpr int TextureBase = 0;    // 纹理单元基序号
constexpr int Texture0 = TextureBase + 0;
constexpr int Texture1 = TextureBase + 1;
constexpr int Texture2 = TextureBase + 2;
constexpr int Texture3 = TextureBase + 3;
constexpr int Texture4 = TextureBase + 4;
constexpr int Texture5 = TextureBase + 5;
constexpr int Texture6 = TextureBase + 6;
constexpr int Texture7 = TextureBase + 7;
constexpr int Texture8 = TextureBase + 8;
constexpr int Texture9 = TextureBase + 9;
constexpr int Texture10 = TextureBase + 10;
constexpr int Texture11 = TextureBase + 11;
constexpr int Texture12 = TextureBase + 12;
constexpr int Texture13 = TextureBase + 13;
constexpr int Texture14 = TextureBase + 14;
constexpr int Texture15 = TextureBase + 15;

// 单次绘制可用的最大纹理单元数，渲染器绑定时不得超过该值。
constexpr int MaxTextureUnits = 16;

// 各渲染通道约定的纹理槽位编号（就是纹理单元序号）。
// 同一个编号在不同着色程序里含义可能不同，所以按通道分组列出，
// 保证着色器里的 sampler 绑定与渲染器绑定到同一单元。
namespace TexSlot {

// 场景主通道与后处理链共用的槽位
constexpr int Scene = 0;            // 场景颜色图（后处理输入）
constexpr int GNormal = 1;          // 延迟着色 G-Buffer 法线
constexpr int GAlbedo = 2;          // 延迟着色 G-Buffer 反照率
constexpr int GMaterial = 3;        // 延迟着色 G-Buffer 材质参数
constexpr int CSMShadow = 4;        // 级联阴影贴图数组
constexpr int PointShadowCube = 5;  // 点光源阴影立方体贴图
constexpr int PointShadowCube2 = 10; // 点阴影立方图 2（多盏投影点光轮转）
constexpr int PointShadowCube3 = 11; // 点阴影立方图 3
constexpr int PointShadowCube4 = 12; // 点阴影立方图 4
constexpr int AsmShadow = 9;        // ASM 子相机深度图集（Parallel Type Shadow Mapping）
constexpr int Depth = 6;            // 场景深度图
constexpr int SkyGradient = 7;      // 天空渐变查找纹理
constexpr int FaceTex = 8;          // 面纹理（例如 GUI/角色贴图）
constexpr int SkyBox = 13;          // 天空盒 equirect 贴图

// 延迟着色光照通道
constexpr int Albedo = 0;     // G-Buffer 反照率
constexpr int NormalTex = 1;  // G-Buffer 法线

// 环境光遮蔽（AO）通道
constexpr int AO = 0;      // AO 结果图
constexpr int Bloom = 1;   // Bloom 输入图
constexpr int Volume = 4;  // 体积雾/体积光数据
constexpr int AODepth = 3; // AO 使用的深度图
constexpr int AOEdges = 5; // AO 边缘检测辅助图

// 阴影与通用辅助通道
constexpr int ShadowMap = 1;  // 阴影贴图
constexpr int Noise = 2;      // 噪声纹理
constexpr int Src = 0;        // 后处理源图（拷贝/模糊的输入）

}

// 纹理参数名（glTexParameteri/f 的 pname）。
constexpr unsigned TexMinFilter = 260;  // GL_TEXTURE_MIN_FILTER 缩小过滤
constexpr unsigned TexMagFilter = 261;  // GL_TEXTURE_MAG_FILTER 放大过滤
constexpr unsigned TexWrapS = 262;      // GL_TEXTURE_WRAP_S S 方向寻址
constexpr unsigned TexWrapT = 263;      // GL_TEXTURE_WRAP_T T 方向寻址
constexpr unsigned TexWrapR = 264;      // GL_TEXTURE_WRAP_R R 方向寻址（3D/立方体）
constexpr unsigned TexMaxAniso = 265;   // GL_TEXTURE_MAX_ANISOTROPY_EXT 各向异性等级
constexpr unsigned MaxTexAniso = 266;   // GL_MAX_TEXTURE_MAX_ANISOTROPY_EXT 硬件上限（查询用）

// 过滤模式与寻址模式取值。
constexpr unsigned Nearest = 270;               // GL_NEAREST 最近点采样
constexpr unsigned Linear = 271;                // GL_LINEAR 双线性采样
constexpr unsigned NearestMipmapNearest = 272;  // GL_NEAREST_MIPMAP_NEAREST
constexpr unsigned LinearMipmapNearest = 273;    // GL_LINEAR_MIPMAP_NEAREST
constexpr unsigned NearestMipmapLinear = 274;   // GL_NEAREST_MIPMAP_LINEAR
constexpr unsigned LinearMipmapLinear = 275;    // GL_LINEAR_MIPMAP_LINEAR 三线性过滤
constexpr unsigned ClampToEdge = 276;           // GL_CLAMP_TO_EDGE 夹取边缘，避免接缝
constexpr unsigned ClampToBorder = 277;         // GL_CLAMP_TO_BORDER 越界取边框色
constexpr unsigned MirroredRepeat = 278;        // GL_MIRRORED_REPEAT 镜像重复
constexpr unsigned Repeat = 279;                // GL_REPEAT 平铺重复

// 着色器阶段与编译/链接查询参数。
constexpr unsigned VertexShader = 280;    // GL_VERTEX_SHADER 顶点着色器
constexpr unsigned FragmentShader = 281;  // GL_FRAGMENT_SHADER 片元着色器
constexpr unsigned GeometryShader = 282;  // GL_GEOMETRY_SHADER 几何着色器（GLES 不支持）
constexpr unsigned ComputeShader = 283;   // GL_COMPUTE_SHADER 计算着色器
// glMemoryBarrier 的屏障位：让 SSBO 写入对后续读取可见。
constexpr unsigned ShaderStorageBarrierBit = 1u << 2;  // GL_SHADER_STORAGE_BARRIER_BIT
constexpr unsigned AllBarrierBits = 0xFFFFFFFFu;       // GL_ALL_BARRIER_BITS
constexpr unsigned CompileStatus = 290;   // GL_COMPILE_STATUS 编译是否成功
constexpr unsigned LinkStatus = 291;      // GL_LINK_STATUS 链接是否成功
constexpr unsigned InfoLogLength = 292;   // GL_INFO_LOG_LENGTH 日志缓冲区所需长度

// 帧缓冲与附件。
constexpr unsigned Framebuffer = 300;                // GL_FRAMEBUFFER 帧缓冲绑定目标
constexpr unsigned FramebufferComplete = 301;        // GL_FRAMEBUFFER_COMPLETE 附件配置合法的返回值
constexpr unsigned DepthAttachment = 302;            // GL_DEPTH_ATTACHMENT 深度附件点
constexpr unsigned StencilAttachment = 303;          // GL_STENCIL_ATTACHMENT 模板附件点
constexpr unsigned DepthStencilAttachment = 305;     // GL_DEPTH_STENCIL_ATTACHMENT 打包深度模板附件点
constexpr unsigned Renderbuffer = 304;               // GL_RENDERBUFFER 渲染缓冲绑定目标
constexpr unsigned ColorAttachment0 = 310;           // GL_COLOR_ATTACHMENT0
constexpr unsigned ColorAttachment1 = 311;           // GL_COLOR_ATTACHMENT1
constexpr unsigned ColorAttachment2 = 312;           // GL_COLOR_ATTACHMENT2
constexpr unsigned ColorAttachment3 = 313;           // GL_COLOR_ATTACHMENT3
constexpr unsigned ColorAttachment4 = 314;           // GL_COLOR_ATTACHMENT4
constexpr unsigned ColorAttachment5 = 315;           // GL_COLOR_ATTACHMENT5
constexpr unsigned ColorAttachment6 = 316;           // GL_COLOR_ATTACHMENT6
constexpr unsigned ColorAttachment7 = 317;           // GL_COLOR_ATTACHMENT7

// 驱动信息与硬件上限查询名。
constexpr unsigned Version = 320;                  // GL_VERSION 版本字符串
constexpr unsigned RendererName = 321;             // GL_RENDERER 渲染器名称
constexpr unsigned VendorName = 322;               // GL_VENDOR 厂商名称
constexpr unsigned ShadingLanguageVersion = 323;   // GL_SHADING_LANGUAGE_VERSION GLSL 版本
constexpr unsigned MajorVersion = 324;             // GL_MAJOR_VERSION 主版本号
constexpr unsigned MinorVersion = 325;             // GL_MINOR_VERSION 次版本号
constexpr unsigned MaxTextureSize = 326;           // GL_MAX_TEXTURE_SIZE 最大纹理边长
constexpr unsigned MaxSamples = 327;               // GL_MAX_SAMPLES 最大多重采样数

// ---------------------------------------------------------------------------
// 生命周期与全局查询
// ---------------------------------------------------------------------------

// 初始化渲染后端并建立绘制上下文。
// hwnd：窗口句柄，GL 后端用它创建 WGL 上下文；GLES 后端忽略它，改用 Gvtcier_Rhi_SetWindow 传入的窗口。
// 返回：成功为 true；无可用后端为 false。重复调用会先关闭旧后端再重建。
GVTCIER_API bool Gvtcier_Rhi_Init(HWND hwnd);
#ifdef GVWCIER_USE_GLES
// 仅在 GLES 编译开关下存在：登记 GLES 后端要渲染的原生窗口（GvWWindow*），
// 因为 EGL/GLES 的窗口不是 Win32 HWND，必须绕开 Gvtcier_Rhi_Init 的 hwnd 参数。
GVTCIER_API void Gvtcier_Rhi_SetWindow(void* w);
#endif
// 返回当前实际生效的后端，供上层针对不同后端走不同渲染路径（着色器版本、特性开关等）。
GVTCIER_API Backend Gvtcier_Rhi_Backend();
// 提交当前帧并交换前后缓冲，把画面推到屏幕上。
GVTCIER_API void Gvtcier_Rhi_Present();
// 开/关垂直同步，立即作用于交换间隔。
GVTCIER_API void Gvtcier_Rhi_SetVsync(bool vsync);
// 通知后端绘制区域尺寸变化（GL 只在交换链层面记录，真正视口由 Gvtcier_Rhi_Viewport 设置）。
GVTCIER_API void Gvtcier_Rhi_Resize(int w, int h);
// 销毁上下文与后端资源，并把内部函数表置空，使后续 RHI 调用变成空操作。
GVTCIER_API void Gvtcier_Rhi_Shutdown();
// 查询驱动字符串，name 取 Version/RendererName/VendorName/ShadingLanguageVersion。
GVTCIER_API const char* Gvtcier_Rhi_GetString(unsigned name);
// 查询整型能力值，pname 取 MajorVersion/MinorVersion/MaxTextureSize/MaxSamples/MaxTexAniso 等。
GVTCIER_API void Gvtcier_Rhi_GetIntegerv(unsigned pname, int* data);
// 取出并清除最近一个 GL 错误码；返回 0 表示无错误。
GVTCIER_API unsigned Gvtcier_Rhi_GetError();

// ---------------------------------------------------------------------------
// 帧级状态与像素回读
// ---------------------------------------------------------------------------

// 设置颜色清屏值，各分量取值 0~1。
GVTCIER_API void Gvtcier_Rhi_ClearColor(float r, float g, float b, float a);
// 设置深度清屏值 d。采用反向 Z 时深度缓冲“远”为 0.0，因此清屏应传 0.0；
// 阴影贴图等仍按需自行传入其他值。
GVTCIER_API void Gvtcier_Rhi_ClearDepth(double d);
// 按 ColorBit/DepthBit/StencilBit 组合掩码清屏。
GVTCIER_API void Gvtcier_Rhi_Clear(unsigned mask);
// 设置视口左下角 (x,y) 与宽高（像素，原点在左下）。
GVTCIER_API void Gvtcier_Rhi_Viewport(int x, int y, int w, int h);
// 把 RasterState 全量套用到当前状态机，见结构体各字段说明。
GVTCIER_API void Gvtcier_Rhi_SetRasterState(const RasterState* s);
// 指定当前帧缓冲唯一的绘制目标（单个颜色附件）。
GVTCIER_API void Gvtcier_Rhi_DrawBuffer(unsigned buf);
// 指定多个绘制目标，n 为数量，最多 8 个；用于 G-Buffer 一次写多张图。
GVTCIER_API void Gvtcier_Rhi_DrawBuffers(int n, const unsigned* bufs);
// 指定 glReadPixels 的读取源附件。
GVTCIER_API void Gvtcier_Rhi_ReadBuffer(unsigned buf);
// 从当前读取缓冲回读像素到 data，data 需按 w*h*通道大小 预分配。
GVTCIER_API void Gvtcier_Rhi_ReadPixels(int x, int y, int w, int h, unsigned format, unsigned type, void* data);
// 把当前读取缓冲的内容拷贝进纹理 tex（只取视口区域），format/type 参数目前被忽略。
GVTCIER_API void Gvtcier_Rhi_ReadBufferTex(unsigned format, unsigned type, unsigned tex);

// ---------------------------------------------------------------------------
// 纹理
// ---------------------------------------------------------------------------

// 生成 n 个纹理对象句柄，写入 out。
GVTCIER_API void Gvtcier_Rhi_GenTextures(int n, Texture* out);
// 把纹理 t 绑定到 target（Tex2D/TexCube 等）当前活动的纹理单元。
GVTCIER_API void Gvtcier_Rhi_BindTexture(unsigned target, Texture t);
// 删除 n 个纹理对象。
GVTCIER_API void Gvtcier_Rhi_DeleteTextures(int n, const Texture* t);
// 分配 2D 纹理存储并可选上传数据。level 为 mip 级别，internalFormat 用 Rgba8/DepthComponent24 等，
// format/type 描述 data 的像素排布，data 为 nullptr 时只分配不清零。
GVTCIER_API void Gvtcier_Rhi_TexImage2D(unsigned target, int level, int internalFormat, int w, int h,
                                        unsigned format, unsigned type, const void* data);
// 更新 2D 纹理的子区域，(x,y) 为子区域左下角。
GVTCIER_API void Gvtcier_Rhi_TexSubImage2D(unsigned target, int level, int x, int y, int w, int h,
                                           unsigned format, unsigned type, const void* data);
// 分配 3D/2DArray 纹理存储，d 为深度层数。
GVTCIER_API void Gvtcier_Rhi_TexImage3D(unsigned target, int level, int internalFormat, int w, int h, int d,
                                        unsigned format, unsigned type, const void* data);
// 更新 3D/2DArray 纹理的子区域，z 为起始层，dd 为层数。
GVTCIER_API void Gvtcier_Rhi_TexSubImage3D(unsigned target, int level, int x, int y, int z, int w, int h, int dd,
                                           unsigned format, unsigned type, const void* data);
// 为已带 mip 数据的纹理生成完整 mip 链。
GVTCIER_API void Gvtcier_Rhi_GenerateMipmap(unsigned target);
// 设置整数纹理参数，value 会先按过滤/寻址枚举翻译；过滤与寻址以外的参数（如各向异性等级）按原值传。
GVTCIER_API void Gvtcier_Rhi_TexParameteri(unsigned target, unsigned pname, int value);
// 设置浮点纹理参数，同样会翻译 value。
GVTCIER_API void Gvtcier_Rhi_TexParameterf(unsigned target, unsigned pname, float value);
// 记录全局各向异性等级（GL 夹到 1~16），不直接下发到纹理，由上层采样设置时取用。
GVTCIER_API void Gvtcier_Rhi_SetAnisotropy(int level);
// 取回当前记录的各向异性等级。
GVTCIER_API int Gvtcier_Rhi_Anisotropy();
// 激活纹理单元 unit 并在其上绑定纹理，unit 为 0 起始序号。
GVTCIER_API void Gvtcier_Rhi_BindTextureUnit(int unit, unsigned target, Texture t);

// ---------------------------------------------------------------------------
// 顶点数组与缓冲
// ---------------------------------------------------------------------------

// 生成 n 个顶点数组对象。
GVTCIER_API void Gvtcier_Rhi_GenVertexArrays(int n, Vao* out);
// 绑定顶点数组对象，0 表示解绑。
GVTCIER_API void Gvtcier_Rhi_BindVertexArray(Vao v);
// 删除 n 个顶点数组对象。
GVTCIER_API void Gvtcier_Rhi_DeleteVertexArrays(int n, const Vao* v);
// 生成 n 个缓冲对象。
GVTCIER_API void Gvtcier_Rhi_GenBuffers(int n, Buffer* out);
// 把缓冲 b 绑定到 target（ArrayBuffer/UniformBuffer 等）。
GVTCIER_API void Gvtcier_Rhi_BindBuffer(unsigned target, Buffer b);
// 分配缓冲存储并上传初始数据，usage 提示驱动放置策略。
GVTCIER_API void Gvtcier_Rhi_BufferData(unsigned target, size_t size, const void* data, unsigned usage);
// 局部更新缓冲内容，offset 为字节偏移。
GVTCIER_API void Gvtcier_Rhi_BufferSubData(unsigned target, ptrdiff_t offset, size_t size, const void* data);
// 把缓冲绑定到 target 的指定绑定点 index（UBO/SSBO 用，绑定后着色器可直接访问）。
GVTCIER_API void Gvtcier_Rhi_BindBufferBase(unsigned target, unsigned index, Buffer b);
// 查询程序中名为 name 的 uniform 块索引，不存在时返回 InvalidIndex。
GVTCIER_API unsigned Gvtcier_Rhi_GetUniformBlockIndex(Program p, const char* name);
// 建立“块索引 → 绑定点点号”的对应关系，之后 Gvtcier_Rhi_BindBufferBase 绑定该点即可供该程序使用。
GVTCIER_API void Gvtcier_Rhi_UniformBlockBinding(Program p, unsigned blockIndex, unsigned binding);
// 查询 uniform 块信息，pname 取 UniformBlockDataSize/UniformBlockBinding 等，结果写入 params。
GVTCIER_API void Gvtcier_Rhi_GetActiveUniformBlockiv(Program p, unsigned blockIndex, unsigned pname, int* params);
// 把一组 uniform 名字翻译成程序内的成员索引，供 GetActiveUniformsiv 查询布局。
GVTCIER_API void Gvtcier_Rhi_GetUniformIndices(Program p, int count, const char* const* names, unsigned* indices);
// 按成员索引查询布局属性，pname 取 UniformOffset/UniformType/UniformArrayStride 等。
GVTCIER_API void Gvtcier_Rhi_GetActiveUniformsiv(Program p, int count, const unsigned* indices,
                                                 unsigned pname, int* params);
// 按成员索引反查成员名；驱动不支持时输出空串。
GVTCIER_API void Gvtcier_Rhi_GetActiveUniformName(Program p, unsigned index, int bufSize, int* length, char* name);
// 派发计算着色器，(x,y,z) 为工作组数量。
GVTCIER_API void Gvtcier_Rhi_DispatchCompute(unsigned x, unsigned y, unsigned z);
// 插入内存屏障，barriers 取 ShaderStorageBarrierBit/AllBarrierBits，保证写入对后续读取可见。
GVTCIER_API void Gvtcier_Rhi_MemoryBarrier(unsigned barriers);
// 计算着色器能力探测，返回 true 表示可派发计算任务。
GVTCIER_API bool Gvtcier_Rhi_HasCompute();
// 实例化绘制能力探测，返回 true 表示可用 DrawElementsInstanced。
GVTCIER_API bool Gvtcier_Rhi_HasInstancing();
// 删除 n 个缓冲对象。
GVTCIER_API void Gvtcier_Rhi_DeleteBuffers(int n, const Buffer* b);
// 描述顶点属性来源：index 为属性槽，size 为分量数，type 为元素类型，
// normalized 表示是否把整数归一化到 0~1，stride 为顶点跨距，ptr 为属性在缓冲中的字节偏移。
GVTCIER_API void Gvtcier_Rhi_VertexAttribPointer(unsigned index, int size, unsigned type, bool normalized,
                                                 int stride, const void* ptr);
// 启用指定顶点属性槽。
GVTCIER_API void Gvtcier_Rhi_EnableAttrib(unsigned index);
// 设置属性更新频率，divisor 为 0 表示逐顶点，大于 0 表示每 divisor 个实例前进一次（实例化数据）。
GVTCIER_API void Gvtcier_Rhi_VertexAttribDivisor(unsigned index, unsigned divisor);

// ---------------------------------------------------------------------------
// 着色器
// ---------------------------------------------------------------------------

// 创建指定阶段（VertexShader/FragmentShader/ComputeShader 等）的着色器对象。
GVTCIER_API Stage Gvtcier_Rhi_CreateShader(unsigned type);
// 上传着色器源码，count 为字符串段数，len 可传 nullptr 表示按零结尾处理。
GVTCIER_API void Gvtcier_Rhi_ShaderSource(Stage s, int count, const char* const* str, const int* len);
// 直接上传已编译的二进制着色器（SPIR-V），仅 Vulkan 后端支持，GL 后端只打印提示。
GVTCIER_API void Gvtcier_Rhi_ShaderSourceBinary(Stage s, const void* data, int len);
// 编译着色器，结果用 GetShaderiv(CompileStatus) 查询。
GVTCIER_API void Gvtcier_Rhi_CompileShader(Stage s);
// 查询着色器属性，pname 取 CompileStatus/InfoLogLength。
GVTCIER_API void Gvtcier_Rhi_GetShaderiv(Stage s, unsigned pname, int* params);
// 取出编译日志，len 可为 nullptr。
GVTCIER_API void Gvtcier_Rhi_GetShaderInfoLog(Stage s, int bufSize, int* len, char* log);
// 删除着色器对象（链接完成的程序不再依赖它）。
GVTCIER_API void Gvtcier_Rhi_DeleteShader(Stage s);
// 创建空的着色器程序对象。
GVTCIER_API Program Gvtcier_Rhi_CreateProgram();
// 把着色器阶段挂到程序上。
GVTCIER_API void Gvtcier_Rhi_AttachShader(Program p, Stage s);
// 链接程序，结果用 GetProgramiv(LinkStatus) 查询。
GVTCIER_API void Gvtcier_Rhi_LinkProgram(Program p);
// 查询程序属性，pname 取 LinkStatus/InfoLogLength。
GVTCIER_API void Gvtcier_Rhi_GetProgramiv(Program p, unsigned pname, int* params);
// 取出链接日志，len 可为 nullptr。
GVTCIER_API void Gvtcier_Rhi_GetProgramInfoLog(Program p, int bufSize, int* len, char* log);
// 删除程序，同时释放该程序名下的引擎 UBO。
GVTCIER_API void Gvtcier_Rhi_DeleteProgram(Program p);
// 激活程序，之后的 uniform 上传与绘制都针对它。
GVTCIER_API void Gvtcier_Rhi_UseProgram(Program p);
// 查询 uniform 位置。GL 后端会把 ubo 内成员翻译成带标签的伪位置，
// 之后对应的 Uniform* 调用会改为写 UBO 影子缓冲而不是走 glUniform。
GVTCIER_API Location Gvtcier_Rhi_UniformLocation(Program p, const char* name);
// 上传单整数（常用于 sampler 指定纹理单元）。
GVTCIER_API void Gvtcier_Rhi_Uniform1i(Location l, int v);
// 上传单浮点。
GVTCIER_API void Gvtcier_Rhi_Uniform1f(Location l, float v);
// 上传二维向量。
GVTCIER_API void Gvtcier_Rhi_Uniform2f(Location l, float x, float y);
// 上传三维向量。
GVTCIER_API void Gvtcier_Rhi_Uniform3f(Location l, float x, float y, float z);
// 上传四维向量。
GVTCIER_API void Gvtcier_Rhi_Uniform4f(Location l, float x, float y, float z, float w);
// 上传 count 个 4x4 矩阵，transpose 表示是否转置。
GVTCIER_API void Gvtcier_Rhi_UniformMatrix4fv(Location l, int count, bool transpose, const float* v);

// ---------------------------------------------------------------------------
// 绘制
// ---------------------------------------------------------------------------

// 按顶点数组直接绘制 first 起始的 count 个顶点。
GVTCIER_API void Gvtcier_Rhi_DrawArrays(unsigned mode, int first, int count);
// 用索引缓冲绘制 count 个索引，type 为索引元素类型，indices 为字节偏移或客户端指针。
GVTCIER_API void Gvtcier_Rhi_DrawElements(unsigned mode, int count, unsigned type, const void* indices);
// 实例化索引绘制，同一套索引重复 instanceCount 次。
GVTCIER_API void Gvtcier_Rhi_DrawElementsInstanced(unsigned mode, int count, unsigned type,
                                                   const void* indices, int instanceCount);

// ---------------------------------------------------------------------------
// 帧缓冲与渲染缓冲
// ---------------------------------------------------------------------------

// 生成 n 个帧缓冲对象。
GVTCIER_API void Gvtcier_Rhi_GenFramebuffers(int n, Fbo* out);
// 绑定帧缓冲，f 为 0 表示默认帧缓冲（窗口）。
GVTCIER_API void Gvtcier_Rhi_BindFramebuffer(unsigned target, Fbo f);
// 删除 n 个帧缓冲对象。
GVTCIER_API void Gvtcier_Rhi_DeleteFramebuffers(int n, const Fbo* f);
// 把纹理 t（textarget 指定面或 2D 目标）的 level 级挂到附件的 attachment 上。
GVTCIER_API void Gvtcier_Rhi_FramebufferTexture2D(unsigned target, unsigned attachment, unsigned textarget,
                                                  Texture t, int level);
// 把 3D/2DArray 纹理的某一层挂到附件上，用于级联阴影逐层渲染。
GVTCIER_API void Gvtcier_Rhi_FramebufferTextureLayer(unsigned target, unsigned attachment, Texture t,
                                                     int level, int layer);
// 检查帧缓冲完整性，完整时返回 FramebufferComplete，否则返回驱动给的不完整码。
GVTCIER_API unsigned Gvtcier_Rhi_CheckFramebufferStatus(unsigned target);
// 生成 n 个渲染缓冲对象。
GVTCIER_API void Gvtcier_Rhi_GenRenderbuffers(int n, Rbo* out);
// 绑定渲染缓冲。
GVTCIER_API void Gvtcier_Rhi_BindRenderbuffer(unsigned target, Rbo r);
// 删除 n 个渲染缓冲对象。
GVTCIER_API void Gvtcier_Rhi_DeleteRenderbuffers(int n, const Rbo* r);
// 为渲染缓冲分配不可采样存储，internalformat 决定格式与位深。
GVTCIER_API void Gvtcier_Rhi_RenderbufferStorage(unsigned target, unsigned internalformat, int w, int h);
// 把渲染缓冲挂到帧缓冲附件上，rendertarget 用 Renderbuffer。
GVTCIER_API void Gvtcier_Rhi_FramebufferRenderbuffer(unsigned target, unsigned attachment, unsigned rendertarget,
                                                     Rbo r);

// 兼容（固定管线）上下文管理：主上下文是 4.6 核心，让 glBegin 等旧接口可用需要
// 额外创建并切换到一个 2.1 兼容上下文。GLES/Vulkan 后端不支持，LegacyCreate 直接返回 false。
GVTCIER_API bool Gvtcier_Rhi_LegacyCreate();
GVTCIER_API bool Gvtcier_Rhi_LegacyBind();
GVTCIER_API bool Gvtcier_Rhi_CoreBind();

}
