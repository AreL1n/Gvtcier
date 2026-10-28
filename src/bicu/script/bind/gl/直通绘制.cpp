#include "Gvtcier.h"

// 把脚本传的枚举参数翻译成 rhi/gl1 的整数常量。
// 脚本里既可以写数字（直接当枚举值用），也可以写中英文名字（"三角形"/"triangles"），
// 认不出来时返回 fallback，所以每个调用点都能给出合适的默认值。
// 支持的范围覆盖了图元类型、着色器阶段、缓冲用法、数据类型、缓冲目标、
// 深度/剔除/混合开关、面/绕序/深度函数、多边形模式、混合因子，以及 GL1 固定管线的少量枚举。
static int gl_num(gvts_State* L, int idx, unsigned fallback) {
    if (gvts_isnoneornil(L, idx)) return (int)fallback;
    if (gvts_isnumber(L, idx)) return (int)gvts_tointeger(L, idx);
    const char* s = gvts_tostring(L, idx);
    if (!s) return (int)fallback;
    if (strcmp(s, "三角形") == 0 || strcmp(s, "triangles") == 0) return (int)rhi::Triangles;
    if (strcmp(s, "三角带") == 0 || strcmp(s, "triangle_strip") == 0) return (int)rhi::TriangleStrip;
    if (strcmp(s, "三角扇") == 0 || strcmp(s, "triangle_fan") == 0) return (int)rhi::TriangleFan;
    if (strcmp(s, "线") == 0 || strcmp(s, "lines") == 0) return (int)rhi::Lines;
    if (strcmp(s, "线带") == 0 || strcmp(s, "line_strip") == 0) return (int)rhi::LineStrip;
    if (strcmp(s, "点") == 0 || strcmp(s, "points") == 0) return (int)rhi::Points;
    if (strcmp(s, "顶点着色器") == 0 || strcmp(s, "vertex") == 0) return (int)rhi::VertexShader;
    if (strcmp(s, "片元着色器") == 0 || strcmp(s, "fragment") == 0) return (int)rhi::FragmentShader;
    if (strcmp(s, "静态") == 0 || strcmp(s, "static") == 0) return (int)rhi::StaticDraw;
    if (strcmp(s, "动态") == 0 || strcmp(s, "dynamic") == 0) return (int)rhi::DynamicDraw;
    if (strcmp(s, "浮点") == 0 || strcmp(s, "float") == 0) return (int)rhi::Float;
    if (strcmp(s, "无符号整型") == 0 || strcmp(s, "uint") == 0) return (int)rhi::UnsignedInt;
    if (strcmp(s, "无符号字节") == 0 || strcmp(s, "ubyte") == 0) return (int)rhi::UnsignedByte;
    if (strcmp(s, "无符号短整型") == 0 || strcmp(s, "ushort") == 0) return (int)rhi::UnsignedShort;
    if (strcmp(s, "顶点缓冲") == 0 || strcmp(s, "array") == 0) return (int)rhi::ArrayBuffer;
    if (strcmp(s, "索引缓冲") == 0 || strcmp(s, "element") == 0) return (int)rhi::ElementArrayBuffer;
    if (strcmp(s, "深度测试") == 0 || strcmp(s, "depth_test") == 0) return (int)rhi::DepthTest;
    if (strcmp(s, "剔除") == 0 || strcmp(s, "cull_face") == 0) return (int)rhi::CullFace;
    if (strcmp(s, "混合") == 0 || strcmp(s, "blend") == 0) return (int)rhi::Blend;
    if (strcmp(s, "背面") == 0 || strcmp(s, "back") == 0) return (int)rhi::Back;
    if (strcmp(s, "正面") == 0 || strcmp(s, "front") == 0) return (int)rhi::Front;
    if (strcmp(s, "双面") == 0 || strcmp(s, "front_and_back") == 0) return (int)rhi::FrontAndBack;
    if (strcmp(s, "逆时针") == 0 || strcmp(s, "ccw") == 0) return (int)rhi::Ccw;
    if (strcmp(s, "顺时针") == 0 || strcmp(s, "cw") == 0) return (int)rhi::Cw;
    if (strcmp(s, "小于等于") == 0 || strcmp(s, "lequal") == 0) return (int)rhi::Lequal;
    if (strcmp(s, "小于") == 0 || strcmp(s, "less") == 0) return (int)rhi::Less;
    if (strcmp(s, "总是") == 0 || strcmp(s, "always") == 0) return (int)rhi::Always;
    if (strcmp(s, "填充") == 0 || strcmp(s, "fill") == 0) return (int)rhi::Fill;
    if (strcmp(s, "线框") == 0 || strcmp(s, "line") == 0) return (int)rhi::Line;
    if (strcmp(s, "源透明") == 0 || strcmp(s, "src_alpha") == 0) return (int)rhi::SrcAlpha;
    if (strcmp(s, "一减源透明") == 0 || strcmp(s, "one_minus_src_alpha") == 0) return (int)rhi::OneMinusSrcAlpha;
    if (strcmp(s, "一") == 0 || strcmp(s, "one") == 0) return (int)rhi::One;
    if (strcmp(s, "零") == 0 || strcmp(s, "zero") == 0) return (int)rhi::Zero;
    if (strcmp(s, "四边形") == 0 || strcmp(s, "quads") == 0) return (int)gl1::GL1_QUADS;
    if (strcmp(s, "四边形带") == 0 || strcmp(s, "quad_strip") == 0) return (int)gl1::GL1_QUAD_STRIP;
    if (strcmp(s, "多边形") == 0 || strcmp(s, "polygon") == 0) return (int)gl1::GL1_POLYGON;
    if (strcmp(s, "投影") == 0 || strcmp(s, "projection") == 0) return (int)gl1::GL1_PROJECTION;
    if (strcmp(s, "模型视图") == 0 || strcmp(s, "modelview") == 0) return (int)gl1::GL1_MODELVIEW;
    if (strcmp(s, "纹理矩阵") == 0 || strcmp(s, "texture_matrix") == 0) return (int)gl1::GL1_TEXTURE;
    if (strcmp(s, "平滑") == 0 || strcmp(s, "smooth") == 0) return (int)gl1::GL1_SMOOTH;
    if (strcmp(s, "平面") == 0 || strcmp(s, "flat") == 0) return (int)gl1::GL1_FLAT;
    return (int)fallback;
}

