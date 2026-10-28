#include "Gvtcier.h"

// 脚本绑定：纹理。脚本查询引擎内置纹理名、按名字取纹理。
// 纹理资源统一由 Scene 的 texStore_ 管理，脚本拿到的是 GvtId 而非裸句柄。

// 列出所有内置纹理名，返回一个数组给脚本遍历
static int g_texNames(gvts_State* L) {
    GvtsEngineCtx* ctx = (GvtsEngineCtx*)gvts_touserdata(L, gvts_upvalueindex(1));
    if (!ctx->scene) { gvts_newtable(L); return 1; }
    int n = ctx->scene->Gvtcier_Scene_BuiltinTextureCount();
    gvts_newtable(L);
    for (int i = 0; i < n; ++i) {
        gvts_pushstring(L, ctx->scene->Gvtcier_Scene_BuiltinTextureName(i));
        gvts_rawseti(L, -2, i + 1);
    }
    return 1;
}

static int g_texApply(gvts_State* L) {
    GvtsEngineCtx* ctx = (GvtsEngineCtx*)gvts_touserdata(L, gvts_upvalueindex(1));
    Object* o = argObject(L, 1);
    const char* name = gvtsG_checkstring(L, 2);
    if (!o || !ctx->scene) return 0;
    GvtId<Texture> t = ctx->scene->Gvtcier_Scene_BuiltinTexture(name);
    if (t.valid()) { o->tex = t; o->texName = name; }
    return 0;
}

static int g_cr_loadTexture(gvts_State* L) {
    GvtsEngineCtx* ctx = (GvtsEngineCtx*)gvts_touserdata(L, gvts_upvalueindex(1));
    if (!ctx->scene) { gvts_pushboolean(L, 0); return 1; }
    const char* path = gvtsG_checkstring(L, 1);
    gvts_pushboolean(L, ctx->scene->Gvtcier_Scene_LoadPngTexture(path).valid());
    return 1;
}

void Gvtcier_Bind_Texture(gvts_State* L, GvtsEngineCtx* ctx) {
    regCap(L, ctx, u8"纹理", u8"获取_纹理列表", u8"返回全部内置纹理名称列表", g_texNames);
    regCap(L, ctx, u8"纹理", u8"设置_纹理", u8"给实体套用内置纹理", g_texApply);
    regCap(L, ctx, u8"场景", u8"加载_纹理", u8"从 png 文件加载贴图", g_cr_loadTexture);
}
