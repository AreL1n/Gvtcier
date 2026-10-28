#ifdef __ANDROID__

#include <android/native_activity.h>
#include <android/native_window.h>
#include <android/log.h>
#include <android/input.h>
#include <android/keycodes.h>
#include <android/asset_manager.h>
#include <android/looper.h>
#include <jni.h>
#include <pthread.h>
#include <deque>
#include <vector>
#include <cstring>
#include <cstdlib>
#include <cstdio>
#include <unistd.h>
#include <sys/stat.h>
#include <sys/time.h>
#include <string>

#include "Gvtcier.h"

//安卓应用入口（NativeActivity）。整体结构：
//  onCreate 注册生命周期回调，并立刻启动一条独立的渲染/游戏线程；
//  Java 线程只负责把生命周期与输入事件通过回调或命令队列交给游戏线程，绝不直接做游戏逻辑；
//  游戏线程独占 EGL 上下文与全部引擎对象，按固定步长跑模拟、按需渲染。
//之所以这样拆：EGL 上下文与 GL 资源只能在创建它们的线程上使用，所有触摸/生命周期事件都必须跨线程转交。

#define ALOG(...) __android_log_print(ANDROID_LOG_INFO, "GvEngine", __VA_ARGS__)

void Gvtcier_Android_SetAssetManager(AAssetManager* mgr);
//GLES 后端的绘制调用计数器，用于性能统计。
extern "C" unsigned long long Gvtcier_Gles_DrawCalls();
extern "C" void Gvtcier_Gles_ResetDrawCalls();

//弱符号钩子：应用层可以自行提供，用于在每帧末尾叠加自定义调试 UI；不提供时为空指针。
extern "C" __attribute__((weak)) void Gvtcier_Android_DrawOverlay(Renderer& r, const GvWWindow* w);
//弱符号钩子：应用层可提供自定义应用名，作为窗口标题。
extern "C" __attribute__((weak)) const char* Gvtcier_Android_AppName();

//标准输出重定向用的管道。安卓下 stdout 默认丢弃，必须转成 __android_log 与文件才能看到日志。
static int g_logPipe[2] = { -1, -1 };
static FILE* g_logFile = nullptr;

//日志消纳线程：从管道读数据，按行拆分后同时写入 logcat 与日志文件。
//之所以用独立线程而不是在写端处理，是因为写端（printf）可能在任意位置被调用，不能阻塞渲染。
static void* logDrainThread(void* arg) {
    (void)arg;
    char buf[1024];
    std::string line;
    ssize_t n = 0;
    while ((n = read(g_logPipe[0], buf, sizeof(buf))) > 0) {
        for (ssize_t i = 0; i < n; ++i) {
            if (buf[i] == '\n') {
                if (!line.empty()) {
                    __android_log_write(ANDROID_LOG_INFO, "GvEngine", line.c_str());
                    if (g_logFile) {
                        fputs(line.c_str(), g_logFile);
                        fputc('\n', g_logFile);
                        fflush(g_logFile);
                    }
                    line.clear();
                }
            } else {
                line.push_back(buf[i]);
            }
        }
    }
    return nullptr;
}

//把 stdout 接到日志管道。只执行一次。
//先打开文件再建管道：即使管道创建失败，日志文件仍然可用。
//dup2 让 fd 1 指向管道写端，之后所有 printf 的输出都会被消纳线程取走。
static void redirectStdoutToLog(const char* filePath) {
    if (g_logPipe[0] != -1) return;
    if (filePath && filePath[0]) g_logFile = fopen(filePath, "w");
    if (pipe(g_logPipe) != 0) return;
    dup2(g_logPipe[1], 1);
    setvbuf(stdout, nullptr, _IONBF, 0);
    pthread_t tid;
    pthread_create(&tid, nullptr, logDrainThread, nullptr);
    pthread_detach(tid);
}

//Activity 句柄，用于读取 assetManager 与各数据目录。
static ANativeActivity* g_activity = nullptr;
//游戏线程的退出请求。用普通 bool 而不加锁，因为只在极少数时刻读写，且最坏情况只是晚一帧退出。
static bool g_loopStop = false;
//原生窗口是否已就绪（onNativeWindowCreated 之后、Destroyed 之前）。
static bool g_windowReady = false;
static bool g_glReady = false;
//是否处于暂停（切后台）。游戏线程会阻塞等待，避免后台消耗电量与触屏误触。
static bool g_paused = false;
//命令队列与暂停条件变量用的互斥量。
static pthread_mutex_t g_mutex = PTHREAD_MUTEX_INITIALIZER;
static pthread_mutex_t g_pauseMutex = PTHREAD_MUTEX_INITIALIZER;
static pthread_cond_t g_pauseCond = PTHREAD_COND_INITIALIZER;

//设置暂停状态并唤醒等待中的游戏线程。broadcast 而不是 signal，保证即使多处等待也能全部醒来重新检查条件。
static void setPaused(bool paused) {
    pthread_mutex_lock(&g_pauseMutex);
    g_paused = paused;
    pthread_cond_broadcast(&g_pauseCond);
    pthread_mutex_unlock(&g_pauseMutex);
}

//读取当前暂停状态（加锁拷贝，避免读到撕裂的值）。
static bool isPaused() {
    pthread_mutex_lock(&g_pauseMutex);
    bool p = g_paused;
    pthread_mutex_unlock(&g_pauseMutex);
    return p;
}

//阻塞直到恢复运行或收到退出请求。
//wait 放在 while 里，是因为条件变量可能被虚假唤醒；循环还兼顾了等待期间收到退出命令的情况。
static void waitWhilePaused() {
    pthread_mutex_lock(&g_pauseMutex);
    while (g_paused && !g_loopStop) {
        pthread_cond_wait(&g_pauseCond, &g_pauseMutex);
    }
    pthread_mutex_unlock(&g_pauseMutex);
}