// 是否已经切到 OpenGL 1.1 固定管线模式。切换后脚本可以直接用 glBegin/glVertex 那套老 API。
static bool g_legacyMode = false;
// 直通模式自己维护一份光栅状态，和引擎 3D/2D 的状态互不干扰，
// 每次改字段都整体推给 RHI，所以脚本里的 设置_开启/设置_混合函数 等只影响它自己的状态。
static rhi::RasterState g_glRs;

// 脚本名：执行_开始绘制([版本号])
// 参数：可选整数版本号，100~199 之间表示要 OpenGL 1.x 的固定管线；其他值或省略走现代管线。
// 返回：1 个布尔，成功为真；固定管线加载失败或兼容上下文创建不出来时为假。
// 副作用：置 ctx->onlyGl = true，即告诉引擎"这一帧渲染器不要插手，全部交给脚本"。
// 同时先把 GL 错误队列清空（循环取 error 直到 0），避免把之前的旧错误算到脚本头上。
// 现代管线分支什么都不用建，VAO/着色器都由脚本自己创建。
static int g_gl_begin(gvts_State* L) {
    GvtsEngineCtx* ctx = (GvtsEngineCtx*)gvts_touserdata(L, gvts_upvalueindex(1));
    int ver = (int)gvtsG_optinteger(L, 1, 0);
    ctx->onlyGl = true;
    while (rhi::Gvtcier_Rhi_GetError() != 0) {}
    if (ver >= 100 && ver < 200) {
        if (!gl1::Gvtcier_Gl1_LoadFunctions()) {
            printf(u8"[Gv_only_OpenGL] 固定管线函数不可用\n");
            gvts_pushboolean(L, 0);
            return 1;
        }
        if (!rhi::Gvtcier_Rhi_LegacyCreate() || !rhi::Gvtcier_Rhi_LegacyBind()) {
            printf(u8"[Gv_only_OpenGL] 兼容上下文不可用，回退现代管线\n");
            g_legacyMode = false;
            gvts_pushboolean(L, 0);
            return 1;
        }
        g_legacyMode = true;
        printf(u8"[Gv_only_OpenGL] 已进入 OpenGL 1.1 固定管线模式\n");
        gvts_pushboolean(L, 1);
        return 1;
    }
    g_legacyMode = false;
    gvts_pushboolean(L, 1);
    return 1;
}

// 脚本名：设置_视口([x[, y[, w[, h]]]])
// 参数：左下角坐标默认 0；宽高必填（第 3、4 参）。
// 返回：无。调用：rhi::Gvtcier_Rhi_Viewport。
static int g_gl_viewport(gvts_State* L) {
    int x = (int)gvtsG_optinteger(L, 1, 0);
    int y = (int)gvtsG_optinteger(L, 2, 0);
    int w = (int)gvtsG_checkinteger(L, 3);
    int h = (int)gvtsG_checkinteger(L, 4);
    rhi::Gvtcier_Rhi_Viewport(x, y, w, h);
    return 0;
}

// 脚本名：设置_清屏颜色([r[, g[, b[, a]]]])
// 参数：四个分量默认 0,0,0,1。返回：无。
// 只设置颜色，不真正清屏，清屏要调 执行_清屏。
static int g_gl_clearColor(gvts_State* L) {
    float r = (float)gvtsG_optnumber(L, 1, 0.0);
    float g = (float)gvtsG_optnumber(L, 2, 0.0);
    float b = (float)gvtsG_optnumber(L, 3, 0.0);
    float a = (float)gvtsG_optnumber(L, 4, 1.0);
    rhi::Gvtcier_Rhi_ClearColor(r, g, b, a);
    return 0;
}

// 脚本名：执行_清屏([清颜色[, 清深度]])
// 参数：两个布尔，缺省都是真（不传就两个都清）。
// 返回：无。调用：rhi::Gvtcier_Rhi_Clear，掩码按参数拼出来。
static int g_gl_clear(gvts_State* L) {
    bool color = gvts_isnoneornil(L, 1) ? true : (gvts_toboolean(L, 1) != 0);
    bool depth = gvts_isnoneornil(L, 2) ? true : (gvts_toboolean(L, 2) != 0);
    unsigned mask = 0;
    if (color) mask |= rhi::ColorBit;
    if (depth) mask |= rhi::DepthBit;
    rhi::Gvtcier_Rhi_Clear(mask);
    return 0;
}

// 脚本名：设置_开启(开关名) / 设置_关闭(开关名)
// 参数：开关名，可识别 "深度测试"/"剔除"/"裁剪"/"混合"（也接受 100~103 的数字）。
// 返回：无。
// 实现说明：这里用 100/101/102/103 这组魔法编号来做分支，
// 是因为 gl_num 没给这几个能力名定义常量，脚本传中文时 gl_num 会落到 fallback 回到 Lequal 之类，
// 传数字时就直接按编号匹配。改这里要连同 gl_num 的解析表一起看。
// "裁剪" 对应 scissor，只打开扫描裁剪的开关，具体矩形还需别的方式设置。
static int g_gl_enable(gvts_State* L) {
    unsigned cap = (unsigned)gl_num(L, 1, rhi::Lequal);
    if (cap == 100) g_glRs.depthTest = true;
    else if (cap == 101) g_glRs.cull = true;
    else if (cap == 102) g_glRs.scissor = true;
    else if (cap == 103) g_glRs.blend = true;
    rhi::Gvtcier_Rhi_SetRasterState(&g_glRs);
    return 0;
}

// 与 设置_开启 对称，把对应开关关掉。
static int g_gl_disable(gvts_State* L) {
    unsigned cap = (unsigned)gl_num(L, 1, rhi::Lequal);
    if (cap == 100) g_glRs.depthTest = false;
    else if (cap == 101) g_glRs.cull = false;
    else if (cap == 102) g_glRs.scissor = false;
    else if (cap == 103) g_glRs.blend = false;
    rhi::Gvtcier_Rhi_SetRasterState(&g_glRs);
    return 0;
}

// 脚本名：设置_深度函数(比较方式)。参数：如"小于"/"小于等于"/"总是"。返回：无。
static int g_gl_depthFunc(gvts_State* L) {
    g_glRs.depthFunc = (unsigned)gl_num(L, 1, rhi::Lequal);
    rhi::Gvtcier_Rhi_SetRasterState(&g_glRs);
    return 0;
}

// 脚本名：设置_深度写入(开关)。参数：布尔。返回：无。关掉后不再更新深度缓冲，透明物体常这么用。
static int g_gl_depthMask(gvts_State* L) {
    g_glRs.depthWrite = gvts_toboolean(L, 1) != 0;
    rhi::Gvtcier_Rhi_SetRasterState(&g_glRs);
    return 0;
}

