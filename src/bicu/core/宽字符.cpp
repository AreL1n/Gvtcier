#include "Gvtcier.h"
#include <windows.h>
#include <cstring>

//本文件是 UTF-8 与宽字符之间的转换层。
//Windows 的宽字符 API 是唯一能正确处理任意路径（含中文）的入口，而引擎内部统一用 UTF-8 字符串，
//所以所有文件、模块、窗口操作都要先经过这里转换。

//UTF-8 → 宽字符。返回结果不含结尾 0（std::wstring 自己管理）。
//空输入返回空串；转换失败（n <= 1，即只有结尾 0 或出错）也返回空串。
std::wstring Gvtcier_W32_ToWide(const char* utf8) {
    if (!utf8 || !utf8[0]) return std::wstring();
    int n = MultiByteToWideChar(CP_UTF8, 0, utf8, -1, nullptr, 0);
    if (n <= 1) return std::wstring();
    //先按 n-1 预留，再额外多给 1 个位置让 API 写入结尾 0，最后把 0 去掉。
    std::wstring ws((size_t)n - 1, L'\0');
    MultiByteToWideChar(CP_UTF8, 0, utf8, -1, &ws[0], n);
    if (!ws.empty() && ws.back() == L'\0') ws.pop_back();
    return ws;
}

//std::string 版本的重载。
std::wstring Gvtcier_W32_ToWide(const std::string& utf8) {
    return Gvtcier_W32_ToWide(utf8.c_str());
}

//宽字符 → UTF-8，规则与 ToWide 对称（结果不含结尾 0）。
std::string Gvtcier_W32_ToUtf8(const wchar_t* wide) {
    if (!wide || !wide[0]) return std::string();
    int n = WideCharToMultiByte(CP_UTF8, 0, wide, -1, nullptr, 0, nullptr, nullptr);
    if (n <= 1) return std::string();
    std::string s((size_t)n - 1, '\0');
    WideCharToMultiByte(CP_UTF8, 0, wide, -1, &s[0], n, nullptr, nullptr);
    if (!s.empty() && s.back() == '\0') s.pop_back();
    return s;
}

//std::wstring 版本的重载。
std::string Gvtcier_W32_ToUtf8(const std::wstring& wide) {
    return Gvtcier_W32_ToUtf8(wide.c_str());
}

//缓冲区版转换，直接写入调用方提供的 out（容量 cap 个 wchar_t）。成功返回 true。
//适用场景是不想分配 std::wstring 的栈上临时转换；失败时 out 被置为空串。
bool Gvtcier_W32_ToWideBuf(const char* utf8, wchar_t* out, int cap) {
    if (!out || cap <= 0) return false;
    out[0] = 0;
    if (!utf8 || !utf8[0]) return false;
    int n = MultiByteToWideChar(CP_UTF8, 0, utf8, -1, out, cap);
    return n > 0;
}

//缓冲区版的宽字符 → UTF-8，容量 cap 是字节数。
bool Gvtcier_W32_ToUtf8Buf(const wchar_t* wide, char* out, int cap) {
    if (!out || cap <= 0) return false;
    out[0] = 0;
    if (!wide || !wide[0]) return false;
    int n = WideCharToMultiByte(CP_UTF8, 0, wide, -1, out, cap, nullptr, nullptr);
    return n > 0;
}

//当前系统 ANSI 代码页是否为 UTF-8（即 Windows 的"使用 Unicode UTF-8 提供全球语言支持"选项已开启）。
//该选项会影响用窄字符 API 打开文件的行为，引擎据此判断能否走窄路径。
//谁不喜欢UTF8呢
bool Gvtcier_W32_IsUtf8CodePage() {
    return GetACP() == CP_UTF8;
}

//取当前可执行文件的完整路径（UTF-8）。缓冲区按 MAX_PATH * 2 留足余量，容纳长路径。
std::string Gvtcier_W32_ModulePathUtf8() {
    wchar_t buf[MAX_PATH * 2] = {};
    DWORD n = GetModuleFileNameW(nullptr, buf, (DWORD)(sizeof(buf) / sizeof(buf[0])));
    if (n == 0) return std::string();
    return Gvtcier_W32_ToUtf8(buf);
}

//取可执行文件所在目录（UTF-8）。从字符串末尾往前找第一个分隔符，找到即截断；
//没有任何分隔符说明路径异常，返回空串。
std::string Gvtcier_W32_ModuleDirUtf8() {
    std::string p = Gvtcier_W32_ModulePathUtf8();
    if (p.empty()) return p;
    for (size_t i = p.size(); i > 0; --i) {
        if (p[i - 1] == '\\' || p[i - 1] == '/') return p.substr(0, i - 1);
    }
    return std::string();
}

//把相对路径解析成绝对路径（UTF-8），相对基准是当前工作目录。
//缓冲区按 MAX_PATH * 4 留足余量以支持较长路径；结果过长（n 达到容量）视为失败返回空串，
//因为此时路径已被截断，继续使用只会指向错误位置。
std::string Gvtcier_W32_FullPathUtf8(const char* utf8Path) {
    if (!utf8Path || !utf8Path[0]) return std::string();
    std::wstring w = Gvtcier_W32_ToWide(utf8Path);
    if (w.empty()) return std::string();
    wchar_t buf[MAX_PATH * 4] = {};
    DWORD n = GetFullPathNameW(w.c_str(), (DWORD)(sizeof(buf) / sizeof(buf[0])), buf, nullptr);
    if (n == 0 || n >= (DWORD)(sizeof(buf) / sizeof(buf[0]))) return std::string();
    return Gvtcier_W32_ToUtf8(buf);
}

