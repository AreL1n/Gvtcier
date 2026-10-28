#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <cmath>
#include <algorithm>
#include <string>
#include <windows.h>
#include <gdiplus.h>
#include "Gvtcier.h"
#include "json.hpp"

//按键绑定查询：把抽象动作（前进/跳跃等）翻译成设置里记录的虚拟键码，再问窗口当前是否按下。
//vk 为 0 表示玩家把该动作解绑了，此时永远返回 false，避免把 0 当成某个有效键。
static bool bindDown(const Window& win, const GameSettings& gs, int action) {
    int vk = gs.keyBindings[action];
    if (vk == 0) return false;
    return win.isKeyDown(vk);
}

//把游戏包装载为 DLL 形态：DLL 里导出游戏信息、字节码，可选导出内嵌资源包与封面。
//dllPath 为 UTF-8 路径（Windows 只认宽字符，故先转 wstring）；out 成功时被填充。
//返回 false 时不改动 out，且调用方无需负责卸载模块；返回 true 时 out.module 必须交给 Gvtcier_GamePack_Close 释放。
bool Gvtcier_GamePack_Open(const char* dllPath, GamePack& out) {
    if (!dllPath || !dllPath[0]) return false;
    int wlen = MultiByteToWideChar(CP_UTF8, 0, dllPath, -1, NULL, 0);
    if (wlen <= 1) return false;
    std::wstring wp((size_t)wlen, L'\0');
    MultiByteToWideChar(CP_UTF8, 0, dllPath, -1, &wp[0], wlen);
    //必须用 LoadLibraryW：LoadLibraryA 会按系统 ANSI 代码页解码路径，中文目录下必然打不开。
    HMODULE mod = LoadLibraryW(wp.c_str());
    if (!mod) {
        printf("[gamepack] LoadLibrary failed '%s' err=%lu\n", dllPath, GetLastError());
        return false;
    }
    auto infoFn = (GvtcierGameInfoFn)GetProcAddress(mod, GVTCIER_GAME_INFO_SYMBOL);
    auto codeFn = (GvtcierGameBytecodeFn)GetProcAddress(mod, GVTCIER_GAME_BYTECODE_SYMBOL);
    //两个导出缺一不可：没有信息就拿不到名字与 gpak 名，没有字节码则根本没有可执行的游戏逻辑。
    if (!infoFn || !codeFn) {
        printf("[gamepack] missing export in '%s'\n", dllPath);
        FreeLibrary(mod);
        return false;
    }
    const GvtcierGameInfo* info = infoFn();
    //ABI 号是引擎与游戏 DLL 之间的编译期契约，不一致说明两边的结构体布局可能已经改了，直接用会读到错位内存。
    if (!info || info->abi != GVTCIER_GAME_ABI) {
        printf("[gamepack] abi mismatch in '%s'\n", dllPath);
        FreeLibrary(mod);
        return false;
    }
    uint64_t size = 0;
    const char* code = codeFn(&size);
    if (!code || size == 0) {
        printf("[gamepack] empty bytecode in '%s'\n", dllPath);
        FreeLibrary(mod);
        return false;
    }
    out.module = mod;
    out.info = *info;
    out.bytecode = code;
    out.bytecodeSize = size;
    //资源包与封面是可选导出：老版本游戏 DLL 没有它们，缺了也只是退回外部 gpak 或没有封面，不应当作加载失败。
    auto resFn = (GvtcierGameResourceFn)GetProcAddress(mod, GVTCIER_GAME_RESOURCE_SYMBOL);
    if (resFn) {
        uint64_t rsize = 0;
        const void* rdata = resFn(&rsize);
        if (rdata && rsize > 0) {
            out.resource = rdata;
            out.resourceSize = rsize;
        }
    }
    auto coverFn = (GvtcierGameCoverFn)GetProcAddress(mod, GVTCIER_GAME_COVER_SYMBOL);
    if (coverFn) {
        uint64_t csize = 0;
        const void* cdata = coverFn(&csize);
        if (cdata && csize > 0) {
            out.cover = cdata;
            out.coverSize = csize;
        }
    }
    {
        //外部资源包约定与游戏 DLL 同目录，文件名由游戏信息里的 gpak 字段给出；文件不存在就留空，稍后会退回 DLL 内嵌资源。
        std::string dp = dllPath;
        size_t sl = dp.find_last_of("\\/");
        std::string dir = (sl == std::string::npos) ? std::string(".") : dp.substr(0, sl);
        std::string pakName = (info->gpak && info->gpak[0]) ? std::string(info->gpak) : std::string();
        if (!pakName.empty()) {
            std::string pak = dir + "\\" + pakName;
            if (Gvtcier_W32_FileExistsUtf8(pak.c_str())) out.pakPath = pak;
        }
    }
    printf("[gamepack] loaded '%s' name='%s' gpak='%s' code=%llu bytes res=%llu bytes\n",
           dllPath, info->name ? info->name : "", info->gpak ? info->gpak : "",
           (unsigned long long)size, (unsigned long long)out.resourceSize);
    return true;
}

//卸载游戏 DLL 并清空所有借用其内存的指针。
//字节码、资源、封面都指向 DLL 模块内部，FreeLibrary 之后立刻失效，所以必须一并置空，防止后续误用悬垂指针。
void Gvtcier_GamePack_Close(GamePack& pack) {
    if (pack.module) {
        FreeLibrary((HMODULE)pack.module);
        pack.module = nullptr;
    }
    pack.bytecode = nullptr;
    pack.bytecodeSize = 0;
    pack.resource = nullptr;
    pack.resourceSize = 0;
    pack.cover = nullptr;
    pack.coverSize = 0;
}

namespace {
//gpk 形态没有 DLL 模块可借用内存，所以脚本字节码、封面、meta 这三块要从包里解出来保存在全局缓冲里。
//用全局变量是因为 GamePack 里的指针只持有地址、不持有所有权，必须保证缓冲活到整个运行期。
std::vector<unsigned char> g_gpkBytecode;
std::vector<unsigned char> g_gpkCover;
std::vector<unsigned char> g_gpkMeta;

//扩展名比较，忽略大小写，避免玩家把 .GPK 写成 .GPK 之外的大小写组合时被当成未知格式。
bool endsWithNoCase(const std::string& s, const char* suf) {
    size_t n = strlen(suf);
    return s.size() >= n && _stricmp(s.c_str() + s.size() - n, suf) == 0;
}

//取路径最后一段文件名（同时兼容反斜杠与正斜杠）。
std::string fileNameOf(const std::string& p) {
    size_t sl = p.find_last_of("\\/");
    return (sl == std::string::npos) ? p : p.substr(sl + 1);
}

//取路径所在目录；没有分隔符时返回空串，表示相对当前工作目录。
std::string dirOf(const std::string& p) {
    size_t sl = p.find_last_of("\\/");
    return (sl == std::string::npos) ? std::string() : p.substr(0, sl);
}

//扫描游戏包同目录下的 mods 子目录，把全部 .glc 模组按文件名升序挂成资源覆盖层。
//排序是为了让加载顺序稳定可复现：名字靠后的模组覆盖靠前的，玩家改文件名就能调整优先级。
//同名的 xxx.glc.disabled 文件视为已禁用，跳过不加载。
//新版打包把游戏包放在 <游戏名>_Data 里，所以 mods 也在那一层；老布局的 mod 仍然认。
void loadMods(const char* gpkPath) {
    std::string gpk = gpkPath ? gpkPath : "";
    std::string dir = dirOf(gpk);
    if (dir.empty()) dir = ".";
    std::string modDir = dir + "\\mods";
    if (!Gvtcier_W32_FileExistsUtf8(modDir.c_str())) modDir = dir + "\\mod";
    if (!Gvtcier_W32_FileExistsUtf8(modDir.c_str())) {
        printf("[mod] 无模组目录\n");
        return;
    }
    std::vector<std::string> files = Gvtcier_W32_FindFilesUtf8((modDir + "\\*.glc").c_str());
    if (files.empty()) {
        printf("[mod] 模组目录为空\n");
        return;
    }
    std::vector<std::string> names;
    for (size_t i = 0; i < files.size(); ++i) names.push_back(fileNameOf(files[i]));
    std::sort(names.begin(), names.end());
    int loadedCount = 0;
    for (size_t i = 0; i < names.size(); ++i) {
        std::string full = modDir + "\\" + names[i];
        std::string disabled = full + ".disabled";
        if (Gvtcier_W32_FileExistsUtf8(disabled.c_str())) {
            printf("[mod] 已禁用跳过: %s\n", names[i].c_str());
            continue;
        }
        if (Gvtcier_Res_AddMod(full.c_str())) ++loadedCount;
    }
    printf("[mod] 模组加载完成，共 %d 个\n", loadedCount);
}
}

//以 gpk 资源包形态打开游戏：先挂资源包、再挂模组，然后从包里读取元信息、主脚本、封面。
//meta.json 缺失或解析失败不算致命，名字会退回文件名，游戏仍可运行。
//返回 true 表示至少拿到了名字或字节码之一。
bool Gvtcier_GamePack_OpenGpk(const char* gpkPath, GamePack& out) {
    if (!gpkPath || !gpkPath[0]) return false;
    if (!Gvtcier_Res_SetPackFileOk(gpkPath)) {
        printf("[gamepack] gpk 打开失败: %s\n", gpkPath);
        return false;
    }
    loadMods(gpkPath);
    out.pakPath = gpkPath;
    out.name = std::string();
    out.description = std::string();
    out.author = std::string();

    g_gpkMeta.clear();
    //meta.json 只读主包，不走模组层：游戏身份信息不应被模组改写。
    if (Gvtcier_Res_ReadMain("meta.json", g_gpkMeta) && !g_gpkMeta.empty()) {
        try {
            auto j = nlohmann::json::parse(std::string(g_gpkMeta.begin(), g_gpkMeta.end()));
            auto itName = j.find("name");
            if (itName != j.end() && itName->is_string()) out.name = itName->get<std::string>();
            auto itDesc = j.find("description");
            if (itDesc != j.end() && itDesc->is_string()) out.description = itDesc->get<std::string>();
            auto itAuthor = j.find("author");
            if (itAuthor != j.end() && itAuthor->is_string()) out.author = itAuthor->get<std::string>();
        } catch (...) {
            printf("[gamepack] meta.json 解析失败\n");
        }
    }
    if (out.name.empty()) {
        //没有 meta.json 时用包文件名兜底，保证窗口标题与主菜单标题不会是空白。
        std::string p = gpkPath;
        size_t sl = p.find_last_of("\\/");
        std::string base = (sl == std::string::npos) ? p : p.substr(sl + 1);
        size_t dot = base.find_last_of('.');
        out.name = (dot == std::string::npos) ? base : base.substr(0, dot);
    }

    g_gpkBytecode.clear();
    //主脚本走 ReadFile 而不是 ReadMain，允许 mod 用同名 game.gvts 覆盖游戏逻辑。
    if (Gvtcier_Res_ReadFile("game.gvts", g_gpkBytecode) && !g_gpkBytecode.empty()) {
        out.bytecode = (const char*)g_gpkBytecode.data();
        out.bytecodeSize = g_gpkBytecode.size();
    }

    g_gpkCover.clear();
    //封面固定从主包取，模组不应替换游戏封面。
    if (Gvtcier_Res_ReadMain("cover.png", g_gpkCover) && !g_gpkCover.empty()) {
        out.cover = g_gpkCover.data();
        out.coverSize = g_gpkCover.size();
    }

    out.info.name = out.name.c_str();
    out.info.description = out.description.c_str();
    out.info.author = out.author.c_str();

    printf("[gamepack] gpk '%s' name='%s' code=%llu cover=%llu\n",
           gpkPath, out.name.c_str(),
           (unsigned long long)out.bytecodeSize, (unsigned long long)out.coverSize);
    return !out.name.empty() || out.bytecodeSize > 0;
}

