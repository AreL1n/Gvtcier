#include "Gvtcier.h"

//渲染上限的全局实例。渲染器与场景在分配光照缓冲前都要读它，保证分配量与着色器循环上限一致。
static RenderLimits g_limits;

//取全局渲染上限。
RenderLimits& Gvtcier_Limits() { return g_limits; }

//按光照画质档位设置各类光源上限。lightQuality 与 LightQuality 枚举对应：0 低、1 中、2 高。
//数值是"上限"而非实际用量：渲染器按上限预分配缓冲，着色器按上限截断循环，
//所以档位越高，能同时显示的光源越多，但显存占用与每像素循环次数也越高。
//三档分别允许 256/1024/4096 个动态光与常驻光，每块瓦片最多 64/512/2048 个光，
//瓦片上限远高于总数上限是为瓦片式光照的粗筛留余量。
void Gvtcier_Limits_ApplyQuality(int lightQuality) {
    g_limits.lightQuality = lightQuality;
    switch (lightQuality) {
        case 0:
            g_limits.maxDynamicLights = 256;
            g_limits.maxPersistentLights = 256;
            g_limits.maxLightsPerTile = 64;
            break;
        case 1:
            g_limits.maxDynamicLights = 1024;
            g_limits.maxPersistentLights = 1024;
            g_limits.maxLightsPerTile = 512;
            break;
        default:
            g_limits.maxDynamicLights = 4096;
            g_limits.maxPersistentLights = 4096;
            g_limits.maxLightsPerTile = 2048;
            break;
    }
}
