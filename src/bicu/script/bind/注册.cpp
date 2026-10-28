#include "Gvtcier.h"

// 把某个全局名设为 nil，等于删除这个全局功能。
static void dropGlobal(gvts_State* L, const char* name) {
    gvts_pushnil(L);
    gvts_setglobal(L, name);
}

// 全局变量改名：把 from 的值搬到 to 上再删掉 from。
// 原本不存在（nil）时什么都不做，避免把 to 也写成 nil 把已有东西删掉。
static void renameGlobal(gvts_State* L, const char* from, const char* to) {
    gvts_getglobal(L, from);
    if (gvts_isnil(L, -1)) { gvts_pop(L, 1); return; }
    gvts_setglobal(L, to);
    dropGlobal(L, from);
}

// 整体重命名一个库表：把全局 from 表里的英文键按 map 换成中文键，再把这整张表挂到全局 to 上，删掉旧的全局名。
// map 是成对的 [英文键, 中文键, 英文键, 中文键, ...]，以 NULL 结尾。
// 做法是先取旧键值 -> 复制一份写到新键 -> 再把旧键清成 nil；
// 最后 gvts_setglobal 用 to 覆盖掉栈顶的表，所以这一步完成的是"表整体改名"。
static void renameTableKeys(gvts_State* L, const char* from, const char* to, const char* const* map) {
    gvts_getglobal(L, from);
    if (!gvts_istable(L, -1)) { gvts_pop(L, 1); return; }
    for (int i = 0; map[i]; i += 2) {
        gvts_getfield(L, -1, map[i]);
        if (!gvts_isnil(L, -1)) {
            gvts_pushvalue(L, -1);
            gvts_setfield(L, -3, map[i + 1]);
        }
        gvts_pop(L, 1);
        gvts_pushnil(L);
        gvts_setfield(L, -2, map[i]);
    }
    gvts_setglobal(L, to);
    dropGlobal(L, from);
}

// 把 from 表里部分键合并进已存在的 to 表，然后删掉 from。
// 与 renameTableKeys 的差别：这里不重命名表本身，而是把条目并进目标表（数学库就是这用法），
// 所以 from 里没被 map 覆盖到的条目会随 dropGlobal 一起丢掉。
static void mergeTable(gvts_State* L, const char* from, const char* to, const char* const* map) {
    gvts_getglobal(L, from);
    if (!gvts_istable(L, -1)) { gvts_pop(L, 1); return; }
    gvts_getglobal(L, to);
    if (!gvts_istable(L, -1)) { gvts_pop(L, 2); return; }
    for (int i = 0; map[i]; i += 2) {
        gvts_getfield(L, -2, map[i]);
        if (!gvts_isnil(L, -1)) gvts_setfield(L, -2, map[i + 1]);
        else gvts_pop(L, 1);
    }
    gvts_pop(L, 2);
    dropGlobal(L, from);
}

// 把标准库的英文 API 全部换成中文名，这是"全中文脚本"体验的关键一环。
// 四张映射表分别对应 基础函数、字符串库、表库、数学库；
// print 被直接删掉而不是改名，因为引擎另有 执行_打印，避免脚本作者用两套输出。
static void openStdLibsCN(gvts_State* L) {
    static const char* const baseMap[] = {
        "assert", u8"断言", "error", u8"报错", "getmetatable", u8"取元表",
        "ipairs", u8"数组遍历", "next", u8"下一项", "pairs", u8"遍历",
        "pcall", u8"保护调用", "rawequal", u8"原始相等", "rawget", u8"原始读",
        "rawlen", u8"原始长度", "rawset", u8"原始写", "select", u8"选择",
        "setmetatable", u8"设元表", "tonumber", u8"转数字", "tostring", u8"转文本",
        "type", u8"类型", "xpcall", u8"保护调用带处理", "warn", u8"警告",
        "_G", u8"全局表", "_VERSION", u8"版本",
        NULL
    };
    static const char* const stringMap[] = {
        "byte", u8"字节", "char", u8"字符", "dump", u8"导出", "find", u8"查找",
        "format", u8"格式化", "gmatch", u8"全部匹配", "gsub", u8"替换",
        "len", u8"长度", "lower", u8"转小写", "match", u8"匹配",
        "pack", u8"打包", "packsize", u8"打包大小", "rep", u8"重复",
        "reverse", u8"反转", "sub", u8"子串", "unpack", u8"解包", "upper", u8"转大写",
        NULL
    };
    static const char* const tableMap[] = {
        "concat", u8"连接", "insert", u8"插入", "move", u8"移动",
        "pack", u8"打包", "remove", u8"移除", "sort", u8"排序", "unpack", u8"解包",
        NULL
    };
    static const char* const mathMap[] = {
        "abs", u8"绝对值", "ceil", u8"向上取整", "floor", u8"向下取整",
        "sqrt", u8"开方", "sin", u8"正弦", "cos", u8"余弦", "tan", u8"正切",
        "asin", u8"反正弦", "acos", u8"反余弦", "atan", u8"反正切",
        "exp", u8"指数", "log", u8"对数", "fmod", u8"浮点取余", "modf", u8"拆分",
        "random", u8"随机", "randomseed", u8"设随机种子", "pi", u8"圆周率",
        "huge", u8"极大值", "maxinteger", u8"最大整数", "mininteger", u8"最小整数",
        "max", u8"最大", "min", u8"最小", "tointeger", u8"转整数",
        "type", u8"数学类型", "ult", u8"无符号小于",
        NULL
    };
    for (int i = 0; baseMap[i]; i += 2) renameGlobal(L, baseMap[i], baseMap[i + 1]);
    dropGlobal(L, "print");
    renameTableKeys(L, GVTS_STRLIBNAME, u8"字符串", stringMap);
    renameTableKeys(L, GVTS_TABLIBNAME, u8"表", tableMap);
    mergeTable(L, GVTS_MATHLIBNAME, u8"数学", mathMap);
}

