#include "Gvtcier.h"

// 所有绑定函数都是统一签名 gvts_CFunction，引擎上下文 ctx 通过 upvalue(1) 传进来，
// 所以每个函数的第一行都要先把它取出来。

// 脚本名：绘制_裁剪(x, y, 宽, 高)
// 参数：4 个数字，屏幕像素矩形。
// 返回：0 个值。
// 调用：Canvas2D::Gvtcier_Canvas2D_PushClip，把后续 2D 绘制限制在矩形内，
// 要配对使用 删除_裁剪，否则后面所有绘制都被裁掉。
static int g_cr_clipRect(gvts_State* L) {
    GvtsEngineCtx* ctx = (GvtsEngineCtx*)gvts_touserdata(L, gvts_upvalueindex(1));
    if (!ctx->canvas2d) return 0;
    C2Rect r;
    r.x = (float)gvtsG_checknumber(L, 1);
    r.y = (float)gvtsG_checknumber(L, 2);
    r.w = (float)gvtsG_checknumber(L, 3);
    r.h = (float)gvtsG_checknumber(L, 4);
    ctx->canvas2d->Gvtcier_Canvas2D_PushClip(r);
    return 0;
}

// 脚本名：删除_裁剪()
// 参数：无。返回：无。
// 调用：Canvas2D::Gvtcier_Canvas2D_PopClip。注意画布内部没有裁剪栈，一次调用就回到无裁剪状态。
static int g_cr_unclip(gvts_State* L) {
    GvtsEngineCtx* ctx = (GvtsEngineCtx*)gvts_touserdata(L, gvts_upvalueindex(1));
    if (ctx->canvas2d) ctx->canvas2d->Gvtcier_Canvas2D_PopClip();
    return 0;
}

// 脚本名：设置_透明度(实体[, 值])
// 参数：实体句柄；第二个可选数字 0~1。
// 返回：无参时读取，返回当前透明度（1 个值）；有参时写入，返回 0 个值。
// 调用：Opacity::Gvtcier_Opacity_Object / Gvtcier_Opacity_SetObject。
// 实体句柄转成引擎 ID 后再用，ID 无效时什么都不做。
static int g_cr_setOpacity(gvts_State* L) {
    Object* o = argObject(L, 1);
    GvtsEngineCtx* ctx = (GvtsEngineCtx*)gvts_touserdata(L, gvts_upvalueindex(1));
    if (!o || !ctx->scene) return 0;
    GvtId<Object> id = ctx->scene->Gvtcier_Scene_IdOf(o);
    if (!id.valid()) return 0;
    if (gvts_isnoneornil(L, 2)) {
        gvts_pushnumber(L, Opacity::Gvtcier_Opacity_Object(id));
        return 1;
    }
    Opacity::Gvtcier_Opacity_SetObject(id, (float)gvtsG_checknumber(L, 2));
    return 0;
}

// 脚本名：绘制_旋转图片(贴图, x, y, 宽, 高[, 角度[, 透明度]])
// 参数：png 资源名；中心点坐标；宽高；角度默认 0；透明度默认 1。
// 返回：无。
// 调用：Scene::Gvtcier_Scene_LoadPngTexture 取纹理，再调 Canvas2D::Gvtcier_Canvas2D_Sprite。
// 坐标是中心点而不是左上角，这点与 绘制_图片 相反。纹理加载失败时静默跳过。
static int g_cr_sprite(gvts_State* L) {
    GvtsEngineCtx* ctx = (GvtsEngineCtx*)gvts_touserdata(L, gvts_upvalueindex(1));
    if (!ctx->canvas2d || !ctx->scene) return 0;
    const char* name = gvtsG_checkstring(L, 1);
    float x = (float)gvtsG_checknumber(L, 2);
    float y = (float)gvtsG_checknumber(L, 3);
    float w = (float)gvtsG_checknumber(L, 4);
    float h = (float)gvtsG_checknumber(L, 5);
    float rot = (float)gvtsG_optnumber(L, 6, 0.0);
    float a = (float)gvtsG_optnumber(L, 7, 1.0);
    Texture* t = ctx->scene->Gvtcier_Scene_GetTex(ctx->scene->Gvtcier_Scene_LoadPngTexture(name));
    if (!t || t->id() == 0) return 0;
    C2Color c = { 1.0f, 1.0f, 1.0f, a };
    ctx->canvas2d->Gvtcier_Canvas2D_Sprite(t->id(), x, y, rot, 1.0f, 1.0f,
                                           w * 0.5f, h * 0.5f,
                                           0.0f, 0.0f, 1.0f, 1.0f, c, 0, 0.0f, 0.0f);
    return 0;
}