//无窗口服务端模式：只跑网络层，不建窗口、不初始化图形，适合在服务器上开房间。
//这个函数是死循环，正常不会返回；启动失败返回 1。返回 0 的分支实际上不可达。
static int Gvtcier_RunHeadlessServer(uint16_t port, const char* room) {
    Gvtcier_Net_Init(port);
    //房间上限写死 32 人，是目前网络层的容量假设。
    if (!Gvtcier_Net_StartServer(port, room, 32)) {
        GvtcierError::engine("net", u8"服务端启动失败");
        return 1;
    }
    printf("[net] headless server on port %u room %s\n", (unsigned)port, room);
    for (;;) {
        //固定按 60Hz 逻辑步进；Sleep(16) 只是让出 CPU，真实节拍由 Net_Tick 内部的时间推进决定。
        Gvtcier_Net_Tick(1.0f / 60.0f);
        Sleep(16);
    }
    return 0;
}

//启动阶段的上下文汇总。所有成员都是借用（不持有所有权），由 Gvtcier_Run 里的栈对象提供，
//各阶段函数通过它拿到需要的子系统，避免层层传参；同时 bootFail 也靠它在失败时统一逆序释放。
struct BootCtx {
    Window* win = nullptr;
    Scene* scene = nullptr;
    Renderer* renderer = nullptr;
    Splash* splash = nullptr;
    GamePack* pack = nullptr;
    Player* player = nullptr;
    PlayerSettings* psBase = nullptr;
    PhysicsWorld* physics = nullptr;
    GvtsBind* gvts = nullptr;
    Camera* camera = nullptr;
    Canvas2D* canvas2d = nullptr;
    const char* title = nullptr;
    const char* gvtsFile = nullptr;
    bool dev = false;
    bool licensed = false;
};

//把 BootCtx 交给渲染器的用户绘制回调，让脚本侧的即时模式 OpenGL 代码插进渲染管线。
static void userGlDrawThunk(void* user) {
    GvtsBind* gvts = (GvtsBind*)user;
    if (gvts) gvts->draw();
}

//启动失败统一善后：打印错误、按与初始化相反的顺序关掉已经起来的子系统，再退出 GDI+。
//用 static 标志保证只清一次，因为多个阶段都可能调用它，重复释放会崩。
static void bootFail(BootCtx& c, ULONG_PTR gdiplusToken, const char* where, const char* msg) {
    static bool cleaned = false;
    GvtcierError::engine(where, msg);
    if (cleaned) return;
    cleaned = true;
    if (c.gvts) c.gvts->shutdown();
    if (c.canvas2d) c.canvas2d->Gvtcier_Canvas2D_Shutdown();
    if (c.physics) c.physics->Gvtcier_Physics_Shutdown();
    if (c.renderer) c.renderer->Gvtcier_Render_Shutdown();
    if (c.scene) c.scene->Gvtcier_Scene_Destroy();
    if (c.win) c.win->Gvtcier_Window_Destroy();
    if (c.pack) {
        Gvtcier_GamePack_Close(*c.pack);
        c.pack = nullptr;
    }
    Gdiplus::GdiplusShutdown(gdiplusToken);
}

//启动流程第二步：初始化渲染器与空场景，并开始播放启动画面。
//此时游戏包还没解析，窗口也不接收输入，玩家看到的只是一个可转视角的启动画面。
static bool loadEngineCore(BootCtx& c, ULONG_PTR gdiplusToken) {
    Renderer& renderer = *c.renderer;
    Scene& scene = *c.scene;
    Window& win = *c.win;

    if (!renderer.Gvtcier_Render_Init()) {
        bootFail(c, gdiplusToken, "renderer", u8"渲染器初始化失败");
        return false;
    }
    if (!scene.Gvtcier_Scene_Build()) {
        bootFail(c, gdiplusToken, "scene", u8"场景构建失败");
        return false;
    }
    //场景构建产出的是空壳，天空盒/太阳/体积光/雾这四套默认环境要素在这里补齐，
    //这样即使游戏不带地图，启动画面也有东西可看。
    scene.Gvtcier_Scene_InitSkybox();
    scene.Gvtcier_Scene_InitSun();
    scene.Gvtcier_Scene_InitVolumeLight();
    scene.Gvtcier_Scene_InitFog();

    //启动画面播放期间关闭输入，防止玩家按键穿透到尚未初始化的游戏逻辑。
    win.Gvtcier_Window_SetInputEnabled(false);
    c.splash->Gvtcier_Splash_SetDev(c.dev);
    c.splash->Gvtcier_Splash_SetLicensed(c.licensed);
    c.splash->Gvtcier_Splash_Begin(scene, 0);
    return true;
}

//设置下发在文件后面才定义，开场阶段要用到，先在这里打个招呼
static void applySettingsToEngine(Renderer& renderer, Scene& scene, Camera& camera);

namespace {
//开场四个阶段。玩家在 Logo 底下等的时候，就是这几段在跑，跑完才切主菜单。
enum BootStage {
    BOOT_DEVICE = 0,   // 设备：图形、音频这些底层东西起起来，顺便把版本和型号记进日志
    BOOT_CHECK = 1,    // 自检：每个组件挨个过一遍，谁起不来当场就记下来
    BOOT_GAME = 2,     // 进游戏：天空盒 → 碰撞 → 几何 → 后处理(含太阳) → 玩家，一步都不能抢先
    BOOT_SWITCH = 3,   // 收尾：Logo 淡出，进主菜单
    BOOT_STAGE_COUNT
};

//把启动画面那一份进度写成日志里的一行，只在阶段切换时写，免得刷屏。
void logBootStage(int stage) {
    switch (stage) {
        case BOOT_DEVICE: rlog::Gvtcier_Log_Stage(0, u8"初始化设备"); break;
        case BOOT_CHECK: rlog::Gvtcier_Log_Stage(1, u8"组件自检"); break;
        case BOOT_GAME: rlog::Gvtcier_Log_Stage(2, u8"加载资源"); break;
        case BOOT_SWITCH: rlog::Gvtcier_Log_Stage(3, u8"切入主菜单"); break;
        default: break;
    }
}
}

//启动流程第三步：加载游戏内容，顺序是地图 → 物理 → Gvts 解释器 → 音频 → 网络 → 2D 画布。
//之所以按这个顺序，是因为解释器需要物理与玩家已经就绪，脚本一跑就会直接调用它们。
//
//本类把这段加载切成若干个阶段，由启动画面循环逐帧推进。
//真正的并行做不到：脚本一跑就会创建网格与纹理，全都要求当前线程持有 GL 上下文，
//所以没法丢到后台线程去。改成逐阶段推进至少能让 Logo 在加载期间保持响应，
//玩家不会看到窗口假死。
class LoadStages {
public:
    LoadStages(BootCtx& c, ULONG_PTR token) : c_(c), token_(token) {}

    bool done = false;
    bool failed = false;
    //当前跑到第几段，启动循环靠它决定 Logo 底下还剩多少活
    int stage = BOOT_DEVICE;

    // 推进一步，每次执行一个阶段。阶段跑完把 stage 加一，越界即完成。
    void step() {
        if (done || failed) return;
        //阶段标题只在刚进这一阶段时写一次，阶段内部的小步不重复刷
        if (stage != lastLogged_) {
            logBootStage(stage);
            lastLogged_ = stage;
        }
        bool stageFinished = true;
        switch (stage) {
            case BOOT_DEVICE: bootDevice(); break;
            case BOOT_CHECK: bootCheck(); break;
            //第 2 阶段里面还分五小步，一步没走完这阶段就不算完
            case BOOT_GAME: stageFinished = stepGame(); break;
            case BOOT_SWITCH: bootSwitch(); break;
            default: break;
        }
        if (failed) return;
        if (!stageFinished) return;
        ++stage;
        if (stage >= BOOT_STAGE_COUNT) done = true;
    }

private:
    void fail(const char* where, const char* msg) {
        bootFail(c_, token_, where, msg);
        failed = true;
    }

    // 第 0 阶段：日志里报清楚这台机器上跑的是什么。
    // 图形与音频的句柄在这之前已经建好了，这里只是把它们的来头记下来。
    void bootDevice() {
        const GpuProfile& gp = Gvtcier_Gpu();
        char ver[64];
        snprintf(ver, sizeof(ver), "%d.%d", gp.glMajor, gp.glMinor);
        // 后端名与版本都按实际情况报，别写死，不然换后端以后日志会骗人
        const rhi::Backend be = rhi::Gvtcier_Rhi_Backend();
        if (be == rhi::Backend::Vulkan) {
            // Vulkan 版本号取自物理设备的 apiVersion，比通用的 glMajor.glMinor 有意义
            char vkVer[64];
            snprintf(vkVer, sizeof(vkVer), "%d.%d", gp.glMajor, gp.glMinor);
            rlog::Gvtcier_Log_Banner("Vulkan", true, vkVer);
        } else {
            const char* name = (be == rhi::Backend::Gles) ? "OpenGL ES" : "OpenGL";
            rlog::Gvtcier_Log_Banner(name, true, ver);
        }
        rlog::Gvtcier_Log_BannerLine("GPU", gp.rendererName[0] ? gp.rendererName : gp.profileName);
        rlog::Gvtcier_Log_BannerLine("厂商", gp.vendorName);

        bool audioOk = Gvtcier_Audio_Init();
        const char* dev = audioOk ? Gvtcier_Audio_DeviceName() : "";
        rlog::Gvtcier_Log_Banner("Audio", audioOk, "");
        rlog::Gvtcier_Log_BannerLine("声卡", dev);

        rlog::Gvtcier_Log_Banner("Window", true, "");
        char wh[64];
        snprintf(wh, sizeof(wh), "%dx%d", c_.win->width(), c_.win->height());
        rlog::Gvtcier_Log_BannerLine("画面", wh);
        if (!audioOk) {
            // 没声卡不影响玩，只是后面界面音效那几条自检会跟着失败。
            rlog::Gvtcier_Log_Write(u8"[Audio] 没有可用输出设备，游戏继续\n");
        }
    }