//判断文件或目录是否存在。用属性查询而非打开文件，因此也能识别目录，且不会触发文件锁。
bool Gvtcier_W32_FileExistsUtf8(const char* utf8Path) {
    if (!utf8Path || !utf8Path[0]) return false;
    std::wstring w = Gvtcier_W32_ToWide(utf8Path);
    if (w.empty()) return false;
    DWORD attr = GetFileAttributesW(w.c_str());
    return attr != INVALID_FILE_ATTRIBUTES;
}

//加载动态库（UTF-8 路径）。返回模块句柄，失败返回空；调用方负责用 FreeLibrary 释放。
void* Gvtcier_W32_LoadLibraryUtf8(const char* utf8Path) {
    if (!utf8Path || !utf8Path[0]) return nullptr;
    std::wstring w = Gvtcier_W32_ToWide(utf8Path);
    if (w.empty()) return nullptr;
    return (void*)LoadLibraryW(w.c_str());
}

//带标志的库加载。flags 例如 LOAD_LIBRARY_AS_DATAFILE，用于"只查看不执行"的探测式加载。
void* Gvtcier_W32_LoadLibraryExUtf8(const char* utf8Path, unsigned long flags) {
    if (!utf8Path || !utf8Path[0]) return nullptr;
    std::wstring w = Gvtcier_W32_ToWide(utf8Path);
    if (w.empty()) return nullptr;
    return (void*)LoadLibraryExW(w.c_str(), nullptr, (DWORD)flags);
}

//按通配符（如 "mod\\*.glc"）列出匹配的文件名，返回纯文件名而非完整路径。
//目录会被跳过：通配符可能匹配到目录，而调用方都只需要文件。结果不排序，由调用方自行决定顺序。
std::vector<std::string> Gvtcier_W32_FindFilesUtf8(const char* utf8Pattern) {
    std::vector<std::string> out;
    if (!utf8Pattern || !utf8Pattern[0]) return out;
    std::wstring w = Gvtcier_W32_ToWide(utf8Pattern);
    if (w.empty()) return out;
    WIN32_FIND_DATAW fd;
    HANDLE h = FindFirstFileW(w.c_str(), &fd);
    if (h == INVALID_HANDLE_VALUE) return out;
    do {
        if (fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) continue;
        out.push_back(Gvtcier_W32_ToUtf8(fd.cFileName));
    } while (FindNextFileW(h, &fd));
    FindClose(h);
    return out;
}

//弹出消息框（UTF-8 文本与标题），返回玩家点击的按钮值。flags 为 MB_ICON* / MB_OK 等标准标志。
int Gvtcier_W32_MessageBoxUtf8(const char* utf8Text, const char* utf8Title, unsigned int flags) {
    std::wstring t = Gvtcier_W32_ToWide(utf8Text);
    std::wstring c = Gvtcier_W32_ToWide(utf8Title);
    return (int)MessageBoxW(nullptr, t.c_str(), c.c_str(), (UINT)flags);
}

//设置窗口标题（UTF-8）。hwnd 以 void* 传递以避免头文件泄漏 windows.h。
bool Gvtcier_W32_SetWindowTextUtf8(void* hwnd, const char* utf8Text) {
    if (!hwnd) return false;
    std::wstring w = Gvtcier_W32_ToWide(utf8Text);
    return SetWindowTextW((HWND)hwnd, w.c_str()) != 0;
}

//读取窗口标题（UTF-8）。缓冲区固定 512 个宽字符，超长标题会被截断。
std::string Gvtcier_W32_GetWindowTextUtf8(void* hwnd) {
    if (!hwnd) return std::string();
    wchar_t buf[512] = {};
    int n = GetWindowTextW((HWND)hwnd, buf, (int)(sizeof(buf) / sizeof(buf[0])));
    if (n <= 0) return std::string();
    return Gvtcier_W32_ToUtf8(buf);
}

//把 GetLastError 等系统错误码翻译成可读文本（UTF-8）。
//FormatMessageW 分配一块系统内存返回，拷贝成 UTF-8 后必须 LocalFree。
//系统消息末尾常带换行与空格，这里统一去掉，方便拼进自己的日志。
std::string Gvtcier_W32_LastErrorUtf8(unsigned long code) {
    wchar_t* wbuf = nullptr;
    DWORD n = FormatMessageW(FORMAT_MESSAGE_ALLOCATE_BUFFER | FORMAT_MESSAGE_FROM_SYSTEM |
                             FORMAT_MESSAGE_IGNORE_INSERTS,
                             nullptr, (DWORD)code, 0, (LPWSTR)&wbuf, 0, nullptr);
    std::string out;
    if (n > 0 && wbuf) {
        out = Gvtcier_W32_ToUtf8(wbuf);
        while (!out.empty() && (out.back() == '\r' || out.back() == '\n' || out.back() == ' '))
            out.pop_back();
    }
    if (wbuf) LocalFree(wbuf);
    return out;
}
