#include "Gvtcier.h"

// 脚本绑定：通用注册表。两个全局表：
//   capRegistry()  「能力」表，记录每个脚本接口的中文名、分组、说明与函数指针，
//                  供编辑器/调试器列出可用接口（regCap 往里塞）。
//   objRegistry()  「对象方法」表，记录可挂到对象上的脚本方法。
// 两张表都只在初始化阶段写入，运行期只读，所以不需要加锁。

std::vector<CapEntry>& capRegistry() {
    static std::vector<CapEntry> v;
    return v;
}

std::vector<ObjEntry>& objRegistry() {
    static std::vector<ObjEntry> v;
    return v;
}

void regObject(const char* name, const char* group) {
    std::vector<ObjEntry>& v = objRegistry();
    for (size_t i = 0; i < v.size(); ++i) {
        if (v[i].name == name) return;
    }
    v.push_back({ name, group });
}

int g_capFind(gvts_State* L) {
    const char* key = gvtsG_checkstring(L, 1);
    std::vector<CapEntry>& v = capRegistry();
    gvts_newtable(L);
    int n = 0;
    for (size_t i = 0; i < v.size(); ++i) {
        if (strstr(v[i].name, key) == NULL) continue;
        gvts_newtable(L);
        gvts_pushstring(L, v[i].name);
        gvts_setfield(L, -2, "名称");
        gvts_pushstring(L, v[i].group);
        gvts_setfield(L, -2, "分类");
        gvts_pushstring(L, v[i].desc);
        gvts_setfield(L, -2, "说明");
        gvts_pushboolean(L, v[i].fn != NULL);
        gvts_setfield(L, -2, "可用");
        gvts_rawseti(L, -2, ++n);
    }
    return 1;
}

void regFunc(gvts_State* L, GvtsEngineCtx* ctx, const char* name, gvts_CFunction fn) {
    gvts_pushlightuserdata(L, ctx);
    gvts_pushcclosure(L, fn, 1);
    gvts_setglobal(L, name);
}

void regCap(gvts_State* L, GvtsEngineCtx* ctx, const char* group,
                   const char* name, const char* desc, gvts_CFunction fn) {
    regFunc(L, ctx, name, fn);
    CapEntry e = { name, group, desc, fn, std::string(), std::string() };
    const char* us = strchr(name, '_');
    if (us && us != name) {
        e.verb.assign(name, us - name);
        e.object.assign(us + 1);
        regObject(e.object.c_str(), group);
    } else {
        e.object.assign(name);
        regObject(e.object.c_str(), group);
    }
    capRegistry().push_back(e);
}

static int g_capList(gvts_State* L) {
    gvts_newtable(L);
    std::vector<CapEntry>& v = capRegistry();
    for (size_t i = 0; i < v.size(); ++i) {
        gvts_newtable(L);
        gvts_pushstring(L, v[i].name);
        gvts_setfield(L, -2, "名称");
        gvts_pushstring(L, v[i].group);
        gvts_setfield(L, -2, "分组");
        gvts_pushstring(L, v[i].desc);
        gvts_setfield(L, -2, "说明");
        gvts_rawseti(L, -2, (int)i + 1);
    }
    return 1;
}

static const char* g_typeName(gvts_State* L, int idx) {
    switch (gvts_type(L, idx)) {
    case GVTS_TNIL: return u8"空";
    case GVTS_TBOOLEAN: return u8"布尔";
    case GVTS_TNUMBER: return u8"数值";
    case GVTS_TSTRING: return u8"文本";
    case GVTS_TTABLE: return u8"表";
    case GVTS_TFUNCTION: return u8"函数";
    default: return u8"其他";
    }
}