// 脚本名：设置_卡通描边([开关])
// 参数：可选布尔。返回：无参时读取，返回布尔（1 个值）；有参时写入，返回 0 个值。
// 调用：Renderer::Gvtcier_Render_GetToon / SetToon（写 1/0）。
static int g_cr_setToon(gvts_State* L) {
    GvtsEngineCtx* ctx = (GvtsEngineCtx*)gvts_touserdata(L, gvts_upvalueindex(1));
    if (gvts_isnoneornil(L, 1)) { gvts_pushboolean(L, ctx->renderer && ctx->renderer->Gvtcier_Render_GetToon()); return 1; }
    if (ctx->renderer) ctx->renderer->Gvtcier_Render_SetToon(gvts_toboolean(L, 1) ? 1 : 0);
    return 0;
}

// 脚本名：设置_渲染分辨率(宽[, 高])
// 参数：宽；高默认等于宽（做等比缩小）。
// 返回：无参时读取，返回 宽, 高 两个值；有参时返回 0 个值。
// 调用：Renderer::Gvtcier_Render_GetRatio / SetRatio。引擎先按这个尺寸离屏渲染再拉伸到窗口，
// 用来做像素风或降负载。
static int g_cr_renderRatio(gvts_State* L) {
    GvtsEngineCtx* ctx = (GvtsEngineCtx*)gvts_touserdata(L, gvts_upvalueindex(1));
    if (gvts_isnoneornil(L, 1)) {
        if (ctx->renderer) {
            int w = 0, h = 0;
            ctx->renderer->Gvtcier_Render_GetRatio(&w, &h);
            gvts_pushinteger(L, w);
            gvts_pushinteger(L, h);
            return 2;
        }
        gvts_pushinteger(L, 0);
        gvts_pushinteger(L, 0);
        return 2;
    }
    int w = (int)gvtsG_checknumber(L, 1);
    int h = (int)gvtsG_optnumber(L, 2, (double)w);
    if (ctx->renderer) ctx->renderer->Gvtcier_Render_SetRatio(w, h);
    return 0;
}

// 脚本名：获取_渲染分辨率()
// 参数：无。返回：宽, 高 两个整数。
// 与 设置_渲染分辨率 的无参分支等价，只是不提供写入重载，光看名字更像只读接口。
static int g_cr_getRenderRatio(gvts_State* L) {
    GvtsEngineCtx* ctx = (GvtsEngineCtx*)gvts_touserdata(L, gvts_upvalueindex(1));
    int w = 0, h = 0;
    if (ctx->renderer) ctx->renderer->Gvtcier_Render_GetRatio(&w, &h);
    gvts_pushinteger(L, w);
    gvts_pushinteger(L, h);
    return 2;
}

// 脚本名：设置_描边宽度([宽度])
// 参数：可选数字。返回：无参时读取，返回当前宽度；有参时写入，返回 0 个值。
// 调用：Renderer::Gvtcier_Render_GetOutlineWidth / SetOutlineWidth，配合 设置_卡通描边 使用。
static int g_cr_setOutlineWidth(gvts_State* L) {
    GvtsEngineCtx* ctx = (GvtsEngineCtx*)gvts_touserdata(L, gvts_upvalueindex(1));
    if (gvts_isnoneornil(L, 1)) { gvts_pushnumber(L, ctx->renderer ? ctx->renderer->Gvtcier_Render_GetOutlineWidth() : 0.0); return 1; }
    if (ctx->renderer) ctx->renderer->Gvtcier_Render_SetOutlineWidth((float)gvtsG_checknumber(L, 1));
    return 0;
}

// 脚本名：设置_遮挡剔除([开关])
// 参数：可选布尔。返回：始终返回 1 个布尔值，即设置之后的状态。
// 注意这个函数的返回值语义与其它"设置_"不同：写入时也返回结果，方便链式判断；
// 第一参不是布尔时只当读取，不写入。
// 调用：Renderer::Gvtcier_Render_SetOcclusion / GetOcclusion。
static int g_cr_occlusion(gvts_State* L) {
    GvtsEngineCtx* ctx = (GvtsEngineCtx*)gvts_touserdata(L, gvts_upvalueindex(1));
    if (gvts_isboolean(L, 1)) {
        if (ctx->renderer) ctx->renderer->Gvtcier_Render_SetOcclusion(gvts_toboolean(L, 1) != 0);
    }
    gvts_pushboolean(L, ctx->renderer && ctx->renderer->Gvtcier_Render_GetOcclusion());
    return 1;
}

