#include "Gvtcier.h"
#include <vector>
#include <cstring>

using rhi::Tex2D;
using rhi::Tex2DArray;
using rhi::Rgba8;
using rhi::Rgba;
using rhi::UnsignedByte;
using rhi::TexMinFilter;
using rhi::TexMagFilter;
using rhi::TexWrapS;
using rhi::TexWrapT;
using rhi::TexMaxAniso;
using rhi::MaxTexAniso;
using rhi::Linear;
using rhi::LinearMipmapLinear;
using rhi::Repeat;
using rhi::Texture0;

// 创建一张普通 2D 纹理（RGBA8），像素数据由 rgba 提供，可为空指针表示先分配不填。
// 细节：
//   - 先 destroy 旧纹理，所以重复调用是安全的，不会泄漏
//   - 上传时临时绑到 15 号纹理单元，避开 0~14 的常用槽位，减少对渲染状态的影响
//   - 自动生成完整 mipmap，过滤用三线性插值，寻址用 Repeat 平铺
//   - 各向异性等级取引擎设置与硬件上限的较小值，最低 1（即关闭）
// 返回值：固定返回 true，创建是“尽力而为”，失败与否由 RHI 层自行处理。
bool Texture::create(int w, int h, const unsigned char* rgba) {
    destroy();
    w_ = w; h_ = h;
    rhi::Gvtcier_Rhi_GenTextures(1, &id_);
    rhi::Gvtcier_Rhi_BindTextureUnit(15, Tex2D, id_);
    rhi::Gvtcier_Rhi_TexImage2D(Tex2D, 0, Rgba8, w, h, Rgba, UnsignedByte, rgba);
    rhi::Gvtcier_Rhi_GenerateMipmap(Tex2D);
    rhi::Gvtcier_Rhi_TexParameteri(Tex2D, TexMinFilter, LinearMipmapLinear);
    rhi::Gvtcier_Rhi_TexParameteri(Tex2D, TexMagFilter, Linear);
    rhi::Gvtcier_Rhi_TexParameteri(Tex2D, TexWrapS, Repeat);
    rhi::Gvtcier_Rhi_TexParameteri(Tex2D, TexWrapT, Repeat);
    int maxAniso = 0;
    rhi::Gvtcier_Rhi_GetIntegerv(MaxTexAniso, &maxAniso);
    int want = rhi::Gvtcier_Rhi_Anisotropy();
    if (want > maxAniso) want = maxAniso;
    if (want < 1) want = 1;
    rhi::Gvtcier_Rhi_TexParameterf(Tex2D, TexMaxAniso, (float)want);
    return true;
}

// 释放 GL 纹理对象并把句柄清零，重复调用无副作用。
void Texture::destroy() {
    if (id_) { rhi::Gvtcier_Rhi_DeleteTextures(1, &id_); id_ = 0; }
}

// 创建一张 2D 纹理数组（RGBA8），常用于面部表情或多帧序列图。
// layers 是层数，imgs 是每层的像素指针（每层 w*h*4 字节）。
// 各层数据先连续拷进一个打包缓冲再一次性上传，因为 GL 的 TexImage3D 期望连续内存。
// 过滤与寻址策略和 2D 版本一致：三线性 + Repeat + 各向异性。
bool Texture::createArray(int w, int h, int layers, const unsigned char* const* imgs) {
    destroy();
    w_ = w; h_ = h; layers_ = layers;
    std::vector<unsigned char> packed((size_t)w * h * layers * 4);
    for (int i = 0; i < layers; ++i)
        memcpy(packed.data() + (size_t)w * h * 4 * i, imgs[i], (size_t)w * h * 4);
    rhi::Gvtcier_Rhi_GenTextures(1, &id_);
    rhi::Gvtcier_Rhi_BindTextureUnit(15, Tex2DArray, id_);
    rhi::Gvtcier_Rhi_TexImage3D(Tex2DArray, 0, Rgba8, w, h, layers, Rgba, UnsignedByte, packed.data());
    rhi::Gvtcier_Rhi_GenerateMipmap(Tex2DArray);
    rhi::Gvtcier_Rhi_TexParameteri(Tex2DArray, TexMinFilter, LinearMipmapLinear);
    rhi::Gvtcier_Rhi_TexParameteri(Tex2DArray, TexMagFilter, Linear);
    rhi::Gvtcier_Rhi_TexParameteri(Tex2DArray, TexWrapS, Repeat);
    rhi::Gvtcier_Rhi_TexParameteri(Tex2DArray, TexWrapT, Repeat);
    int maxAniso = 0;
    rhi::Gvtcier_Rhi_GetIntegerv(MaxTexAniso, &maxAniso);
    int want = rhi::Gvtcier_Rhi_Anisotropy();
    if (want > maxAniso) want = maxAniso;
    if (want < 1) want = 1;
    rhi::Gvtcier_Rhi_TexParameterf(Tex2DArray, TexMaxAniso, (float)want);
    return true;
}

// 把这张 2D 纹理绑到第 unit 个采样槽（rhi 的槽位从 TextureBase 起算）。
void Texture::bind(int unit) const {
    rhi::Gvtcier_Rhi_BindTextureUnit(rhi::TextureBase + unit, Tex2D, id_);
}

// 往像素缓冲里写下 (x,y) 处的一个像素。
// 输入颜色是 0~1 的浮点，直接乘 255 转成字节；Alpha 固定写成 255（不透明），
// 所以这个函数只能用来生成纯色不透明纹理。
static void fillPixel(std::vector<unsigned char>& data, int w, int x, int y, Vec3 c) {
    unsigned char* p = &data[((size_t)y * w + x) * 4];
    p[0] = (unsigned char)(c.x * 255.0f);
    p[1] = (unsigned char)(c.y * 255.0f);
    p[2] = (unsigned char)(c.z * 255.0f);
    p[3] = 255;
}

// 生成一张纯色纹理，多用于“没有贴图时”的白色/灰色兜底贴图。
// w、h 是纹理分辨率，c 是 0~1 的 RGB 颜色。
Texture makeSolidTexture(int w, int h, Vec3 c) {
    std::vector<unsigned char> data((size_t)w * h * 4);
    for (int y = 0; y < h; ++y)
        for (int x = 0; x < w; ++x)
            fillPixel(data, w, x, y, c);
    Texture t;
    t.create(w, h, data.data());
    return t;
}