//跨线程命令。cmd 取值：0 退出，1 原生窗口已创建，2 原生窗口已销毁。
//用它而不是共享变量，是因为事件可能连续到达（例如快速旋转屏幕），队列能保证顺序且不丢事件。
struct AppCmd {
    int cmd;
    ANativeWindow* win;
};

static std::deque<AppCmd> g_cmds;

//向命令队列投递一条命令（Java 线程调用）。加锁是必须的：游戏线程同时在出队。
static void pushCmd(int cmd, ANativeWindow* win) {
    pthread_mutex_lock(&g_mutex);
    g_cmds.push_back({cmd, win});
    pthread_mutex_unlock(&g_mutex);
}

//从命令队列取一条命令（游戏线程调用），队列为空返回 false。
static bool popCmd(AppCmd* out) {
    pthread_mutex_lock(&g_mutex);
    if (g_cmds.empty()) {
        pthread_mutex_unlock(&g_mutex);
        return false;
    }
    *out = g_cmds.front();
    g_cmds.pop_front();
    pthread_mutex_unlock(&g_mutex);
    return true;
}

static void applyImmersive(ANativeActivity* activity);

//以下是一组 Activity 生命周期回调，全部在 Java 线程执行，因此只做标志位修改与命令投递，
//绝不直接触碰引擎对象（那些对象属于游戏线程）。

//销毁：置退出标志、解除暂停（否则游戏线程会一直睡在等待里）、投递退出命令促使其及时醒来。
static void onDestroy(ANativeActivity* activity) {
    ALOG("onDestroy");
    g_loopStop = true;
    setPaused(false);
    pushCmd(0, nullptr);
}

//进入可见状态：恢复运行。
static void onStart(ANativeActivity* activity) {
    ALOG("onStart");
    setPaused(false);
}

//回到前台：重申沉浸式全屏（系统可能在切回时恢复状态栏），并恢复运行。
static void onResume(ANativeActivity* activity) {
    ALOG("onResume");
    applyImmersive(activity);
    setPaused(false);
}

//失去前台（被其它应用遮挡或锁屏）：暂停，避免后台继续渲染耗电。
static void onPause(ANativeActivity* activity) {
    ALOG("onPause");
    setPaused(true);
}

//完全不可见：同样暂停。onPause 与 onStop 都置暂停，是因为两者的到达顺序与是否成对出现并不总是可靠。
static void onStop(ANativeActivity* activity) {
    ALOG("onStop");
    setPaused(true);
}

//原生窗口创建（首次启动、旋转、从后台恢复都可能触发）：登记窗口并通知游戏线程。
//GvW_Android_SetNativeWindow 内部会持有窗口引用，游戏线程随后可安全使用。
static void onNativeWindowCreated(ANativeActivity* activity, ANativeWindow* window) {
    ALOG("onNativeWindowCreated %dx%d", ANativeWindow_getWidth(window), ANativeWindow_getHeight(window));
    GvW_Android_SetNativeWindow((void*)window);
    g_windowReady = true;
    pushCmd(1, window);
}

//原生窗口销毁：释放窗口引用并清空触点。
//清触点是必须的：窗口消失后收不到抬起事件，不清会让虚拟按键卡在按下状态。
static void onNativeWindowDestroyed(ANativeActivity* activity, ANativeWindow* window) {
    ALOG("onNativeWindowDestroyed");
    GvW_Android_ClearNativeWindow();
    GvW_Android_ClearTouches();
    g_windowReady = false;
    pushCmd(2, nullptr);
}

static int32_t handleInputEvent(AInputEvent* event);

//ALooper 的输入事件回调：把队列里的所有输入事件逐个交给 handleInputEvent。
//preDispatchEvent 返回真表示事件已在 Java 层被处理（例如被输入法或无障碍服务消费），直接跳过。
//finishEvent 必须调用，并把"是否已处理"回传给系统，否则系统会认为应用无响应。
static int32_t onInputQueueHandler(int fd, int events, void* data) {
    (void)fd;
    (void)events;
    AInputQueue* queue = (AInputQueue*)data;
    AInputEvent* event = nullptr;
    while (AInputQueue_getEvent(queue, &event) >= 0) {
        if (AInputQueue_preDispatchEvent(queue, event)) continue;
        int32_t handled = handleInputEvent(event);
        AInputQueue_finishEvent(queue, event, handled);
    }
    return 1;
}

//输入队列创建：把它挂到当前线程的 ALooper 上，之后有输入时系统会调用 onInputQueueHandler。
static void onInputQueueCreated(ANativeActivity* activity, AInputQueue* queue) {
    ALOG("onInputQueueCreated");
    (void)activity;
    AInputQueue_attachLooper(queue, ALooper_forThread(), 1, onInputQueueHandler, queue);
}

//输入队列销毁：从 looper 解绑，避免之后继续收到已失效队列的回调。
static void onInputQueueDestroyed(ANativeActivity* activity, AInputQueue* queue) {
    ALOG("onInputQueueDestroyed");
    (void)activity;
    AInputQueue_detachLooper(queue);
}

//按键事件处理。当前只拦截返回键：按下时清空触点（防止返回动画期间按键卡住），并始终返回 1 表示已处理。
//返回 1 会阻止返回键触发系统默认的"退出 Activity"，因此安卓上不能用返回键关闭应用。
static int32_t onKeyEvent(AInputEvent* event) {
    int32_t action = AKeyEvent_getAction(event);
    int32_t code = AKeyEvent_getKeyCode(event);
    if (code == AKEYCODE_BACK) {
        if (action == AKEY_EVENT_ACTION_DOWN) GvW_Android_ClearTouches();
        return 1;
    }
    return 0;
}