// 开游戏脚本用的库集合。只开 base/table/string/math 四个，io/os/debug 一律不开，
// 因为游戏脚本不需要直接摸文件与操作系统（文件操作走引擎的 执行_保存 等接口）。
// 开完之后把会破坏沙箱或让流程失控的全局函数删掉：
//   dofile/loadfile/load 能绕过 包含 机制加载任意代码，require/module/collectgarbage 要么不适用要么能卡帧。
// 最后把剩下的英文名统一换成中文。
void openGameLibs(gvts_State* L) {
    static const gvtsG_Reg baseLibs[] = {
        { "", gvtsopen_base },
        { GVTS_TABLIBNAME, gvtsopen_table },
        { GVTS_STRLIBNAME, gvtsopen_string },
        { GVTS_MATHLIBNAME, gvtsopen_math },
        { NULL, NULL }
    };
    for (const gvtsG_Reg* lib = baseLibs; lib->func; ++lib) {
        gvtsG_requiref(L, lib->name, lib->func, 1);
        gvts_pop(L, 1);
    }
    openMathLibCN(L);
    openMathGvLib(L);
    static const char* banished[] = {
        "dofile", "loadfile", "load", "require", "collectgarbage", "module",
        NULL
    };
    for (int i = 0; banished[i]; ++i) dropGlobal(L, banished[i]);
    openStdLibsCN(L);
}

// 注册全部引擎 API 到脚本全局表，并在最后汇总注册 Gv_only_OpenGL 那批图形底层能力。
// 顺序基本是按功能分组：核心 -> 输入 -> 界面 -> 交互 -> 场景 -> 动画 -> 对象 -> 相机 ->
// 玩家 -> 物理 -> 音频 -> 纹理 -> 渲染 -> 2D 画布 -> 骨骼 -> 特效 -> 数学 -> GL -> 通用 -> 时间 -> 网络。
// 分文件注册的好处是每个绑定模块自成一摊，加新功能不用改这里（除非开新分组）。
void registerGvtApi(gvts_State* L, GvtsEngineCtx* ctx) {
    openOnlyGlLib(L, ctx);
    Gvtcier_Bind_Core(L, ctx);
    Gvtcier_Bind_Input(L, ctx);
    Gvtcier_Bind_Ui(L, ctx);
    Gvtcier_Bind_UiControls(L, ctx);
    Gvtcier_Bind_Trigger(L, ctx);
    Gvtcier_Bind_Scene(L, ctx);
    Gvtcier_Bind_AnimSm(L, ctx);
    Gvtcier_Bind_Object(L, ctx);
    Gvtcier_Bind_Camera(L, ctx);
    Gvtcier_Bind_Player(L, ctx);
    Gvtcier_Bind_Physics(L, ctx);
    Gvtcier_Bind_Audio(L, ctx);
    Gvtcier_Bind_Texture(L, ctx);
    Gvtcier_Bind_Render(L, ctx);
    Gvtcier_Bind_Canvas2d(L, ctx);
    Gvtcier_Bind_Skeleton(L, ctx);
    Gvtcier_Bind_Effect(L, ctx);
    Gvtcier_Bind_Math(L, ctx);
    Gvtcier_Bind_Gl(L, ctx);
    Gvtcier_Bind_Common(L, ctx);
    Gvtcier_Bind_Time(L, ctx);
    Gvtcier_Bind_Net(L, ctx);
    registerOnlyGlCaps(L, ctx);
}
