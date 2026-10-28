#include "Gvtcier.h"
#include <map>

// 把对象 ID（索引 + 世代号）打成一个 64 位整数当 map 的键。
// 高 32 位放 index、低 32 位放 generation，用世代号保证对象销毁后旧 ID 不会撞上新对象。
static uint64_t objKey(GvtId<Object> id) {
    return ((uint64_t)id.index << 32) | (uint64_t)id.generation;
}

// 对象透明度表：键是打包后的 ID，值是 0~1 的透明度。
// 只有透明度小于 1 的对象才存进来，全不透明的不占表项，省内存。
static std::map<uint64_t, float> g_objAlpha;
// UI 元素透明度表，键是业务侧自定义的字符串键
static std::map<std::string, float> g_uiAlpha;
// 两组透明度各自的全局倍率，用来做整体淡入淡出（比如切场景）
static float g_objMaster = 1.0f;
static float g_uiMaster = 1.0f;

// 把透明度夹到 [0,1]，越界值直接截断。
static float clamp01(float v) {
    if (v < 0.0f) return 0.0f;
    if (v > 1.0f) return 1.0f;
    return v;
}

// 设置某个对象的透明度。alpha >= 1 时视为恢复不透明，直接从表里删掉条目，
// 避免表无限增长。
void Opacity::Gvtcier_Opacity_SetObject(GvtId<Object> id, float alpha) {
    alpha = clamp01(alpha);
    if (alpha >= 1.0f) g_objAlpha.erase(objKey(id));
    else g_objAlpha[objKey(id)] = alpha;
}

// 取某个对象当前的有效透明度：查不到就当 1（默认不透明），
// 查到后再乘上全局倍率 g_objMaster 并夹紧，保证结果始终在 [0,1]。
float Opacity::Gvtcier_Opacity_Object(GvtId<Object> id) {
    std::map<uint64_t, float>::const_iterator it = g_objAlpha.find(objKey(id));
    float a = (it == g_objAlpha.end()) ? 1.0f : it->second;
    return clamp01(a * g_objMaster);
}

// 清掉某个对象的透明度记录，效果等同于恢复不透明。
void Opacity::Gvtcier_Opacity_ClearObject(GvtId<Object> id) {
    g_objAlpha.erase(objKey(id));
}

// 设置某个 UI 元素的透明度，语义与对象版本一致：>=1 就删条目。
void Opacity::Gvtcier_Opacity_SetUi(const std::string& key, float alpha) {
    alpha = clamp01(alpha);
    if (alpha >= 1.0f) g_uiAlpha.erase(key);
    else g_uiAlpha[key] = alpha;
}

// 取 UI 元素有效透明度，查不到按 1 算，再乘 UI 全局倍率。
float Opacity::Gvtcier_Opacity_Ui(const std::string& key) {
    std::map<std::string, float>::const_iterator it = g_uiAlpha.find(key);
    float a = (it == g_uiAlpha.end()) ? 1.0f : it->second;
    return clamp01(a * g_uiMaster);
}

// 清掉某个 UI 元素的透明度记录。
void Opacity::Gvtcier_Opacity_ClearUi(const std::string& key) {
    g_uiAlpha.erase(key);
}

// 设置所有 3D 对象的总透明度倍率，用于整体淡出。
void Opacity::Gvtcier_Opacity_SetObjectMaster(float alpha) {
    g_objMaster = clamp01(alpha);
}

// 读当前 3D 对象总倍率。
float Opacity::Gvtcier_Opacity_ObjectMaster() {
    return g_objMaster;
}

// 设置所有 UI 的总透明度倍率。
void Opacity::Gvtcier_Opacity_SetUiMaster(float alpha) {
    g_uiMaster = clamp01(alpha);
}

// 读当前 UI 总倍率。
float Opacity::Gvtcier_Opacity_UiMaster() {
    return g_uiMaster;
}

// 一次性把所有透明度状态恢复成初始值，通常在场景切换或热重载时调用。
void Opacity::Gvtcier_Opacity_Reset() {
    g_objAlpha.clear();
    g_uiAlpha.clear();
    g_objMaster = 1.0f;
    g_uiMaster = 1.0f;
}