// 脚本名：设置_剔除面(面)。参数："背面"/"正面"/"双面"。返回：无。需配合 设置_开启("剔除") 才生效。
static int g_gl_cullFace(gvts_State* L) {
    g_glRs.cullMode = (unsigned)gl_num(L, 1, rhi::Back);
    rhi::Gvtcier_Rhi_SetRasterState(&g_glRs);
    return 0;
}

// 脚本名：设置_正面朝向(绕序)。参数："逆时针"/"顺时针"。返回：无。决定哪面算正面。
static int g_gl_frontFace(gvts_State* L) {
    g_glRs.frontFace = (unsigned)gl_num(L, 1, rhi::Ccw);
    rhi::Gvtcier_Rhi_SetRasterState(&g_glRs);
    return 0;
}

// 脚本名：设置_混合函数(源因子[, 目标因子])
// 参数：如"源透明"/"一减源透明"/"一"/"零"；目标因子缺省为"一减源透明"。返回：无。
static int g_gl_blendFunc(gvts_State* L) {
    g_glRs.blendSrc = (unsigned)gl_num(L, 1, rhi::SrcAlpha);
    g_glRs.blendDst = (unsigned)gl_num(L, 2, rhi::OneMinusSrcAlpha);
    rhi::Gvtcier_Rhi_SetRasterState(&g_glRs);
    return 0;
}

// 脚本名：设置_多边形模式(填充或线框)
// 参数：脚本约定第 1 参是目标（被忽略），第 2 参才是模式（"填充"/"线框"）。返回：无。
// 注意读的是第 2 个参数，写脚本时别漏掉第一个。
static int g_gl_polygonMode(gvts_State* L) {
    g_glRs.polygonMode = (unsigned)gl_num(L, 2, rhi::Fill);
    rhi::Gvtcier_Rhi_SetRasterState(&g_glRs);
    return 0;
}

// 脚本名：创建_着色器(类型, 源码)
// 参数：类型 "顶点着色器"/"片元着色器"（或 "vertex"/"fragment"）；GLSL 源码字符串。
// 返回：1 个整数句柄，编译失败返回 0 并把 info log 打到控制台。
// 调用：rhi::Gvtcier_Rhi_CreateShader / ShaderSource / CompileShader。
static int g_gl_createShader(gvts_State* L) {
    unsigned type = (unsigned)gl_num(L, 1, rhi::VertexShader);
    const char* src = gvtsG_checkstring(L, 2);
    rhi::Stage sh = rhi::Gvtcier_Rhi_CreateShader(type);
    rhi::Gvtcier_Rhi_ShaderSource(sh, 1, &src, nullptr);
    rhi::Gvtcier_Rhi_CompileShader(sh);
    int ok = 0;
    rhi::Gvtcier_Rhi_GetShaderiv(sh, rhi::CompileStatus, &ok);
    if (!ok) {
        int len = 0;
        rhi::Gvtcier_Rhi_GetShaderiv(sh, rhi::InfoLogLength, &len);
        std::vector<char> buf((size_t)(len > 0 ? len : 1), 0);
        int got = 0;
        rhi::Gvtcier_Rhi_GetShaderInfoLog(sh, (int)buf.size(), &got, buf.data());
        printf(u8"[Gv_only_OpenGL] 着色器编译失败: %s\n", buf.data());
        rhi::Gvtcier_Rhi_DeleteShader(sh);
        gvts_pushinteger(L, 0);
        return 1;
    }
    gvts_pushinteger(L, (gvts_Integer)sh);
    return 1;
}

// 脚本名：创建_程序(顶点着色器句柄, 片元着色器句柄)
// 参数：两个从句柄（0 表示不挂），允许只挂一个。
// 返回：1 个整数程序句柄；链接失败返回 0，并把 info log 打出来。
// 副作用：链接完成后会删掉传入的两个着色器对象（链接只是引用，删除是安全的），
// 所以脚本拿到程序后不必再管着色器，也不能重复用同一个着色器再建第二个程序。
static int g_gl_createProgram(gvts_State* L) {
    rhi::Stage vs = (rhi::Stage)gvtsG_checkinteger(L, 1);
    rhi::Stage fs = (rhi::Stage)gvtsG_checkinteger(L, 2);
    rhi::Program p = rhi::Gvtcier_Rhi_CreateProgram();
    if (vs) rhi::Gvtcier_Rhi_AttachShader(p, vs);
    if (fs) rhi::Gvtcier_Rhi_AttachShader(p, fs);
    rhi::Gvtcier_Rhi_LinkProgram(p);
    int ok = 0;
    rhi::Gvtcier_Rhi_GetProgramiv(p, rhi::LinkStatus, &ok);
    if (!ok) {
        int len = 0;
        rhi::Gvtcier_Rhi_GetProgramiv(p, rhi::InfoLogLength, &len);
        std::vector<char> buf((size_t)(len > 0 ? len : 1), 0);
        int got = 0;
        rhi::Gvtcier_Rhi_GetProgramInfoLog(p, (int)buf.size(), &got, buf.data());
        printf(u8"[Gv_only_OpenGL] 程序链接失败: %s\n", buf.data());
        rhi::Gvtcier_Rhi_DeleteProgram(p);
        if (vs) rhi::Gvtcier_Rhi_DeleteShader(vs);
        if (fs) rhi::Gvtcier_Rhi_DeleteShader(fs);
        gvts_pushinteger(L, 0);
        return 1;
    }
    if (vs) rhi::Gvtcier_Rhi_DeleteShader(vs);
    if (fs) rhi::Gvtcier_Rhi_DeleteShader(fs);
    gvts_pushinteger(L, (gvts_Integer)p);
    return 1;
}

// 脚本名：设置_使用程序(程序句柄)。参数：句柄，0 表示解绑。返回：无。
static int g_gl_useProgram(gvts_State* L) {
    rhi::Gvtcier_Rhi_UseProgram((rhi::Program)gvtsG_checkinteger(L, 1));
    return 0;
}

// 脚本名：删除_程序(程序句柄)。参数：句柄。返回：无。
static int g_gl_deleteProgram(gvts_State* L) {
    rhi::Gvtcier_Rhi_DeleteProgram((rhi::Program)gvtsG_checkinteger(L, 1));
    return 0;
}