// 脚本名：获取_被遮挡数()
// 参数：无。返回：1 个整数，上一帧因遮挡剔除被跳过的物体数，可用来判断当前视锥的剔除效果。
// 调用：Renderer::Gvtcier_Render_OccludedCount。
static int g_cr_occludedCount(gvts_State* L) {
    GvtsEngineCtx* ctx = (GvtsEngineCtx*)gvts_touserdata(L, gvts_upvalueindex(1));
    gvts_pushinteger(L, ctx->renderer ? ctx->renderer->Gvtcier_Render_OccludedCount() : 0);
    return 1;
}

// 脚本名：设置_描边颜色([r[, g[, b]]])
// 参数：可选三个 0~1 的浮点分量，缺省按 0 处理。
// 返回：无参时读取，返回 r, g, b 三个值；有参时写入，返回 0 个值。
// 调用：Renderer::Gvtcier_Render_GetOutlineColor / SetOutlineColor。
static int g_cr_setOutlineColor(gvts_State* L) {
    GvtsEngineCtx* ctx = (GvtsEngineCtx*)gvts_touserdata(L, gvts_upvalueindex(1));
    if (gvts_isnoneornil(L, 1)) {
        float r = 0.0f, g = 0.0f, b = 0.0f;
        if (ctx->renderer) ctx->renderer->Gvtcier_Render_GetOutlineColor(&r, &g, &b);
        gvts_pushnumber(L, r);
        gvts_pushnumber(L, g);
        gvts_pushnumber(L, b);
        return 3;
    }
    float r = (float)gvtsG_optnumber(L, 1, 0.0);
    float g = (float)gvtsG_optnumber(L, 2, 0.0);
    float b = (float)gvtsG_optnumber(L, 3, 0.0);
    if (ctx->renderer) ctx->renderer->Gvtcier_Render_SetOutlineColor(r, g, b);
    return 0;
}

// 下面四个宏批量生成"读写属性"型绑定函数，把重复的样板代码压掉。
// 统一约定：第一参为 空 时读取并返回 1 个值；否则写入，返回 0 个值。
// 所有属性都直接读写 Scene 的同名字段，所以宏参数只要给函数名与字段名即可。
// 字段类型决定用哪个宏：
//   RENDER_TOGGLE 布尔（脚本用真假值），RENDER_NUMBER 浮点，RENDER_INT 整数。
// 举例：RENDER_TOGGLE(g_cr_shadowOn, showShadow) 会生成 设置_阴影，
//   设置_阴影()        -> 返回当前开关
//   设置_阴影(真/假)   -> 写入开关
#define RENDER_TOGGLE(fnName, field)                                        \
static int fnName(gvts_State* L) {                                           \
    GvtsEngineCtx* ctx = (GvtsEngineCtx*)gvts_touserdata(L, gvts_upvalueindex(1)); \
    if (!ctx->scene) return 0;                                              \
    if (gvts_isnoneornil(L, 1)) { gvts_pushboolean(L, ctx->scene->field); return 1; } \
    ctx->scene->field = gvts_toboolean(L, 1) != 0;                           \
    return 0;                                                               \
}

// 把脚本给的第一个参数当数字写进 Scene 字段，读时以浮点返回。见上面宏的说明。
#define RENDER_NUMBER(fnName, field)                                        \
static int fnName(gvts_State* L) {                                           \
    GvtsEngineCtx* ctx = (GvtsEngineCtx*)gvts_touserdata(L, gvts_upvalueindex(1)); \
    if (!ctx->scene) return 0;                                              \
    if (gvts_isnoneornil(L, 1)) { gvts_pushnumber(L, (double)ctx->scene->field); return 1; } \
    ctx->scene->field = (float)gvtsG_checknumber(L, 1);                      \
    return 0;                                                               \
}

// 同 NUMBER，但写时强转 int，用于层数、步数这类整数字段。
#define RENDER_INT(fnName, field)                                           \
static int fnName(gvts_State* L) {                                           \
    GvtsEngineCtx* ctx = (GvtsEngineCtx*)gvts_touserdata(L, gvts_upvalueindex(1)); \
    if (!ctx->scene) return 0;                                              \
    if (gvts_isnoneornil(L, 1)) { gvts_pushinteger(L, ctx->scene->field); return 1; } \
    ctx->scene->field = (int)gvtsG_checkinteger(L, 1);                       \
    return 0;                                                               \
}

