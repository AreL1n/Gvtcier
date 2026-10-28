#include "Gvtcier.h"
#include <cstdio>
#include <cstring>
#include <deque>

// Vulkan 资源对象管理：缓冲、纹理、顶点数组封装、帧缓冲。
// 属于已封禁的 VK 后端（原因见 Vulkan后端.cpp 顶部说明）。
// 资源用 deque 持有，好处是地址稳定（扩容时不会搬家），上层拿到的句柄不会失效。
// 注意：这里的缓冲复用策略正是当初内存泄漏的重点怀疑对象，重新启用前需要先查清。
namespace rhi {
namespace vk {

namespace {

std::deque<VkBufferObj> g_buffers;    // 全部缓冲对象（顶点/索引/Uniform/SSBO 等）
std::deque<VkTextureObj> g_textures;  // 全部纹理对象
std::deque<VkVaoObj> g_vaos;          // 顶点数组封装，绑定了缓冲与属性布局
std::deque<VkFboObj> g_fbos;          // 帧缓冲对象

unsigned g_boundBuffer[3] = {};
unsigned g_boundVao = 0;

VkFormat toVkFormat(unsigned fmt) {
    switch (fmt) {
        case 230: return VK_FORMAT_R8_UNORM;
        case 231: return VK_FORMAT_R16_UNORM;
        case 232: return VK_FORMAT_R16_SFLOAT;
        case 233: return VK_FORMAT_R32_SFLOAT;
        case 234: return VK_FORMAT_R8G8_UNORM;
        case 235: return VK_FORMAT_R16G16_SFLOAT;
        case 236: return VK_FORMAT_R32G32_SFLOAT;
        case 237: return VK_FORMAT_R8G8B8_UNORM;
        case 238: return VK_FORMAT_R16G16B16_SFLOAT;
        case 239: return VK_FORMAT_R8G8B8A8_UNORM;
        case 240: return VK_FORMAT_R16G16B16A16_UNORM;
        case 241: return VK_FORMAT_R16G16B16A16_SFLOAT;
        case 242: return VK_FORMAT_R32G32B32A32_SFLOAT;
        case 243: return VK_FORMAT_R8G8B8A8_SRGB;
        case 248: return VK_FORMAT_D24_UNORM_S8_UINT;
        case 244: return VK_FORMAT_D32_SFLOAT;
        case 245: return VK_FORMAT_D16_UNORM;
        case 246: return VK_FORMAT_D24_UNORM_S8_UINT;
        case 247: return VK_FORMAT_D32_SFLOAT;
        case 249: return VK_FORMAT_B10G11R11_UFLOAT_PACK32;
        default: return VK_FORMAT_R8G8B8A8_UNORM;
    }
}

VkFormat toVkFormatFromPixel(unsigned fmt) {
    switch (fmt) {
        case 210: return VK_FORMAT_R8_UNORM;
        case 211: return VK_FORMAT_R8G8_UNORM;
        case 212: return VK_FORMAT_R8G8B8_UNORM;
        case 213: return VK_FORMAT_B8G8R8A8_UNORM;
        case 214: return VK_FORMAT_R8G8B8A8_UNORM;
        case 219: return VK_FORMAT_D24_UNORM_S8_UINT;
        default: return VK_FORMAT_R8G8B8A8_UNORM;
    }
}

bool isDepthFormat(VkFormat f) {
    return f == VK_FORMAT_D32_SFLOAT || f == VK_FORMAT_D16_UNORM ||
           f == VK_FORMAT_D24_UNORM_S8_UINT || f == VK_FORMAT_D32_SFLOAT_S8_UINT ||
           f == VK_FORMAT_X8_D24_UNORM_PACK32;
}

bool hasStencil(VkFormat f) {
    return f == VK_FORMAT_D24_UNORM_S8_UINT || f == VK_FORMAT_D32_SFLOAT_S8_UINT;
}

bool createImageForImpl(VkTextureObj& t, int w, int h, int d, VkFormat fmt, VkImageUsageFlags usage) {
    if (t.image && t.view && t.width == w && t.height == h && t.format == fmt &&
        t.depth == (t.target == 252 ? d : 1)) {
        return true;
    }
    if (t.view) { vkDestroyImageView(ctx().device, t.view, nullptr); t.view = VK_NULL_HANDLE; }
    if (t.image) { vkDestroyImage(ctx().device, t.image, nullptr); t.image = VK_NULL_HANDLE; }
    if (t.memory) { vkFreeMemory(ctx().device, t.memory, nullptr); t.memory = VK_NULL_HANDLE; }
    VkImageCreateInfo ci{};
    ci.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
    if (t.target == 251 || t.target == 259) {
        ci.imageType = VK_IMAGE_TYPE_2D;
        ci.arrayLayers = (uint32_t)(t.layers > 1 ? t.layers : 1);
        if (t.target == 259) ci.flags |= VK_IMAGE_CREATE_CUBE_COMPATIBLE_BIT;
    } else if (t.target == 252) {
        ci.imageType = VK_IMAGE_TYPE_3D;
    } else {
        ci.imageType = VK_IMAGE_TYPE_2D;
    }
    ci.format = fmt;
    ci.extent.width = (uint32_t)w;
    ci.extent.height = (uint32_t)h;
    ci.extent.depth = (uint32_t)(ci.imageType == VK_IMAGE_TYPE_3D ? d : 1);
    ci.mipLevels = 1;
    ci.arrayLayers = ci.arrayLayers ? ci.arrayLayers : 1;
    ci.samples = VK_SAMPLE_COUNT_1_BIT;
    ci.tiling = VK_IMAGE_TILING_OPTIMAL;
    ci.usage = usage;
    ci.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
    ci.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;

    if (vkCreateImage(ctx().device, &ci, nullptr, &t.image) != VK_SUCCESS) return false;

    VkMemoryRequirements req{};
    vkGetImageMemoryRequirements(ctx().device, t.image, &req);
    VkMemoryAllocateInfo ai{};
    ai.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
    ai.allocationSize = req.size;
    ai.memoryTypeIndex = findMemoryType(req.memoryTypeBits, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
    if (vkAllocateMemory(ctx().device, &ai, nullptr, &t.memory) != VK_SUCCESS) return false;
    vkBindImageMemory(ctx().device, t.image, t.memory, 0);

    bool stencil = hasStencil(fmt);
    bool depth = isDepthFormat(fmt);
    VkImageViewCreateInfo vi{};
    vi.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
    vi.image = t.image;
    vi.format = fmt;
    vi.subresourceRange.aspectMask = depth ? VK_IMAGE_ASPECT_DEPTH_BIT : VK_IMAGE_ASPECT_COLOR_BIT;
    vi.subresourceRange.levelCount = 1;
    vi.subresourceRange.layerCount = ci.arrayLayers;
    if (t.target == 251) vi.viewType = VK_IMAGE_VIEW_TYPE_2D_ARRAY;
    else if (t.target == 252) vi.viewType = VK_IMAGE_VIEW_TYPE_3D;
    else if (t.target == 259) vi.viewType = VK_IMAGE_VIEW_TYPE_CUBE;
    else vi.viewType = VK_IMAGE_VIEW_TYPE_2D;
    vkCreateImageView(ctx().device, &vi, nullptr, &t.view);

    if (!t.sampler) {
        VkSamplerCreateInfo si{};
        si.sType = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO;
        si.magFilter = VK_FILTER_LINEAR;
        si.minFilter = VK_FILTER_LINEAR;
        si.mipmapMode = VK_SAMPLER_MIPMAP_MODE_LINEAR;
        si.addressModeU = VK_SAMPLER_ADDRESS_MODE_REPEAT;
        si.addressModeV = VK_SAMPLER_ADDRESS_MODE_REPEAT;
        si.addressModeW = VK_SAMPLER_ADDRESS_MODE_REPEAT;
        si.maxLod = 1.0f;
        vkCreateSampler(ctx().device, &si, nullptr, &t.sampler);
    }

    t.width = w;
    t.height = h;
    t.depth = d;
    t.format = fmt;
    t.aspect = vi.subresourceRange.aspectMask;
    if (stencil) t.aspect |= VK_IMAGE_ASPECT_STENCIL_BIT;
    t.layout = VK_IMAGE_LAYOUT_UNDEFINED;

    if (depth) {
        cmdTransitionImage(VK_NULL_HANDLE, t.image, t.aspect, ci.arrayLayers,
                           VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_GENERAL);
    } else {
        VkCommandBuffer tmp = VK_NULL_HANDLE;
        VkCommandBufferAllocateInfo ai2{};
        ai2.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
        ai2.commandPool = ctx().commandPool;
        ai2.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
        ai2.commandBufferCount = 1;
        if (vkAllocateCommandBuffers(ctx().device, &ai2, &tmp) == VK_SUCCESS) {
            VkCommandBufferBeginInfo bi2{};
            bi2.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
            bi2.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
            vkBeginCommandBuffer(tmp, &bi2);
            cmdTransitionImage(tmp, t.image, t.aspect, ci.arrayLayers,
                               VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_GENERAL);
            VkClearColorValue cv{};
            VkImageSubresourceRange range{};
            range.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
            range.baseMipLevel = 0;
            range.levelCount = 1;
            range.baseArrayLayer = 0;
            range.layerCount = ci.arrayLayers;
            vkCmdClearColorImage(tmp, t.image, VK_IMAGE_LAYOUT_GENERAL, &cv, 1, &range);
            vkEndCommandBuffer(tmp);
            VkSubmitInfo si{};
            si.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
            si.commandBufferCount = 1;
            si.pCommandBuffers = &tmp;
            vkQueueSubmit(ctx().graphicsQueue, 1, &si, VK_NULL_HANDLE);
            vkQueueWaitIdle(ctx().graphicsQueue);
            vkFreeCommandBuffers(ctx().device, ctx().commandPool, 1, &tmp);
        }
    }
    t.layout = VK_IMAGE_LAYOUT_GENERAL;
    return true;
}

}

uint32_t findMemoryType(uint32_t typeBits, VkMemoryPropertyFlags props) {
    VkPhysicalDeviceMemoryProperties mp{};
    vkGetPhysicalDeviceMemoryProperties(ctx().physical, &mp);
    for (uint32_t i = 0; i < mp.memoryTypeCount; ++i) {
        if ((typeBits & (1u << i)) && (mp.memoryTypes[i].propertyFlags & props) == props)
            return i;
    }
    return 0;
}

void resourceInit() {
    g_buffers.clear();
    g_textures.clear();
    g_vaos.clear();
    g_fbos.clear();
    memset(g_boundBuffer, 0, sizeof(g_boundBuffer));
    g_boundVao = 0;
}

void resourceShutdown() {
    for (size_t i = 0; i < g_buffers.size(); ++i) {
        if (g_buffers[i].buffer) vkDestroyBuffer(ctx().device, g_buffers[i].buffer, nullptr);
        if (g_buffers[i].memory) vkFreeMemory(ctx().device, g_buffers[i].memory, nullptr);
    }
    for (size_t i = 0; i < g_textures.size(); ++i) {
        if (g_textures[i].sampler) vkDestroySampler(ctx().device, g_textures[i].sampler, nullptr);
        if (g_textures[i].view) vkDestroyImageView(ctx().device, g_textures[i].view, nullptr);
        if (g_textures[i].image) vkDestroyImage(ctx().device, g_textures[i].image, nullptr);
        if (g_textures[i].memory) vkFreeMemory(ctx().device, g_textures[i].memory, nullptr);
    }
    for (size_t i = 0; i < g_fbos.size(); ++i) {
        if (g_fbos[i].framebuffer) vkDestroyFramebuffer(ctx().device, g_fbos[i].framebuffer, nullptr);
        if (g_fbos[i].renderPass) vkDestroyRenderPass(ctx().device, g_fbos[i].renderPass, nullptr);
    }
    g_buffers.clear();
    g_textures.clear();
    g_vaos.clear();
    g_fbos.clear();
}

VkBufferObj* bufferAt(unsigned id) {
    if (id == 0 || id > g_buffers.size()) return nullptr;
    return &g_buffers[id - 1];
}

unsigned createBuffer() {
    g_buffers.push_back(VkBufferObj{});
    return (unsigned)g_buffers.size();
}

void resetBufferOffsets() {
    for (size_t i = 0; i < g_buffers.size(); ++i) g_buffers[i].writeOfs = 0;
}

unsigned createTexture() {
    g_textures.push_back(VkTextureObj{});
    return (unsigned)g_textures.size();
}

unsigned createVao() {
    g_vaos.push_back(VkVaoObj{});
    return (unsigned)g_vaos.size();
}

unsigned createFbo() {
    g_fbos.push_back(VkFboObj{});
    return (unsigned)g_fbos.size();
}

unsigned createRbo() {
    g_textures.push_back(VkTextureObj{});
    g_textures.back().isRenderTarget = true;
    return (unsigned)g_textures.size();
}

void destroyBuffer(unsigned id) {
    VkBufferObj* b = bufferAt(id);
    if (!b) return;
    for (int i = 0; i < 4; ++i) {
        if (b->slots[i].buffer) vkDestroyBuffer(ctx().device, b->slots[i].buffer, nullptr);
        if (b->slots[i].memory) vkFreeMemory(ctx().device, b->slots[i].memory, nullptr);
    }
    *b = VkBufferObj{};
}

void destroyTexture(unsigned id) {
    if (id == 0 || id > g_textures.size()) return;
    VkTextureObj& t = g_textures[id - 1];
    if (t.view) vkDestroyImageView(ctx().device, t.view, nullptr);
    if (t.sampler) vkDestroySampler(ctx().device, t.sampler, nullptr);
    if (t.image) vkDestroyImage(ctx().device, t.image, nullptr);
    if (t.memory) vkFreeMemory(ctx().device, t.memory, nullptr);
    t = VkTextureObj{};
}

void destroyVao(unsigned id) {
    if (id == 0 || id > g_vaos.size()) return;
    g_vaos[id - 1] = VkVaoObj{};
}

void destroyFbo(unsigned id) {
    if (id == 0 || id > g_fbos.size()) return;
    VkFboObj& f = g_fbos[id - 1];
    if (f.framebuffer) vkDestroyFramebuffer(ctx().device, f.framebuffer, nullptr);
    if (f.renderPass) vkDestroyRenderPass(ctx().device, f.renderPass, nullptr);
    f = VkFboObj{};
}

void bindBuffer(unsigned target, unsigned id) {
    if (target == 160) g_boundBuffer[0] = id;
    else if (target == 161) {
        g_boundBuffer[1] = id;
        VkVaoObj* v = vaoAt(g_boundVao);
        if (v) v->ebo = id;
    }
    else g_boundBuffer[2] = id;
}

unsigned currentBuffer(unsigned target) {
    if (target == 160) return g_boundBuffer[0];
    if (target == 161) return g_boundBuffer[1];
    return g_boundBuffer[2];
}

void bindVao(unsigned id) {
    g_boundVao = id;
    VkVaoObj* v = vaoAt(id);
    if (v) g_boundBuffer[1] = v->ebo;
}

VkTextureObj* textureAt(unsigned id) {
    if (id == 0 || id > g_textures.size()) return nullptr;
    return &g_textures[id - 1];
}

bool createImageFor(VkTextureObj* t, int w, int h, int d, int fmt) {
    if (!t) return false;
    VkFormat vf = toVkFormat((unsigned)fmt);
    t->hasDepth = isDepthFormat(vf);
    t->isRenderTarget = true;
    VkImageUsageFlags usage = VK_IMAGE_USAGE_SAMPLED_BIT |
                              VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT;
    if (t->hasDepth) usage |= VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT;
    else usage |= VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT;
    return createImageForImpl(*t, w, h, d, vf, usage);
}

void cmdTransitionImage(VkCommandBuffer cmd, VkImage image, VkImageAspectFlags aspect,
                        uint32_t layers, VkImageLayout from, VkImageLayout to) {
    if (from == to) return;
    VkImageMemoryBarrier b{};
    b.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
    b.oldLayout = from;
    b.newLayout = to;
    b.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    b.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    b.image = image;
    b.subresourceRange.aspectMask = aspect;
    b.subresourceRange.levelCount = 1;
    b.subresourceRange.layerCount = layers ? layers : 1;

    VkPipelineStageFlags srcStage = VK_PIPELINE_STAGE_ALL_COMMANDS_BIT;
    VkPipelineStageFlags dstStage = VK_PIPELINE_STAGE_ALL_COMMANDS_BIT;
    if (from == VK_IMAGE_LAYOUT_UNDEFINED) {
        b.srcAccessMask = 0;
        srcStage = VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT;
    } else if (from == VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL) {
        b.srcAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
        srcStage = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
    } else if (from == VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL) {
        b.srcAccessMask = VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT;
        srcStage = VK_PIPELINE_STAGE_LATE_FRAGMENT_TESTS_BIT;
    } else {
        b.srcAccessMask = VK_ACCESS_MEMORY_READ_BIT | VK_ACCESS_MEMORY_WRITE_BIT |
                          VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_SHADER_WRITE_BIT;
    }

    if (to == VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL) {
        b.dstAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT | VK_ACCESS_COLOR_ATTACHMENT_READ_BIT;
        dstStage = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
    } else if (to == VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL) {
        b.dstAccessMask = VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT | VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_READ_BIT;
        dstStage = VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT | VK_PIPELINE_STAGE_LATE_FRAGMENT_TESTS_BIT;
    } else if (to == VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL) {
        b.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
        dstStage = VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT | VK_PIPELINE_STAGE_VERTEX_SHADER_BIT |
                   VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT;
    } else if (to == VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL) {
        b.dstAccessMask = VK_ACCESS_TRANSFER_READ_BIT;
        dstStage = VK_PIPELINE_STAGE_TRANSFER_BIT;
    } else if (to == VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL) {
        b.dstAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
        dstStage = VK_PIPELINE_STAGE_TRANSFER_BIT;
    } else {
        b.dstAccessMask = VK_ACCESS_MEMORY_READ_BIT | VK_ACCESS_MEMORY_WRITE_BIT |
                          VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_SHADER_WRITE_BIT |
                          VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT | VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT;
    }

    if (cmd != VK_NULL_HANDLE) {
        vkCmdPipelineBarrier(cmd, srcStage, dstStage, 0, 0, nullptr, 0, nullptr, 1, &b);
        return;
    }
    VkCommandBuffer tmp = VK_NULL_HANDLE;
    VkCommandBufferAllocateInfo ai{};
    ai.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
    ai.commandPool = ctx().commandPool;
    ai.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
    ai.commandBufferCount = 1;
    if (vkAllocateCommandBuffers(ctx().device, &ai, &tmp) != VK_SUCCESS) return;
    VkCommandBufferBeginInfo bi{};
    bi.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
    bi.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
    vkBeginCommandBuffer(tmp, &bi);
    vkCmdPipelineBarrier(tmp, srcStage, dstStage, 0, 0, nullptr, 0, nullptr, 1, &b);
    vkEndCommandBuffer(tmp);
    VkSubmitInfo si{};
    si.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
    si.commandBufferCount = 1;
    si.pCommandBuffers = &tmp;
    vkQueueSubmit(ctx().graphicsQueue, 1, &si, VK_NULL_HANDLE);
    vkQueueWaitIdle(ctx().graphicsQueue);
    vkFreeCommandBuffers(ctx().device, ctx().commandPool, 1, &tmp);
}

void ensureImageLayout(VkTextureObj* t, VkImageLayout want) {
    if (!t || !t->image) return;
    if (t->layout == want) return;
    VkCommandBuffer cmd = ctx().cmd.empty() ? VK_NULL_HANDLE : ctx().cmd[ctx().frameIndex];
    if (ctx().frameActive) {
        cmdTransitionImage(cmd, t->image, t->aspect, (uint32_t)t->layers, t->layout, want);
    } else {
        cmdTransitionImage(VK_NULL_HANDLE, t->image, t->aspect, (uint32_t)t->layers, t->layout, want);
    }
    t->layout = want;
}

static uint32_t texelBytes(VkFormat f) {
    switch (f) {
        case VK_FORMAT_R8_UNORM: return 1;
        case VK_FORMAT_R8G8_UNORM: return 2;
        case VK_FORMAT_R8G8B8_UNORM: return 3;
        case VK_FORMAT_B8G8R8A8_UNORM:
        case VK_FORMAT_B8G8R8A8_SRGB:
        case VK_FORMAT_R8G8B8A8_UNORM:
        case VK_FORMAT_R8G8B8A8_SRGB: return 4;
        case VK_FORMAT_R16_UNORM: return 2;
        case VK_FORMAT_R16G16_UNORM: return 4;
        case VK_FORMAT_R16G16B16A16_UNORM: return 8;
        case VK_FORMAT_R32_SFLOAT: return 4;
        case VK_FORMAT_R32G32_SFLOAT: return 8;
        case VK_FORMAT_R32G32B32A32_SFLOAT: return 16;
        default: return 4;
    }
}

void uploadImageData(VkTextureObj* t, int x, int y, int w, int h,
                     const void* data, unsigned pixelFmt) {
    if (!t || !t->image || !data || w <= 0 || h <= 0) return;
    if (x < 0 || y < 0 || x + w > t->width || y + h > t->height) return;
    uint32_t bpp = texelBytes(t->format);
    if (!bpp) return;

    bool swapRb = false;
    if (bpp == 4) {
        bool imgBgra = (t->format == VK_FORMAT_B8G8R8A8_UNORM ||
                        t->format == VK_FORMAT_B8G8R8A8_SRGB);
        bool srcBgra = (pixelFmt == 213);
        swapRb = (imgBgra != srcBgra);
    }

    size_t rowBytes = (size_t)w * bpp;
    size_t srcRowBytes = (rowBytes + 3) & ~(size_t)3;
    std::vector<unsigned char> packed(rowBytes * (size_t)h);
    const unsigned char* src = (const unsigned char*)data;
    for (int row = 0; row < h; ++row) {
        unsigned char* dst = packed.data() + (size_t)row * rowBytes;
        memcpy(dst, src + (size_t)row * srcRowBytes, rowBytes);
        if (swapRb) {
            for (int px = 0; px < w; ++px) {
                unsigned char tmp = dst[px * 4 + 0];
                dst[px * 4 + 0] = dst[px * 4 + 2];
                dst[px * 4 + 2] = tmp;
            }
        }
    }

    VkBufferCreateInfo bi{};
    bi.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
    bi.size = packed.size();
    bi.usage = VK_BUFFER_USAGE_TRANSFER_SRC_BIT;
    bi.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
    VkBuffer staging = VK_NULL_HANDLE;
    if (vkCreateBuffer(ctx().device, &bi, nullptr, &staging) != VK_SUCCESS) return;

    VkMemoryRequirements req{};
    vkGetBufferMemoryRequirements(ctx().device, staging, &req);
    VkMemoryAllocateInfo ai{};
    ai.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
    ai.allocationSize = req.size;
    ai.memoryTypeIndex = findMemoryType(req.memoryTypeBits,
                                        VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT |
                                        VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);
    VkDeviceMemory mem = VK_NULL_HANDLE;
    if (vkAllocateMemory(ctx().device, &ai, nullptr, &mem) != VK_SUCCESS) {
        vkDestroyBuffer(ctx().device, staging, nullptr);
        return;
    }
    vkBindBufferMemory(ctx().device, staging, mem, 0);
    void* mapped = nullptr;
    vkMapMemory(ctx().device, mem, 0, packed.size(), 0, &mapped);
    memcpy(mapped, packed.data(), packed.size());
    vkUnmapMemory(ctx().device, mem);

    vkQueueWaitIdle(ctx().graphicsQueue);

    VkCommandBuffer cmd = VK_NULL_HANDLE;
    VkCommandBufferAllocateInfo cai{};
    cai.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
    cai.commandPool = ctx().commandPool;
    cai.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
    cai.commandBufferCount = 1;
    vkAllocateCommandBuffers(ctx().device, &cai, &cmd);
    VkCommandBufferBeginInfo cbi{};
    cbi.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
    cbi.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
    vkBeginCommandBuffer(cmd, &cbi);

    uint32_t layers = (uint32_t)(t->layers > 1 ? t->layers : 1);
    cmdTransitionImage(cmd, t->image, t->aspect, layers,
                       t->layout, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL);
    VkBufferImageCopy region{};
    region.bufferOffset = 0;
    region.imageSubresource.aspectMask = t->aspect;
    region.imageSubresource.mipLevel = 0;
    region.imageSubresource.baseArrayLayer = 0;
    region.imageSubresource.layerCount = 1;
    region.imageOffset.x = x;
    region.imageOffset.y = y;
    region.imageOffset.z = 0;
    region.imageExtent.width = (uint32_t)w;
    region.imageExtent.height = (uint32_t)h;
    region.imageExtent.depth = 1;
    vkCmdCopyBufferToImage(cmd, staging, t->image,
                           VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &region);
    cmdTransitionImage(cmd, t->image, t->aspect, layers,
                       VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, VK_IMAGE_LAYOUT_GENERAL);
    vkEndCommandBuffer(cmd);

    VkSubmitInfo si{};
    si.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
    si.commandBufferCount = 1;
    si.pCommandBuffers = &cmd;
    vkQueueSubmit(ctx().graphicsQueue, 1, &si, VK_NULL_HANDLE);
    vkQueueWaitIdle(ctx().graphicsQueue);
    vkFreeCommandBuffers(ctx().device, ctx().commandPool, 1, &cmd);
    vkDestroyBuffer(ctx().device, staging, nullptr);
    vkFreeMemory(ctx().device, mem, nullptr);
    t->layout = VK_IMAGE_LAYOUT_GENERAL;
}

VkVaoObj* vaoAt(unsigned id) {
    if (id == 0 || id > g_vaos.size()) return nullptr;
    return &g_vaos[id - 1];
}

VkFboObj* fboAt(unsigned id) {
    if (id == 0 || id > g_fbos.size()) return nullptr;
    return &g_fbos[id - 1];
}

bool fboBuild(unsigned id) {
    if (id == 0 || id > g_fbos.size()) return false;
    VkFboObj& f = g_fbos[id - 1];
    if (f.built && f.framebuffer && f.renderPass) return true;
    if (f.framebuffer) { deferRawFramebuffer(f.framebuffer); f.framebuffer = VK_NULL_HANDLE; }
    if (f.renderPass) { deferRawRenderPass(f.renderPass); f.renderPass = VK_NULL_HANDLE; }

    int w = 0, h = 0;
    std::vector<VkAttachmentDescription> atts;
    std::vector<VkAttachmentReference> refs;
    std::vector<VkImageView> views;

    for (size_t i = 0; i < f.colors.size(); ++i) {
        VkTextureObj* t = textureAt(f.colors[i].texture);
        if (!t || !t->image || !t->view) continue;
        if (w == 0) { w = t->width; h = t->height; }
        VkAttachmentDescription d{};
        d.format = t->format;
        d.samples = VK_SAMPLE_COUNT_1_BIT;
        d.loadOp = VK_ATTACHMENT_LOAD_OP_LOAD;
        d.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
        d.stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
        d.stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
        d.initialLayout = VK_IMAGE_LAYOUT_GENERAL;
        d.finalLayout = VK_IMAGE_LAYOUT_GENERAL;
        VkAttachmentReference r{};
        r.attachment = (uint32_t)atts.size();
        r.layout = VK_IMAGE_LAYOUT_GENERAL;
        atts.push_back(d);
        refs.push_back(r);
        views.push_back(t->view);
        t->colorAttach = true;
        t->sampled = true;
    }
    uint32_t colorCount = (uint32_t)atts.size();

    VkAttachmentReference depthRef{};
    bool hasDepth = false;
    if (f.depth) {
        VkTextureObj* dt = textureAt(f.depth);
        if (dt && dt->image && dt->view) {
            if (w == 0) { w = dt->width; h = dt->height; }
            VkAttachmentDescription d{};
            d.format = dt->format;
            d.samples = VK_SAMPLE_COUNT_1_BIT;
            d.loadOp = VK_ATTACHMENT_LOAD_OP_LOAD;
            d.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
            d.stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
            d.stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
            d.initialLayout = VK_IMAGE_LAYOUT_GENERAL;
            d.finalLayout = VK_IMAGE_LAYOUT_GENERAL;
            depthRef.attachment = (uint32_t)atts.size();
            depthRef.layout = VK_IMAGE_LAYOUT_GENERAL;
            atts.push_back(d);
            views.push_back(dt->view);
            hasDepth = true;
            dt->depthAttach = true;
        }
    }
    if (w == 0) { w = ctx().width ? (int)ctx().width : 1; h = ctx().height ? (int)ctx().height : 1; }

    VkSubpassDescription sub{};
    sub.pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS;
    sub.colorAttachmentCount = colorCount;
    sub.pColorAttachments = colorCount ? refs.data() : nullptr;
    sub.pDepthStencilAttachment = hasDepth ? &depthRef : nullptr;

    VkSubpassDependency dep{};
    dep.srcSubpass = VK_SUBPASS_EXTERNAL;
    dep.dstSubpass = 0;
    dep.srcStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT |
                       VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT | VK_PIPELINE_STAGE_LATE_FRAGMENT_TESTS_BIT;
    dep.dstStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT |
                       VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT | VK_PIPELINE_STAGE_LATE_FRAGMENT_TESTS_BIT;
    dep.srcAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT | VK_ACCESS_COLOR_ATTACHMENT_READ_BIT |
                        VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT;
    dep.dstAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT | VK_ACCESS_COLOR_ATTACHMENT_READ_BIT |
                        VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT | VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_READ_BIT;

    VkRenderPassCreateInfo rci{};
    rci.sType = VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO;
    rci.attachmentCount = (uint32_t)atts.size();
    rci.pAttachments = atts.empty() ? nullptr : atts.data();
    rci.subpassCount = 1;
    rci.pSubpasses = &sub;
    rci.dependencyCount = 1;
    rci.pDependencies = &dep;
    if (vkCreateRenderPass(ctx().device, &rci, nullptr, &f.renderPass) != VK_SUCCESS) return false;

    VkFramebufferCreateInfo fci{};
    fci.sType = VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO;
    fci.renderPass = f.renderPass;
    fci.attachmentCount = (uint32_t)views.size();
    fci.pAttachments = views.empty() ? nullptr : views.data();
    fci.width = (uint32_t)w;
    fci.height = (uint32_t)h;
    fci.layers = 1;
    if (vkCreateFramebuffer(ctx().device, &fci, nullptr, &f.framebuffer) != VK_SUCCESS) return false;

    f.width = w;
    f.height = h;
    f.built = true;
    return true;
}

unsigned boundVaoId() { return g_boundVao; }
unsigned boundIndexBuffer() { return g_boundBuffer[1]; }

unsigned bufferCount() { return (unsigned)g_buffers.size(); }
unsigned textureCount() { return (unsigned)g_textures.size(); }
unsigned fboCount() { return (unsigned)g_fbos.size(); }

VkImageView imageViewOf(unsigned textureId) {
    VkTextureObj* t = textureAt(textureId);
    return t ? t->view : VK_NULL_HANDLE;
}

VkSampler samplerOf(unsigned textureId) {
    VkTextureObj* t = textureAt(textureId);
    return t ? t->sampler : VK_NULL_HANDLE;
}

bool textureReady(unsigned textureId) {
    VkTextureObj* t = textureAt(textureId);
    return t && t->view && t->sampler;
}

unsigned dummyTexture() {
    static unsigned g_dummy = 0;
    if (g_dummy) {
        VkTextureObj* ex = textureAt(g_dummy);
        if (ex && ex->view && ex->sampler) return g_dummy;
        g_dummy = 0;
    }
    g_dummy = createTexture();
    VkTextureObj* t = textureAt(g_dummy);
    if (!t) return 0;
    t->target = 250;
    createImageFor(t, 1, 1, 1, 239);
    return g_dummy;
}

VkBuffer dummyBuffer() {
    static VkBuffer g_buf = VK_NULL_HANDLE;
    static VkDeviceMemory g_mem = VK_NULL_HANDLE;
    if (g_buf && g_mem) return g_buf;
    VkBufferCreateInfo bi{};
    bi.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
    bi.size = 16;
    bi.usage = VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT | VK_BUFFER_USAGE_STORAGE_BUFFER_BIT |
               VK_BUFFER_USAGE_VERTEX_BUFFER_BIT | VK_BUFFER_USAGE_INDEX_BUFFER_BIT;
    bi.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
    if (vkCreateBuffer(ctx().device, &bi, nullptr, &g_buf) != VK_SUCCESS) {
        g_buf = VK_NULL_HANDLE;
        return VK_NULL_HANDLE;
    }
    VkMemoryRequirements req{};
    vkGetBufferMemoryRequirements(ctx().device, g_buf, &req);
    VkMemoryAllocateInfo ai{};
    ai.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
    ai.allocationSize = req.size;
    ai.memoryTypeIndex = findMemoryType(req.memoryTypeBits, VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT |
                                                               VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);
    if (vkAllocateMemory(ctx().device, &ai, nullptr, &g_mem) != VK_SUCCESS) {
        vkDestroyBuffer(ctx().device, g_buf, nullptr);
        g_buf = VK_NULL_HANDLE;
        g_mem = VK_NULL_HANDLE;
        return VK_NULL_HANDLE;
    }
    vkBindBufferMemory(ctx().device, g_buf, g_mem, 0);
    return g_buf;
}

void fboInvalidate(unsigned id) {
    if (id == 0 || id > g_fbos.size()) return;
    VkFboObj& f = g_fbos[id - 1];
    if (f.framebuffer) deferRawFramebuffer(f.framebuffer);
    if (f.renderPass) deferRawRenderPass(f.renderPass);
    unsigned keepD = f.depth;
    unsigned keepDA = f.depthAttachment;
    auto colors = f.colors;
    f = VkFboObj{};
    f.depth = keepD;
    f.depthAttachment = keepDA;
    f.colors = colors;
}

}
}