//触摸/鼠标事件处理，转成平台层的触点状态。
//安卓把多指信息打包在一个事件里：MOVE 事件会一次报出所有手指的新位置，所以要把每个指针都更新一遍；
//其它动作（DOWN/POINTER_DOWN/UP/POINTER_UP）只针对 action 索引指向的那一根手指。
//指针索引来自 ACTION 字段的高位，必须越界检查后才用于取坐标。
static int32_t onMotionEvent(AInputEvent* event) {
    int32_t src = AInputEvent_getSource(event);
    if (!(src & AINPUT_SOURCE_TOUCHSCREEN) && !(src & AINPUT_SOURCE_MOUSE)) return 0;
    int32_t masked = AMotionEvent_getAction(event);
    int32_t action = masked & AMOTION_EVENT_ACTION_MASK;
    size_t idx = (size_t)((masked & AMOTION_EVENT_ACTION_POINTER_INDEX_MASK)
                          >> AMOTION_EVENT_ACTION_POINTER_INDEX_SHIFT);
    size_t count = AMotionEvent_getPointerCount(event);
    if (action == AMOTION_EVENT_ACTION_MOVE) {
        for (size_t i = 0; i < count; ++i) {
            int32_t id = AMotionEvent_getPointerId(event, i);
            float x = AMotionEvent_getX(event, i);
            float y = AMotionEvent_getY(event, i);
            GvW_Android_SetTouch(id, (int)x, (int)y, action, (int)i);
        }
        return 1;
    }
    if (idx >= count) idx = count > 0 ? count - 1 : 0;
    int32_t id = AMotionEvent_getPointerId(event, idx);
    float x = AMotionEvent_getX(event, idx);
    float y = AMotionEvent_getY(event, idx);
    GvW_Android_SetTouch(id, (int)x, (int)y, action, (int)idx);
    return 1;
}

//输入事件总分发：按类型转给触摸或按键处理。返回 1 表示已消费。
static int32_t handleInputEvent(AInputEvent* event) {
    int32_t type = AInputEvent_getType(event);
    if (type == AINPUT_EVENT_TYPE_MOTION) return onMotionEvent(event);
    if (type == AINPUT_EVENT_TYPE_KEY) return onKeyEvent(event);
    return 0;
}

//把内存数据写成文件。失败（目录不存在、无权限）时静默返回。
static void writeFile(const std::string& path, const char* data, size_t len) {
    FILE* f = fopen(path.c_str(), "wb");
    if (!f) return;
    fwrite(data, 1, len, f);
    fclose(f);
}

//从 APK 的 assets 读取一个文件到 out。文件不存在返回 false。
//assets 是只读的，只能整块读进内存，不能像文件一样随机访问。
static bool readAssetBytes(const std::string& assetRel, std::string& out) {
    if (!g_activity || !g_activity->assetManager) return false;
    //AASSET_MODE_BUFFER 让系统一次性缓冲整个资源，适合后续整体读取。
    AAsset* a = AAssetManager_open(g_activity->assetManager, assetRel.c_str(), AASSET_MODE_BUFFER);
    if (!a) return false;
    size_t n = (size_t)AAsset_getLength(a);
    out.resize(n);
    if (n > 0) AAsset_read(a, &out[0], n);
    AAsset_close(a);
    return true;
}

//确定应用私有数据目录并确保其存在，结果写入 out。
//优先用 internalDataPath（应用私有、无需存储权限）；
//它为空时退到 externalDataPath，再不行就用 /data/local/tmp。mkdir 失败不视为错误，后续写文件会再暴露问题。
static bool ensureDataDir(std::string& out) {
    if (!g_activity) return false;
    const char* dir = g_activity->internalDataPath;
    if (!dir || !dir[0]) {
        std::string fallback = g_activity->externalDataPath ? g_activity->externalDataPath : "/data/local/tmp";
        mkdir(fallback.c_str(), 0755);
        out = fallback;
        return true;
    }
    out = dir;
    mkdir(out.c_str(), 0755);
    return true;
}

//挑选日志输出文件的路径，优先放在外部可访问目录，方便玩家直接从手机取日志。
//externalDataPath 通常指向 /sdcard/Android/data/<包名>/files，不需要额外权限就能被电脑读取；
//它为空时从 internalDataPath 里解析出包名，手工拼出等价的 sdcard 路径并逐级建目录。
//都不可用时返回空串，此时日志只进 logcat。
static std::string externalLogPath() {
    if (!g_activity) return std::string();
    const char* ext = g_activity->externalDataPath;
    ALOG("externalDataPath=%s internalDataPath=%s", ext ? ext : "(null)",
         g_activity->internalDataPath ? g_activity->internalDataPath : "(null)");
    if (ext && ext[0]) return std::string(ext) + "/gvengine.txt";
    const char* in = g_activity->internalDataPath;
    if (in && in[0]) {
        //内部路径形如 /data/user/0/<包名>/files，从中截出 <包名> 再拼外部路径。
        std::string s = in;
        size_t p = s.find("/files");
        if (p != std::string::npos) {
            size_t slash = s.rfind('/', p - 1);
            if (slash != std::string::npos) {
                std::string pkg = s.substr(slash + 1, p - slash - 1);
                std::string path = "/sdcard/Android/data/" + pkg + "/files";
                int r1 = mkdir("/sdcard/Android/data", 0755);
                int r2 = mkdir(("/sdcard/Android/data/" + pkg).c_str(), 0755);
                int r3 = mkdir(path.c_str(), 0755);
                ALOG("mkdir ext %d %d %d -> %s", r1, r2, r3, path.c_str());
                return path + "/gvengine.txt";
            }
        }
    }
    return std::string();
}