RENDER_TOGGLE(g_cr_shadowOn, showShadow)
RENDER_TOGGLE(g_cr_shadowDebug, showShadowDebug)
RENDER_TOGGLE(g_cr_inputLock, inputLocked)
RENDER_TOGGLE(g_cr_hdr, hdr)
RENDER_TOGGLE(g_cr_wireframe, wireframe)
RENDER_TOGGLE(g_cr_volumeWork, volumeWork)
RENDER_TOGGLE(g_cr_gtaoOn, gtaoEnabled)
RENDER_INT(g_cr_gtaoSlices, gtaoSlices)
RENDER_INT(g_cr_gtaoSteps, gtaoSteps)
RENDER_NUMBER(g_cr_gtaoRadius, gtaoRadius)
RENDER_NUMBER(g_cr_gtaoIntensity, gtaoIntensity)
RENDER_NUMBER(g_cr_gtaoPower, gtaoPower)
RENDER_NUMBER(g_cr_gtaoBias, gtaoBias)
RENDER_NUMBER(g_cr_fogDensity, fogDensity)
RENDER_NUMBER(g_cr_bloom, bloomStrength)
RENDER_NUMBER(g_cr_volumeStrength, volStrength)
RENDER_NUMBER(g_cr_ambient, ambientIntensity)
RENDER_NUMBER(g_cr_timeScale, timeScale)
RENDER_NUMBER(g_cr_gravity, gravity)

// 纯读取型宏组，与上面的读写宏对应，但只读不写。用于"获取_xxx"这批接口。
// 好处是脚本里误传参数也不会改状态，语义更明确。
#define RENDER_GET_TOGGLE(fnName, field)                                    \
static int fnName(gvts_State* L) {                                           \
    GvtsEngineCtx* ctx = (GvtsEngineCtx*)gvts_touserdata(L, gvts_upvalueindex(1)); \
    gvts_pushboolean(L, ctx->scene && ctx->scene->field);                    \
    return 1;                                                               \
}

#define RENDER_GET_NUMBER(fnName, field)                                    \
static int fnName(gvts_State* L) {                                           \
    GvtsEngineCtx* ctx = (GvtsEngineCtx*)gvts_touserdata(L, gvts_upvalueindex(1)); \
    gvts_pushnumber(L, ctx->scene ? (double)ctx->scene->field : 0.0);        \
    return 1;                                                               \
}

#define RENDER_GET_INT(fnName, field)                                       \
static int fnName(gvts_State* L) {                                           \
    GvtsEngineCtx* ctx = (GvtsEngineCtx*)gvts_touserdata(L, gvts_upvalueindex(1)); \
    gvts_pushinteger(L, ctx->scene ? ctx->scene->field : 0);                 \
    return 1;                                                               \
}

// 纯读取绑定实例清单，脚本名以"获取_"开头，只读不写。
RENDER_GET_TOGGLE(g_cr_getShadowOn, showShadow)
RENDER_GET_TOGGLE(g_cr_getShadowDebug, showShadowDebug)
RENDER_GET_TOGGLE(g_cr_getInputLock, inputLocked)
RENDER_GET_TOGGLE(g_cr_getHdr, hdr)
RENDER_GET_TOGGLE(g_cr_getWireframe, wireframe)
RENDER_GET_TOGGLE(g_cr_getVolumeWork, volumeWork)
RENDER_GET_TOGGLE(g_cr_getGtaoOn, gtaoEnabled)
RENDER_GET_INT(g_cr_getGtaoSlices, gtaoSlices)
RENDER_GET_INT(g_cr_getGtaoSteps, gtaoSteps)
RENDER_GET_NUMBER(g_cr_getGtaoRadius, gtaoRadius)
RENDER_GET_NUMBER(g_cr_getGtaoIntensity, gtaoIntensity)
RENDER_GET_NUMBER(g_cr_getGtaoPower, gtaoPower)
RENDER_GET_NUMBER(g_cr_getGtaoBias, gtaoBias)
RENDER_GET_NUMBER(g_cr_getFogDensity, fogDensity)
RENDER_GET_NUMBER(g_cr_getBloom, bloomStrength)
RENDER_GET_NUMBER(g_cr_getVolumeStrength, volStrength)
RENDER_GET_NUMBER(g_cr_getAmbient, ambientIntensity)
RENDER_GET_NUMBER(g_cr_getTimeScale, timeScale)
RENDER_GET_NUMBER(g_cr_getGravity, gravity)