    // 第 1 阶段：所有组件挨个自检。只报结果，不拦启动，谁挂了都记一笔接着走。
    void bootCheck() {
        rlog::Gvtcier_Log_Check(u8"渲染器", true, nullptr);
        rlog::Gvtcier_Log_Check(u8"场景", true, nullptr);
        rlog::Gvtcier_Log_Check(u8"物理世界", c_.physics != nullptr, nullptr);

        bool gvtsOk = c_.gvts->init();
        rlog::Gvtcier_Log_Check(u8"Gvts 解释器", gvtsOk, nullptr);
        if (gvtsOk) {
            //解释器起好了就把后面要接触的子系统一次交过去，脚本一跑就调得上
            Scene& scene = *c_.scene;
            Player& player = *c_.player;
            c_.gvts->setScene(&scene);
            c_.gvts->setPhysics(c_.physics);
            c_.gvts->setPlayer(&player);
            c_.gvts->setCamera(c_.camera);
            c_.gvts->setWindow(c_.win);
            c_.gvts->setFx(&c_.renderer->Gvtcier_Render_Fx());
            c_.gvts->setRenderer(c_.renderer);
        }

        rlog::Gvtcier_Log_Check(u8"2D 画布", c_.canvas2d->Gvtcier_Canvas2D_Init(), nullptr);
        c_.gvts->setCanvas2D(c_.canvas2d);

        //界面音效两个都加载上才算过，缺一个就写清楚是哪一条
        GvtUiSound::Gvtcier_UiSound_Init("7_4", "7_3");
        bool hoverOk = GvtUiSound::Gvtcier_UiSound_Loaded(GvtUiSound::UiHover);
        bool pressOk = GvtUiSound::Gvtcier_UiSound_Loaded(GvtUiSound::UiPress);
        rlog::Gvtcier_Log_Check(u8"界面音效 掠过", hoverOk, GvtUiSound::Gvtcier_UiSound_Resource(GvtUiSound::UiHover));
        rlog::Gvtcier_Log_Check(u8"界面音效 按下", pressOk, GvtUiSound::Gvtcier_UiSound_Resource(GvtUiSound::UiPress));

        Gvtcier_Net_Init((uint16_t)Gvtcier_Settings().netPort);
        Gvtcier_Net_SetScene(c_.scene);
        Gvtcier_Net_SetPlayer(c_.player);
        rlog::Gvtcier_Log_Check(u8"网络", true, nullptr);
    }

    // 第 2 阶段：把游戏要用到的资源全过一遍。
    // 先天上地下把图片、音频、模型这些底料都备齐，再谈碰撞与几何。
    // 五小步的结果先攒着，整段跑完一次性列出来，日志里不用一截一截地看。
    bool stepGame() {
        switch (gameStep_) {
            case 0: preloadMedia(); break;
            case 1: gameCollision(); break;
            case 2: gameGeometry(); break;
            case 3: gamePost(); break;
            case 4: gamePlayer(); break;
            default: break;
        }
        if (failed) return false;
        ++gameStep_;
        if (gameStep_ < 5) return false;
        flushStageLines();
        return true;
    }

    // 记一条本阶段的结果，等这一阶段全部跑完再一起打
    void stageLine(const std::string& line) { stageLines_.push_back(line); }

    // 把攒下来的结果一次写完
    void flushStageLines() {
        for (size_t i = 0; i < stageLines_.size(); ++i) {
            rlog::Gvtcier_Log_Write("%s\n", stageLines_[i].c_str());
        }
        stageLines_.clear();
    }

    // 0 资源：贴图、音频分两批过。数量多，按批记总数，个别失败才单独点出来。
    void preloadMedia() {
        Scene& scene = *c_.scene;
        int texOk = 0, texFail = 0;
        for (int i = 0; i < kGvVeinTexCount; ++i) {
            if (scene.Gvtcier_Scene_PreloadTex(kGvVeinTexs[i].res)) {
                ++texOk;
            } else {
                ++texFail;
                stageLine(std::string(u8"[资源] ") + kGvVeinTexs[i].name + u8" 失败: 贴图没解开");
            }
        }
        stageLine(u8"[资源] 贴图 " + std::to_string(texOk) + u8" / " + std::to_string(kGvVeinTexCount)
                  + (texFail == 0 ? u8" 成功" : u8" 有失败"));

        int sndOk = 0, sndFail = 0;
        for (int i = 0; i < kGvAudioSndCount; ++i) {
            if (Gvtcier_Audio_CanLoad(kGvAudioSnds[i].name)) {
                ++sndOk;
            } else {
                ++sndFail;
                stageLine(std::string(u8"[资源] ") + kGvAudioSnds[i].name + u8" 失败: 音频没解开");
            }
        }
        stageLine(u8"[资源] 音频 " + std::to_string(sndOk) + u8" / " + std::to_string(kGvAudioSndCount)
                  + (sndFail == 0 ? u8" 成功" : u8" 有失败"));
    }

    // 1 碰撞：物理世界建起来，几何才有地方站
    void gameCollision() {
        bool ok = c_.physics->Gvtcier_Physics_Init(*c_.scene, *c_.player, *c_.psBase);
        stageLine(std::string(u8"[资源] 碰撞 ") + (ok ? u8"成功" : u8"失败: 物理世界初始化失败"));
        if (!ok) fail("physics", u8"物理世界初始化失败");
    }

    // 2 几何：天空、太阳、地图、脚本都归这一类，场景从这里开始有形
    void gameGeometry() {
        Scene& scene = *c_.scene;
        scene.Gvtcier_Scene_InitSkybox();
        scene.Gvtcier_Scene_InitSun();
        stageLine(u8"[资源] 天空盒 成功");
        stageLine(std::string(u8"[资源] 太阳 ") + (scene.sunEnabled ? u8"成功" : u8"失败: 未启用"));

        loadMap();
        if (failed) return;
        prepareCode();
        if (failed) return;
        if (!c_.gvts->run(code_)) {
            stageLine(u8"[资源] 几何 失败: 游戏字节码执行失败");
            fail("gvts", u8"加载游戏字节码失败");
            return;
        }
        //脚本里通常还会自己调一次 加载_地图，地图对象那会儿才真正进场景。
        //物理是在地图之前建的，角色还停在空场景的出生点，这里必须把人放到地面上，
        //否则角色胶囊中心正好卡进地面，玩家看着能穿模、也走不动。
        resyncPlayerAfterMap();
        stageLine(u8"[资源] 几何 成功");
    }

    // 地图就位后重新摆玩家：优先用地图里标的出生点，没有就按默认位置往上抬一点，
    // 让脚底离地留出余量，落到地面上而不是嵌在里面。
    void resyncPlayerAfterMap() {
        Scene& scene = *c_.scene;
        Player& player = *c_.player;
        bool found = false;
        scene.ForEachObject([&](const Object& po, GvtId<Object>) {
            if (found) return;
            if (po.kind != ObjKind::Player) return;
            found = true;
            player.pos = po.pos;
            if (po.moveSpeed > 0.1f) c_.psBase->speed = po.moveSpeed;
            c_.psBase->jumpSpeed = std::sqrt(2.0f * scene.gravity * std::fmax(po.jumpHeight, 0.1f));
        });
        if (!found) player.pos.y += kPlayerTotalHalf + 0.2f;
        c_.physics->Gvtcier_Physics_SetPlayerPos(player.pos);
        c_.camera->pos = player.pos + Vec3(0, player.eyeHeight, 0);
    }

    // 3 后处理：渲染开关全部下发，体积光、雾、阴影、Bloom 从这一帧起生效
    void gamePost() {
        Scene& scene = *c_.scene;
        scene.Gvtcier_Scene_InitVolumeLight();
        scene.Gvtcier_Scene_InitFog();
        //后处理这一坨全在设置里，主机这边只负责把它们推给渲染器
        applySettingsToEngine(*c_.renderer, scene, *c_.camera);
        stageLine(u8"[资源] 后处理 成功");
        stageLine(std::string(u8"[资源] 太阳阴影 ") + (scene.showShadow ? u8"成功" : u8"关闭"));
    }

    // 4 玩家：把人放到出生点，相机跟上
    void gamePlayer() {
        Player& player = *c_.player;
        c_.camera->pos = player.pos + Vec3(0, player.eyeHeight, 0);
        c_.camera->yawDeg = player.yawDeg;
        c_.camera->pitchDeg = player.pitchDeg;
        stageLine(u8"[资源] 玩家 成功");
    }

    // 第 3 阶段：前面都成了，把输入放开，Logo 自己淡出，主菜单接上。
    void bootSwitch() {
        c_.win->Gvtcier_Window_SetInputEnabled(true);
        rlog::Gvtcier_Log_Write(u8"[主菜单] 进入\n");
    }

    // 地图是可选内容，没有地图就以空场景继续，由脚本自行搭建。
    void loadMap() {
        Scene& scene = *c_.scene;
        std::string gmapPath = "test_scene.gmap";
        if (c_.gvtsFile && c_.gvtsFile[0]) {
            std::string gf = c_.gvtsFile;
            std::string::size_type slash = gf.find_last_of("\\/");
            if (slash != std::string::npos) gmapPath = gf.substr(0, slash + 1) + "test_scene.gmap";
        }
        if (!Gvtcier_Res_Exists(gmapPath.c_str())) {
            rlog::Gvtcier_Log_Asset(u8"地图", true, u8"未附带 gmap，空场景");
            return;
        }
        if (!scene.Gvtcier_Scene_LoadGmap(gmapPath.c_str())) {
            rlog::Gvtcier_Log_Asset(u8"地图", false, u8"解析失败，以空场景继续");
            return;
        }
        //地图对象必须一个个登记进物理，否则只有画面没有碰撞，玩家会直接穿过去。
        //物理世界在这一步之前已经建好，所以能直接加。
        scene.ForEachObject([&](const Object& o, GvtId<Object> id) {
            c_.physics->Gvtcier_Physics_AddObject(o, id);
        });
        rlog::Gvtcier_Log_Asset(u8"地图", true, gmapPath.c_str());
        //地图里若放了 Player 出生点对象，优先级高于默认出生点：位置直接覆盖玩家，移动速度与跳跃高度反推成玩家参数。
        //跳跃初速度由 v = sqrt(2*g*h) 得到，也就是让玩家的跳跃顶点正好等于地图里设定的 jumpHeight。
        Player& player = *c_.player;
        scene.ForEachObject([&](const Object& po, GvtId<Object>) {
            if (po.kind != ObjKind::Player) return;
            player.pos = po.pos;
            if (po.moveSpeed > 0.1f) c_.psBase->speed = po.moveSpeed;
            c_.psBase->jumpSpeed = std::sqrt(2.0f * scene.gravity * std::fmax(po.jumpHeight, 0.1f));
        });
    }

