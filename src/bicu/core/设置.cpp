#include "Gvtcier.h"
#ifdef _WIN32
#endif
#ifdef _WIN32
#include <windows.h>
#endif
#include <cstdio>
#include <cstring>
#include <cstdlib>

//全局唯一的设置实例。所有模块共享同一份，改一处即可全局生效。
static GameSettings g_settings;
//settings.ini 的绝对路径，由 Gvtcier_Settings_Load 记录；为空表示还没确定位置，此时保存直接跳过。
static std::string g_settingsPath;
//是否已做过一次性初始化（应用默认键位预设）。Load 也会把它置真，避免加载后又被预设覆盖。
static bool g_loaded = false;

//取全局设置的唯一入口。
//首次调用时按当前键位预设填充一套默认键位，保证即使没有 settings.ini，方向键也是可用的。
GameSettings& Gvtcier_Settings() {
    if (!g_loaded) {
        g_loaded = true;
        Gvtcier_Settings_ApplyPreset(g_settings.keyPreset);
    }
    return g_settings;
}

//应用一套预置键位。
//左手预设沿用 WASD + 空格跳 + 左 Shift 疾跑 + 左 Ctrl 下蹲；
//右手预设把移动移到 IJKL，并把疾跑/下蹲换到右 Shift/右 Ctrl，方便左手操作鼠标。
//preset 为 PRESET_CUSTOM 时不做任何改动，保留玩家自己设置的键位。
void Gvtcier_Settings_ApplyPreset(int preset) {
    g_settings.keyPreset = preset;
    if (preset == PRESET_LEFT_HAND) {
        g_settings.keyBindings[ACTION_FORWARD] = 'W';
        g_settings.keyBindings[ACTION_BACK] = 'S';
        g_settings.keyBindings[ACTION_LEFT] = 'A';
        g_settings.keyBindings[ACTION_RIGHT] = 'D';
        g_settings.keyBindings[ACTION_JUMP] = 0x20;
        g_settings.keyBindings[ACTION_SPRINT] = 0xA0;
        g_settings.keyBindings[ACTION_CROUCH] = 0xA2;
    } else if (preset == PRESET_RIGHT_HAND) {
        g_settings.keyBindings[ACTION_FORWARD] = 'I';
        g_settings.keyBindings[ACTION_BACK] = 'K';
        g_settings.keyBindings[ACTION_LEFT] = 'J';
        g_settings.keyBindings[ACTION_RIGHT] = 'L';
        g_settings.keyBindings[ACTION_JUMP] = 0x20;
        g_settings.keyBindings[ACTION_SPRINT] = 0xA1;
        g_settings.keyBindings[ACTION_CROUCH] = 0xA3;
    }
}

//把内置动作编号翻译成中文显示名，用于设置界面的键位列表。未知编号返回空串。
const char* Gvtcier_Settings_ActionName(int action) {
    switch (action) {
        case ACTION_FORWARD: return u8"前进";
        case ACTION_BACK: return u8"后退";
        case ACTION_LEFT: return u8"左移";
        case ACTION_RIGHT: return u8"右移";
        case ACTION_JUMP: return u8"跳跃";
        case ACTION_SPRINT: return u8"疾跑";
        case ACTION_CROUCH: return u8"下蹲";
        default: return "";
    }
}