// 脚本名：设置_遮蔽预设(档位名)
// 参数：一个字符串，可取 "关"/"低"/"中"/"高"（也兼容 off/low/medium/high）。
// 返回：1 个布尔，档位名不认识时返回 假，否则返回 真。
// 调用：直接改 Scene 的 gtaoEnabled/gtaoSlices/gtaoSteps/gtaoRadius/gtaoIntensity/gtaoPower 六个字段，
// 这组参数是调出来的观感档位，改一个档位数值要连带看 GTAO 的着色器。
static int g_cr_ssaoPreset(gvts_State* L) {
    GvtsEngineCtx* ctx = (GvtsEngineCtx*)gvts_touserdata(L, gvts_upvalueindex(1));
    if (!ctx->scene) return 0;
    const char* n = gvtsG_checkstring(L, 1);
    Scene* sc = ctx->scene;
    if (strcmp(n, "关") == 0 || strcmp(n, "off") == 0) {
        sc->gtaoEnabled = false;
    } else if (strcmp(n, "低") == 0 || strcmp(n, "low") == 0) {
        sc->gtaoEnabled = true; sc->gtaoSlices = 2; sc->gtaoSteps = 6;
        sc->gtaoRadius = 1.0f; sc->gtaoIntensity = 2.0f; sc->gtaoPower = 1.5f;
    } else if (strcmp(n, "中") == 0 || strcmp(n, "medium") == 0) {
        sc->gtaoEnabled = true; sc->gtaoSlices = 4; sc->gtaoSteps = 10;
        sc->gtaoRadius = 1.2f; sc->gtaoIntensity = 3.5f; sc->gtaoPower = 1.8f;
    } else if (strcmp(n, "高") == 0 || strcmp(n, "high") == 0) {
        sc->gtaoEnabled = true; sc->gtaoSlices = 6; sc->gtaoSteps = 16;
        sc->gtaoRadius = 1.5f; sc->gtaoIntensity = 4.5f; sc->gtaoPower = 2.0f;
    } else {
        gvts_pushboolean(L, 0);
        return 1;
    }
    gvts_pushboolean(L, 1);
    return 1;
}

// 脚本名：设置_阴影预设(档位名)
// 参数：一个字符串，可取 "关"/"开"/"调试"（也兼容 off/on/debug）。
// 返回：1 个布尔，档位名不认识时返回 假。
// 调用：改 Scene 的 showShadow 与 showShadowDebug。"调试"档会把阴影贴图画到屏幕上便于排查。
static int g_cr_shadowPreset(gvts_State* L) {
    GvtsEngineCtx* ctx = (GvtsEngineCtx*)gvts_touserdata(L, gvts_upvalueindex(1));
    if (!ctx->scene) return 0;
    const char* n = gvtsG_checkstring(L, 1);
    Scene* sc = ctx->scene;
    if (strcmp(n, "关") == 0 || strcmp(n, "off") == 0) {
        sc->showShadow = false; sc->showShadowDebug = false;
    } else if (strcmp(n, "开") == 0 || strcmp(n, "on") == 0) {
        sc->showShadow = true; sc->showShadowDebug = false;
    } else if (strcmp(n, "调试") == 0 || strcmp(n, "debug") == 0) {
        sc->showShadow = true; sc->showShadowDebug = true;
    } else {
        gvts_pushboolean(L, 0);
        return 1;
    }
    gvts_pushboolean(L, 1);
    return 1;
}

// 脚本名：设置_天空颜色([r[, g[, b]]])
// 参数：可选三个分量。返回：无参时读取，返回 r, g, b 三个值；有参时写入，返回 0 个值。
// 与 设置_描边颜色 不同，这个函数写入时 g、b 缺省会沿用当前值，只写 r 是安全的。
// 调用：直接改 Scene 的 skyColor。
static int g_cr_skyColor(gvts_State* L) {
    GvtsEngineCtx* ctx = (GvtsEngineCtx*)gvts_touserdata(L, gvts_upvalueindex(1));
    if (!ctx->scene) return 0;
    if (gvts_isnoneornil(L, 1)) {
        gvts_pushnumber(L, ctx->scene->skyColor.x);
        gvts_pushnumber(L, ctx->scene->skyColor.y);
        gvts_pushnumber(L, ctx->scene->skyColor.z);
        return 3;
    }
    ctx->scene->skyColor = Vec3((float)gvtsG_checknumber(L, 1),
                                (float)gvtsG_optnumber(L, 2, ctx->scene->skyColor.y),
                                (float)gvtsG_optnumber(L, 3, ctx->scene->skyColor.z));
    return 0;
}

// 脚本名：设置_天空盒([开关])
// 参数：可选布尔。返回：无参时读取，返回布尔；有参时写入，返回 0 个值。
// 调用：改 Scene 的 showSkybox，关掉之后背景只剩 设置_天空颜色 的纯色。
static int g_cr_skybox(gvts_State* L) {
    GvtsEngineCtx* ctx = (GvtsEngineCtx*)gvts_touserdata(L, gvts_upvalueindex(1));
    if (!ctx->scene) return 0;
    if (gvts_isnoneornil(L, 1)) {
        gvts_pushboolean(L, ctx->scene->showSkybox ? 1 : 0);
        return 1;
    }
    ctx->scene->showSkybox = gvts_toboolean(L, 1) != 0;
    return 0;
}