    void prepareCode() {
        if (c_.gvtsFile && c_.gvtsFile[0]) {
            //裸脚本模式：脚本文件从资源层（含模组层、主包层、磁盘）读取。
            std::vector<unsigned char> raw;
            if (!Gvtcier_Res_ReadFile(c_.gvtsFile, raw) || raw.empty()) {
                fail("gvts", u8"读取 Gvts 脚本失败");
                return;
            }
            code_.assign(raw.begin(), raw.end());
        } else {
            //gpk 模式：主脚本来自包里解出的 game.gvts。
            code_.assign(c_.pack->bytecode, (size_t)c_.pack->bytecodeSize);
        }
    }

    BootCtx& c_;
    ULONG_PTR token_;
    int gameStep_ = 0;
    //上一次写过标题的阶段号，用来避免同一阶段重复打印
    int lastLogged_ = -1;
    //本阶段攒下来的结果行，阶段跑完一起打进日志
    std::vector<std::string> stageLines_;
    std::string code_;
};

//把帧率档位枚举翻译成每秒帧数；返回 0 表示不限制（由垂直同步或硬件决定）。
static int frameRateCapOf(const GameSettings& s) {
    switch (s.frameRate) {
        case FRAMERATE_30: return 30;
        case FRAMERATE_60: return 60;
        case FRAMERATE_90: return 90;
        case FRAMERATE_120: return 120;
        case FRAMERATE_UNLIMITED: return 0;
        default: return 0;
    }
}

//设置下发链路的核心：把一份 GameSettings 分别推给场景、相机、RHI 与渲染器。
//场景与相机负责数据（哪些效果开、FOV 多少），渲染器负责把这些开关翻译成管线状态与资源重分配。
//垂直同步是 RHI 层的能力，所以单独调用；FXAA 走独立入口，其余画质项统一走 Render_ApplySettings。
static void applySettingsToEngine(Renderer& renderer, Scene& scene, Camera& camera) {
    GameSettings& s = Gvtcier_Settings();
    scene.hdr = s.hdr;
    //下一行是整条链路上最冤的一行。bloomStrength 写进 scene 之后，
    //渲染器压根不读它（混合强度写死成 0.22），所以这是个"传了个寂寞"的赋值。
    //更要命的是 scene.bloomStrength 还有默认值 0.65 —— 这个数从来没上过 GPU，
    //只在快照和调试里飘着，看起来很像真的在工作。
    scene.bloomStrength = s.bloomStrength;
    scene.volumeLightEnabled = s.volumeLight;
    scene.gtaoEnabled = s.gtao;
    //阴影档位只有"关"和"开若干级"两种语义，开关标志从档位推导，避免两个字段互相矛盾。
    scene.showShadow = (s.shadowQuality > SHADOW_OFF);
    camera.fov = s.fov;
    rhi::Gvtcier_Rhi_SetVsync(s.frameRate == FRAMERATE_VSYNC);
    renderer.Gvtcier_Render_SetFxaa(s.fxaa ? 1 : 0);
    renderer.Gvtcier_Render_ApplySettings(s.shadowQuality, s.anisotropic, s.specular,
                                          s.autoExposure, s.lightQuality, s.fogQuality, s.vignetteStrength);
    //这份函数是"三个配置源"的现场。GameSettings 一份、Scene 一份、
    //Renderer 内部私有一份（vignette_/fogLevel_/autoExposureOn_），彼此不同步。
    //后果之一：脚本用 渲染.设置_环境光遮蔽() 改了 Scene，玩家一开设置菜单，
    //这里就把 Scene 覆盖回去，脚本设置无声消失。
    //后果之二：GameSettings 里的 occlusion 字段连这个函数都没进来，桌面端永远是默认值。
    //所以以后看到"改了没用"或者"改完又被还原"，先来这儿看看是不是没进这条链。
}

//主循环的共享状态。所有成员都指向 Gvtcier_Run 栈上的对象，生命周期覆盖整个游戏循环；
//把状态打包成结构体是为了让各个辅助函数不必接收十几个参数。
struct MainLoop {
    Window* win = nullptr;
    Renderer* renderer = nullptr;
    Scene* scene = nullptr;
    Player* player = nullptr;
    PhysicsWorld* physics = nullptr;
    GvtsBind* gvts = nullptr;
    Camera* camera = nullptr;
    Canvas2D* canvas2d = nullptr;
    Hud* hud = nullptr;
    PauseMenu* menu = nullptr;
    bool showDebugPanel = false;
    bool showWireframe = false;
    RenderOptions opt;
    //上一物理步的玩家位置。渲染时在它和目标位置之间做插值，抵消固定步长与渲染帧率不一致造成的抖动。
    Vec3 prevCamPos;
    //最近一次物理步的时间戳（秒）。与 kStep 一起算插值系数，也用于判断本帧是否需要补步。
    double lastPhys = 0.0;
    //进入游戏循环的时刻。HUD 的计时显示用它算相对时长，而不是系统绝对时间。
    double startTime = 0.0;
    //场景里代表玩家的那个对象。用 ID 而不是指针，玩家对象被脚本删掉重建后旧 ID 会自动失效，这里能重新找到新的。
    GvtId<Object> playerObj;
    //是否处于引擎自带的自由飞行相机模式（camera.mode == 1）。开启时鼠标行为与正常游玩不同。
    bool engineCamEnabled = false;
    //右键上一帧是否按下。用边沿检测只在状态变化时抓放鼠标，避免每帧重复调用系统 API。
    bool rmbPrev = false;
    //碰撞体线框的顶点缓冲。第一帧按需分配，容量不够时成倍扩容，避免每帧重新分配。
    std::vector<float> dbgLineBuf;
};

//暂停菜单的输入处理：更新菜单、响应设置改动。
//设置一旦变化必须立刻下发到引擎并落盘，否则玩家关掉窗口后设置会丢失。
static void handleMenuInput(MainLoop& m, const MenuInput& mi, bool justOpened, bool escEdge) {
    Scene& scene = *m.scene;
    Renderer& renderer = *m.renderer;
    Window& win = *m.win;
    PauseMenu& menu = *m.menu;
    menu.Gvtcier_Menu_Update(mi, win.width(), win.height(), true);
    if (menu.Gvtcier_Menu_ConsumeSettingsChanged()) {
        applySettingsToEngine(renderer, scene, *m.camera);
        win.Gvtcier_Window_ApplyMode(Gvtcier_Settings().windowMode);
        Gvtcier_Settings_Save();
    }
}

//切到引擎自由相机：相机参数从玩家状态拷一份作为起点，同时放开鼠标锁定，
//这样开发者可以用右键 + WASD 在场景里自由飞行观察。
static void enterEngineCamera(MainLoop& m) {
    Player& player = *m.player;
    Camera& camera = *m.camera;
    Window& win = *m.win;
    m.engineCamEnabled = true;
    camera.pos = player.pos + Vec3(0, player.eyeHeight, 0);
    camera.yawDeg = player.yawDeg;
    camera.pitchDeg = player.pitchDeg;
    win.Gvtcier_Window_SetAutoGrab(false);
    win.Gvtcier_Window_GrabMouse(false);
}

//退出自由相机：恢复自动抓鼠标，并按场景的 captureMouse 决定是否重新锁定，
//否则玩家回到正常游玩时会处于鼠标未捕获状态。
static void leaveEngineCamera(MainLoop& m) {
    Window& win = *m.win;
    m.engineCamEnabled = false;
    win.Gvtcier_Window_SetAutoGrab(true);
    win.Gvtcier_Window_GrabMouse(m.scene->captureMouse);
}

//自由相机每帧更新：按住右键才锁定并转视角，WASD 水平移动，QE 升降，Shift 加速。
//用右键按住作为"观察"的开关，是为了能在不退出游戏的情况下把鼠标交还给系统去点别的窗口。
static void updateEngineCamera(MainLoop& m, float dt) {
    Window& win = *m.win;
    Camera& camera = *m.camera;
    bool rmb = win.isKeyDown(VK_RBUTTON);
    if (rmb != m.rmbPrev) {
        win.Gvtcier_Window_GrabMouse(rmb);
        m.rmbPrev = rmb;
    }
    if (rmb) {
        int mx = 0, my = 0;
        win.Gvtcier_Window_MouseDelta(mx, my);
        float sens = Gvtcier_Settings().mouseSensitivity;
        camera.turn((float)mx * sens, (float)my * sens);
    }
    float fly = camera.flySpeed;
    //Shift 加速 3 倍，方便在大地图上快速跨越，不改设置里的基础速度。
    if (win.isKeyDown(VK_SHIFT) || win.isKeyDown(VK_LSHIFT) || win.isKeyDown(VK_RSHIFT)) fly *= 3.0f;
    Vec3 fwd = camera.forward();
    Vec3 right = dirRightFlat(camera.yawDeg);
    Vec3 vel(0, 0, 0);
    if (win.isKeyDown('W')) vel = vel + fwd;
    if (win.isKeyDown('S')) vel = vel - fwd;
    if (win.isKeyDown('D')) vel = vel + right;
    if (win.isKeyDown('A')) vel = vel - right;
    if (win.isKeyDown('E')) vel = vel + Vec3(0, 1, 0);
    if (win.isKeyDown('Q')) vel = vel - Vec3(0, 1, 0);
    if (vel.length() > 1e-5f) camera.pos = camera.pos + vel.normalized() * (fly * dt);
    camera.rollRad = 0.0f;
}

//正常游玩时的相机与玩家控制：鼠标转视角并同步给玩家朝向，按设置里的键位驱动移动；
//scene.inputLocked 为真时（脚本接管输入或过场）只清空移动状态，不让玩家走动。
//运动相机开关打开时，再叠加由速度和落地状态驱动的镜头晃动。
static void updateGameCamera(MainLoop& m, float dt) {
    Window& win = *m.win;
    Scene& scene = *m.scene;
    Player& player = *m.player;
    PhysicsWorld& physics = *m.physics;
    Camera& camera = *m.camera;
    if (!m.engineCamEnabled) {
        int mx = 0, my = 0;
        win.Gvtcier_Window_MouseDelta(mx, my);
        float sens = Gvtcier_Settings().mouseSensitivity;
        if (!scene.inputLocked) {
            camera.turn((float)mx * sens, (float)my * sens);
            player.yawDeg = camera.yawDeg;
            const GameSettings& gs = Gvtcier_Settings();
            player.move(bindDown(win, gs, ACTION_FORWARD), bindDown(win, gs, ACTION_BACK),
                        bindDown(win, gs, ACTION_LEFT), bindDown(win, gs, ACTION_RIGHT));
            player.running = bindDown(win, gs, ACTION_SPRINT);
        } else {
            player.move(false, false, false, false);
            player.running = false;
        }
        if (Gvtcier_Settings().motionCamera)
            cameraMotionSway(camera, physics.Gvtcier_Physics_PlayerVelocity(), dt,
                             physics.Gvtcier_Physics_PlayerGrounded());
    } else {
        player.move(false, false, false, false);
        player.running = false;
    }
}

//F3 调试面板开关。必须做边沿检测，否则按住 F3 时面板每帧翻转，看起来像在闪。
static void toggleDebugPanel(MainLoop& m, bool dbgKey, bool& f3Prev) {
    if (dbgKey && !f3Prev) {
        m.showDebugPanel = !m.showDebugPanel;
    }
    f3Prev = dbgKey;
}

