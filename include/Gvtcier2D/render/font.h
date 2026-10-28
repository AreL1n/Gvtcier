#pragma once
#include <vector>
#include "Rhidriver/rhi.h"

// 一个字形的度量信息。
// page 是该字形所在图集的页号，u0/v0/u1/v1 是那一页里的 uv 包围盒；
// advance 是前进宽度（已换算到 kBasePx 尺度）；ox/oy 是字形原点相对基线的偏移，
// w/h 是位图尺寸（位图为空时 uv 全 0）。
struct FontGlyph {
    float u0, v0, u1, v1;
    float advance;
    int ox, oy;
    int w, h;
    int page;
};

// 从 UTF-8 串取出一个码点并推进指针，非法编码返回 U+FFFD。
GVTCIER_API unsigned int gvUtf8Next(const char*& s);
// 注销内存里注册的字体资源（Windows 下对应 RemoveFontMemResourceEx）。
GVTCIER_API void releaseSceneFont();

// 字形图集：把用到的字按固定高分辨率栅格化成 8 位灰度图，拼进单通道纹理里，
// 文本渲染时只靠 uv 取字形，不用为每个字号各建一张图。
// 一页装满后自动开新页，字形永远不会因为放不下而丢失。
class FontAtlas {
public:
    // 栅格化字号。对外接口里 scale=1 对应的就是这个像素高，
    // 字形始终按这个尺寸栅格，绘制时缩小而不是放大，边缘才干净。
    static const int kBasePx = 96;
    // 创建第一页图集。px 为栅格化字号像素高，atlasSize 为每页边长（2 的幂最佳）。
    GVTCIER_API bool init(int px, int atlasSize = 1024);
    GVTCIER_API void destroy();
    // 取码点对应的字形，未缓存时现场栅格化并上传。
    GVTCIER_API const FontGlyph* glyph(unsigned int cp);
    // 图集总页数，绘制时按字形的 page 切换纹理。
    GVTCIER_API int pageCount() const { return (int)pages_.size(); }
    GVTCIER_API unsigned int textureAt(int page) const;
    GVTCIER_API int px() const { return px_; }
    // 基线到字形顶部的距离，排版时把文本 y 加上它就能对齐到期望的视觉顶部。
    GVTCIER_API int ascent() const { return asc_; }

private:
    struct Page {
        unsigned int tex = 0;
        unsigned char* pixels = nullptr;
        int cx = 0, cy = 0, rowH = 0;
    };
    // 开一页新图集并设为当前写入页。
    bool addPage();

    void* dc_ = nullptr;              // Windows：GDI 内存 DC（其他平台不用）
    void* font_ = nullptr;            // Windows：GDI 字体句柄
    std::vector<Page> pages_;         // 按需增长的图集页
    int size_ = 1024;                 // 每页边长
    int px_ = 32;                     // 栅格化字号像素高
    int asc_ = 0;                     // 基线偏移
    void* glyphs_ = nullptr;          // 实际是 std::unordered_map<unsigned int, FontGlyph>*
};