// 与 RENDER_TOGGLE 功能完全一样，只是这组挂在 Scene 上而不是 Renderer 上，
// 实现重复了一份，历史原因没有合并。
// 对应的脚本名：设置_体积光开关 / 设置_太阳光开关 / 设置_环境光开关 / 设置_准星 / 设置_暂停。
#define SCENE_TOGGLE(fnName, field)                                         \
static int fnName(gvts_State* L) {                                           \
    GvtsEngineCtx* ctx = (GvtsEngineCtx*)gvts_touserdata(L, gvts_upvalueindex(1)); \
    if (!ctx->scene) return 0;                                              \
    if (gvts_isnoneornil(L, 1)) { gvts_pushboolean(L, ctx->scene->field); return 1; } \
    ctx->scene->field = gvts_toboolean(L, 1) != 0;                           \
    return 0;                                                               \
}

SCENE_TOGGLE(g_cr_volumeLight, volumeLightEnabled)
SCENE_TOGGLE(g_cr_sunEnabled, sunEnabled)
SCENE_TOGGLE(g_cr_ambientEnabled, ambientEnabled)
SCENE_TOGGLE(g_cr_crosshair, showCrosshair)
SCENE_TOGGLE(g_cr_paused, paused)

// 脚本名：绘制_世界线(x1, y1, z1, x2, y2, z2[, r[, g[, b[, a]]]])
// 参数：两个世界坐标端点；颜色四个分量默认全 1（白色不透明）。
// 返回：无。
// 调用：Renderer::Gvtcier_Render_AddWorldLine。提交的线段是"本帧有效"的，
// 每帧都会清空，所以调试画线要在每帧重画；想手动清空可用 删除_世界线。
static int g_cr_worldLine(gvts_State* L) {
    GvtsEngineCtx* ctx = (GvtsEngineCtx*)gvts_touserdata(L, gvts_upvalueindex(1));
    if (!ctx->renderer) return 0;
    float x1 = (float)gvtsG_checknumber(L, 1);
    float y1 = (float)gvtsG_checknumber(L, 2);
    float z1 = (float)gvtsG_checknumber(L, 3);
    float x2 = (float)gvtsG_checknumber(L, 4);
    float y2 = (float)gvtsG_checknumber(L, 5);
    float z2 = (float)gvtsG_checknumber(L, 6);
    float r = (float)gvtsG_optnumber(L, 7, 1.0);
    float g = (float)gvtsG_optnumber(L, 8, 1.0);
    float bl = (float)gvtsG_optnumber(L, 9, 1.0);
    float a = (float)gvtsG_optnumber(L, 10, 1.0);
    ctx->renderer->Gvtcier_Render_AddWorldLine(x1, y1, z1, x2, y2, z2, r, g, bl, a);
    return 0;
}

// 脚本名：删除_世界线()
// 参数：无。返回：无。
// 调用：Renderer::Gvtcier_Render_ClearWorldLines，清掉本帧已提交的全部世界线段。
static int g_cr_clearWorldLines(gvts_State* L) {
    GvtsEngineCtx* ctx = (GvtsEngineCtx*)gvts_touserdata(L, gvts_upvalueindex(1));
    if (!ctx->renderer) return 0;
    ctx->renderer->Gvtcier_Render_ClearWorldLines();
    return 0;
}

// 脚本名：设置_抗锯齿([开关])
// 参数：可选布尔。返回：无参时读取，返回布尔；有参时写入，返回 0 个值。
// 调用：Renderer::Gvtcier_Render_GetFxaa / SetFxaa。关掉 FXAA 时引擎改用邻近采样放大，
// 这样像素风素材放大后不会糊成一片，是像素游戏必须关掉的一项。
static int g_cr_fxaa(gvts_State* L) {
    GvtsEngineCtx* ctx = (GvtsEngineCtx*)gvts_touserdata(L, gvts_upvalueindex(1));
    if (!ctx->renderer) return 0;
    if (gvts_isnoneornil(L, 1)) {
        gvts_pushboolean(L, ctx->renderer->Gvtcier_Render_GetFxaa() != 0);
        return 1;
    }
    ctx->renderer->Gvtcier_Render_SetFxaa(gvts_toboolean(L, 1) != 0);
    return 0;
}