// 脚本名：创建_VAO()。参数：无。返回：1 个整数 VAO 句柄。
static int g_gl_createVao(gvts_State* L) {
    rhi::Vao v = 0;
    rhi::Gvtcier_Rhi_GenVertexArrays(1, &v);
    gvts_pushinteger(L, (gvts_Integer)v);
    return 1;
}

// 脚本名：设置_绑定VAO(句柄)。参数：VAO 句柄。返回：无。
static int g_gl_bindVao(gvts_State* L) {
    rhi::Gvtcier_Rhi_BindVertexArray((rhi::Vao)gvtsG_checkinteger(L, 1));
    return 0;
}

// 脚本名：删除_VAO(句柄)。参数：VAO 句柄。返回：无。
static int g_gl_deleteVao(gvts_State* L) {
    rhi::Vao v = (rhi::Vao)gvtsG_checkinteger(L, 1);
    rhi::Gvtcier_Rhi_DeleteVertexArrays(1, &v);
    return 0;
}

// 脚本名：创建_缓冲()。参数：无。返回：1 个整数缓冲句柄。
static int g_gl_createBuffer(gvts_State* L) {
    rhi::Buffer b = 0;
    rhi::Gvtcier_Rhi_GenBuffers(1, &b);
    gvts_pushinteger(L, (gvts_Integer)b);
    return 1;
}

// 脚本名：设置_绑定缓冲(目标, 缓冲句柄)
// 参数：目标"顶点缓冲"/"索引缓冲"（或 "array"/"element"），缺省顶点缓冲；句柄。返回：无。
static int g_gl_bindBuffer(gvts_State* L) {
    rhi::Gvtcier_Rhi_BindBuffer((unsigned)gl_num(L, 1, rhi::ArrayBuffer),
                                (rhi::Buffer)gvtsG_checkinteger(L, 2));
    return 0;
}

// 脚本名：删除_缓冲(句柄)。参数：缓冲句柄。返回：无。
static int g_gl_deleteBuffer(gvts_State* L) {
    rhi::Buffer b = (rhi::Buffer)gvtsG_checkinteger(L, 1);
    rhi::Gvtcier_Rhi_DeleteBuffers(1, &b);
    return 0;
}

// 脚本名：设置_缓冲数据(目标, 浮点数组[, 用法])
// 参数：目标缺省顶点缓冲；数组是 gvts 表（每个元素转成 float）；用法"静态"/"动态"，缺省静态。
// 返回：无。
// 数组按 1 起始下标读，元素个数由 rawlen 决定，空表也合法（只分配空间不填内容）。
static int g_gl_bufferData(gvts_State* L) {
    unsigned target = (unsigned)gl_num(L, 1, rhi::ArrayBuffer);
    gvtsG_checktype(L, 2, GVTS_TTABLE);
    int n = (int)gvts_rawlen(L, 2);
    std::vector<float> data((size_t)n);
    for (int i = 0; i < n; ++i) {
        gvts_rawgeti(L, 2, i + 1);
        data[(size_t)i] = (float)gvts_tonumber(L, -1);
        gvts_pop(L, 1);
    }
    unsigned usage = (unsigned)gl_num(L, 3, rhi::StaticDraw);
    rhi::Gvtcier_Rhi_BufferData(target, data.size() * sizeof(float), data.empty() ? nullptr : data.data(), usage);
    return 0;
}

// 脚本名：设置_缓冲字节(目标, 字节数组[, 用法])
// 参数：同 设置_缓冲数据，但每个元素按 0~255 截断成单字节。返回：无。
// 这是给索引缓冲传 8 位索引用的；每个元素都会 &0xFF，传负数也能得到确定的字节值。
static int g_gl_bufferDataBytes(gvts_State* L) {
    unsigned target = (unsigned)gl_num(L, 1, rhi::ArrayBuffer);
    gvtsG_checktype(L, 2, GVTS_TTABLE);
    int n = (int)gvts_rawlen(L, 2);
    std::vector<unsigned char> data((size_t)n);
    for (int i = 0; i < n; ++i) {
        gvts_rawgeti(L, 2, i + 1);
        int v = (int)gvts_tointeger(L, -1);
        data[(size_t)i] = (unsigned char)(v & 0xFF);
        gvts_pop(L, 1);
    }
    unsigned usage = (unsigned)gl_num(L, 3, rhi::StaticDraw);
    rhi::Gvtcier_Rhi_BufferData(target, data.size(), data.empty() ? nullptr : data.data(), usage);
    return 0;
}

// 脚本名：设置_缓冲整数(目标, 整数数组[, 用法])
// 参数：目标缺省是索引缓冲（与浮点版默认顶点缓冲不同）；元素转 32 位无符号整型。返回：无。
// 脚本里的索引通常是 1 起始的直观写法，传进来后原样写入，不做减一，所以脚本要自己从 0 开始编号。
static int g_gl_bufferDataUint(gvts_State* L) {
    unsigned target = (unsigned)gl_num(L, 1, rhi::ElementArrayBuffer);
    gvtsG_checktype(L, 2, GVTS_TTABLE);
    int n = (int)gvts_rawlen(L, 2);
    std::vector<unsigned int> data((size_t)n);
    for (int i = 0; i < n; ++i) {
        gvts_rawgeti(L, 2, i + 1);
        data[(size_t)i] = (unsigned int)gvts_tointeger(L, -1);
        gvts_pop(L, 1);
    }
    unsigned usage = (unsigned)gl_num(L, 3, rhi::StaticDraw);
    rhi::Gvtcier_Rhi_BufferData(target, data.size() * sizeof(unsigned int),
                                data.empty() ? nullptr : data.data(), usage);
    return 0;
}

// 脚本名：设置_顶点属性(索引, 分量数, 类型, 是否归一化, 步长[, 偏移])
// 参数：索引是 attribute location；类型如"浮点"/"无符号字节"；步长与偏移单位是字节。
// 返回：无。调用：rhi::Gvtcier_Rhi_VertexAttribPointer，偏移直接转成指针。
// 这个函数同时适用于普通顶点属性与实例属性，实例化所需的 divisor 由别的接口设置。
static int g_gl_attribPointer(gvts_State* L) {
    unsigned index = (unsigned)gvtsG_checkinteger(L, 1);
    int size = (int)gvtsG_checkinteger(L, 2);
    unsigned type = (unsigned)gl_num(L, 3, rhi::Float);
    bool normalized = gvts_toboolean(L, 4) != 0;
    int stride = (int)gvtsG_checkinteger(L, 5);
    int offset = (int)gvtsG_optinteger(L, 6, 0);
    rhi::Gvtcier_Rhi_VertexAttribPointer(index, size, type, normalized, stride,
                                         (const void*)(ptrdiff_t)offset);
    return 0;
}

