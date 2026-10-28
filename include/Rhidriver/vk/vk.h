#pragma once
#include <windows.h>
#include <cstdint>
#include <vector>

#ifndef VK_USE_PLATFORM_WIN32_KHR
#define VK_USE_PLATFORM_WIN32_KHR 1
#endif
#include <vulkan/vulkan.h>

namespace rhi {
namespace vk {

constexpr uint32_t kMaxFramesInFlight = 2;

struct VkContext {
    VkInstance instance = VK_NULL_HANDLE;
    VkPhysicalDevice physical = VK_NULL_HANDLE;
    VkDevice device = VK_NULL_HANDLE;
    char deviceName[256] = { 0 };
    uint32_t apiVersion = 0;
    VkQueue graphicsQueue = VK_NULL_HANDLE;
    VkQueue presentQueue = VK_NULL_HANDLE;
    uint32_t graphicsFamily = 0;
    uint32_t presentFamily = 0;
    VkSurfaceKHR surface = VK_NULL_HANDLE;
    VkSwapchainKHR swapchain = VK_NULL_HANDLE;
    VkFormat swapFormat = VK_FORMAT_UNDEFINED;
    VkExtent2D swapExtent{};
    std::vector<VkImage> swapImages;
    std::vector<VkImageView> swapViews;
    std::vector<VkFramebuffer> swapFramebuffers;
    VkRenderPass swapRenderPass = VK_NULL_HANDLE;
    VkImage defaultDepthImage = VK_NULL_HANDLE;
    VkImageView defaultDepthView = VK_NULL_HANDLE;
    VkDeviceMemory defaultDepthMem = VK_NULL_HANDLE;
    VkFormat depthFormat = VK_FORMAT_D32_SFLOAT;
    bool pendingResize = false;
    uint32_t pendingWidth = 0;
    uint32_t pendingHeight = 0;
    VkCommandPool commandPool = VK_NULL_HANDLE;
    std::vector<VkCommandBuffer> cmd;
    std::vector<VkSemaphore> imgAvailable;
    std::vector<VkSemaphore> renderFinished;
    std::vector<VkFence> inFlight;
    uint32_t frameIndex = 0;
    uint32_t imageIndex = 0;
    bool vsync = true;
    bool frameActive = false;
    bool swapPassActive = false;
    uint32_t width = 0;
    uint32_t height = 0;
    int minImageCount = 2;
    float clearR = 0.0f;
    float clearG = 0.0f;
    float clearB = 0.0f;
    float clearA = 1.0f;
    float clearDepth = 1.0f;
};

VkContext& ctx();
bool create(HWND hwnd);
void destroy();
bool beginFrame();
bool endFrame();
bool recreateSwapchain();
void waitIdle();
void releaseDeferred();
void deferTexture(unsigned id);
void deferBuffer(unsigned id);
void deferFbo(unsigned id);
void deferVkBuffer(VkBuffer raw);
void deferVkPipeline(VkPipeline raw);
void deferVkMemory(VkDeviceMemory raw);
void deferRawFramebuffer(VkFramebuffer raw);
void deferRawRenderPass(VkRenderPass raw);
void deferRawImageView(VkImageView raw);
void deferRawSampler(VkSampler raw);
void deferRawImage(VkImage raw);

}
}