// 注册本文件全部渲染相关能力。
// regCap 的参数依次是：语言状态、引擎上下文、分组名、脚本里看到的中文函数名、说明文字、C++ 实现。
// 分组名决定文档与 查询_能力 里的归类，说明文字会直接出现在脚本补全提示里，
// 所以这里的中文要写得让脚本作者一眼就知道参数与返回值形态。
// 注册顺序有意把常用的放前面（世界线、裁剪、透明度、图片），方便补全时优先出现。
void Gvtcier_Bind_Render(gvts_State* L, GvtsEngineCtx* ctx) {
    regCap(L, ctx, u8"玩家", u8"设置_输入锁", u8"读写输入锁：锁定后忽略键盘移动与鼠标转向，无参读取", g_cr_inputLock);
    regCap(L, ctx, u8"绘制", u8"绘制_世界线", u8"世界空间绘制一条彩色线段，每帧调用会立即清空，需在每帧重画", g_cr_worldLine);
    regCap(L, ctx, u8"绘制", u8"删除_世界线", u8"手动清空本帧已提交的世界线段", g_cr_clearWorldLines);
    regCap(L, ctx, u8"绘制", u8"绘制_裁剪", u8"限制后续 2D 绘制在矩形内，需配对取消裁剪", g_cr_clipRect);
    regCap(L, ctx, u8"绘制", u8"删除_裁剪", u8"结束最近一次裁剪矩形", g_cr_unclip);
    regCap(L, ctx, u8"绘制", u8"设置_透明度", u8"读写实体透明度 0~1，无第二参读取", g_cr_setOpacity);
    regCap(L, ctx, u8"绘制", u8"绘制_旋转图片", u8"2D 画布绘制带旋转的 png 图片，参数 贴图/x/y/宽/高/角度/透明度", g_cr_sprite);
    regCap(L, ctx, u8"渲染", u8"设置_卡通描边", u8"读写卡通描边开关，无参读取", g_cr_setToon);
    regCap(L, ctx, u8"渲染", u8"设置_描边宽度", u8"读写描边外扩宽度，无参读取", g_cr_setOutlineWidth);
    regCap(L, ctx, u8"渲染", u8"设置_遮挡剔除", u8"读写遮挡剔除开关（HZB），无参读取", g_cr_occlusion);
    regCap(L, ctx, u8"渲染", u8"获取_被遮挡数", u8"上一帧因遮挡被剔除的物体数", g_cr_occludedCount);
    regCap(L, ctx, u8"渲染", u8"设置_描边颜色", u8"读写描边颜色 RGB，无参读取", g_cr_setOutlineColor);
    regCap(L, ctx, u8"渲染", u8"设置_阴影", u8"读写阴影开关，无参读取", g_cr_shadowOn);
    regCap(L, ctx, u8"渲染", u8"设置_阴影调试", u8"读写阴影可视化调试，无参读取", g_cr_shadowDebug);
    regCap(L, ctx, u8"渲染", u8"设置_阴影预设", u8"按名称切换阴影档位：关/开/调试", g_cr_shadowPreset);
    regCap(L, ctx, u8"渲染", u8"设置_HDR", u8"读写 HDR 开关，无参读取", g_cr_hdr);
    regCap(L, ctx, u8"渲染", u8"设置_线框", u8"读写线框渲染开关，无参读取", g_cr_wireframe);
    regCap(L, ctx, u8"渲染", u8"设置_体积光开关", u8"读写体积光开关，无参读取", g_cr_volumeLight);
    regCap(L, ctx, u8"渲染", u8"设置_体积光强度", u8"读写体积光强度，无参读取", g_cr_volumeStrength);
    regCap(L, ctx, u8"渲染", u8"设置_泛光强度", u8"读写泛光强度，无参读取", g_cr_bloom);
    regCap(L, ctx, u8"渲染", u8"设置_渲染分辨率", u8"设置离屏渲染分辨率 宽/高，引擎按此尺寸渲染后拉伸到窗口", g_cr_renderRatio);
    regCap(L, ctx, u8"渲染", u8"获取_渲染分辨率", u8"返回当前离屏渲染分辨率 宽, 高", g_cr_getRenderRatio);
    regCap(L, ctx, u8"渲染", u8"获取_阴影", u8"读取阴影开关", g_cr_getShadowOn);
    regCap(L, ctx, u8"渲染", u8"获取_阴影调试", u8"读取阴影可视化调试开关", g_cr_getShadowDebug);
    regCap(L, ctx, u8"渲染", u8"获取_HDR", u8"读取 HDR 开关", g_cr_getHdr);
    regCap(L, ctx, u8"渲染", u8"获取_线框", u8"读取线框渲染开关", g_cr_getWireframe);
    regCap(L, ctx, u8"渲染", u8"获取_体积光开关", u8"读取体积光开关", g_cr_getVolumeWork);
    regCap(L, ctx, u8"渲染", u8"获取_体积光强度", u8"读取体积光强度", g_cr_getVolumeStrength);
    regCap(L, ctx, u8"渲染", u8"获取_泛光强度", u8"读取泛光强度", g_cr_getBloom);
    regCap(L, ctx, u8"渲染", u8"获取_环境光强度", u8"读取环境光强度", g_cr_getAmbient);
    regCap(L, ctx, u8"渲染", u8"获取_雾浓度", u8"读取雾浓度", g_cr_getFogDensity);
    regCap(L, ctx, u8"渲染", u8"获取_环境光遮蔽", u8"读取环境光遮蔽开关", g_cr_getGtaoOn);
    regCap(L, ctx, u8"渲染", u8"获取_遮蔽层数", u8"读取环境光遮蔽层数", g_cr_getGtaoSlices);
    regCap(L, ctx, u8"渲染", u8"获取_遮蔽步数", u8"读取环境光遮蔽步数", g_cr_getGtaoSteps);
    regCap(L, ctx, u8"渲染", u8"获取_遮蔽半径", u8"读取环境光遮蔽半径", g_cr_getGtaoRadius);
    regCap(L, ctx, u8"渲染", u8"获取_遮蔽强度", u8"读取环境光遮蔽强度", g_cr_getGtaoIntensity);
    regCap(L, ctx, u8"渲染", u8"获取_遮蔽衰减", u8"读取环境光遮蔽衰减", g_cr_getGtaoPower);
    regCap(L, ctx, u8"渲染", u8"获取_遮蔽偏移", u8"读取环境光遮蔽偏移", g_cr_getGtaoBias);
    regCap(L, ctx, u8"场景", u8"获取_时间流速", u8"读取时间流速倍率", g_cr_getTimeScale);
    regCap(L, ctx, u8"场景", u8"获取_重力", u8"读取场景重力", g_cr_getGravity);
    regCap(L, ctx, u8"玩家", u8"获取_输入锁", u8"读取输入锁状态", g_cr_getInputLock);
    regCap(L, ctx, u8"渲染", u8"设置_抗锯齿", u8"读写 FXAA 抗锯齿开关，关闭后按邻近采样放大以保留大颗粒像素，无参读取", g_cr_fxaa);
    regCap(L, ctx, u8"渲染", u8"设置_环境光强度", u8"读写环境光强度，无参读取", g_cr_ambient);
    regCap(L, ctx, u8"渲染", u8"设置_太阳光开关", u8"读写太阳平行光开关，无参读取", g_cr_sunEnabled);
    regCap(L, ctx, u8"渲染", u8"设置_环境光开关", u8"读写环境光开关，无参读取", g_cr_ambientEnabled);
    regCap(L, ctx, u8"渲染", u8"设置_雾浓度", u8"读写雾浓度，无参读取", g_cr_fogDensity);
    regCap(L, ctx, u8"渲染", u8"设置_天空颜色", u8"读写天空颜色 RGB，无参读取", g_cr_skyColor);
    regCap(L, ctx, u8"渲染", u8"设置_天空盒", u8"读写天空盒开关，关闭后暴露纯色背景，无参读取", g_cr_skybox);
    regCap(L, ctx, u8"渲染", u8"设置_准星", u8"读写准星显示开关，无参读取", g_cr_crosshair);
    regCap(L, ctx, u8"渲染", u8"设置_环境光遮蔽", u8"读写 GTAO 开关，无参读取", g_cr_gtaoOn);
    regCap(L, ctx, u8"渲染", u8"设置_遮蔽预设", u8"按名称切换 GTAO 档位：关/低/中/高", g_cr_ssaoPreset);
    regCap(L, ctx, u8"渲染", u8"设置_遮蔽层数", u8"读写 GTAO 方位采样层数，无参读取", g_cr_gtaoSlices);
    regCap(L, ctx, u8"渲染", u8"设置_遮蔽步数", u8"读写 GTAO 每层步进数，无参读取", g_cr_gtaoSteps);
    regCap(L, ctx, u8"渲染", u8"设置_遮蔽半径", u8"读写 GTAO 采样半径，无参读取", g_cr_gtaoRadius);
    regCap(L, ctx, u8"渲染", u8"设置_遮蔽强度", u8"读写 GTAO 强度，无参读取", g_cr_gtaoIntensity);
    regCap(L, ctx, u8"渲染", u8"设置_遮蔽衰减", u8"读写 GTAO 幂次衰减，无参读取", g_cr_gtaoPower);
    regCap(L, ctx, u8"渲染", u8"设置_遮蔽偏移", u8"读写 GTAO 深度偏移，无参读取", g_cr_gtaoBias);
    regCap(L, ctx, u8"场景", u8"设置_时间流速", u8"读写时间缩放，无参读取", g_cr_timeScale);
    regCap(L, ctx, u8"场景", u8"设置_暂停", u8"读写暂停开关，无参读取", g_cr_paused);
    regCap(L, ctx, u8"场景", u8"设置_重力", u8"读写重力加速度，无参读取", g_cr_gravity);
}