// 脚本名：设置_启用属性(索引)。参数：attribute location。返回：无。
static int g_gl_enableAttrib(gvts_State* L) {
    rhi::Gvtcier_Rhi_EnableAttrib((unsigned)gvtsG_checkinteger(L, 1));
    return 0;
}

// 脚本名：设置_浮点(程序句柄, uniform名, 值)
// 返回：无。每次调用都会重新查一次 uniform 位置，脚本高频调用时性能一般，
// 但换来的是脚本不用自己缓存位置，写起来简单。
static int g_gl_uniform1f(gvts_State* L) {
    rhi::Program p = (rhi::Program)gvtsG_checkinteger(L, 1);
    const char* name = gvtsG_checkstring(L, 2);
    rhi::Gvtcier_Rhi_Uniform1f(rhi::Gvtcier_Rhi_UniformLocation(p, name), (float)gvtsG_checknumber(L, 3));
    return 0;
}

// 脚本名：设置_整数(程序句柄, uniform名, 整数值)。返回：无。常用来绑采样器纹理单元。
static int g_gl_uniform1i(gvts_State* L) {
    rhi::Program p = (rhi::Program)gvtsG_checkinteger(L, 1);
    const char* name = gvtsG_checkstring(L, 2);
    rhi::Gvtcier_Rhi_Uniform1i(rhi::Gvtcier_Rhi_UniformLocation(p, name), (int)gvtsG_checkinteger(L, 3));
    return 0;
}

// 脚本名：设置_三维(程序句柄, uniform名, x, y, z)。返回：无。
static int g_gl_uniform3f(gvts_State* L) {
    rhi::Program p = (rhi::Program)gvtsG_checkinteger(L, 1);
    const char* name = gvtsG_checkstring(L, 2);
    rhi::Gvtcier_Rhi_Uniform3f(rhi::Gvtcier_Rhi_UniformLocation(p, name),
                               (float)gvtsG_checknumber(L, 3),
                               (float)gvtsG_checknumber(L, 4),
                               (float)gvtsG_checknumber(L, 5));
    return 0;
}

// 脚本名：设置_矩阵(程序句柄, uniform名, 16 元素数组)
// 返回：无。数组按列主序读，与 MathGv 里矩阵的存储顺序一致。
// 上传前先清空 GL 错误队列：uniform 名不存在时会产生一个 GL_INVALID_OPERATION，
// 不清掉的话下一次 获取_错误码 会拿到这个与本次无关的旧错误。
static int g_gl_uniformMat4(gvts_State* L) {
    rhi::Program p = (rhi::Program)gvtsG_checkinteger(L, 1);
    const char* name = gvtsG_checkstring(L, 2);
    gvtsG_checktype(L, 3, GVTS_TTABLE);
    float m[16];
    for (int i = 0; i < 16; ++i) {
        gvts_rawgeti(L, 3, i + 1);
        m[i] = (float)gvts_tonumber(L, -1);
        gvts_pop(L, 1);
    }
    while (rhi::Gvtcier_Rhi_GetError() != 0) {}
    rhi::Location loc = rhi::Gvtcier_Rhi_UniformLocation(p, name);
    rhi::Gvtcier_Rhi_UniformMatrix4fv(loc, 1, false, m);
    return 0;
}

// 脚本名：绘制_数组(图元[, 起点[, 数量]])
// 参数：图元类型缺省"三角形"；起点缺省 0；数量必填。返回：无。
static int g_gl_drawArrays(gvts_State* L) {
    unsigned mode = (unsigned)gl_num(L, 1, rhi::Triangles);
    int first = (int)gvtsG_optinteger(L, 2, 0);
    int count = (int)gvtsG_checkinteger(L, 3);
    rhi::Gvtcier_Rhi_DrawArrays(mode, first, count);
    return 0;
}

// 脚本名：绘制_索引(图元, 索引数量[, 索引类型[, 偏移]])
// 参数：索引类型缺省"无符号整型"（32 位）；偏移单位是字节。返回：无。
static int g_gl_drawElements(gvts_State* L) {
    unsigned mode = (unsigned)gl_num(L, 1, rhi::Triangles);
    int count = (int)gvtsG_checkinteger(L, 2);
    unsigned type = (unsigned)gl_num(L, 3, rhi::UnsignedInt);
    int offset = (int)gvtsG_optinteger(L, 4, 0);
    rhi::Gvtcier_Rhi_DrawElements(mode, count, type, (const void*)(ptrdiff_t)offset);
    return 0;
}

// 脚本名：获取_错误码()
// 参数：无。返回：1 个整数 GL 错误码，0 表示没有错误。
// GL 的错误是"读取即清除"的语义，所以脚本第一次拿到错误后必须再调一次确认清零。
static int g_gl_getError(gvts_State* L) {
    gvts_pushinteger(L, (gvts_Integer)rhi::Gvtcier_Rhi_GetError());
    return 1;
}

// 以下是从这里开始是固定管线（GL 1.x）相关接口，只有 执行_开始绘制(110) 成功后才可用。
// 固定管线可用 会在没有进入兼容上下文时返回假，脚本可以先探测再决定走哪条渲染路径。

// 脚本名：绘制图元(图元类型, 点1, 点2, ...)
// 参数：图元类型（缺省三角形）；后面每个参数都是一张含 x,y,z 三元素的小表。
// 返回：无。
// 内部等价于 glBegin + 逐个 glVertex3f + glEnd，所以必须在 glBegin/glEnd 的语义范围内一次调用传完所有点，
// 不像现代管线那样可以分批。固定管线不可用时静默返回。
static int g_gl1_prim(gvts_State* L) {
    if (!gl1::Gvtcier_Gl1_Available()) return 0;
    int m = gl_num(L, 1, gl1::GL1_TRIANGLES);
    gl1::g.glBegin((unsigned)m);
    int n = gvts_gettop(L);
    for (int i = 2; i <= n; ++i) {
        if (!gvts_istable(L, i)) continue;
        gvts_rawgeti(L, i, 1);
        gvts_rawgeti(L, i, 2);
        gvts_rawgeti(L, i, 3);
        float x = (float)gvts_tonumber(L, -3);
        float y = (float)gvts_tonumber(L, -2);
        float z = (float)gvts_tonumber(L, -1);
        gvts_pop(L, 3);
        gl1::g.glVertex3f(x, y, z);
    }
    gl1::g.glEnd();
    return 0;
}

