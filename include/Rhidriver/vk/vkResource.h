#pragma once
#include "Rhidriver/vk/vk.h"
#include <cstdint>
#include <string>
#include <vector>

namespace rhi {
namespace vk {

struct VkBufferSlot {
    VkBuffer buffer = VK_NULL_HANDLE;
    VkDeviceMemory memory = VK_NULL_HANDLE;
    uint64_t capacity = 0;
    void* mapped = nullptr;
};

struct VkBufferObj {
    VkBuffer buffer = VK_NULL_HANDLE;
    VkDeviceMemory memory = VK_NULL_HANDLE;
    uint64_t size = 0;
    void* mapped = nullptr;
    unsigned target = 0;
    uint64_t writeOfs = 0;
    uint32_t slotBase = 0;
    VkBufferSlot slots[4];
};

struct VkTextureObj {
    VkImage image = VK_NULL_HANDLE;
    VkDeviceMemory memory = VK_NULL_HANDLE;
    VkImageView view = VK_NULL_HANDLE;
    VkSampler sampler = VK_NULL_HANDLE;
    unsigned target = 0;
    int width = 0;
    int height = 0;
    int depth = 1;
    int layers = 1;
    int levels = 1;
    VkFormat format = VK_FORMAT_UNDEFINED;
    bool isRenderTarget = false;
    bool hasDepth = false;
    unsigned minFilter = 0;
    unsigned magFilter = 0;
    unsigned wrapS = 0;
    unsigned wrapT = 0;
    VkImageLayout layout = VK_IMAGE_LAYOUT_UNDEFINED;
    VkImageAspectFlags aspect = VK_IMAGE_ASPECT_COLOR_BIT;
    bool sampled = false;
    bool colorAttach = false;
    bool depthAttach = false;
};

struct VkAttrib {
    unsigned index = 0;
    int size = 0;
    unsigned type = 0;
    bool normalized = false;
    int stride = 0;
    uint64_t offset = 0;
    unsigned divisor = 0;
};

struct VkVaoObj {
    std::vector<VkAttrib> attribs;
    unsigned vbo = 0;
    unsigned ebo = 0;
    unsigned instBuffer = 0;
};

struct VkAttachment {
    unsigned texture = 0;
    unsigned attachment = 0;
    int layer = -1;
};

struct VkFboObj {
    VkFramebuffer framebuffer = VK_NULL_HANDLE;
    VkRenderPass renderPass = VK_NULL_HANDLE;
    std::vector<VkAttachment> colors;
    unsigned depth = 0;
    unsigned depthAttachment = 0;
    int width = 0;
    int height = 0;
    bool built = false;
};

uint32_t findMemoryType(uint32_t typeBits, VkMemoryPropertyFlags props);

void resourceInit();
void resourceShutdown();

VkBufferObj* bufferAt(unsigned id);
unsigned createBuffer();
void resetBufferOffsets();
unsigned createTexture();
unsigned createVao();
unsigned createFbo();
unsigned createRbo();

void destroyBuffer(unsigned id);
void destroyTexture(unsigned id);
void destroyVao(unsigned id);
void destroyFbo(unsigned id);

void bindBuffer(unsigned target, unsigned id);
unsigned currentBuffer(unsigned target);
void bindVao(unsigned id);

VkTextureObj* textureAt(unsigned id);
bool createImageFor(VkTextureObj* t, int w, int h, int d, int fmt);
VkVaoObj* vaoAt(unsigned id);
VkFboObj* fboAt(unsigned id);
bool fboBuild(unsigned id);
unsigned boundVaoId();
unsigned boundIndexBuffer();
unsigned bufferCount();
unsigned textureCount();
unsigned fboCount();

VkImageView imageViewOf(unsigned textureId);
VkSampler samplerOf(unsigned textureId);
bool textureReady(unsigned textureId);
unsigned dummyTexture();
VkBuffer dummyBuffer();

void fboInvalidate(unsigned id);

void uploadImageData(VkTextureObj* t, int x, int y, int w, int h, const void* data, unsigned pixelFmt);

void cmdTransitionImage(VkCommandBuffer cmd, VkImage image, VkImageAspectFlags aspect,
                        uint32_t layers, VkImageLayout from, VkImageLayout to);
void ensureImageLayout(VkTextureObj* t, VkImageLayout want);

}
}