//把 Windows 虚拟键码翻译成玩家看得懂的按键名。
//先查常见特殊键的中文名；功能键 F1~F12 的虚拟键码是连续的 0x70~0x7B，按偏移算编号；
//数字与字母键的键码恰好等于 ASCII 码，直接转成单字符；其余不认识的键退回 VK_XX 十六进制形式。
std::string Gvtcier_Settings_KeyName(int vk) {
    if (vk == 0) return u8"无按键";
    switch (vk) {
        case 0x20: return u8"空格";
        case 0x0D: return u8"回车";
        case 0xA0: return u8"左Shift";
        case 0xA1: return u8"右Shift";
        case 0xA2: return u8"左Ctrl";
        case 0xA3: return u8"右Ctrl";
        case 0x09: return u8"Tab";
        case 0x2D: return u8"Insert";
        case 0x2E: return u8"Delete";
        case 0x24: return u8"Home";
        case 0x23: return u8"End";
        case 0x21: return u8"PageUp";
        case 0x22: return u8"PageDown";
        case 0x25: return u8"左方向";
        case 0x26: return u8"上方向";
        case 0x27: return u8"右方向";
        case 0x28: return u8"下方向";
        default: break;
    }
    if (vk >= 0x70 && vk <= 0x7B) {
        char buf[8];
        snprintf(buf, sizeof(buf), "F%d", vk - 0x6F);
        return std::string(buf);
    }
    if (vk >= 0x30 && vk <= 0x39) {
        char buf[4] = { (char)vk, 0, 0, 0 };
        return std::string(buf);
    }
    if (vk >= 0x41 && vk <= 0x5A) {
        char buf[4] = { (char)vk, 0, 0, 0 };
        return std::string(buf);
    }
    char buf[16];
    snprintf(buf, sizeof(buf), "VK_%02X", vk);
    return std::string(buf);
}

//从 gameDir\settings.ini 读取设置。
//文件格式是极简的"键=值"逐行文本，不是标准 ini：没有节名、没有引号、不做转义，
//每行按第一个 '=' 切成键与值，再各自去掉首尾空白与行尾换行。
//解析是容错的：任何一行不认识就忽略，缺失的项保持结构体默认值，因此损坏的文件不会让游戏启动失败。
//副作用：记录保存路径，并把 g_loaded 置真（之后不会再套用键位预设，避免覆盖玩家配置）。
void Gvtcier_Settings_Load(const char* gameDir) {
    g_loaded = true;
    g_settingsPath.clear();
    if (!gameDir || !gameDir[0]) return;
    g_settingsPath = std::string(gameDir) + "\\settings.ini";
    FILE* f = Gvtcier_File_Gfh(g_settingsPath.c_str(), L"rb");
    //读不到文件不算错误：全新安装第一次运行时设置文件本来就不存在。
    if (!f) return;
    char line[512];
    while (fgets(line, sizeof(line), f)) {
        char* eq = strchr(line, '=');
        if (!eq) continue;
        *eq = 0;
        const char* key = line;
        const char* val = eq + 1;
        while (*key == ' ' || *key == '\t') ++key;
        while (*val == ' ' || *val == '\t') ++val;
        char* end = (char*)val + strlen(val);
        //去掉行尾的换行、回车与空格，否则字符串型设置（如 serverRoom）会把换行符带进去。
        while (end > val && (end[-1] == '\n' || end[-1] == '\r' || end[-1] == ' ')) --end;
        *end = 0;
        //布尔项统一按"0/非 0"存整型；浮点项用 atof，整型项用 atoi。
        if (strcmp(key, "masterVolume") == 0) g_settings.masterVolume = (float)atof(val);
        else if (strcmp(key, "soundVolume") == 0) g_settings.soundVolume = (float)atof(val);
        else if (strcmp(key, "musicVolume") == 0) g_settings.musicVolume = (float)atof(val);
        else if (strcmp(key, "motionCamera") == 0) g_settings.motionCamera = atoi(val) != 0;
        else if (strcmp(key, "fov") == 0) g_settings.fov = (float)atof(val);
        else if (strcmp(key, "frameRate") == 0) g_settings.frameRate = atoi(val);
        else if (strcmp(key, "windowMode") == 0) g_settings.windowMode = atoi(val);
        else if (strcmp(key, "hdr") == 0) g_settings.hdr = atoi(val) != 0;
        else if (strcmp(key, "bloomStrength") == 0) g_settings.bloomStrength = (float)atof(val);
        else if (strcmp(key, "anisotropic") == 0) g_settings.anisotropic = atoi(val);
        else if (strcmp(key, "shadowQuality") == 0) g_settings.shadowQuality = atoi(val);
        else if (strcmp(key, "volumeLight") == 0) g_settings.volumeLight = atoi(val) != 0;
        else if (strcmp(key, "autoExposure") == 0) g_settings.autoExposure = atoi(val) != 0;
        else if (strcmp(key, "fogQuality") == 0) g_settings.fogQuality = atoi(val);
        else if (strcmp(key, "specular") == 0) g_settings.specular = atoi(val) != 0;
        else if (strcmp(key, "lightQuality") == 0) g_settings.lightQuality = atoi(val);
        else if (strcmp(key, "vignetteStrength") == 0) g_settings.vignetteStrength = (float)atof(val);
        else if (strcmp(key, "gtao") == 0) g_settings.gtao = atoi(val) != 0;
        else if (strcmp(key, "fxaa") == 0) g_settings.fxaa = atoi(val) != 0;
        else if (strcmp(key, "occlusion") == 0) g_settings.occlusion = atoi(val) != 0;
        else if (strcmp(key, "mouseSensitivity") == 0) g_settings.mouseSensitivity = (float)atof(val);
        else if (strcmp(key, "keyPreset") == 0) g_settings.keyPreset = atoi(val);
        else if (strcmp(key, "menuKey") == 0) g_settings.menuKey = atoi(val);
        else if (strcmp(key, "netPort") == 0) g_settings.netPort = atoi(val);
        else if (strcmp(key, "serverRoom") == 0) g_settings.serverRoom = val;
        //"keyN" 是内置动作的键位，N 为动作编号；"action.标识" 是脚本注册的自定义动作键位。
        else if (strncmp(key, "key", 3) == 0) {
            int idx = atoi(key + 3);
            if (idx >= 0 && idx < ACTION_COUNT) g_settings.keyBindings[idx] = atoi(val);
        }
        else if (strncmp(key, "action.", 7) == 0) {
            //自定义动作首次出现时自动补建条目，id 与显示名暂用同一个标识，游戏后续可以再改显示名。
            const char* id = key + 7;
            int idx = Gvtcier_Settings_FindAction(id);
            if (idx < 0) {
                GvtKeyAction a;
                a.id = id;
                a.name = id;
                a.vk = atoi(val);
                g_settings.actions.push_back(a);
            } else {
                g_settings.actions[idx].vk = atoi(val);
            }
        }
    }
    fclose(f);
}