// 脚本名：顶点(x, y[, z])。参数：坐标，z 缺省 0。返回：无。只能在 绘制图元 的实现内部使用，
// 脚本单独调用它不会产生任何结果，因为外面没有 begin/end。
static int g_gl1_vertex(gvts_State* L) {
    gl1::g.glVertex3f((float)gvtsG_checknumber(L, 1),
                      (float)gvtsG_checknumber(L, 2),
                      (float)gvtsG_optnumber(L, 3, 0.0));
    return 0;
}

// 脚本名：颜色(r, g, b[, a])。参数：分量，a 缺省 1。返回：无。设置后续顶点的当前颜色。
static int g_gl1_color(gvts_State* L) {
    gl1::g.glColor4f((float)gvtsG_checknumber(L, 1),
                     (float)gvtsG_checknumber(L, 2),
                     (float)gvtsG_checknumber(L, 3),
                     (float)gvtsG_optnumber(L, 4, 1.0));
    return 0;
}

// 脚本名：法线(x, y, z)。参数：法线向量。返回：无。设置后续顶点的当前法线，光照用它。
static int g_gl1_normal(gvts_State* L) {
    gl1::g.glNormal3f((float)gvtsG_checknumber(L, 1),
                      (float)gvtsG_checknumber(L, 2),
                      (float)gvtsG_checknumber(L, 3));
    return 0;
}

// 脚本名：矩阵模式(模式)。参数："模型视图"/"投影"/"纹理矩阵"，缺省模型视图。返回：无。
static int g_gl1_matrixMode(gvts_State* L) {
    gl1::g.glMatrixMode((unsigned)gl_num(L, 1, gl1::GL1_MODELVIEW));
    return 0;
}

// 矩阵栈操作：单位阵 / 压栈 / 弹栈。都是无参无返回，作用于当前矩阵模式。
static int g_gl1_loadIdentity(gvts_State* L) { gl1::g.glLoadIdentity(); return 0; }

static int g_gl1_pushMatrix(gvts_State* L) { gl1::g.glPushMatrix(); return 0; }

static int g_gl1_popMatrix(gvts_State* L) { gl1::g.glPopMatrix(); return 0; }

// 脚本名：平移(x, y, z)。返回：无。在当前矩阵右乘一个平移。
static int g_gl1_translate(gvts_State* L) {
    gl1::g.glTranslatef((float)gvtsG_checknumber(L, 1),
                        (float)gvtsG_checknumber(L, 2),
                        (float)gvtsG_checknumber(L, 3));
    return 0;
}

// 脚本名：旋转(角度, x, y, z)。参数：角度（度）与旋转轴。返回：无。
static int g_gl1_rotate(gvts_State* L) {
    gl1::g.glRotatef((float)gvtsG_checknumber(L, 1),
                     (float)gvtsG_checknumber(L, 2),
                     (float)gvtsG_checknumber(L, 3),
                     (float)gvtsG_checknumber(L, 4));
    return 0;
}

// 脚本名：缩放(x[, y[, z]])。参数：y、z 缺省都等于 x，所以传一个数是等比缩放。
static int g_gl1_scale(gvts_State* L) {
    float x = (float)gvtsG_checknumber(L, 1);
    gl1::g.glScalef(x, (float)gvtsG_optnumber(L, 2, x), (float)gvtsG_optnumber(L, 3, x));
    return 0;
}

// 脚本名：透视(视场角, 宽高比, 近裁面, 远裁面)
// 参数：视场角是垂直方向的度数。返回：无。
// 内部先用 tan(fov/2) 算出近平面半高，再乘宽高比得到半宽，最终调 glFrustum。
// 360 度是"度数折半再转弧度"的一次性写法（fov * pi / 360 = fov/2 * pi/180）。
static int g_gl1_perspective(gvts_State* L) {
    float fov = (float)gvtsG_checknumber(L, 1);
    float aspect = (float)gvtsG_checknumber(L, 2);
    float zn = (float)gvtsG_checknumber(L, 3);
    float zf = (float)gvtsG_checknumber(L, 4);
    double top = (double)zn * std::tan((double)fov * 3.14159265358979323846 / 360.0);
    double right = top * (double)aspect;
    gl1::g.glFrustum(-right, right, -top, top, (double)zn, (double)zf);
    return 0;
}

// 脚本名：观察(eyeX, eyeY, eyeZ, 目标X, 目标Y, 目标Z[, 上X, 上Y, 上Z])
// 参数：相机位置与注视点；上方向可省略，省略时用 (0,1,0)。
// 返回：无。
// 实现说明：没有直接调某个 glLookAt（固定管线接口里不一定有），而是自己算右手视图矩阵再 glMultMatrixf。
// 三个基向量的算法是：f=归一化(目标-eye)，s=归一化(f×up)，u=s×f，然后按行填进视图矩阵。
// eye 与目标重合（长度过小）时直接返回；up 与 f 平行导致 s 为零向量时保持未归一化的值，视图矩阵会退化。
static int g_gl1_lookAt(gvts_State* L) {
    float ex = (float)gvtsG_checknumber(L, 1);
    float ey = (float)gvtsG_checknumber(L, 2);
    float ez = (float)gvtsG_checknumber(L, 3);
    float cx = (float)gvtsG_checknumber(L, 4);
    float cy = (float)gvtsG_checknumber(L, 5);
    float cz = (float)gvtsG_checknumber(L, 6);
    float fx = cx - ex, fy = cy - ey, fz = cz - ez;
    float fl = std::sqrt(fx * fx + fy * fy + fz * fz);
    if (fl < 1e-6f) return 0;
    fx /= fl; fy /= fl; fz /= fl;
    float ux = 0.0f, uy = 1.0f, uz = 0.0f;
    if (gvts_gettop(L) >= 9) {
        ux = (float)gvtsG_checknumber(L, 7);
        uy = (float)gvtsG_checknumber(L, 8);
        uz = (float)gvtsG_checknumber(L, 9);
    }
    float sx = fy * uz - fz * uy, sy = fz * ux - fx * uz, sz = fx * uy - fy * ux;
    float sl = std::sqrt(sx * sx + sy * sy + sz * sz);
    if (sl > 1e-6f) { sx /= sl; sy /= sl; sz /= sl; }
    float ux2 = sy * fz - sz * fy, uy2 = sz * fx - sx * fz, uz2 = sx * fy - sy * fx;
    float m[16];
    m[0] = sx; m[4] = sy; m[8] = sz; m[12] = -(sx * ex + sy * ey + sz * ez);
    m[1] = ux2; m[5] = uy2; m[9] = uz2; m[13] = -(ux2 * ex + uy2 * ey + uz2 * ez);
    m[2] = -fx; m[6] = -fy; m[10] = -fz; m[14] = (fx * ex + fy * ey + fz * ez);
    m[3] = 0; m[7] = 0; m[11] = 0; m[15] = 1.0f;
    gl1::g.glMultMatrixf(m);
    return 0;
}