static int g_listGlobals(gvts_State* L) {
    const char* key = gvts_isnoneornil(L, 1) ? "" : gvtsG_checkstring(L, 1);
    int n = 0;
    gvts_newtable(L);
    int out = gvts_gettop(L);
    gvts_rawgeti(L, GVTS_REGISTRYINDEX, GVTS_RIDX_GLOBALS);
    int t = gvts_gettop(L);
    gvts_pushnil(L);
    while (gvts_next(L, t)) {
        if (gvts_type(L, -2) == GVTS_TSTRING) {
            const char* nm = gvts_tostring(L, -2);
            if (nm && (key[0] == 0 || strstr(nm, key) != NULL)) {
                const char* tn = g_typeName(L, -1);
                gvts_newtable(L);
                gvts_pushstring(L, nm);
                gvts_setfield(L, -2, u8"名称");
                gvts_pushstring(L, tn);
                gvts_setfield(L, -2, u8"类型");
                gvts_rawseti(L, out, ++n);
            }
        }
        gvts_pop(L, 1);
    }
    gvts_settop(L, out);
    return 1;
}

void pushVec3(gvts_State* L, const Vec3& v) {
    gvts_newtable(L);
    gvts_pushnumber(L, v.x); gvts_setfield(L, -2, "x");
    gvts_pushnumber(L, v.y); gvts_setfield(L, -2, "y");
    gvts_pushnumber(L, v.z); gvts_setfield(L, -2, "z");
}

void pushQuat(gvts_State* L, const Quat& q) {
    gvts_newtable(L);
    gvts_pushnumber(L, q.x); gvts_setfield(L, -2, "x");
    gvts_pushnumber(L, q.y); gvts_setfield(L, -2, "y");
    gvts_pushnumber(L, q.z); gvts_setfield(L, -2, "z");
    gvts_pushnumber(L, q.w); gvts_setfield(L, -2, "w");
}

void pushMat4(gvts_State* L, const Mat4& m) {
    gvts_newtable(L);
    for (int i = 0; i < 16; ++i) {
        gvts_pushnumber(L, m.m[i]);
        gvts_rawseti(L, -2, i + 1);
    }
}

Vec3 readVec3(gvts_State* L, int idx) {
    if (gvts_isnumber(L, idx)) {
        float x = (float)gvts_tonumber(L, idx);
        float y = (float)gvtsG_optnumber(L, idx + 1, 0.0);
        float z = (float)gvtsG_optnumber(L, idx + 2, 0.0);
        return Vec3(x, y, z);
    }
    gvtsG_checktype(L, idx, GVTS_TTABLE);
    gvts_getfield(L, idx, "x");
    gvts_getfield(L, idx, "y");
    gvts_getfield(L, idx, "z");
    Vec3 v((float)gvts_tonumber(L, -3), (float)gvts_tonumber(L, -2), (float)gvts_tonumber(L, -1));
    gvts_pop(L, 3);
    return v;
}

int vec3Slots(gvts_State* L, int idx) {
    return gvts_isnumber(L, idx) ? 3 : 1;
}

Quat readQuat(gvts_State* L, int idx) {
    gvtsG_checktype(L, idx, GVTS_TTABLE);
    gvts_getfield(L, idx, "x");
    gvts_getfield(L, idx, "y");
    gvts_getfield(L, idx, "z");
    gvts_getfield(L, idx, "w");
    Quat q((float)gvts_tonumber(L, -4), (float)gvts_tonumber(L, -3),
           (float)gvts_tonumber(L, -2), (float)gvts_tonumber(L, -1));
    gvts_pop(L, 4);
    return q;
}

Mat4 readMat4(gvts_State* L, int idx) {
    gvtsG_checktype(L, idx, GVTS_TTABLE);
    Mat4 m;
    for (int i = 0; i < 16; ++i) {
        gvts_rawgeti(L, idx, i + 1);
        m.m[i] = (float)gvts_tonumber(L, -1);
        gvts_pop(L, 1);
    }
    return m;
}

void Gvtcier_Bind_Common(gvts_State* L, GvtsEngineCtx* ctx) {
    regCap(L, ctx, u8"引擎", u8"查询_能力", u8"按名称片段模糊查找已注册能力，返回 {名称, 分类, 说明, 可用} 列表", g_capFind);
    regCap(L, ctx, u8"自省", u8"获取_能力列表", u8"返回全部已注册能力及分组说明", g_capList);
    regCap(L, ctx, u8"自省", u8"列出_全局", u8"按名称片段列出全局变量与全局函数，返回 {名称, 类型} 列表；传空串列全部", g_listGlobals);
}