//把当前设置写回 settings.ini，键名与 Load 的解析一一对应，整体覆盖旧文件。
//浮点用固定小数位输出，既避免科学计数法，也避免二进制浮点展开成超长数字。
//副作用：路径未确定时静默返回，此时设置只存在于内存中。
void Gvtcier_Settings_Save() {
    if (g_settingsPath.empty()) return;
    FILE* f = Gvtcier_File_Gfh(g_settingsPath.c_str(), L"wb");
    if (!f) return;
    fprintf(f, "masterVolume=%.3f\n", g_settings.masterVolume);
    fprintf(f, "soundVolume=%.3f\n", g_settings.soundVolume);
    fprintf(f, "musicVolume=%.3f\n", g_settings.musicVolume);
    fprintf(f, "motionCamera=%d\n", g_settings.motionCamera ? 1 : 0);
    fprintf(f, "fov=%.1f\n", g_settings.fov);
    fprintf(f, "frameRate=%d\n", g_settings.frameRate);
    fprintf(f, "windowMode=%d\n", g_settings.windowMode);
    fprintf(f, "hdr=%d\n", g_settings.hdr ? 1 : 0);
    fprintf(f, "bloomStrength=%.3f\n", g_settings.bloomStrength);
    fprintf(f, "anisotropic=%d\n", g_settings.anisotropic);
    fprintf(f, "shadowQuality=%d\n", g_settings.shadowQuality);
    fprintf(f, "volumeLight=%d\n", g_settings.volumeLight ? 1 : 0);
    fprintf(f, "autoExposure=%d\n", g_settings.autoExposure ? 1 : 0);
    fprintf(f, "fogQuality=%d\n", g_settings.fogQuality);
    fprintf(f, "specular=%d\n", g_settings.specular ? 1 : 0);
    fprintf(f, "lightQuality=%d\n", g_settings.lightQuality);
    fprintf(f, "vignetteStrength=%.3f\n", g_settings.vignetteStrength);
    fprintf(f, "gtao=%d\n", g_settings.gtao ? 1 : 0);
    fprintf(f, "fxaa=%d\n", g_settings.fxaa ? 1 : 0);
    fprintf(f, "occlusion=%d\n", g_settings.occlusion ? 1 : 0);
    fprintf(f, "mouseSensitivity=%.3f\n", g_settings.mouseSensitivity);
    fprintf(f, "keyPreset=%d\n", g_settings.keyPreset);
    fprintf(f, "menuKey=%d\n", g_settings.menuKey);
    fprintf(f, "netPort=%d\n", g_settings.netPort);
    fprintf(f, "serverRoom=%s\n", g_settings.serverRoom.c_str());
    for (int i = 0; i < ACTION_COUNT; ++i)
        fprintf(f, "key%d=%d\n", i, g_settings.keyBindings[i]);
    //自定义动作按登记顺序输出，Load 时靠 "action." 前缀重新识别并按 id 匹配，顺序本身不影响正确性。
    for (size_t i = 0; i < g_settings.actions.size(); ++i)
        fprintf(f, "action.%s=%d\n", g_settings.actions[i].id.c_str(), g_settings.actions[i].vk);
    fclose(f);
}