// 脚本名：加载矩阵(16 元素数组)。返回：无。替换当前矩阵，数组按列主序。
static int g_gl1_loadMatrix(gvts_State* L) {
    gvtsG_checktype(L, 1, GVTS_TTABLE);
    float m[16];
    for (int i = 0; i < 16; ++i) {
        gvts_rawgeti(L, 1, i + 1);
        m[i] = (float)gvts_tonumber(L, -1);
        gvts_pop(L, 1);
    }
    gl1::g.glLoadMatrixf(m);
    return 0;
}

// 脚本名：乘矩阵(16 元素数组)。返回：无。在当前矩阵上右乘，常用于叠加自定义变换。
static int g_gl1_multMatrix(gvts_State* L) {
    gvtsG_checktype(L, 1, GVTS_TTABLE);
    float m[16];
    for (int i = 0; i < 16; ++i) {
        gvts_rawgeti(L, 1, i + 1);
        m[i] = (float)gvts_tonumber(L, -1);
        gvts_pop(L, 1);
    }
    gl1::g.glMultMatrixf(m);
    return 0;
}

// 脚本名：着色模式(模式)。参数："平滑"/"平面"，缺省平滑。返回：无。
// 平滑是顶点颜色插值，平面是每个三角形取最后一个顶点的颜色。
static int g_gl1_shadeModel(gvts_State* L) {
    gl1::g.glShadeModel((unsigned)gl_num(L, 1, gl1::GL1_SMOOTH));
    return 0;
}

// 脚本名：线宽(像素)。返回：无。固定管线的线宽上限由驱动决定，通常只有 1 像素。
static int g_gl1_lineWidth(gvts_State* L) {
    gl1::g.glLineWidth((float)gvtsG_checknumber(L, 1));
    return 0;
}

// 脚本名：点大小(像素)。返回：无。
static int g_gl1_pointSize(gvts_State* L) {
    gl1::g.glPointSize((float)gvtsG_checknumber(L, 1));
    return 0;
}

// 脚本名：固定管线可用()。参数：无。返回：1 个布尔，表示当前上下文里能否调 GL1 那批接口。
static int g_gl1_available(gvts_State* L) {
    gvts_pushboolean(L, gl1::Gvtcier_Gl1_Available() ? 1 : 0);
    return 1;
}

// 脚本名：获取_读像素([x[, y[, 宽, 高]]])
// 参数：读取区域，x/y 缺省 0，宽高必填（像素，原点在左下角）。
// 返回：1 个整数数组，RGBA 四个分量依次平铺（0~255），读失败或宽高非正时返回空表（1 个空表值）。
// 注意这是同步读回，会产生管线停顿，逐帧调用会明显掉帧，只适合截图与测试用。
static int g_gl_readPixels(gvts_State* L) {
    int x = (int)gvtsG_optinteger(L, 1, 0);
    int y = (int)gvtsG_optinteger(L, 2, 0);
    int w = (int)gvtsG_checkinteger(L, 3);
    int h = (int)gvtsG_checkinteger(L, 4);
    if (w <= 0 || h <= 0) { gvts_newtable(L); return 1; }
    std::vector<unsigned char> px((size_t)w * (size_t)h * 4u);
    rhi::Gvtcier_Rhi_ReadPixels(x, y, w, h, rhi::Rgba, rhi::UnsignedByte, px.data());
    gvts_newtable(L);
    for (size_t i = 0; i < px.size(); ++i) {
        gvts_pushinteger(L, px[i]);
        gvts_rawseti(L, -2, (int)i + 1);
    }
    return 1;
}

static const gvtsG_Reg g_onlyGlLib[] = {
    { "开始", g_gl_begin },
    { "视口", g_gl_viewport },
    { "清屏颜色", g_gl_clearColor },
    { "清屏", g_gl_clear },
    { "开启", g_gl_enable },
    { "关闭", g_gl_disable },
    { "深度函数", g_gl_depthFunc },
    { "深度写入", g_gl_depthMask },
    { "剔除面", g_gl_cullFace },
    { "正面朝向", g_gl_frontFace },
    { "混合函数", g_gl_blendFunc },
    { "多边形模式", g_gl_polygonMode },
    { "创建着色器", g_gl_createShader },
    { "创建程序", g_gl_createProgram },
    { "使用程序", g_gl_useProgram },
    { "释放程序", g_gl_deleteProgram },
    { "创建VAO", g_gl_createVao },
    { "绑定VAO", g_gl_bindVao },
    { "释放VAO", g_gl_deleteVao },
    { "创建缓冲", g_gl_createBuffer },
    { "绑定缓冲", g_gl_bindBuffer },
    { "释放缓冲", g_gl_deleteBuffer },
    { "缓冲数据", g_gl_bufferData },
    { "缓冲字节", g_gl_bufferDataBytes },
    { "缓冲整数", g_gl_bufferDataUint },
    { "顶点属性", g_gl_attribPointer },
    { "启用属性", g_gl_enableAttrib },
    { "设置浮点", g_gl_uniform1f },
    { "设置整数", g_gl_uniform1i },
    { "设置三维", g_gl_uniform3f },
    { "设置矩阵", g_gl_uniformMat4 },
    { "绘制数组", g_gl_drawArrays },
    { "绘制索引", g_gl_drawElements },
    { "取错误", g_gl_getError },
    { "读像素", g_gl_readPixels },
    { "固定管线可用", g_gl1_available },
    { "绘制图元", g_gl1_prim },
    { "顶点", g_gl1_vertex },
    { "颜色", g_gl1_color },
    { "法线", g_gl1_normal },
    { "矩阵模式", g_gl1_matrixMode },
    { "单位阵", g_gl1_loadIdentity },
    { "压入矩阵", g_gl1_pushMatrix },
    { "弹出矩阵", g_gl1_popMatrix },
    { "平移", g_gl1_translate },
    { "旋转", g_gl1_rotate },
    { "缩放", g_gl1_scale },
    { "透视", g_gl1_perspective },
    { "观察", g_gl1_lookAt },
    { "加载矩阵", g_gl1_loadMatrix },
    { "乘矩阵", g_gl1_multMatrix },
    { "着色模式", g_gl1_shadeModel },
    { "线宽", g_gl1_lineWidth },
    { "点大小", g_gl1_pointSize },
    { NULL, NULL }
};