//收集碰撞体线框顶点交给渲染器。
//物理层返回的是需要的 float 数量；若一次没装下就把缓冲翻倍重试，最多重试 8 次，
//用次数上限代替死循环，防止物理层返回异常值时无限扩容把内存吃光。
static void updateDebugLines(MainLoop& m) {
    Renderer& renderer = *m.renderer;
    PhysicsWorld& physics = *m.physics;
    if (m.opt.debugCollider) {
        if (m.dbgLineBuf.empty()) m.dbgLineBuf.resize(4096 * 7);
        int n = 0;
        for (int guard = 0; guard < 8; ++guard) {
            n = physics.Gvtcier_Physics_CollectWireframe(m.dbgLineBuf.data(), (int)m.dbgLineBuf.size());
            if (n < (int)m.dbgLineBuf.size()) break;
            m.dbgLineBuf.resize(m.dbgLineBuf.size() * 2);
        }
        renderer.Gvtcier_Render_SetDebugLines(m.dbgLineBuf.data(), n);
    } else {
        renderer.Gvtcier_Render_SetDebugLines(nullptr, 0);
    }
}

//固定步长物理驱动的核心，也是整个游戏循环里最关键的一段。
//kStep 固定为 1/60 秒，物理、动画、脚本 tick 一律按它推进，因此游戏表现与渲染帧率解耦：
//不论显示器是 60Hz 还是 144Hz、这一帧渲染花了多久，物理走过的总时间都是一致的，结果可复现。
//acc 是时间累加器，把本帧真实耗时 dt 攒起来，每攒够一步就消化掉一步，剩下的零头留给下一帧。
//顺序上先物理与动画、再脚本、最后刷新变换与子物体同步，保证脚本读到的是本步最终的物理状态。
static void stepSimulation(MainLoop& m, float dt, double t, float kStep, float& acc, bool& jumpRequested) {
    Scene& scene = *m.scene;
    Player& player = *m.player;
    PhysicsWorld& physics = *m.physics;
    GvtsBind& gvts = *m.gvts;
    //跳跃请求只保留一帧：按下瞬间若本帧还没执行物理步也要记住，执行完立刻清除，避免长按连跳。
    if (jumpRequested && !scene.inputLocked) player.jumpRequested = true;
    jumpRequested = false;
    if (!scene.paused) {
        acc += dt;
        //卡顿钳制：单帧最多补 0.25 秒，即最多 15 个物理步。
        //若不钳制，一次长卡顿（断点、磁盘停顿、窗口拖动）后 acc 会攒到几秒，
        //接下来会连续补几百步物理，造成"时间跳变"——角色瞬移穿墙、脚本回调被刷爆。
        //宁可让游戏时间短暂慢于真实时间，也不能让物理一步跨过头。
        if (acc > 0.25f) acc = 0.25f;
        while (acc >= kStep) {
            //记录步前位置，用于渲染插值。
            m.prevCamPos = player.pos;
            //物理步的时间再乘场景时间缩放，慢动作/加速时物理与动画一起变慢变快。
            float sdt = kStep * scene.timeScale;
            physics.Gvtcier_Physics_Step(player, scene, sdt);
            scene.Gvtcier_Scene_AnimTick(sdt);
            //缓存 playerObj 失效（对象被脚本删除）时重新扫描一次场景找新的玩家对象，
            //只在失效时扫，正常情况不额外遍历场景。
            const Object* po = scene.Gvtcier_Scene_GetObject(m.playerObj);
            if (!po || po->kind != ObjKind::Player) {
                m.playerObj = GvtId<Object>{};
                scene.ForEachObject([&](const Object& o, GvtId<Object> id) {
                    if (m.playerObj.valid()) return;
                    if (o.kind == ObjKind::Player) m.playerObj = id;
                });
            }
            if (Object* pw = scene.Gvtcier_Scene_GetObject(m.playerObj)) {
                //把物理算出的玩家位置回写到场景对象，渲染才能看到。
                pw->pos = player.pos;
                //带网格的玩家把对象抬高 0.35，让模型脚底对齐胶囊体底部，否则模型会陷进地面。
                if (pw->mesh.valid()) pw->pos.y += 0.35f;
                pw->rotDeg.y = player.yawDeg;
                pw->xformDirty = true;
            }
            gvts.tick(scene, sdt);
            gvts.tickObjectScripts(scene, sdt);
            scene.Gvtcier_Scene_AnimSmTick(sdt);
            scene.Gvtcier_Scene_RefreshTransforms();
            physics.Gvtcier_Physics_SyncChildren(scene);
            acc -= kStep;
            //记下这一步的完成时刻，渲染时据此计算插值系数。
            m.lastPhys = t;
        }
    } else {
        //暂停时清空累加器；否则恢复游戏的第一帧会把暂停期间的真实耗时全部补进物理。
        acc = 0.0f;
    }
}

//决定本帧用哪个相机位置渲染，并返回是否找到了有效相机源。
//先算出玩家位置的插值（上一物理步 → 当前物理步按剩余时间比例混合），
//再依次尝试：自由相机、场景指定的主相机对象、相机跟随目标对象、最后退回玩家眼睛位置。
//正因有插值，60Hz 物理在 144Hz 屏幕上也不会出现明显的步进感。
static bool resolveRenderCamera(MainLoop& m, double t, float kStep) {
    Scene& scene = *m.scene;
    Player& player = *m.player;
    Camera& camera = *m.camera;
    //插值系数 = 距上一次物理步过去的时长 / 一步时长，钳到 [0,1]。
    //理论上它不会超过 1（超过就说明该补步了），但浮点误差与渲染超前可能让它略越界，钳制后插值不会外插导致画面穿透。
    float frac = (float)((t - m.lastPhys) * (1.0 / kStep));
    if (frac < 0.0f) frac = 0.0f;
    else if (frac > 1.0f) frac = 1.0f;
    Vec3 renderCamPos = m.prevCamPos + (player.pos - m.prevCamPos) * frac;
    bool camSet = false;
    if (m.engineCamEnabled) {
        //自由相机模式：相机已由 updateEngineCamera 直接驱动，这里不再覆盖。
        camSet = true;
    } else if (!scene.mainCamera.empty()) {
        GvtId<Object> ci = scene.Gvtcier_Scene_FindByName(scene.mainCamera.c_str());
        const Object* co = scene.Gvtcier_Scene_GetObject(ci);
        if (co && co->kind == ObjKind::Camera) {
            camera.pos = co->pos;
            camera.yawDeg = co->rotDeg.y;
            camera.pitchDeg = co->rotDeg.x;
            camera.fov = co->fov;
            camera.nearPlane = co->nearPlane;
            camera.farPlane = co->farPlane;
            camSet = true;
        }
    }
    if (!camSet) {
        //跟随目标优先于玩家默认视角：有目标时相机贴在目标位置（加偏移），常用于第三人称或过场。
        if (!camera.target.empty()) {
            GvtId<Object> ti = scene.Gvtcier_Scene_FindByName(camera.target.c_str());
            const Object* to = scene.Gvtcier_Scene_GetObject(ti);
            if (to) {
                camera.pos = to->pos + camera.offset;
                camSet = true;
            }
        }
        //兜底：第一人称视角，使用插值后的位置加眼睛高度，再叠加相机偏移。
        if (!camSet) camera.pos = renderCamPos + Vec3(0, player.eyeHeight, 0) + camera.offset;
    }
    return camSet;
}

//绘制 HUD 文本层：帧率/毫秒数由 Hud 内部统计，游玩时长与坐标取自玩家状态，
//F3 面板与准星显隐由调用方传入的开关决定。
static void drawHudPanel(MainLoop& m, float dt, double t) {
    Scene& scene = *m.scene;
    Player& player = *m.player;
    Renderer& renderer = *m.renderer;
    Hud& hud = *m.hud;
    Window& win = *m.win;
    hud.Gvtcier_Hud_Update(dt);
    HudStats hst;
    hst.fps = hud.fps();
    hst.ms = hud.ms();
    hst.elapsed = (float)(t - m.startTime);
    hst.px = player.pos.x;
    hst.py = player.pos.y;
    hst.pz = player.pos.z;
    hud.Gvtcier_Hud_Draw(renderer, win.width(), win.height(), hst,
                         m.showDebugPanel, scene.showCrosshair);
}