//把 assets 里的文件复制成立磁盘文件。用于需要真实路径的场合（如交给资源包读取器按文件打开）。
//返回 true 只表示 assets 里存在该文件且已尝试写入，不保证写盘成功。
static bool copyAssetToFile(const std::string& assetRel, const std::string& dst) {
    if (!g_activity || !g_activity->assetManager) return false;
    AAsset* a = AAssetManager_open(g_activity->assetManager, assetRel.c_str(), AASSET_MODE_BUFFER);
    if (!a) return false;
    size_t n = (size_t)AAsset_getLength(a);
    std::string data;
    data.resize(n);
    if (n > 0) AAsset_read(a, &data[0], n);
    AAsset_close(a);
    writeFile(dst, data.data(), data.size());
    return true;
}

//准备游戏资源包，依次尝试：本地已解出的 game.gpk → 直接从 assets 读进内存 → 把 assets 里的复制到磁盘。
//返回值语义特殊：非空表示"需要调用方按文件路径挂载"；空字符串表示已直接挂载到内存或没找到，
//调用方需再用 Gvtcier_Res_HasPack 判断是否可用。
//优先复用本地文件，是为了避免每次启动都做一次几十 MB 的复制。
static std::string ensureGamePack(const std::string& dir) {
    std::string pakPath = dir + "/game.gpk";
    struct stat st;
    if (stat(pakPath.c_str(), &st) == 0 && st.st_size > 0) {
        ALOG("reuse game.gpk %lld bytes", (long long)st.st_size);
        return pakPath;
    }
    std::string data;
    if (readAssetBytes("game.gpk", data) && !data.empty()) {
        Gvtcier_Res_SetPack(data.data(), (uint64_t)data.size());
        ALOG("gpk from assets %llu bytes hasPack=%d",
             (unsigned long long)data.size(), (int)Gvtcier_Res_HasPack());
        return std::string();
    }
    if (copyAssetToFile("game.gpk", pakPath)) {
        ALOG("copied game.gpk -> %s", pakPath.c_str());
        return pakPath;
    }
    ALOG("gpk not found in assets");
    return std::string();
}