void openOnlyGlLib(gvts_State* L, GvtsEngineCtx* ctx) {
    gvts_newtable(L);
    for (const gvtsG_Reg* r = g_onlyGlLib; r->name; ++r) {
        gvts_pushlightuserdata(L, ctx);
        gvts_pushcclosure(L, r->func, 1);
        gvts_setfield(L, -2, r->name);
    }
    gvts_setglobal(L, "Gv_only_OpenGL");
}

void registerOnlyGlCaps(gvts_State* L, GvtsEngineCtx* ctx) {
    regCap(L, ctx, u8"图形底层", u8"执行_开始绘制", u8"进入纯 OpenGL 直通模式，关闭引擎一切渲染", g_gl_begin);
    regCap(L, ctx, u8"图形底层", u8"设置_视口", u8"设置 OpenGL 视口 x y w h", g_gl_viewport);
    regCap(L, ctx, u8"图形底层", u8"设置_清屏颜色", u8"设置清屏颜色 RGBA", g_gl_clearColor);
    regCap(L, ctx, u8"图形底层", u8"执行_清屏", u8"清屏，参数 颜色/深度，默认都清", g_gl_clear);
    regCap(L, ctx, u8"图形底层", u8"设置_开启", u8"开启 GL 开关，如 深度测试/剔除/混合", g_gl_enable);
    regCap(L, ctx, u8"图形底层", u8"设置_关闭", u8"关闭 GL 开关", g_gl_disable);
    regCap(L, ctx, u8"图形底层", u8"设置_深度函数", u8"设置深度比较函数", g_gl_depthFunc);
    regCap(L, ctx, u8"图形底层", u8"设置_深度写入", u8"开关深度写入", g_gl_depthMask);
    regCap(L, ctx, u8"图形底层", u8"设置_剔除面", u8"设置剔除面 正面/背面/双面", g_gl_cullFace);
    regCap(L, ctx, u8"图形底层", u8"设置_正面朝向", u8"设置正面绕序 逆时针/顺时针", g_gl_frontFace);
    regCap(L, ctx, u8"图形底层", u8"设置_混合函数", u8"设置混合源/目标因子", g_gl_blendFunc);
    regCap(L, ctx, u8"图形底层", u8"设置_多边形模式", u8"设置填充或线框", g_gl_polygonMode);
    regCap(L, ctx, u8"图形底层", u8"创建_着色器", u8"编译着色器，参数 类型/源码，返回句柄", g_gl_createShader);
    regCap(L, ctx, u8"图形底层", u8"创建_程序", u8"链接程序，参数 顶点句柄/片元句柄，返回句柄", g_gl_createProgram);
    regCap(L, ctx, u8"图形底层", u8"设置_使用程序", u8"绑定当前程序", g_gl_useProgram);
    regCap(L, ctx, u8"图形底层", u8"删除_程序", u8"删除程序", g_gl_deleteProgram);
    regCap(L, ctx, u8"图形底层", u8"创建_VAO", u8"创建顶点数组对象，返回句柄", g_gl_createVao);
    regCap(L, ctx, u8"图形底层", u8"设置_绑定VAO", u8"绑定顶点数组对象", g_gl_bindVao);
    regCap(L, ctx, u8"图形底层", u8"删除_VAO", u8"删除顶点数组对象", g_gl_deleteVao);
    regCap(L, ctx, u8"图形底层", u8"创建_缓冲", u8"创建缓冲对象，返回句柄", g_gl_createBuffer);
    regCap(L, ctx, u8"图形底层", u8"设置_绑定缓冲", u8"绑定缓冲，参数 目标/句柄", g_gl_bindBuffer);
    regCap(L, ctx, u8"图形底层", u8"删除_缓冲", u8"删除缓冲", g_gl_deleteBuffer);
    regCap(L, ctx, u8"图形底层", u8"设置_缓冲数据", u8"上传浮点数组，参数 目标/数组/用法", g_gl_bufferData);
    regCap(L, ctx, u8"图形底层", u8"设置_缓冲字节", u8"上传字节数组，用于索引缓冲", g_gl_bufferDataBytes);
    regCap(L, ctx, u8"图形底层", u8"设置_缓冲整数", u8"上传 32 位整数数组，用于索引缓冲", g_gl_bufferDataUint);
    regCap(L, ctx, u8"图形底层", u8"设置_顶点属性", u8"配置顶点属性 索引/分量/类型/归一/步长/偏移", g_gl_attribPointer);
    regCap(L, ctx, u8"图形底层", u8"设置_启用属性", u8"启用顶点属性索引", g_gl_enableAttrib);
    regCap(L, ctx, u8"图形底层", u8"设置_浮点", u8"设置程序浮点 uniform", g_gl_uniform1f);
    regCap(L, ctx, u8"图形底层", u8"设置_整数", u8"设置程序整数 uniform", g_gl_uniform1i);
    regCap(L, ctx, u8"图形底层", u8"设置_三维", u8"设置程序三维向量 uniform", g_gl_uniform3f);
    regCap(L, ctx, u8"图形底层", u8"设置_矩阵", u8"设置程序 4x4 矩阵 uniform", g_gl_uniformMat4);
    regCap(L, ctx, u8"图形底层", u8"绘制_数组", u8"按图元绘制数组，参数 图元/起点/数量", g_gl_drawArrays);
    regCap(L, ctx, u8"图形底层", u8"绘制_索引", u8"按索引绘制，参数 图元/索引数/索引类型/偏移", g_gl_drawElements);
    regCap(L, ctx, u8"图形底层", u8"获取_错误码", u8"读取并清零 GL 错误码", g_gl_getError);
    regCap(L, ctx, u8"图形底层", u8"获取_读像素", u8"读取屏幕像素 RGBA，返回 0~255 数组", g_gl_readPixels);
}

void Gvtcier_Bind_Gl(gvts_State* L, GvtsEngineCtx* ctx) {
}