//按标识查自定义动作的下标，找不到返回 -1。标识区分大小写，脚本应固定使用同一写法。
int Gvtcier_Settings_FindAction(const char* id) {
    if (!id) return -1;
    GameSettings& s = Gvtcier_Settings();
    for (size_t i = 0; i < s.actions.size(); ++i)
        if (s.actions[i].id == id) return (int)i;
    return -1;
}

//登记一个自定义动作并返回其下标；已存在则只做补充更新。
//更新策略：只覆盖非空的显示名，只在当前键位为空时填入默认键，
//这样玩家改过的键位不会被脚本再次登记时冲掉。
int Gvtcier_Settings_RegisterAction(const char* id, const char* name, int defaultVk) {
    if (!id || !*id) return -1;
    GameSettings& s = Gvtcier_Settings();
    int idx = Gvtcier_Settings_FindAction(id);
    if (idx >= 0) {
        if (name && *name) s.actions[idx].name = name;
        if (defaultVk != 0 && s.actions[idx].vk == 0) s.actions[idx].vk = defaultVk;
        return idx;
    }
    GvtKeyAction a;
    a.id = id;
    a.name = (name && *name) ? name : id;
    a.vk = defaultVk;
    s.actions.push_back(a);
    return (int)s.actions.size() - 1;
}

//自定义动作的只读访问组：数量、标识、显示名、键位。
//越界时返回空串或 0，调用方无需自己判断下标合法性。
int Gvtcier_Settings_ActionCount() {
    return (int)Gvtcier_Settings().actions.size();
}

const char* Gvtcier_Settings_ActionIdAt(int i) {
    GameSettings& s = Gvtcier_Settings();
    if (i < 0 || i >= (int)s.actions.size()) return "";
    return s.actions[i].id.c_str();
}

const char* Gvtcier_Settings_ActionNameAt(int i) {
    GameSettings& s = Gvtcier_Settings();
    if (i < 0 || i >= (int)s.actions.size()) return "";
    return s.actions[i].name.c_str();
}

int Gvtcier_Settings_ActionVkAt(int i) {
    GameSettings& s = Gvtcier_Settings();
    if (i < 0 || i >= (int)s.actions.size()) return 0;
    return s.actions[i].vk;
}

//返回键位字段的可写指针，供设置界面做"按键捕获后直接改值"。越界返回空指针。
int* Gvtcier_Settings_ActionVkPtrAt(int i) {
    GameSettings& s = Gvtcier_Settings();
    if (i < 0 || i >= (int)s.actions.size()) return nullptr;
    return &s.actions[i].vk;
}