//游戏主线程。全部引擎对象都在这里创建与使用，EGL 上下文也归它所有。
//启动顺序与桌面端基本一致：准备目录与日志 → 挂载资源 → 创建窗口并等原生窗口就绪 → 初始化 GL/RHI
//→ 构建场景与渲染器 → 播放启动画面 → 初始化物理与脚本 → 主循环 → 退出清理。
//任何一步失败都直接返回 nullptr，进程会停留在系统默认界面上，原因只能从 logcat 与日志文件里查。
static void* loopThread(void* arg) {
    ALOG("loop thread start");
    std::string dataDir;
    if (!ensureDataDir(dataDir)) {
        ALOG("data dir unavailable");
        return nullptr;
    }
    //日志重定向要在切目录之前完成：路径是绝对路径，不受 chdir 影响，但越早接上越能抓到早期错误。
    redirectStdoutToLog(externalLogPath().c_str());
    ALOG("data dir = %s", dataDir.c_str());
    //切到私有目录后，游戏内所有相对路径（存档、临时文件）都落在应用沙箱内，无需存储权限。
    chdir(dataDir.c_str());
    if (g_activity && g_activity->assetManager) {
        Gvtcier_Android_SetAssetManager(g_activity->assetManager);
    }
    std::string pakPath = ensureGamePack(dataDir);
    if (!pakPath.empty()) {
        Gvtcier_Res_SetPackFile(pakPath.c_str());
        ALOG("res pack loaded hasPack=%d", (int)Gvtcier_Res_HasPack());
    } else if (!Gvtcier_Res_HasPack()) {
        ALOG("no game pack found");
    }

    rlog::Gvtcier_Log_InitFile((dataDir + "/gvtcier.log").c_str());
    rlog::Gvtcier_Log_Write("[android] boot begin dir=%s\n", dataDir.c_str());

    const char* title = Gvtcier_Android_AppName ? Gvtcier_Android_AppName() : "Gvtcier";
    //窗口对象必须是静态或长生命周期的：它在整个运行期被多处引用，栈上会提前失效。
    static GvWWindow win;
    memset(&win, 0, sizeof(win));
    //这里的 1280x720 只是初始估算，真实尺寸在原生窗口就绪后由 GvW_Poll 更新。
    if (!GvW_Create(&win, title, 1280, 720)) {
        ALOG("GvW_Create failed");
        return nullptr;
    }
    //等待 Java 线程送来原生窗口，每次睡 8ms 轮询；同时检查退出标志，避免销毁时死等。
    while (!g_loopStop && !g_windowReady) usleep(8000);
    if (g_loopStop) return nullptr;
    if (!GvW_GL_Init(&win)) {
        ALOG("GvW_GL_Init failed");
        return nullptr;
    }
    g_glReady = true;
    //RHI 需要知道窗口对象才能查尺寸与函数地址；安卓端上下文已由 EGL 建好，所以传 nullptr。
    rhi::Gvtcier_Rhi_SetWindow((void*)&win);
    if (!rhi::Gvtcier_Rhi_Init(nullptr)) {
        ALOG("Rhi init failed");
        rlog::Gvtcier_Log_Write("[android] rhi init failed\n");
        return nullptr;
    }

    //安卓端不走设置文件，直接给一套固定画质：HDR、泛光 0.65、体积光、阴影与 GTAO 全开。
    //这些值在移动 GPU 上偏重，实际帧率依赖后续的自适应比例缩放（见主循环的 SetRatio）。
    static Scene scene;
    scene.hdr = true;
    scene.bloomStrength = 0.65f;
    scene.volumeLightEnabled = true;
    scene.showShadow = true;
    scene.gtaoEnabled = true;
    //以下子系统同样必须是静态对象，生命周期覆盖整个游戏运行期。
    static Renderer renderer;
    static Camera camera;
    static Canvas2D canvas2d;
    static Splash splash;
    static Player player;
    static PlayerSettings psBase;
    static PhysicsWorld physics;
    static GvtsBind gvts;
    static Hud hud;

    if (!renderer.Gvtcier_Render_Init()) {
        ALOG("render init failed");
        rlog::Gvtcier_Log_Write("[android] render init failed\n");
        return nullptr;
    }
    //移动 GPU 上遮挡剔除（GPU 查询回读）代价过高且会打断流水线，直接关闭。
    renderer.Gvtcier_Render_SetOcclusion(false);
    //渲染缩放的目标尺寸。初始按 1280x720，等拿到真实窗口尺寸后按 640 宽等比换算（见主循环）。
    int androidRatioW = 1280;
    int androidRatioH = 720;
    bool androidRatioReady = false;
    if (!scene.Gvtcier_Scene_Build()) {
        ALOG("scene build failed");
        rlog::Gvtcier_Log_Write("[android] scene build failed\n");
        return nullptr;
    }
    //补齐默认环境要素，保证启动画面有内容可看。
    scene.Gvtcier_Scene_InitSkybox();
    scene.Gvtcier_Scene_InitSun();
    scene.Gvtcier_Scene_InitVolumeLight();
    scene.Gvtcier_Scene_InitFog();

    splash.Gvtcier_Splash_SetDev(false);
    splash.Gvtcier_Splash_Begin(scene, 0);

    //启动画面的单帧。若原生窗口还没就绪（旋转或恢复中），GvW_GL_Init 会失败，
    //此时只睡 8ms 并返回 true 继续等待，而不是让整个启动失败。
    auto splashFrame = [&]() -> bool {
        if (!GvW_Poll(&win)) return false;
        if (!GvW_GL_Init(&win)) { usleep(8000); return true; }
        rhi::Gvtcier_Rhi_Viewport(0, 0, GvW_Width(&win), GvW_Height(&win));
        rhi::Gvtcier_Rhi_ClearColor(0.0f, 0.0f, 0.0f, 1.0f);
        rhi::Gvtcier_Rhi_Clear(rhi::ColorBit);
        //第三个参数固定为 3：安卓没有窗口模式概念，按窗口化样式绘制启动画面。
        splash.Gvtcier_Splash_Draw(renderer, GvW_Width(&win), GvW_Height(&win), 3);
        renderer.Gvtcier_Render_FlushCanvas(GvW_Width(&win), GvW_Height(&win));
        GvW_SwapBuffers(&win);
        return true;
    };

    //启动画面循环。除了推进动画，还负责处理命令队列（退出请求）与暂停状态。
    //暂停恢复后重置时间戳，避免把后台停留的几分钟当成一帧动画时间。
    bool alive = true;
    double splashPrev = Gvtcier_NowSeconds();
    while (splash.Gvtcier_Splash_Active() && alive) {
        AppCmd cmd;
        while (popCmd(&cmd)) {
            if (cmd.cmd == 0) g_loopStop = true;
        }
        if (g_loopStop) break;
        if (isPaused()) {
            waitWhilePaused();
            splashPrev = Gvtcier_NowSeconds();
            continue;
        }
        double now = Gvtcier_NowSeconds();
        float sdt = (float)(now - splashPrev);
        splashPrev = now;
        //同样的 0.25 秒钳制，防止加载卡顿后启动动画一帧跳完。
        if (sdt > 0.25f) sdt = 0.25f;
        splash.Gvtcier_Splash_Tick(sdt);
        alive = splashFrame();
    }
    splash.Gvtcier_Splash_End();
    ALOG("splash done alive=%d", (int)alive);

    //用平台窗口包装成引擎窗口对象，交给脚本层使用（脚本通过它查询按键与鼠标）。
    Window winWrap(&win);
    gvts.setScene(&scene);
    gvts.setPhysics(&physics);
    gvts.setPlayer(&player);
    gvts.setCamera(&camera);
    gvts.setWindow(&winWrap);
    gvts.setFx(&renderer.Gvtcier_Render_Fx());
    gvts.setRenderer(&renderer);
    //物理与脚本初始化失败只记日志继续：没有地图的纯 2D 游戏可能完全不需要物理。
    if (!physics.Gvtcier_Physics_Init(scene, player, psBase)) {
        ALOG("physics init failed");
    }
    if (!gvts.init()) {
        ALOG("gvts init failed");
        rlog::Gvtcier_Log_Write("[android] gvts init failed\n");
    } else {
        //脚本固定从资源包里读 game.gvts；读不到就传入空脚本，run 会失败并只记日志。
        std::string code;
        std::vector<unsigned char> packCode;
        if (Gvtcier_Res_ReadFile("game.gvts", packCode) && !packCode.empty()) {
            code.assign(packCode.begin(), packCode.end());
            ALOG("script from gpk %llu bytes", (unsigned long long)packCode.size());
        } else {
            ALOG("script missing in gpk");
        }
        if (!gvts.run(code)) {
            ALOG("gvts run failed");
            rlog::Gvtcier_Log_Write("[android] gvts run failed\n");
        } else {
            //记录脚本声明的渲染形态，便于排查"为什么画面是黑的"之类问题。
            ALOG("gvts run ok, is2DOnly=%d isOnlyGl=%d", (int)gvts.is2DOnly(), (int)gvts.isOnlyGl());
            rlog::Gvtcier_Log_Write("[android] gvts run ok 2d=%d onlygl=%d\n",
                                    (int)gvts.is2DOnly(), (int)gvts.isOnlyGl());
        }
        if (!canvas2d.Gvtcier_Canvas2D_Init()) {
            ALOG("canvas2d init failed");
        }
        gvts.setCanvas2D(&canvas2d);
    }

    RenderOptions opt;
    opt.hdr = scene.hdr;
    double last = Gvtcier_NowSeconds();
    double startTime = last;
    //与桌面端一致的固定物理步长 1/60 秒，安卓上也保持同样的模拟确定性。
    const float kStep = 1.0f / 60.0f;
    //时间累加器，攒够一步才推进一次模拟。
    float acc = 0.0f;
    int frameCount = 0;
    //性能统计累加器：模拟、提交、交换、整帧耗时与绘制调用数，每 60 帧汇总打印一次。
    double profSim = 0.0, profSubmit = 0.0, profSwap = 0.0, profLoop = 0.0, profDraws = 0.0;
    int profFrames = 0;
    //安卓主循环：每帧先处理命令队列与暂停，再按三种模式分支——纯 GL 脚本 / 纯 2D / 3D。
    while (!g_loopStop) {
        double profT0 = Gvtcier_NowSeconds();
        AppCmd cmd;
        while (popCmd(&cmd)) {
            if (cmd.cmd == 0) g_loopStop = true;
        }
        if (g_loopStop) break;
        if (isPaused()) {
            waitWhilePaused();
            //恢复后重置时间基准与累加器，否则会把后台时间当成一次超长卡顿。
            last = Gvtcier_NowSeconds();
            acc = 0.0f;
            continue;
        }
        if (!GvW_Poll(&win)) break;
        //surface 失效（旋转、切前后台）时先睡 16ms 等系统重建，期间不推进模拟也不渲染。
        if (!GvW_GL_Init(&win)) { usleep(16000); last = Gvtcier_NowSeconds(); continue; }
        int w = GvW_Width(&win);
        int h = GvW_Height(&win);
        if (w <= 0 || h <= 0) { usleep(16000); last = Gvtcier_NowSeconds(); continue; }

        double t = Gvtcier_NowSeconds();
        float dt = (float)(t - last);
        last = t;
        //与桌面端不同，这里连渲染用的 dt 也钳到 0.25 秒，避免切回前台时相机一帧转过半圈。
        if (dt > 0.25f) dt = 0.25f;

        //纯 GL 脚本：脚本自己绘制全部内容，引擎只清屏与翻页，然后按 60fps 补足剩余时间。
        if (gvts.isOnlyGl()) {
            gvts.tick(scene, dt);
            rhi::Gvtcier_Rhi_Viewport(0, 0, w, h);
            rhi::Gvtcier_Rhi_ClearColor(0.0f, 0.0f, 0.0f, 1.0f);
            rhi::Gvtcier_Rhi_Clear(rhi::ColorBit);
            gvts.draw(dt);
            GvW_SwapBuffers(&win);
            {
                double used = Gvtcier_NowSeconds() - profT0;
                const double kFrameTime = 1.0 / 60.0;
                if (used < kFrameTime) {
                    double remain = kFrameTime - used;
                    if (remain > 0.001) usleep((useconds_t)(remain * 1000000.0));
                }
            }
            continue;
        }

        //纯 2D 模式：按固定步长推进脚本，只画 2D 画布，不进入 3D 管线。
        if (gvts.is2DOnly()) {
            acc += dt;
            //同样钳制补步上限，防止卡顿后一次补上百步脚本回调。
            if (acc > 0.25f) acc = 0.25f;
            while (acc >= kStep) {
                gvts.tick(scene, kStep * scene.timeScale);
                gvts.tickObjectScripts(scene, kStep * scene.timeScale);
                acc -= kStep;
            }
            C2Camera2D c2cam;
            canvas2d.Gvtcier_Canvas2D_BeginFrame(w, h, c2cam);
            rhi::Gvtcier_Rhi_Viewport(0, 0, w, h);
            rhi::Gvtcier_Rhi_ClearColor(0.0f, 0.0f, 0.0f, 1.0f);
            rhi::Gvtcier_Rhi_Clear(rhi::ColorBit);
            gvts.draw(dt);
            canvas2d.Gvtcier_Canvas2D_EndFrame(nullptr);
            GvW_SwapBuffers(&win);
            {
                double used = Gvtcier_NowSeconds() - profT0;
                const double kFrameTime = 1.0 / 60.0;
                if (used < kFrameTime) {
                    double remain = kFrameTime - used;
                    if (remain > 0.001) usleep((useconds_t)(remain * 1000000.0));
                }
            }
            continue;
        }

        //3D 主路径：累加时间并准备本帧输入。
        acc += dt;
        if (acc > 0.25f) acc = 0.25f;

        //触屏拖拽转视角。0.36 是固定灵敏度：安卓没有设置界面可调，因此写死一个手感适中的值。
        int mdx = 0, mdy = 0;
        GvW_MouseDelta(&win, &mdx, &mdy);
        if (mdx != 0 || mdy != 0) {
            camera.turn((float)mdx * 0.36f, (float)mdy * 0.36f);
        }
        player.yawDeg = camera.yawDeg;
        //虚拟方向键与跳跃键直接映射成桌面键义。
        bool kW = GvW_IsKeyDown(&win, 'W');
        bool kS = GvW_IsKeyDown(&win, 'S');
        bool kA = GvW_IsKeyDown(&win, 'A');
        bool kD = GvW_IsKeyDown(&win, 'D');
        //跳跃必须是边沿触发：按住跳跃键不放不应连续起跳。
        static bool jumpPrev = false;
        bool kJ = GvW_IsKeyDown(&win, ' ');
        player.move(kW, kS, kA, kD);
        if (kJ && !jumpPrev) player.jumpRequested = true;
        jumpPrev = kJ;

        //固定步长推进：与桌面端一致，保证不同帧率下物理结果相同。
        while (acc >= kStep) {
            physics.Gvtcier_Physics_Step(player, scene, kStep * scene.timeScale);
            scene.Gvtcier_Scene_AnimTick(kStep * scene.timeScale);
            gvts.tick(scene, kStep * scene.timeScale);
            gvts.tickObjectScripts(scene, kStep * scene.timeScale);
            scene.Gvtcier_Scene_RefreshTransforms();
            physics.Gvtcier_Physics_SyncChildren(scene);
            acc -= kStep;
        }
        C2Camera2D c2cam;
        canvas2d.Gvtcier_Canvas2D_BeginFrame(w, h, c2cam);
        gvts.draw(dt);
        gvts.drawObjectScripts();
        renderer.Gvtcier_Render_Fx().Gvtcier_Fx_Update(dt);
        scene.Gvtcier_Scene_FlushRemoves(&physics);
        //安卓端相机直接跟随玩家眼睛位置，不做插值、不做目标跟随，逻辑比桌面端简单。
        camera.pos = player.pos + Vec3(0, player.eyeHeight, 0);
        opt.width = w;
        opt.height = h;
        opt.wireframe = scene.wireframe;
        opt.showShadow = scene.showShadow;
        opt.hdr = scene.hdr;
        //自适应渲染比例：第一次拿到有效尺寸时，按固定 640 宽等比算出渲染分辨率。
        //移动 GPU 的像素填充率有限，内部按 640 宽渲染再放大到屏幕，是保证帧率的主要手段。
        //这个换算只做一次，之后窗口尺寸变化不会重新计算。
        if (!androidRatioReady && w > 0 && h > 0) {
            androidRatioReady = true;
            const int kTargetW = 640;
            int rh = h * kTargetW / w;
            if (rh < 1) rh = 1;
            androidRatioW = kTargetW;
            androidRatioH = rh;
            printf("[ratio] render %dx%d (window %dx%d)\n", androidRatioW, androidRatioH, w, h);
            fflush(stdout);
        }
        renderer.Gvtcier_Render_SetRatio(androidRatioW, androidRatioH);
        double profT2 = Gvtcier_NowSeconds();
        //清零绘制调用计数，本帧结束时读取，得到单帧绘制次数。
        Gvtcier_Gles_ResetDrawCalls();
        {
            //HUD 在安卓上固定不显示调试面板，只显示帧率与坐标。
            hud.Gvtcier_Hud_Update(dt);
            HudStats hst;
            hst.fps = hud.fps();
            hst.ms = hud.ms();
            hst.elapsed = (float)(Gvtcier_NowSeconds() - startTime);
            hst.px = player.pos.x;
            hst.py = player.pos.y;
            hst.pz = player.pos.z;
            hud.Gvtcier_Hud_Draw(renderer, w, h, hst, false, scene.showCrosshair);
        }
        renderer.Gvtcier_Render_Render(scene, camera, opt);
        unsigned long long frameDraws = Gvtcier_Gles_DrawCalls();
        canvas2d.Gvtcier_Canvas2D_EndFrame(nullptr);
        //应用层可选的叠加层钩子（如调试信息），未提供时跳过。
        if (Gvtcier_Android_DrawOverlay) Gvtcier_Android_DrawOverlay(renderer, &win);
        double profT3 = Gvtcier_NowSeconds();
        GvW_SwapBuffers(&win);
        double profT4 = Gvtcier_NowSeconds();
        {
            //安卓端统一锁 60fps：移动设备上无节制的满帧渲染会迅速升温降频，
            //固定 60 帧反而更容易保持稳定，因此这里不做可配置的帧率档位。
            const double kFrameTime = 1.0 / 60.0;
            double used = profT4 - profT0;
            if (used < kFrameTime) {
                double remain = kFrameTime - used;
                if (remain > 0.001) usleep((useconds_t)(remain * 1000000.0));
            }
        }
        //累计各阶段耗时，每 60 帧输出一次平均值；绘制调用数与实际渲染分辨率一并打印，便于定位瓶颈。
        profSim += profT2 - profT0;
        profSubmit += profT3 - profT2;
        profSwap += profT4 - profT3;
        profLoop += profT4 - profT0;
        profDraws += frameDraws;
        ++profFrames;
        if (profFrames >= 60) {
            double n = (double)profFrames;
            int rrW = 0, rrH = 0;
            renderer.Gvtcier_Render_GetRatio(&rrW, &rrH);
            printf("[loop] avg ms sim=%.2f submit=%.2f swap=%.2f loop=%.2f fps=%.1f draws=%.0f ratio=%dx%d\n",
                profSim / n * 1000.0, profSubmit / n * 1000.0,
                profSwap / n * 1000.0, profLoop / n * 1000.0, profLoop > 0.0 ? n / profLoop : 0.0,
                profDraws / n, rrW, rrH);
            fflush(stdout);
            profSim = profSubmit = profSwap = profLoop = 0.0;
            profDraws = 0.0;
            profFrames = 0;
        }
        //每 120 帧往 logcat 打一行心跳，含尺寸与 dt，用于确认应用没卡死。
        if ((++frameCount % 120) == 0) {
            ALOG("frame %d %dx%d dt=%.3f", frameCount, w, h, dt);
        }
    }

    //退出清理：顺序与初始化相反，先停脚本与各子系统，最后关 GL 与窗口。
    ALOG("loop end");
    rlog::Gvtcier_Log_Write("[android] loop end frames=%d\n", frameCount);
    gvts.shutdown();
    canvas2d.Gvtcier_Canvas2D_Shutdown();
    physics.Gvtcier_Physics_Shutdown();
    renderer.Gvtcier_Render_Shutdown();
    scene.Gvtcier_Scene_Destroy();
    GvW_GL_Shutdown(&win);
    GvW_Destroy(&win);
    return nullptr;
}