//引擎总入口，由宿主程序（main.exe 或调试器）通过 GetProcAddress 调进来。
//gameDllPath 是游戏路径：可以是游戏 DLL、.gpk 资源包，也可以是裸 .gvts 脚本（调试用）。
//gameArgs 是透传的游戏命令行字符串，用于识别 --dev/--server 等开关。
//返回值即进程退出码：0 正常结束，1 启动失败。
//完整流程：解析命令行 → 打开游戏包 → 加载设置 → 创建窗口 → 初始化 RHI → 检测 GPU 与自适应画质
//→ 构建场景/Renderer → 播放启动画面 → 加载游戏内容 → 主菜单 → 主循环 → 退出清理。
int Gvtcier_Run(void* hostModule, const char* gameDllPath, const char* gameArgs) {
    //控制台统一切到 UTF-8，否则中文日志会变成乱码。
    SetConsoleOutputCP(CP_UTF8);
    SetConsoleCP(CP_UTF8);
    Gdiplus::GdiplusStartupInput gdiplusInput;
    ULONG_PTR gdiplusToken = 0;
    Gdiplus::GdiplusStartup(&gdiplusToken, &gdiplusInput, nullptr);
    //崩溃报告必须尽早安装，之后任何阶段崩溃都能留下调用栈。
    Gvtcier_CrashReport_Install();
    {
        //日志与游戏包放在同一目录，方便玩家把游戏目录整体打包发给开发者排查。
        std::string dir = gameDllPath ? gameDllPath : "";
        size_t sl = dir.find_last_of("\\/");
        if (sl != std::string::npos) dir = dir.substr(0, sl);
        else dir.clear();
        std::string lp = dir.empty() ? std::string("gvtcier.log") : (dir + "\\gvtcier.log");
        rlog::Gvtcier_Log_InitFile(lp.c_str());
    }
    rlog::Gvtcier_Log_Write("[run] enter Gvtcier_Run\n");
    //日志只进文件，不弹控制台。调试构建若从终端启动，把附带出来的控制台藏掉，
    //玩家双击运行时就该只看到游戏窗口。
    SetConsoleCP(CP_UTF8);
    Gvtcier_Debug_HideConsole();
    //关闭 stdout 缓冲：崩溃时未刷新的输出会丢失，而不缓冲能保证最后几行日志落盘。
    setvbuf(stdout, NULL, _IONBF, 0);
    Gvtcier_Debug_InitConsole();

    std::string argBuf = gameArgs ? gameArgs : "";
    bool devMode = argBuf.find("--dev") != std::string::npos;
    //--licensed 表示买断授权：免除启动画面署名义务，展示时长可缩短。
    //默认未买断，按合规标准展示满 3 秒。
    bool licensedMode = argBuf.find("--licensed") != std::string::npos;
    //--server 走无窗口服务端分支，完全不建窗口，因此在窗口与 RHI 之前就分流出去。
    if (argBuf.find("--server") != std::string::npos) {
        GameSettings& s = Gvtcier_Settings();
        uint16_t port = (uint16_t)s.netPort;
        //roomStr 用 static 是因为它的 c_str 会被带出这个作用域传给服务端长期使用。
        static std::string roomStr = s.serverRoom;
        size_t p = argBuf.find("--port");
        if (p != std::string::npos) {
            //"--port" 长度为 6，跳过它直接取数字；端口必须是 1~65535 的合法值，否则忽略改用设置里的值。
            long v = atol(argBuf.c_str() + p + 6);
            if (v > 0 && v < 65536) port = (uint16_t)v;
        }
        size_t r = argBuf.find("--room");
        if (r != std::string::npos) {
            //跳过 "--room" 之后可能存在的空格与等号，再截到第一个空白为止。
            const char* rs = argBuf.c_str() + r + 6;
            while (*rs == ' ' || *rs == '=') ++rs;
            roomStr = rs;
            size_t sp = roomStr.find_first_of(" \t");
            if (sp != std::string::npos) roomStr.resize(sp);
        }
        return Gvtcier_RunHeadlessServer(port, roomStr.c_str());
    }

    //按扩展名判定三种加载形态：.gvts 裸脚本（调试）、.gpk 资源包、其它一律当游戏 DLL。
    GamePack pack;
    bool bareGvts = false;
    bool gpkMode = false;
    std::string argStr = gameDllPath ? gameDllPath : "";
    if (endsWithNoCase(argStr, ".gvts")) {
        bareGvts = true;
    } else if (endsWithNoCase(argStr, ".gpk")) {
        gpkMode = true;
    }
    if (gpkMode) {
        if (!Gvtcier_GamePack_OpenGpk(gameDllPath, pack)) {
            GvtcierError::engine("gamepack", u8"游戏包加载失败");
            Gdiplus::GdiplusShutdown(gdiplusToken);
            return 1;
        }
    } else if (!bareGvts) {
        if (!Gvtcier_GamePack_Open(gameDllPath, pack)) {
            GvtcierError::engine("gamepack", u8"游戏包加载失败");
            Gdiplus::GdiplusShutdown(gdiplusToken);
            return 1;
        }
        //资源来源优先用 DLL 内嵌的资源包（单文件分发），没有则退回与 DLL 同目录的外部 gpak。
        if (pack.resource && pack.resourceSize > 0) {
            Gvtcier_Res_SetPack(pack.resource, pack.resourceSize);
        } else if (!pack.pakPath.empty()) {
            Gvtcier_Res_SetPackFile(pack.pakPath.c_str());
        }
    } else {
        printf("[run] 裸 Gvts 模式: %s\n", gameDllPath);
    }
    {
        //设置从游戏包同目录的 settings.ini 读取；读不到就用默认值。
        //窗口模式在这里被强制改成无边框，是当前的既定行为：启动时一律无边框铺满，玩家之后可在设置里改。
        std::string dir = gameDllPath ? gameDllPath : "";
        size_t sl = dir.find_last_of("\\/");
        if (sl != std::string::npos) dir = dir.substr(0, sl);
        Gvtcier_Settings_Load(dir.c_str());
        Gvtcier_Settings().windowMode = WINDOW_BORDERLESS;
    }

    Window win;
    //窗口标题取游戏名；裸脚本模式没有游戏信息，用固定标题。
    const char* winTitle = bareGvts ? "Gvtcier" : (pack.info.name ? pack.info.name : "Gvtcier Engine");
    rlog::Gvtcier_Log_Write("[boot] 游戏包就绪，准备建窗口 %s\n", winTitle);
    //默认 1280x720：16:9 的通用分辨率，几乎所有显示器都能完整放下，之后由窗口模式决定是否铺满。
    if (!win.Gvtcier_Window_Create(winTitle, gv::kDefaultWindowWidth, gv::kDefaultWindowHeight, nullptr)) {
        GvtcierError::engine("window", u8"创建窗口失败");
        if (!bareGvts) Gvtcier_GamePack_Close(pack);
        Gdiplus::GdiplusShutdown(gdiplusToken);
        return 1;
    }
    win.Gvtcier_Window_ApplyMode(Gvtcier_Settings().windowMode);
    rlog::Gvtcier_Log_Write("[boot] 窗口已建好，初始化渲染后端\n");
    //RHI 必须在窗口之后初始化：OpenGL 上下文绑在窗口的 DC 上。
    if (!rhi::Gvtcier_Rhi_Init(win.hwnd())) {
        GvtcierError::engine("rhi", u8"RHI 初始化失败");
        win.Gvtcier_Window_Destroy();
        if (!bareGvts) Gvtcier_GamePack_Close(pack);
        Gdiplus::GdiplusShutdown(gdiplusToken);
        return 1;
    }
    //后端那一路的 printf 是攒着不发的，这里主动搬一次，免得初始化完成前崩掉就什么都看不到
    rlog::Gvtcier_Log_Write("[boot] 渲染后端已就绪: %s\n",
                            rhi::Gvtcier_Rhi_Backend() == rhi::Backend::Vulkan ? "Vulkan" : "OpenGL");
    //GPU 检测必须放在 RHI 之后：厂商与版本字符串只能从已创建的图形上下文里查询。
    Gvtcier_Gpu_Detect();
    {
        const GpuProfile& gp = Gvtcier_Gpu();
        //自适应画质只针对已知有短板的后端：摩尔线程、ANGLE 转译、软件渲染。
        //主流独显不降档，避免"能跑满却被自动砍画质"。
        if (gp.mthreads || gp.angle || gp.software) {
            if (Gvtcier_Gpu_ApplyAutoTune(Gvtcier_Settings())) {
                rlog::Gvtcier_Log_Write("[gpu] 已按 %s 自动调整画质档位\n", gp.profileName);
            }
        }
    }
    //所有子系统都是栈对象，申请与释放顺序由下面的显式调用控制，不依赖析构顺序。
    Renderer renderer;
    Scene scene;
    Splash splash;
    Player player;
    PlayerSettings psBase;
    PhysicsWorld physics;
    GvtsBind gvts;
    Camera camera;
    Canvas2D canvas2d;
    Hud hud;

    //把栈上各子系统的地址登记到启动上下文；裸脚本模式下把脚本路径也一并带上。
    BootCtx ctx;
    ctx.win = &win;
    ctx.scene = &scene;
    ctx.renderer = &renderer;
    ctx.splash = &splash;
    ctx.pack = &pack;
    ctx.player = &player;
    ctx.psBase = &psBase;
    ctx.physics = &physics;
    ctx.gvts = &gvts;
    ctx.camera = &camera;
    ctx.canvas2d = &canvas2d;
    ctx.gvtsFile = bareGvts ? gameDllPath : nullptr;
    ctx.dev = devMode;
    ctx.licensed = licensedMode;

    if (!loadEngineCore(ctx, gdiplusToken)) return 1;
    //启动画面开始前先抓一份场景的"外观基线"，启动画面在其中加临时元素，结束后可整体还原。
    scene.Gvtcier_Scene_CaptureLook();
    //启动画面的单帧：轮询消息、转视角、清屏、画启动画面、翻页。
    //返回 false 表示窗口已被关闭，上层循环随即退出。
    auto splashFrame = [&]() -> bool {
        if (!win.Gvtcier_Window_Poll()) return false;
        win.Gvtcier_Window_UpdateClipCursorHide();
        int ldx = 0, ldy = 0;
        win.Gvtcier_Window_MouseDelta(ldx, ldy);
        if (ldx != 0 || ldy != 0) splash.Gvtcier_Splash_Look((float)ldx, (float)ldy);
        rhi::Gvtcier_Rhi_Viewport(0, 0, win.width(), win.height());
        rhi::Gvtcier_Rhi_ClearColor(0.0f, 0.0f, 0.0f, 1.0f);
        rhi::Gvtcier_Rhi_Clear(rhi::ColorBit);
        splash.Gvtcier_Splash_Draw(renderer, win.width(), win.height(), win.mode());
        renderer.Gvtcier_Render_FlushCanvas(win.width(), win.height());
        win.Gvtcier_Window_SwapBuffers();
        return true;
    };
    bool windowAlive = true;
    // 启动画面出现的同时开始加载，但不一次做完：四个阶段逐帧推进，
    // 每推进一个阶段就刷一帧 Logo，这样加载期间画面不会僵住。
    // 第四阶段才会放开自动淡出，之前 Logo 一直亮着，等东西都备齐了才切主菜单。
    LoadStages stages(ctx, gdiplusToken);
    double splashPrev = Gvtcier_NowSeconds();
    while (splash.Gvtcier_Splash_Active() && windowAlive) {
        double now = Gvtcier_NowSeconds();
        float sdt = (float)(now - splashPrev);
        splashPrev = now;
        //同样做 0.25 秒钳制：加载阶段可能卡顿，避免启动画面一帧跳过大半个淡出。
        if (sdt > 0.25f) sdt = 0.25f;
        if (!stages.done) {
            stages.step();
            if (stages.failed) break;
            //走到最后一段才通知启动画面可以淡出，前面几段 Logo 要一直挂着
            if (stages.done) splash.Gvtcier_Splash_SetWorkDone();
        }
        splash.Gvtcier_Splash_Tick(sdt);
        windowAlive = splashFrame();
    }
    // 窗口提前关掉时也要把剩下的加载跑完，否则场景与脚本停在半初始化状态，
    // 后面的关闭流程会踩到没建好的对象。
    while (!stages.done && !stages.failed) stages.step();
    splash.Gvtcier_Splash_End();
    win.Gvtcier_Window_RestoreCursor();
    if (!windowAlive) { Gvtcier_GamePack_Close(pack); return 0; }
    if (stages.failed) return 1;
    //纯 OpenGL 脚本（isOnlyGl）自行接管整帧绘制，设置已经由脚本自己处理，不再套用引擎内建渲染设置。
    if (!gvts.isOnlyGl()) {
        applySettingsToEngine(renderer, scene, camera);
    }
    win.Gvtcier_Window_SetInputEnabled(true);
    //菜单输入用的边沿检测状态。这些 Prev 变量记录上一帧按键，用来把"按住"变成"刚按下"。
    bool showDebugPanel = false;
    RenderOptions opt;
    opt.hdr = scene.hdr;
    bool f3Prev = false;
    bool f2Prev = false;
    bool fPrev = false;
    bool escPrev = false;
    bool enterPrev = false;
    bool mousePrev = false;
    bool upPrev = false;
    bool downPrev = false;
    bool leftPrev = false;
    bool rightPrev = false;
    int lastMouseX = -1;
    int lastMouseY = -1;
    PauseMenu menu;
    menu.Gvtcier_Menu_SetTitle(winTitle);
    MainMenu mainMenu;
    mainMenu.Gvtcier_MainMenu_SetTitle(winTitle);
    MainLoop ml;
    ml.win = &win;
    ml.renderer = &renderer;
    ml.scene = &scene;
    ml.player = &player;
    ml.physics = &physics;
    ml.gvts = &gvts;
    ml.camera = &camera;
    ml.canvas2d = &canvas2d;
    ml.hud = &hud;
    ml.menu = &menu;
    ml.opt = opt;
    //last 是上一帧时间戳，主循环靠它算出每帧耗时 dt；主菜单与游戏循环共用同一个变量，切场景时都会重置。
    double last = Gvtcier_NowSeconds();
    //true 表示玩家在暂停菜单里选了"返回主菜单"，外层 for 会重建主菜单而不是退出程序。
    bool wantBackToMenu = false;
    double startTime = last;
    //固定物理步长 1/60 秒。选 60 而不是跟随显示刷新率，是为了让物理在不同机器上完全一致、便于复现问题，
    //也避免高刷屏上物理步数暴涨带来的开销。
    const float kStep = 1.0f / 60.0f;
    //时间累加器，把真实帧间隔攒够一个 kStep 才推进一次物理。
    float acc = 0.0f;
    bool jumpRequested = false;
    {
        //主菜单背景图：用资源 id 取贴图，取不到就保持主菜单默认背景，不影响启动。
        GvtId<Texture> bgId = scene.Gvtcier_Scene_LoadPngRes("vein_1_18");
        Texture* bgTex = scene.Gvtcier_Scene_GetTex(bgId);
        if (bgTex && bgTex->id()) {
            mainMenu.Gvtcier_MainMenu_SetBgMode(MAINMENU_BG_2D);
            mainMenu.Gvtcier_MainMenu_SetBgImage(bgTex->id(), bgTex->width(), bgTex->height());
        }
    }
    //外层大循环：一次迭代 = 一轮"主菜单 → 进入游戏 → 退出游戏"。从暂停菜单返回主菜单时回到这里重新开始。
    for (;;) {
    //进入主菜单：放开鼠标、显示光标，保证玩家能正常点菜单。
    win.Gvtcier_Window_SetAutoGrab(false);
    win.Gvtcier_Window_GrabMouse(false);
    win.Gvtcier_Window_RestoreCursor();
    mainMenu.Gvtcier_MainMenu_Open();
    {
        //主菜单自己的边沿检测状态，与游戏内的那套互不影响。
        bool mmUp = false, mmDown = false, mmEnter = false, mmMouse = false;
        bool mmEsc = false;
        int mmMX = -1, mmMY = -1;
        while (mainMenu.Gvtcier_MainMenu_IsOpen() && win.Gvtcier_Window_Poll()) {
            double mmNow = Gvtcier_NowSeconds();
            float mmDt = (float)(mmNow - last);
            last = mmNow;
            //钳制 0.25 秒，避免主菜单长时间遮挡或卡顿后动画一次跳完。
            if (mmDt > 0.25f) mmDt = 0.25f;
            mainMenu.Gvtcier_MainMenu_Tick(mmDt);
            //菜单期间也推进音频流，保证背景音乐不断。
            Gvtcier_Audio_StreamTick();
            win.Gvtcier_Window_RestoreCursor();
            rhi::Gvtcier_Rhi_Viewport(0, 0, win.width(), win.height());
            MenuInput mi;
            bool kUp = win.isKeyDown(VK_UP);
            bool kDown = win.isKeyDown(VK_DOWN);
            bool kEnter = win.isKeyDown(VK_RETURN);
            bool kMouse = win.isKeyDown(VK_LBUTTON);
            bool kEsc = win.isKeyDown(Gvtcier_Settings().menuKey);
            //方向键与回车做边沿检测，避免按住时菜单光标连续跳动。
            mi.up = kUp && !mmUp;
            mi.down = kDown && !mmDown;
            mi.confirm = kEnter && !mmEnter;
            mi.back = kEsc && !mmEsc;
            int cmx = 0, cmy = 0;
            win.Gvtcier_Window_CursorPos(cmx, cmy);
            mi.mouseX = cmx;
            mi.mouseY = cmy;
            mi.mouseMoved = (cmx != mmMX) || (cmy != mmMY);
            mi.mouseDown = kMouse;
            mi.mouseClicked = kMouse && !mmMouse;
            mmMX = cmx;
            mmMY = cmy;
            mmUp = kUp;
            mmDown = kDown;
            mmEnter = kEnter;
            mmMouse = kMouse;
            mmEsc = kEsc;
            mainMenu.Gvtcier_MainMenu_Update(mi, win.width(), win.height());
            if (mainMenu.Gvtcier_MainMenu_ConsumeSettingsChanged()) {
                applySettingsToEngine(renderer, scene, camera);
                win.Gvtcier_Window_ApplyMode(Gvtcier_Settings().windowMode);
                Gvtcier_Settings_Save();
            }
            if (mainMenu.Gvtcier_MainMenu_WantsQuit()) {
                //主菜单里点了退出：关掉游戏包直接返回，不再走后面的完整清理（此时子系统已由上面初始化，进程退出即回收）。
                Gvtcier_GamePack_Close(pack);
                return 0;
            }
            //主菜单的清屏色 (0.05,0.06,0.08) 是偏蓝的近黑色，比纯黑柔和，避免菜单背景图边缘出现硬轮廓。
            rhi::Gvtcier_Rhi_ClearColor(0.05f, 0.06f, 0.08f, 1.0f);
            rhi::Gvtcier_Rhi_Clear(rhi::ColorBit);
            C2Camera2D mmCam;
            canvas2d.Gvtcier_Canvas2D_BeginFrame(win.width(), win.height(), mmCam);
            //菜单期间让脚本先画一遍但隐藏游戏 UI：这样菜单背后的 3D 场景是活的，同时不会冒出 HUD。
            //2D 游戏不画脚本场景，否则 2D 内容会盖住菜单。
            renderer.Gvtcier_Render_SetGameUIHidden(true);
            if (!Gvtcier_Gvtio_Is2D()) gvts.draw(0.0f);
            renderer.Gvtcier_Render_SetGameUIHidden(false);
            mainMenu.Gvtcier_MainMenu_Draw(renderer, win.width(), win.height());
            renderer.Gvtcier_Render_FlushCanvas(win.width(), win.height());
            win.Gvtcier_Window_SwapBuffers();
        }
        //主菜单循环结束的原因只有两种：点了开始，或窗口被关掉。没点开始就是关窗口，直接退出。
        if (!mainMenu.Gvtcier_MainMenu_IsOpen() && !mainMenu.Gvtcier_MainMenu_ConsumeStart()) {
            Gvtcier_GamePack_Close(pack);
            return 0;
        }
        rlog::Gvtcier_Log_Echo("[mainmenu] left loop, start requested\n");
        //进入游戏：恢复自动抓鼠标并按场景设置决定是否锁定光标；纯 OpenGL 脚本自己管鼠标，不干扰。
        if (!gvts.isOnlyGl()) {
            win.Gvtcier_Window_SetAutoGrab(true);
            win.Gvtcier_Window_GrabMouse(scene.captureMouse);
        }
        //重置 ESC 边沿状态，否则主菜单里按下的 ESC 会在进入游戏的第一帧被当成新按下而立刻弹出暂停菜单。
        escPrev = win.isKeyDown(Gvtcier_Settings().menuKey);
        last = Gvtcier_NowSeconds();
    }
    //进入游戏循环前的状态复位：清空累加器与上一物理步位置，避免把主菜单停留的时间补进物理。
    ml.startTime = startTime;
    ml.prevCamPos = player.pos;
    ml.lastPhys = last;
    acc = 0.0f;
    jumpRequested = false;
    escPrev = win.isKeyDown(Gvtcier_Settings().menuKey);
    wantBackToMenu = false;
    //游戏主循环：每帧顺序是 取消息 → 算 dt → 处理菜单/暂停 → 相机与输入 → 固定步长模拟 → 网络 →
    //脚本绘制 → 后处理 → 解析渲染相机 → HUD → 3D 渲染 → 翻页 → 帧率限制。
    while (win.Gvtcier_Window_Poll()) {
        double t = Gvtcier_NowSeconds();
        float dt = (float)(t - last);
        last = t;
        //注意这里 dt 没有钳制：它只用于相机转动、后处理与脚本注入等非物理用途，物理步有独立的 acc 钳制。
        Gvtcier_Audio_StreamTick();
        //纯 OpenGL 脚本分支：脚本自己提交全部绘制，引擎只提供一个干净的帧边界与翻页。
        if (gvts.isOnlyGl()) {
            gvts.tick(scene, dt);
            renderer.Gvtcier_Render_SetUserGlDraw(userGlDrawThunk, &gvts);
            renderer.Gvtcier_Render_UserGlFrame(win.width(), win.height());
            win.Gvtcier_Window_SwapBuffers();
            continue;
        }
        //菜单键变成"刚按下"的瞬间才算打开菜单，避免按住不放时反复开关。
        bool menuOpen = menu.Gvtcier_Menu_IsOpen();
        bool esc = win.isKeyDown(Gvtcier_Settings().menuKey);
        bool escEdge = esc && !escPrev;
        escPrev = esc;
        //自由相机模式下 ESC 的含义是"结束调试并退出"，而不是打开暂停菜单。
        if (ml.engineCamEnabled && escEdge) {
            win.Gvtcier_Window_SetAutoGrab(true);
            win.Gvtcier_Window_GrabMouse(false);
            PostQuitMessage(0);
            escEdge = false;
        }
        bool justOpened = false;
        if (!menuOpen && escEdge) {
            //打开暂停菜单：必须放手鼠标并显示光标，同时把场景置为暂停（物理步会被跳过）。
            menu.Gvtcier_Menu_Open();
            menu.Gvtcier_Menu_Settings().Gvtcier_SettingsUI_ResetFocus();
            win.Gvtcier_Window_SetAutoGrab(false);
            win.Gvtcier_Window_GrabMouse(false);
            menuOpen = true;
            justOpened = true;
            scene.paused = true;
        }
        if (menuOpen) {
            int cmx = 0, cmy = 0;
            win.Gvtcier_Window_CursorPos(cmx, cmy);
            bool mouseDown = win.isKeyDown(VK_LBUTTON);
            MenuInput mi;
            bool kUp = win.isKeyDown(VK_UP);
            bool kDown = win.isKeyDown(VK_DOWN);
            bool kLeft = win.isKeyDown(VK_LEFT);
            bool kRight = win.isKeyDown(VK_RIGHT);
            mi.up = kUp && !upPrev;
            mi.down = kDown && !downPrev;
            mi.left = kLeft && !leftPrev;
            mi.right = kRight && !rightPrev;
            upPrev = kUp;
            downPrev = kDown;
            leftPrev = kLeft;
            rightPrev = kRight;
            bool enter = win.isKeyDown(VK_RETURN);
            mi.confirm = enter && !enterPrev;
            enterPrev = enter;
            mi.back = justOpened ? false : (escEdge || win.isKeyDown(0xBC));
            mi.capturedKey = win.Gvtcier_Window_ConsumeKeyPress();
            mi.mouseX = cmx;
            mi.mouseY = cmy;
            mi.mouseDown = mouseDown;
            mi.mouseClicked = mouseDown && !mousePrev;
            mi.mouseMoved = (cmx != lastMouseX) || (cmy != lastMouseY);
            lastMouseX = cmx;
            lastMouseY = cmy;
            mi.keyNav = (mi.up || mi.down || mi.left || mi.right);
            mousePrev = mouseDown;
            handleMenuInput(ml, mi, justOpened, escEdge);
            if (menu.Gvtcier_Menu_WantsQuit()) {
                rlog::Gvtcier_Log_Echo("[menu] quit requested\n");
                break;
            }
            if (menu.Gvtcier_Menu_ConsumeReturnToMenu()) {
                rlog::Gvtcier_Log_Echo("[menu] return to main menu requested\n");
                menu.Gvtcier_Menu_Close();
                scene.paused = false;
                wantBackToMenu = true;
                break;
            }
            if (!menu.Gvtcier_Menu_IsOpen()) {
                //菜单被关掉（继续游戏）：恢复鼠标捕获与运行状态；重置时间戳防止把菜单停留时间算成一帧。
                win.Gvtcier_Window_SetAutoGrab(true);
                win.Gvtcier_Window_GrabMouse(scene.captureMouse);
                scene.paused = false;
                last = Gvtcier_NowSeconds();
                continue;
            }
            //菜单仍开着：清空物理累加器，保证关闭菜单后的第一帧不会一次性补上大量物理步。
            acc = 0.0f;
            //暂停时网络用 0 步长推进，只做收发不做时间演算。
            Gvtcier_Net_Tick(0.0f);
            C2Camera2D c2camMenu;
            canvas2d.Gvtcier_Canvas2D_BeginFrame(win.width(), win.height(), c2camMenu);
            //冻结输入地画一遍脚本内容，让菜单背后的世界保持画面但不会响应输入；冻结保证脚本不会改变游戏状态。
            renderer.Gvtcier_Render_SetGameUIHidden(true);
            gvts.setInputFrozen(true);
            gvts.draw(0.0f);
            gvts.setInputFrozen(false);
            gvts.drawObjectScripts();
            renderer.Gvtcier_Render_SetGameUIHidden(false);
            ml.opt.fromScene(scene);
            ml.opt.width = win.width();
            ml.opt.height = win.height();
            ml.opt.debugCollider = ml.showWireframe;
            renderer.Gvtcier_Render_SetDebugLines(nullptr, 0);
            renderer.Gvtcier_Render_Set2DMode(Gvtcier_Gvtio_Is2D());
            //2D 游戏只清屏，不跑 3D 场景与后处理：菜单直接叠在 2D 画布上。
            if (Gvtcier_Gvtio_Is2D()) {
                rhi::Gvtcier_Rhi_Viewport(0, 0, win.width(), win.height());
                rhi::Gvtcier_Rhi_ClearColor(0.05f, 0.06f, 0.08f, 1.0f);
                rhi::Gvtcier_Rhi_Clear(rhi::ColorBit | rhi::DepthBit);
            } else {
                //后处理时间传 0：暂停时不让曝光自适应等随时间演化的效果继续动。
                renderer.Gvtcier_Render_Fx().Gvtcier_Fx_Update(0.0f);
                renderer.Gvtcier_Render_Render(scene, camera, ml.opt);
            }
            renderer.Gvtcier_Render_BeginEngineUI();
            menu.Gvtcier_Menu_Draw(renderer, win.width(), win.height());
            renderer.Gvtcier_Render_EndEngineUI();
            renderer.Gvtcier_Render_Render2D(win.width(), win.height());
            canvas2d.Gvtcier_Canvas2D_EndFrame(nullptr);
            win.Gvtcier_Window_SwapBuffers();
            continue;
        }
        //相机模式切换检测：脚本把 camera.mode 设为 1 即进入引擎自由相机，改回其它值即退出。
        if (!renderer.Gvtcier_Render_Is2DMode()) {
            if (camera.mode == 1 && !ml.engineCamEnabled) {
                enterEngineCamera(ml);
            } else if (camera.mode != 1 && ml.engineCamEnabled) {
                leaveEngineCamera(ml);
            }
            if (ml.engineCamEnabled) {
                updateEngineCamera(ml, dt);
            }
            updateGameCamera(ml, dt);
        }
        //跳跃按下状态转成一次性请求，交给本帧的物理步消费。
        bool sp = bindDown(win, Gvtcier_Settings(), ACTION_JUMP);
        jumpRequested = sp;
        //F3 与 F2 同时接受"单键"和"F 键 + 3/2 键"两种按法，兼容无功能键的键盘与远程桌面环境。
        bool dbgKey = win.isKeyDown(VK_F3) ||
                      (win.isKeyDown('F') && win.isKeyDown('3'));
        toggleDebugPanel(ml, dbgKey, f3Prev);
        bool wireKey = win.isKeyDown(VK_F2) ||
                       (win.isKeyDown('F') && win.isKeyDown('2'));
        if (wireKey && !f2Prev) ml.showWireframe = !ml.showWireframe;
        f2Prev = wireKey;
        bool flKey = win.isKeyDown('F') && !win.isKeyDown('2') && !win.isKeyDown('3');
        if (flKey && !fPrev) renderer.Gvtcier_Render_SetFlashlight(!renderer.Gvtcier_Render_Flashlight());
        fPrev = flKey;
        //本帧的模拟推进（内部按固定步长与累加器决定跑几步）。
        stepSimulation(ml, dt, t, kStep, acc, jumpRequested);
        Gvtcier_Net_Tick(dt);
        C2Camera2D c2cam;
        canvas2d.Gvtcier_Canvas2D_BeginFrame(win.width(), win.height(), c2cam);
        double lpA = Gvtcier_NowSeconds();
        //脚本的即时绘制阶段：脚本在这里提交自己产生的绘制指令，引擎随后统一渲染场景。
        gvts.draw(dt);
        //2D 模式分支：整帧只有 2D 内容，不走 3D 场景与后处理。
        if (renderer.Gvtcier_Render_Is2DMode()) {
            gvts.drawObjectScripts();
            rhi::Gvtcier_Rhi_Viewport(0, 0, win.width(), win.height());
            rhi::Gvtcier_Rhi_ClearColor(0.05f, 0.06f, 0.08f, 1.0f);
            rhi::Gvtcier_Rhi_Clear(rhi::ColorBit | rhi::DepthBit);
            renderer.Gvtcier_Render_Render2D(win.width(), win.height());
            canvas2d.Gvtcier_Canvas2D_EndFrame(nullptr);
            win.Gvtcier_Window_SwapBuffers();
            continue;
        }
        gvts.drawObjectScripts();
        renderer.Gvtcier_Render_Fx().Gvtcier_Fx_Update(dt);
        //本帧统一处理脚本里登记过的对象删除。集中到渲染前做，避免在遍历中就地删除引起迭代失效。
        scene.Gvtcier_Scene_FlushRemoves(&physics);
        resolveRenderCamera(ml, t, kStep);
        //把场景的渲染开关拷进本帧选项，再覆盖窗口尺寸与调试线框。
        ml.opt.fromScene(scene);
        ml.opt.width = win.width();
        ml.opt.height = win.height();
        ml.opt.debugCollider = ml.showWireframe;
        updateDebugLines(ml);
        drawHudPanel(ml, dt, t);
        {
            //把光标位置写进阴影模块的调试探针，供其做局部排查使用。
            int pcur[2] = { 0, 0 };
            win.Gvtcier_Window_CursorPos(pcur[0], pcur[1]);
            int* pq = Gvtcier_Shadow_ProbeCursor();
            pq[0] = pcur[0];
            pq[1] = pcur[1];
        }
        double lpB = Gvtcier_NowSeconds();
        //真正的 3D 渲染：提交场景并由渲染器跑完整条管线。
        renderer.Gvtcier_Render_Render(scene, camera, ml.opt);
        double lpC = Gvtcier_NowSeconds();
        canvas2d.Gvtcier_Canvas2D_EndFrame(nullptr);
        win.Gvtcier_Window_SwapBuffers();
        double lpD = Gvtcier_NowSeconds();
        {
            //每 60 帧统计一次各阶段平均耗时，用于判断瓶颈在提交、渲染还是翻页。
            static double lpPre = 0, lpRender = 0, lpSwap = 0;
            static int lpFrames = 0;
            lpPre += lpB - lpA;
            lpRender += lpC - lpB;
            lpSwap += lpD - lpC;
            if (++lpFrames >= 60) {
                double n = (double)lpFrames;
                printf("[loop] avg ms pre=%.2f render=%.2f swap=%.2f loop=%.2f fps=%.1f\n",
                       lpPre / n * 1000.0, lpRender / n * 1000.0, lpSwap / n * 1000.0,
                       (lpPre + lpRender + lpSwap) / n * 1000.0,
                       n / (lpPre + lpRender + lpSwap));
                fflush(stdout);
                lpPre = lpRender = lpSwap = 0.0;
                lpFrames = 0;
            }
        }
        {
            //软件帧率限制：按 30/60/90/120 档位把本帧结束时间推到"本帧起点 + 1/上限"。
            //先粗睡到距目标 1ms 以内，再用空转精确补齐；纯 Sleep 的粒度通常只有 1~15ms，直接睡会明显掉帧。
            int cap = frameRateCapOf(Gvtcier_Settings());
            if (cap > 0) {
                double target = t + 1.0 / (double)cap;
                double now = Gvtcier_NowSeconds();
                double wait = target - now;
                if (wait > 0.0) {
                    if (wait > 0.002) Sleep((DWORD)((wait - 0.001) * 1000.0));
                    while (Gvtcier_NowSeconds() < target) { }
                }
            }
        }
    }
    //从暂停菜单返回主菜单：关掉菜单，回到外层大循环重新进入主菜单。
    if (wantBackToMenu) {
        rlog::Gvtcier_Log_Echo("[menu] entering main menu loop\n");
        menu.Gvtcier_Menu_Close();
        continue;
    }
    //退出清理：严格按与初始化相反的顺序关闭，先停掉产生资源的模块，最后才销毁窗口与图形上下文。
    rlog::Gvtcier_Log_Echo("[menu] exiting application\n");
    win.Gvtcier_Window_GrabMouse(false);
    Gvtcier_Net_Shutdown();
    physics.Gvtcier_Physics_Shutdown();
    GvtUiSound::Gvtcier_UiSound_Shutdown();
    Gvtcier_Audio_Shutdown();
    gvts.shutdown();
    canvas2d.Gvtcier_Canvas2D_Shutdown();
    renderer.Gvtcier_Render_Shutdown();
    scene.Gvtcier_Scene_Destroy();
    win.Gvtcier_Window_Destroy();
    if (!bareGvts) Gvtcier_GamePack_Close(pack);
    Gdiplus::GdiplusShutdown(gdiplusToken);
    return 0;
    }
}
