#include "Gvtcier.h"
#define STB_TRUETYPE_IMPLEMENTATION
#include "stb/stb_truetype.h"
#include <cstdio>
#include <cstring>
#include <vector>
#include <unordered_map>

using namespace rhi;

// 取某一页的纹理。页号越界返回 0，调用方当作没纹理处理。
unsigned int FontAtlas::textureAt(int page) const {
    if (page < 0 || page >= (int)pages_.size()) return 0;
    return pages_[(size_t)page].tex;
}

// 新建一页图集并设为当前写入页。图集满了不会丢字形，而是继续开新页。
bool FontAtlas::addPage() {
    Page p;
    rhi::Gvtcier_Rhi_GenTextures(1, &p.tex);
    if (!p.tex) return false;
    rhi::Gvtcier_Rhi_BindTexture(Tex2D, p.tex);
    rhi::Gvtcier_Rhi_TexImage2D(Tex2D, 0, R8, size_, size_, Red, UnsignedByte, NULL);
    rhi::Gvtcier_Rhi_TexParameteri(Tex2D, TexMinFilter, Linear);
    rhi::Gvtcier_Rhi_TexParameteri(Tex2D, TexMagFilter, Linear);
    rhi::Gvtcier_Rhi_TexParameteri(Tex2D, TexWrapS, ClampToEdge);
    rhi::Gvtcier_Rhi_TexParameteri(Tex2D, TexWrapT, ClampToEdge);
    p.pixels = new unsigned char[(size_t)size_ * size_]();
    pages_.push_back(p);
    return true;
}

// 每行字形数据的字节数按 4 字节对齐。GL 的 TexSubImage2D 对 R8 纹理虽然有对齐设置，
// 但这里统一按 4 对齐上传，避免奇数字宽时解包出错。
static inline int align4(int v) { return (v + 3) & ~3; }

// 打包字体数据只读一份进内存，所有平台都用 stb_truetype 自己栅格化。
// 这样不依赖系统字体：机器上装没装思源、装的是哪个版本都不影响，
// 而且 CFF 轮廓的 OTF 也能正常解析（GDI 的 CreateFont 系列做不到这点）。
static std::vector<unsigned char> g_ttf;
static stbtt_fontinfo g_info;
static bool g_infoOk = false;

// 释放字体数据。注意这里只是清掉内存，g_info 里的指针会随之失效。
void releaseSceneFont() {
    g_ttf.clear();
    g_ttf.shrink_to_fit();
    g_infoOk = false;
}

// 从 UTF-8 串里取出一个码点，并把指针推进到下一个字符。
// 支持 1~4 字节的编码；遇到非法起始字节或缺失续字节时返回 U+FFFD 并只吞掉最少字节，
// 这样坏字符不会把后面的中文全部带歪。
unsigned int gvUtf8Next(const char*& s) {
    unsigned char c = (unsigned char)*s;
    if (c < 0x80) { s += 1; return c; }
    unsigned int cp = 0;
    int n = 0;
    if ((c & 0xE0) == 0xC0) { cp = c & 0x1F; n = 1; }
    else if ((c & 0xF0) == 0xE0) { cp = c & 0x0F; n = 2; }
    else if ((c & 0xF8) == 0xF0) { cp = c & 0x07; n = 3; }
    else { s += 1; return 0xFFFD; }
    ++s;
    for (int i = 0; i < n; ++i) {
        unsigned char cc = (unsigned char)*s;
        if ((cc & 0xC0) != 0x80) return 0xFFFD;
        cp = (cp << 6) | (cc & 0x3F);
        ++s;
    }
    return cp ? cp : 0xFFFD;
}

// 初始化字形图集。字体数据只从打包资源加载一次；
// ascent 由字体垂直度量乘像素高度比例得到，用于基线对齐。
bool FontAtlas::init(int px, int atlasSize) {
    destroy();
    size_ = atlasSize;
    px_ = px;
    if (!g_infoOk) {
        static const char* kKeys[2] = { "FONT_REGULAR", "FONT_BOLD" };
        for (int i = 0; i < 2 && g_ttf.empty(); ++i) {
            int sz = 0;
            const void* data = res_get(kKeys[i], &sz);
            if (data && sz > 0) g_ttf.assign((const unsigned char*)data, (const unsigned char*)data + sz);
        }
        if (!g_ttf.empty()) {
            int off = stbtt_GetFontOffsetForIndex(g_ttf.data(), 0);
            if (off >= 0) g_infoOk = stbtt_InitFont(&g_info, g_ttf.data(), off);
        }
        if (!g_infoOk) printf("[font] 打包字体加载失败，文本将不可见\n");
    }
    if (!g_infoOk) return false;
    int ascent = 0, descent = 0, lineGap = 0;
    stbtt_GetFontVMetrics(&g_info, &ascent, &descent, &lineGap);
    asc_ = (int)(ascent * stbtt_ScaleForPixelHeight(&g_info, (float)px));

    if (!addPage()) { destroy(); return false; }
    glyphs_ = new std::unordered_map<unsigned int, FontGlyph>();
    return true;
}