//通过 JNI 把窗口设为沉浸式全屏：刘海区域也可以绘制，并隐藏状态栏与导航栏。
//之所以要直接调 Java 反射：NativeActivity 没有提供这些窗口标志的 C 接口，只能自己拼 JNI 调用。
//整个过程在线程上做，若当前线程未附加到 JVM 就先 Attach，结束时再 Detach 回去，避免线程泄漏。
//任何一步取不到方法就跳过（不同 Android 版本 API 有差异），最后统一清理 Java 局部引用与挂起异常。
static void applyImmersive(ANativeActivity* activity) {
    if (!activity || !activity->vm || !activity->clazz) return;
    JavaVM* vm = activity->vm;
    JNIEnv* env = nullptr;
    bool attached = false;
    if (vm->GetEnv((void**)&env, JNI_VERSION_1_6) != JNI_OK) {
        if (vm->AttachCurrentThread(&env, nullptr) != JNI_OK) return;
        attached = true;
    }
    jobject activityObj = activity->clazz;
    jclass activityClass = env->GetObjectClass(activityObj);
    jmethodID getWindow = env->GetMethodID(activityClass, "getWindow", "()Landroid/view/Window;");
    if (getWindow) {
        jobject window = env->CallObjectMethod(activityObj, getWindow);
        if (window) {
            jclass windowClass = env->GetObjectClass(window);
            jmethodID getAttributes = env->GetMethodID(windowClass, "getAttributes", "()Landroid/view/WindowManager$LayoutParams;");
            jmethodID setAttributes = env->GetMethodID(windowClass, "setAttributes", "(Landroid/view/WindowManager$LayoutParams;)V");
            if (getAttributes && setAttributes) {
                jobject attrs = env->CallObjectMethod(window, getAttributes);
                if (attrs) {
                    jclass attrsClass = env->GetObjectClass(attrs);
                    jfieldID cutoutField = env->GetFieldID(attrsClass, "layoutInDisplayCutoutMode", "I");
                    if (cutoutField) {
                        const jint LAYOUT_IN_DISPLAY_CUTOUT_MODE_SHORT_EDGES = 1;
                        env->SetIntField(attrs, cutoutField, LAYOUT_IN_DISPLAY_CUTOUT_MODE_SHORT_EDGES);
                        env->CallVoidMethod(window, setAttributes, attrs);
                    }
                    env->DeleteLocalRef(attrsClass);
                    env->DeleteLocalRef(attrs);
                }
            }
            jmethodID getDecor = env->GetMethodID(windowClass, "getDecorView", "()Landroid/view/View;");
            if (getDecor) {
                jobject decor = env->CallObjectMethod(window, getDecor);
                if (decor) {
                    jclass viewClass = env->GetObjectClass(decor);
                    jmethodID setVis = env->GetMethodID(viewClass, "setSystemUiVisibility", "(I)V");
                    if (setVis) {
                        const jint LAYOUT_STABLE = 0x00000100;
                        const jint LAYOUT_FULLSCREEN = 0x00000400;
                        const jint LAYOUT_HIDE_NAVIGATION = 0x00000200;
                        const jint FULLSCREEN = 0x00000004;
                        const jint HIDE_NAVIGATION = 0x00000002;
                        const jint IMMERSIVE_STICKY = 0x00001000;
                        jint flags = LAYOUT_STABLE | LAYOUT_FULLSCREEN | LAYOUT_HIDE_NAVIGATION |
                                     FULLSCREEN | HIDE_NAVIGATION | IMMERSIVE_STICKY;
                        env->CallVoidMethod(decor, setVis, flags);
                    }
                    env->DeleteLocalRef(viewClass);
                    env->DeleteLocalRef(decor);
                }
            }
            env->DeleteLocalRef(windowClass);
            env->DeleteLocalRef(window);
        }
    }
    env->DeleteLocalRef(activityClass);
    if (env->ExceptionCheck()) env->ExceptionClear();
    if (attached) vm->DetachCurrentThread();
}