// 释放图集资源。glyphs_ 是 void* 存的 unordered_map，删除时要做一次类型转换。
void FontAtlas::destroy() {
    for (size_t i = 0; i < pages_.size(); ++i) {
        Page& p = pages_[i];
        delete[] p.pixels;
        if (p.tex) rhi::Gvtcier_Rhi_DeleteTextures(1, &p.tex);
    }
    pages_.clear();
    delete (std::unordered_map<unsigned int, FontGlyph>*)glyphs_;
    glyphs_ = nullptr;
}

// 取某个码点的字形信息。缓存未命中时才真正栅格化：
//   1) 查哈希表，命中直接返回；
//   2) stbtt 直接生成 8 位抗锯齿位图；
//   3) 按"行优先、行宽 4 对齐"塞进图集，当前页放不下就换新页；
//   4) TexSubImage2D 只上传这一小块，而不是每帧重传整张图集；
//   5) uv 取像素中心偏移 0.5，避免线性过滤采到相邻字形的边缘。
const FontGlyph* FontAtlas::glyph(unsigned int cp) {
    if (!glyphs_ || pages_.empty()) return nullptr;
    auto* map = (std::unordered_map<unsigned int, FontGlyph>*)glyphs_;
    auto it = map->find(cp);
    if (it != map->end()) return &it->second;

    float scale = stbtt_ScaleForPixelHeight(&g_info, (float)px_);
    FontGlyph g;
    g.u0 = g.v0 = g.u1 = g.v1 = 0.0f;
    g.advance = 0.0f;
    g.ox = g.oy = 0;
    g.w = g.h = 0;
    g.page = 0;
    int adv = 0, lsb = 0;
    int x0 = 0, y0 = 0, x1 = 0, y1 = 0;
    int gi = stbtt_FindGlyphIndex(&g_info, (int)cp);
    stbtt_GetGlyphHMetrics(&g_info, gi, &adv, &lsb);
    stbtt_GetGlyphBitmapBox(&g_info, gi, scale, scale, &x0, &y0, &x1, &y1);
    g.w = x1 - x0;
    g.h = y1 - y0;
    g.ox = x0;
    g.oy = -y0;
    g.advance = adv * scale;
    if (gi > 0 && g.w > 0 && g.h > 0 && g.w + 2 <= size_ && g.h + 1 <= size_) {
        Page& pg = pages_.back();
        if (pg.cx + g.w + 1 > size_) { pg.cy += pg.rowH + 1; pg.cx = 0; pg.rowH = 0; }
        if (pg.cy + g.h > size_) {
            if (!addPage()) {
                (*map)[cp] = g;
                return &(*map)[cp];
            }
        }
        Page& cur = pages_.back();
        g.page = (int)pages_.size() - 1;
        std::vector<unsigned char> raw((size_t)g.w * g.h, 0);
        stbtt_MakeGlyphBitmap(&g_info, raw.data(), g.w, g.h, g.w, scale, scale, gi);
        int pack4 = align4(g.w);
        std::vector<unsigned char> compact((size_t)pack4 * g.h, 0);
        for (int y = 0; y < g.h; ++y)
            for (int x = 0; x < g.w; ++x)
                compact[(size_t)y * pack4 + x] = raw[(size_t)y * g.w + x];
        for (int y = 0; y < g.h; ++y)
            for (int x = 0; x < g.w; ++x)
                cur.pixels[(size_t)(cur.cy + y) * size_ + cur.cx + x] = raw[(size_t)y * g.w + x];
        rhi::Gvtcier_Rhi_BindTexture(Tex2D, cur.tex);
        rhi::Gvtcier_Rhi_TexSubImage2D(Tex2D, 0, cur.cx, cur.cy, g.w, g.h, Red, UnsignedByte,
                                       compact.data());
        g.u0 = ((float)cur.cx + 0.5f) / size_;
        g.v0 = ((float)cur.cy + 0.5f) / size_;
        g.u1 = ((float)(cur.cx + g.w) - 0.5f) / size_;
        g.v1 = ((float)(cur.cy + g.h) - 0.5f) / size_;
        cur.cx += g.w + 2;
        if (g.h > cur.rowH) cur.rowH = g.h;
    }
    (*map)[cp] = g;
    return &(*map)[cp];
}