//NativeActivity 的入口函数，由系统在 Activity 创建时调用。
//这里只做三件事：设置窗口标志、注册全部生命周期与输入回调、启动游戏线程。
//savedState 与 savedStateSize 是进程被回收后重建时用来恢复状态的，当前未使用。
//游戏线程以 16MB 栈运行：脚本解释器递归解析、渲染管线里的深调用比默认 1MB 栈更容易吃满。
extern "C" __attribute__((visibility("default"))) void ANativeActivity_onCreate(ANativeActivity* activity,
                                                                                void* savedState,
                                                                                size_t savedStateSize) {
    ALOG("onCreate");
    g_activity = activity;
    //0x04 是 FLAG_KEEP_SCREEN_ON（游戏内不息屏），0x80 是 FLAG_FULLSCREEN（隐藏状态栏）。
    ANativeActivity_setWindowFlags(activity, 0x00000004 | 0x00000080, 0);
    applyImmersive(activity);
    //必须覆盖系统默认回调：默认实现会让 Activity 走正常退出流程，游戏线程就拿不到窗口与输入。
    activity->callbacks->onStart = onStart;
    activity->callbacks->onResume = onResume;
    activity->callbacks->onPause = onPause;
    activity->callbacks->onStop = onStop;
    activity->callbacks->onDestroy = onDestroy;
    activity->callbacks->onNativeWindowCreated = onNativeWindowCreated;
    activity->callbacks->onNativeWindowDestroyed = onNativeWindowDestroyed;
    activity->callbacks->onInputQueueCreated = onInputQueueCreated;
    activity->callbacks->onInputQueueDestroyed = onInputQueueDestroyed;
    pthread_t tid;
    pthread_attr_t attr;
    pthread_attr_init(&attr);
    pthread_attr_setstacksize(&attr, 16 * 1024 * 1024);
    pthread_create(&tid, &attr, loopThread, nullptr);
    pthread_attr_destroy(&attr);
    pthread_detach(tid);
}

#endif
