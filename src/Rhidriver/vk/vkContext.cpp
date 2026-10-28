#include "Gvtcier.h"
#include <cstdio>
#include <cstring>
#include <cstdarg>
#include "bicu/core/log.h"

// Vulkan 上下文管理：实例、物理设备、逻辑设备、队列、交换链与每帧同步对象。
// 属于已封禁的 VK 后端（原因见 Vulkan后端.cpp 顶部说明），当前不会被启用。
namespace rhi {
namespace vk {

// 这条链路上每一步都可能是"进程直接消失"，printf 攒在缓冲里根本来不及落盘，
// 所以统一走无缓冲的日志通道，出事的时候最后一句就是死因
static void vkLog(const char* fmt, ...) {
    char buf[512];
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(buf, sizeof(buf), fmt, ap);
    va_end(ap);
    rlog::Gvtcier_Log_Write("[vk] %s\n", buf);
}

static VkContext g_ctx;   // 全局唯一的 Vulkan 上下文

VkContext& ctx() { return g_ctx; }

// 查询某个校验层是否可用。找不到就跳过，不要硬启用不存在的层
static bool hasLayer(const char* name) {
    uint32_t n = 0;
    vkEnumerateInstanceLayerProperties(&n, nullptr);
    std::vector<VkLayerProperties> props(n);
    vkEnumerateInstanceLayerProperties(&n, props.data());
    for (uint32_t i = 0; i < n; ++i)
        if (strcmp(props[i].layerName, name) == 0) return true;
    return false;
}

// 查询实例层是否提供某个扩展。找不到就不申请，省得 vkCreateInstance 直接失败
static bool hasInstanceExt(const char* name) {
    uint32_t n = 0;
    vkEnumerateInstanceExtensionProperties(nullptr, &n, nullptr);
    std::vector<VkExtensionProperties> props(n);
    vkEnumerateInstanceExtensionProperties(nullptr, &n, props.data());
    for (uint32_t i = 0; i < n; ++i)
        if (strcmp(props[i].extensionName, name) == 0) return true;
    return false;
}

static bool createInstance() {
    VkApplicationInfo app{};
    app.sType = VK_STRUCTURE_TYPE_APPLICATION_INFO;
    app.pApplicationName = "Gvtcier";
    app.applicationVersion = VK_MAKE_VERSION(1, 0, 0);
    app.pEngineName = "Gvtcier";
    app.engineVersion = VK_MAKE_VERSION(1, 0, 0);
    // sokol 的 Vulkan 后端要拿描述符缓冲那套扩展，它内部会用 1.3 的查询入口，
    // 实例这里跟着抬到 1.3，不然拿不到完整的物理设备属性
    app.apiVersion = VK_API_VERSION_1_3;

    // sokol 在 debug 构建里会给每个对象打名字，用的是 vkSetDebugUtilsObjectNameEXT，
    // 那个函数只有开了 VK_EXT_debug_utils 才查得到，不开就直接 panic
    std::vector<const char*> exts = { "VK_KHR_surface", "VK_KHR_win32_surface" };
    if (hasInstanceExt("VK_EXT_debug_utils")) {
        exts.push_back("VK_EXT_debug_utils");
    } else {
        vkLog("实例层没有 VK_EXT_debug_utils，sokol 的 debug 分支会要不到入口");
    }
    const char* layers[] = { "VK_LAYER_KHRONOS_validation" };
    bool useValidation = hasLayer(layers[0]);

    VkInstanceCreateInfo ci{};
    ci.sType = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO;
    ci.pApplicationInfo = &app;
    ci.enabledExtensionCount = (uint32_t)exts.size();
    ci.ppEnabledExtensionNames = exts.data();
    if (useValidation) {
        ci.enabledLayerCount = 1;
        ci.ppEnabledLayerNames = layers;
    }

    VkResult r = vkCreateInstance(&ci, nullptr, &g_ctx.instance);
    if (r != VK_SUCCESS) {
        vkLog("vkCreateInstance 失败 %d", (int)r);
        return false;
    }
    vkLog("实例创建成功 (validation=%d)", (int)useValidation);
    return true;
}

static bool createSurface(HWND hwnd) {
    VkWin32SurfaceCreateInfoKHR ci{};
    ci.sType = VK_STRUCTURE_TYPE_WIN32_SURFACE_CREATE_INFO_KHR;
    ci.hinstance = GetModuleHandleW(nullptr);
    ci.hwnd = hwnd;
    VkResult r = vkCreateWin32SurfaceKHR(g_ctx.instance, &ci, nullptr, &g_ctx.surface);
    if (r != VK_SUCCESS) {
        vkLog("创建 surface 失败 %d", (int)r);
        return false;
    }
    return true;
}

struct QueueFamilies {
    int graphics = -1;
    int present = -1;
};

static QueueFamilies findFamilies(VkPhysicalDevice dev) {
    QueueFamilies qf;
    uint32_t count = 0;
    vkGetPhysicalDeviceQueueFamilyProperties(dev, &count, nullptr);
    std::vector<VkQueueFamilyProperties> props(count);
    vkGetPhysicalDeviceQueueFamilyProperties(dev, &count, props.data());
    for (uint32_t i = 0; i < count; ++i) {
        if ((props[i].queueFlags & VK_QUEUE_GRAPHICS_BIT) && qf.graphics < 0)
            qf.graphics = (int)i;
        VkBool32 present = VK_FALSE;
        vkGetPhysicalDeviceSurfaceSupportKHR(dev, i, g_ctx.surface, &present);
        if (present && qf.present < 0) qf.present = (int)i;
        if (qf.graphics >= 0 && qf.present >= 0) break;
    }
    return qf;
}

static bool hasExt(VkPhysicalDevice dev, const char* name) {
    uint32_t n = 0;
    vkEnumerateDeviceExtensionProperties(dev, nullptr, &n, nullptr);
    std::vector<VkExtensionProperties> props(n);
    vkEnumerateDeviceExtensionProperties(dev, nullptr, &n, props.data());
    for (uint32_t i = 0; i < n; ++i)
        if (strcmp(props[i].extensionName, name) == 0) return true;
    return false;
}

static bool pickPhysical() {
    uint32_t n = 0;
    vkEnumeratePhysicalDevices(g_ctx.instance, &n, nullptr);
    if (n == 0) {
        vkLog("找不到任何物理设备");
        return false;
    }
    std::vector<VkPhysicalDevice> devs(n);
    vkEnumeratePhysicalDevices(g_ctx.instance, &n, devs.data());
    for (uint32_t i = 0; i < n; ++i) {
        if (!hasExt(devs[i], VK_KHR_SWAPCHAIN_EXTENSION_NAME)) continue;
        QueueFamilies qf = findFamilies(devs[i]);
        if (qf.graphics < 0 || qf.present < 0) continue;
        g_ctx.physical = devs[i];
        g_ctx.graphicsFamily = (uint32_t)qf.graphics;
        g_ctx.presentFamily = (uint32_t)qf.present;
        VkPhysicalDeviceProperties props{};
        vkGetPhysicalDeviceProperties(devs[i], &props);
        snprintf(g_ctx.deviceName, sizeof(g_ctx.deviceName), "%s", props.deviceName);
        g_ctx.apiVersion = props.apiVersion;
        vkLog("选中设备: %s (API %u.%u)", props.deviceName,
              VK_API_VERSION_MAJOR(props.apiVersion), VK_API_VERSION_MINOR(props.apiVersion));
        return true;
    }
    vkLog("没有同时支持交换链与呈现的显卡");
    return false;
}

static bool createDevice() {
    float prio = 1.0f;
    std::vector<VkDeviceQueueCreateInfo> qcis;
    uint32_t families[2] = { g_ctx.graphicsFamily, g_ctx.presentFamily };
    uint32_t uniqueCount = (families[0] == families[1]) ? 1 : 2;
    for (uint32_t i = 0; i < uniqueCount; ++i) {
        VkDeviceQueueCreateInfo qci{};
        qci.sType = VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO;
        qci.queueFamilyIndex = families[i];
        qci.queueCount = 1;
        qci.pQueuePriorities = &prio;
        qcis.push_back(qci);
    }
    std::vector<const char*> devExts;
    devExts.push_back(VK_KHR_SWAPCHAIN_EXTENSION_NAME);
    bool hasDescIdx = hasExt(g_ctx.physical, VK_EXT_DESCRIPTOR_INDEXING_EXTENSION_NAME);
    if (hasDescIdx) devExts.push_back(VK_EXT_DESCRIPTOR_INDEXING_EXTENSION_NAME);
    // sokol 的绑定全部走描述符缓冲，vkGetDescriptorSetLayoutSizeEXT 这组函数缺一个它就直接 abort，
    // 所以这个扩展是硬性前提，申请不到就没必要往下走了
    if (!hasExt(g_ctx.physical, VK_EXT_DESCRIPTOR_BUFFER_EXTENSION_NAME)) {
        vkLog("设备不支持 VK_EXT_descriptor_buffer，sokol 跑不起来");
        return false;
    }
    devExts.push_back(VK_EXT_DESCRIPTOR_BUFFER_EXTENSION_NAME);

    VkPhysicalDeviceFeatures features{};
    features.fillModeNonSolid = VK_TRUE;
    features.samplerAnisotropy = VK_TRUE;
    features.fragmentStoresAndAtomics = VK_TRUE;
    features.vertexPipelineStoresAndAtomics = VK_TRUE;
    features.shaderStorageImageExtendedFormats = VK_TRUE;

    VkPhysicalDeviceDescriptorIndexingFeatures di{};
    di.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_DESCRIPTOR_INDEXING_FEATURES;
    di.descriptorBindingUniformBufferUpdateAfterBind = VK_TRUE;
    di.descriptorBindingStorageBufferUpdateAfterBind = VK_TRUE;
    di.descriptorBindingSampledImageUpdateAfterBind = VK_TRUE;
    di.descriptorBindingPartiallyBound = VK_TRUE;
    di.runtimeDescriptorArray = VK_TRUE;
    // 开了 1.3 的实例就得显式要这一串同步特性，驱动默认不给
    VkPhysicalDeviceVulkan13Features vk13{};
    vk13.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_3_FEATURES;
    vk13.synchronization2 = VK_TRUE;
    vk13.dynamicRendering = VK_TRUE;
    di.pNext = &vk13;

    VkDeviceCreateInfo ci{};
    ci.sType = VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO;
    ci.pNext = hasDescIdx ? (void*)&di : (void*)&vk13;
    ci.queueCreateInfoCount = (uint32_t)qcis.size();
    ci.pQueueCreateInfos = qcis.data();
    ci.enabledExtensionCount = (uint32_t)devExts.size();
    ci.ppEnabledExtensionNames = devExts.data();
    ci.pEnabledFeatures = &features;

    VkResult r = vkCreateDevice(g_ctx.physical, &ci, nullptr, &g_ctx.device);
    if (r != VK_SUCCESS) {
        vkLog("vkCreateDevice 失败 %d", (int)r);
        return false;
    }
    vkGetDeviceQueue(g_ctx.device, g_ctx.graphicsFamily, 0, &g_ctx.graphicsQueue);
    vkGetDeviceQueue(g_ctx.device, g_ctx.presentFamily, 0, &g_ctx.presentQueue);

    // 光把名字塞进列表不代表驱动真给了，把实际申请的逐个回读一遍，
    // 缺了就在这里说清楚，别让 sokol 那边 abort 得不明不白
    for (size_t i = 0; i < devExts.size(); ++i) {
        PFN_vkVoidFunction fn = strcmp(devExts[i], VK_KHR_SWAPCHAIN_EXTENSION_NAME) == 0
                                    ? nullptr
                                    : vkGetDeviceProcAddr(g_ctx.device, "vkGetDescriptorSetLayoutSizeEXT");
        vkLog("已申请扩展: %s%s", devExts[i], fn ? "（入口可用）" : "");
    }
    vkLog("描述符缓冲入口 vkGetDescriptorSetLayoutSizeEXT = %p",
          (void*)vkGetDeviceProcAddr(g_ctx.device, "vkGetDescriptorSetLayoutSizeEXT"));
    vkLog("描述符缓冲入口 vkGetDescriptorEXT = %p",
          (void*)vkGetDeviceProcAddr(g_ctx.device, "vkGetDescriptorEXT"));
    vkLog("描述符缓冲入口 vkCmdBindDescriptorBuffersEXT = %p",
          (void*)vkGetDeviceProcAddr(g_ctx.device, "vkCmdBindDescriptorBuffersEXT"));
    vkLog("描述符缓冲入口 vkCmdSetDescriptorBufferOffsetsEXT = %p",
          (void*)vkGetDeviceProcAddr(g_ctx.device, "vkCmdSetDescriptorBufferOffsetsEXT"));
    return true;
}

static bool createSwapchain() {
    VkSurfaceCapabilitiesKHR caps{};
    vkGetPhysicalDeviceSurfaceCapabilitiesKHR(g_ctx.physical, g_ctx.surface, &caps);

    uint32_t fmtCount = 0;
    vkGetPhysicalDeviceSurfaceFormatsKHR(g_ctx.physical, g_ctx.surface, &fmtCount, nullptr);
    std::vector<VkSurfaceFormatKHR> formats(fmtCount);
    vkGetPhysicalDeviceSurfaceFormatsKHR(g_ctx.physical, g_ctx.surface, &fmtCount, formats.data());
    VkSurfaceFormatKHR chosen = formats.empty() ? VkSurfaceFormatKHR{ VK_FORMAT_B8G8R8A8_UNORM, VK_COLOR_SPACE_SRGB_NONLINEAR_KHR } : formats[0];
    for (uint32_t i = 0; i < fmtCount; ++i) {
        if (formats[i].format == VK_FORMAT_B8G8R8A8_UNORM &&
            formats[i].colorSpace == VK_COLOR_SPACE_SRGB_NONLINEAR_KHR) {
            chosen = formats[i];
            break;
        }
    }

    uint32_t modeCount = 0;
    vkGetPhysicalDeviceSurfacePresentModesKHR(g_ctx.physical, g_ctx.surface, &modeCount, nullptr);
    std::vector<VkPresentModeKHR> modes(modeCount);
    vkGetPhysicalDeviceSurfacePresentModesKHR(g_ctx.physical, g_ctx.surface, &modeCount, modes.data());
    VkPresentModeKHR present = VK_PRESENT_MODE_FIFO_KHR;
    if (!g_ctx.vsync) {
        for (uint32_t i = 0; i < modeCount; ++i)
            if (modes[i] == VK_PRESENT_MODE_MAILBOX_KHR) present = modes[i];
    }

    VkExtent2D extent = caps.currentExtent;
    if (extent.width == 0xFFFFFFFFu) {
        extent.width = g_ctx.width;
        extent.height = g_ctx.height;
        if (extent.width < caps.minImageExtent.width) extent.width = caps.minImageExtent.width;
        if (extent.width > caps.maxImageExtent.width) extent.width = caps.maxImageExtent.width;
        if (extent.height < caps.minImageExtent.height) extent.height = caps.minImageExtent.height;
        if (extent.height > caps.maxImageExtent.height) extent.height = caps.maxImageExtent.height;
    }

    uint32_t imageCount = caps.minImageCount + 1;
    if (caps.maxImageCount > 0 && imageCount > caps.maxImageCount) imageCount = caps.maxImageCount;

    VkSwapchainCreateInfoKHR ci{};
    ci.sType = VK_STRUCTURE_TYPE_SWAPCHAIN_CREATE_INFO_KHR;
    ci.surface = g_ctx.surface;
    ci.minImageCount = imageCount;
    ci.imageFormat = chosen.format;
    ci.imageColorSpace = chosen.colorSpace;
    ci.imageExtent = extent;
    ci.imageArrayLayers = 1;
    ci.imageUsage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT;
    uint32_t fams[2] = { g_ctx.graphicsFamily, g_ctx.presentFamily };
    if (g_ctx.graphicsFamily != g_ctx.presentFamily) {
        ci.imageSharingMode = VK_SHARING_MODE_CONCURRENT;
        ci.queueFamilyIndexCount = 2;
        ci.pQueueFamilyIndices = fams;
    } else {
        ci.imageSharingMode = VK_SHARING_MODE_EXCLUSIVE;
    }
    ci.preTransform = caps.currentTransform;
    ci.compositeAlpha = VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR;
    ci.presentMode = present;
    ci.clipped = VK_TRUE;
    ci.flags = 0;

    VkResult r = vkCreateSwapchainKHR(g_ctx.device, &ci, nullptr, &g_ctx.swapchain);
    if (r != VK_SUCCESS) {
        vkLog("创建交换链失败 %d", (int)r);
        return false;
    }
    g_ctx.swapFormat = chosen.format;
    g_ctx.swapExtent = extent;

    uint32_t count = 0;
    vkGetSwapchainImagesKHR(g_ctx.device, g_ctx.swapchain, &count, nullptr);
    g_ctx.swapImages.resize(count);
    vkGetSwapchainImagesKHR(g_ctx.device, g_ctx.swapchain, &count, g_ctx.swapImages.data());

    g_ctx.swapViews.resize(count);
    for (uint32_t i = 0; i < count; ++i) {
        VkImageViewCreateInfo vi{};
        vi.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
        vi.image = g_ctx.swapImages[i];
        vi.viewType = VK_IMAGE_VIEW_TYPE_2D;
        vi.format = g_ctx.swapFormat;
        vi.components.r = VK_COMPONENT_SWIZZLE_IDENTITY;
        vi.components.g = VK_COMPONENT_SWIZZLE_IDENTITY;
        vi.components.b = VK_COMPONENT_SWIZZLE_IDENTITY;
        vi.components.a = VK_COMPONENT_SWIZZLE_IDENTITY;
        vi.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
        vi.subresourceRange.levelCount = 1;
        vi.subresourceRange.layerCount = 1;
        vkCreateImageView(g_ctx.device, &vi, nullptr, &g_ctx.swapViews[i]);
    }
    vkLog("交换链 %ux%u 共 %u 张", extent.width, extent.height, count);

    g_ctx.renderFinished.assign(count, VK_NULL_HANDLE);
    VkSemaphoreCreateInfo rsci{};
    rsci.sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO;
    for (uint32_t i = 0; i < count; ++i)
        vkCreateSemaphore(g_ctx.device, &rsci, nullptr, &g_ctx.renderFinished[i]);
    return true;
}

static VkFormat pickDepthFormat() {
    VkFormat cands[] = { VK_FORMAT_D32_SFLOAT, VK_FORMAT_D32_SFLOAT_S8_UINT, VK_FORMAT_D24_UNORM_S8_UINT };
    for (VkFormat f : cands) {
        VkFormatProperties p{};
        vkGetPhysicalDeviceFormatProperties(g_ctx.physical, f, &p);
        if (p.optimalTilingFeatures & VK_FORMAT_FEATURE_DEPTH_STENCIL_ATTACHMENT_BIT) return f;
    }
    return VK_FORMAT_D32_SFLOAT;
}

static bool createSwapchainPass() {
    if (g_ctx.swapRenderPass) return true;
    VkAttachmentDescription color{};
    color.format = g_ctx.swapFormat;
    color.samples = VK_SAMPLE_COUNT_1_BIT;
    color.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
    color.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
    color.stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
    color.stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
    color.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    color.finalLayout = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR;

    VkAttachmentDescription depth{};
    g_ctx.depthFormat = pickDepthFormat();
    depth.format = g_ctx.depthFormat;
    depth.samples = VK_SAMPLE_COUNT_1_BIT;
    depth.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
    depth.storeOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
    depth.stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
    depth.stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
    depth.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    depth.finalLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;

    VkAttachmentReference colorRef{};
    colorRef.attachment = 0;
    colorRef.layout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
    VkAttachmentReference depthRef{};
    depthRef.attachment = 1;
    depthRef.layout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;

    VkSubpassDescription sub{};
    sub.pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS;
    sub.colorAttachmentCount = 1;
    sub.pColorAttachments = &colorRef;
    sub.pDepthStencilAttachment = &depthRef;

    VkSubpassDependency dep{};
    dep.srcSubpass = VK_SUBPASS_EXTERNAL;
    dep.dstSubpass = 0;
    dep.srcStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT | VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT;
    dep.srcAccessMask = 0;
    dep.dstStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT | VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT;
    dep.dstAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT | VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT;

    VkAttachmentDescription atts[2] = { color, depth };
    VkRenderPassCreateInfo ci{};
    ci.sType = VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO;
    ci.attachmentCount = 2;
    ci.pAttachments = atts;
    ci.subpassCount = 1;
    ci.pSubpasses = &sub;
    ci.dependencyCount = 1;
    ci.pDependencies = &dep;
    if (vkCreateRenderPass(g_ctx.device, &ci, nullptr, &g_ctx.swapRenderPass) != VK_SUCCESS) return false;

    VkImageCreateInfo di{};
    di.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
    di.imageType = VK_IMAGE_TYPE_2D;
    di.format = g_ctx.depthFormat;
    di.extent.width = g_ctx.swapExtent.width;
    di.extent.height = g_ctx.swapExtent.height;
    di.extent.depth = 1;
    di.mipLevels = 1;
    di.arrayLayers = 1;
    di.samples = VK_SAMPLE_COUNT_1_BIT;
    di.tiling = VK_IMAGE_TILING_OPTIMAL;
    di.usage = VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT | VK_IMAGE_USAGE_SAMPLED_BIT;
    di.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
    if (vkCreateImage(g_ctx.device, &di, nullptr, &g_ctx.defaultDepthImage) != VK_SUCCESS) return false;
    VkMemoryRequirements req{};
    vkGetImageMemoryRequirements(g_ctx.device, g_ctx.defaultDepthImage, &req);
    VkMemoryAllocateInfo ai{};
    ai.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
    ai.allocationSize = req.size;
    ai.memoryTypeIndex = findMemoryType(req.memoryTypeBits, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
    vkAllocateMemory(g_ctx.device, &ai, nullptr, &g_ctx.defaultDepthMem);
    vkBindImageMemory(g_ctx.device, g_ctx.defaultDepthImage, g_ctx.defaultDepthMem, 0);

    VkImageViewCreateInfo dvi{};
    dvi.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
    dvi.image = g_ctx.defaultDepthImage;
    dvi.viewType = VK_IMAGE_VIEW_TYPE_2D;
    dvi.format = g_ctx.depthFormat;
    dvi.subresourceRange.aspectMask = VK_IMAGE_ASPECT_DEPTH_BIT;
    dvi.subresourceRange.levelCount = 1;
    dvi.subresourceRange.layerCount = 1;
    vkCreateImageView(g_ctx.device, &dvi, nullptr, &g_ctx.defaultDepthView);

    g_ctx.swapFramebuffers.resize(g_ctx.swapViews.size());
    for (size_t i = 0; i < g_ctx.swapViews.size(); ++i) {
        VkImageView views[2] = { g_ctx.swapViews[i], g_ctx.defaultDepthView };
        VkFramebufferCreateInfo fi{};
        fi.sType = VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO;
        fi.renderPass = g_ctx.swapRenderPass;
        fi.attachmentCount = 2;
        fi.pAttachments = views;
        fi.width = g_ctx.swapExtent.width;
        fi.height = g_ctx.swapExtent.height;
        fi.layers = 1;
        vkCreateFramebuffer(g_ctx.device, &fi, nullptr, &g_ctx.swapFramebuffers[i]);
    }
    printf("[vk] swapchain pass + depth(%d) + %u framebuffers\n",
           (int)g_ctx.depthFormat, (unsigned)g_ctx.swapFramebuffers.size());
    return true;
}

static bool createCommands() {
    VkCommandPoolCreateInfo pi{};
    pi.sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO;
    pi.flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT;
    pi.queueFamilyIndex = g_ctx.graphicsFamily;
    if (vkCreateCommandPool(g_ctx.device, &pi, nullptr, &g_ctx.commandPool) != VK_SUCCESS) return false;

    g_ctx.cmd.resize(kMaxFramesInFlight);
    VkCommandBufferAllocateInfo ai{};
    ai.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
    ai.commandPool = g_ctx.commandPool;
    ai.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
    ai.commandBufferCount = kMaxFramesInFlight;
    vkAllocateCommandBuffers(g_ctx.device, &ai, g_ctx.cmd.data());

    VkSemaphoreCreateInfo si{};
    si.sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO;
    VkFenceCreateInfo fi{};
    fi.sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO;
    fi.flags = VK_FENCE_CREATE_SIGNALED_BIT;
    g_ctx.imgAvailable.assign(kMaxFramesInFlight, VK_NULL_HANDLE);
    g_ctx.inFlight.assign(kMaxFramesInFlight, VK_NULL_HANDLE);
    for (uint32_t i = 0; i < kMaxFramesInFlight; ++i) {
        vkCreateSemaphore(g_ctx.device, &si, nullptr, &g_ctx.imgAvailable[i]);
        vkCreateFence(g_ctx.device, &fi, nullptr, &g_ctx.inFlight[i]);
    }
    return true;
}

bool create(HWND hwnd) {
    if (!createInstance()) return false;
    if (!createSurface(hwnd)) { destroy(); return false; }
    if (!pickPhysical()) { destroy(); return false; }
    if (!createDevice()) { destroy(); return false; }
    if (!createSwapchain()) { destroy(); return false; }
    if (!createSwapchainPass()) { vkLog("交换链通道建立失败"); destroy(); return false; }
    if (!createCommands()) { vkLog("命令缓冲建立失败"); destroy(); return false; }
    vkLog("上下文就绪");
    return true;
}

void waitIdle() {
    if (g_ctx.device) vkDeviceWaitIdle(g_ctx.device);
}

static void destroySwapchainResources() {
    for (size_t i = 0; i < g_ctx.swapFramebuffers.size(); ++i)
        if (g_ctx.swapFramebuffers[i]) vkDestroyFramebuffer(g_ctx.device, g_ctx.swapFramebuffers[i], nullptr);
    g_ctx.swapFramebuffers.clear();
    if (g_ctx.swapRenderPass) {
        vkDestroyRenderPass(g_ctx.device, g_ctx.swapRenderPass, nullptr);
        g_ctx.swapRenderPass = VK_NULL_HANDLE;
    }
    if (g_ctx.defaultDepthView) { vkDestroyImageView(g_ctx.device, g_ctx.defaultDepthView, nullptr); g_ctx.defaultDepthView = VK_NULL_HANDLE; }
    if (g_ctx.defaultDepthImage) { vkDestroyImage(g_ctx.device, g_ctx.defaultDepthImage, nullptr); g_ctx.defaultDepthImage = VK_NULL_HANDLE; }
    if (g_ctx.defaultDepthMem) { vkFreeMemory(g_ctx.device, g_ctx.defaultDepthMem, nullptr); g_ctx.defaultDepthMem = VK_NULL_HANDLE; }
    for (size_t i = 0; i < g_ctx.swapViews.size(); ++i)
        if (g_ctx.swapViews[i]) vkDestroyImageView(g_ctx.device, g_ctx.swapViews[i], nullptr);
    g_ctx.swapViews.clear();
    g_ctx.swapImages.clear();
    for (size_t i = 0; i < g_ctx.renderFinished.size(); ++i)
        if (g_ctx.renderFinished[i]) vkDestroySemaphore(g_ctx.device, g_ctx.renderFinished[i], nullptr);
    g_ctx.renderFinished.clear();
    if (g_ctx.swapchain) {
        vkDestroySwapchainKHR(g_ctx.device, g_ctx.swapchain, nullptr);
        g_ctx.swapchain = VK_NULL_HANDLE;
    }
}

bool recreateSwapchain() {
    if (!g_ctx.device) return false;
    waitIdle();
    for (size_t i = 0; i < g_ctx.imgAvailable.size(); ++i)
        if (g_ctx.imgAvailable[i]) vkDestroySemaphore(g_ctx.device, g_ctx.imgAvailable[i], nullptr);
    for (size_t i = 0; i < g_ctx.inFlight.size(); ++i)
        if (g_ctx.inFlight[i]) vkDestroyFence(g_ctx.device, g_ctx.inFlight[i], nullptr);
    g_ctx.imgAvailable.clear();
    g_ctx.inFlight.clear();
    {
        VkSemaphoreCreateInfo sci{};
        sci.sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO;
        VkFenceCreateInfo fci{};
        fci.sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO;
        fci.flags = VK_FENCE_CREATE_SIGNALED_BIT;
        g_ctx.imgAvailable.assign(kMaxFramesInFlight, VK_NULL_HANDLE);
        g_ctx.inFlight.assign(kMaxFramesInFlight, VK_NULL_HANDLE);
        for (uint32_t i = 0; i < kMaxFramesInFlight; ++i) {
            vkCreateSemaphore(g_ctx.device, &sci, nullptr, &g_ctx.imgAvailable[i]);
            vkCreateFence(g_ctx.device, &fci, nullptr, &g_ctx.inFlight[i]);
        }
    }
    destroySwapchainResources();
    if (!createSwapchain()) return false;
    if (!createSwapchainPass()) return false;
    return true;
}

bool beginFrame() {
    if (!g_ctx.device) return false;
    vkWaitForFences(g_ctx.device, 1, &g_ctx.inFlight[g_ctx.frameIndex], VK_TRUE, UINT64_MAX);
    releaseDeferred();
    resetBufferOffsets();

    VkResult r = vkAcquireNextImageKHR(g_ctx.device, g_ctx.swapchain, UINT64_MAX,
                                       g_ctx.imgAvailable[g_ctx.frameIndex],
                                       VK_NULL_HANDLE, &g_ctx.imageIndex);
    if (r == VK_ERROR_OUT_OF_DATE_KHR) {
        waitIdle();
        recreateSwapchain();
        return false;
    }
    if (r != VK_SUCCESS && r != VK_SUBOPTIMAL_KHR) return false;
    vkResetFences(g_ctx.device, 1, &g_ctx.inFlight[g_ctx.frameIndex]);
    vkResetCommandBuffer(g_ctx.cmd[g_ctx.frameIndex], 0);

    VkCommandBufferBeginInfo bi{};
    bi.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
    bi.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
    vkBeginCommandBuffer(g_ctx.cmd[g_ctx.frameIndex], &bi);

    g_ctx.frameActive = true;
    return true;
}

bool endFrame() {
    if (!g_ctx.frameActive) return false;
    VkCommandBuffer cmd = g_ctx.cmd[g_ctx.frameIndex];
    if (g_ctx.swapPassActive) {
        vkCmdEndRenderPass(cmd);
        g_ctx.swapPassActive = false;
    }
    vkEndCommandBuffer(cmd);

    VkPipelineStageFlags waitStage = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
    VkSubmitInfo si{};
    si.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
    si.waitSemaphoreCount = 1;
    si.pWaitSemaphores = &g_ctx.imgAvailable[g_ctx.frameIndex];
    si.pWaitDstStageMask = &waitStage;
    si.commandBufferCount = 1;
    si.pCommandBuffers = &cmd;
    si.signalSemaphoreCount = 1;
    si.pSignalSemaphores = &g_ctx.renderFinished[g_ctx.imageIndex];
    vkQueueSubmit(g_ctx.graphicsQueue, 1, &si, g_ctx.inFlight[g_ctx.frameIndex]);

    VkPresentInfoKHR pi{};
    pi.sType = VK_STRUCTURE_TYPE_PRESENT_INFO_KHR;
    pi.waitSemaphoreCount = 1;
    pi.pWaitSemaphores = &g_ctx.renderFinished[g_ctx.imageIndex];
    pi.swapchainCount = 1;
    pi.pSwapchains = &g_ctx.swapchain;
    pi.pImageIndices = &g_ctx.imageIndex;
    VkResult r = vkQueuePresentKHR(g_ctx.presentQueue, &pi);
    if (r == VK_ERROR_OUT_OF_DATE_KHR || r == VK_SUBOPTIMAL_KHR) recreateSwapchain();

    g_ctx.frameIndex = (g_ctx.frameIndex + 1) % kMaxFramesInFlight;
    g_ctx.frameActive = false;
    return true;
}

struct DeferredItem {
    int kind = 0;
    uint64_t handle = 0;
    uint64_t frame = 0;
};
static std::vector<DeferredItem> g_deferred;
static uint64_t g_frameSerial = 0;

static void queueDeferred(int kind, uint64_t handle) {
    if (handle) g_deferred.push_back({ kind, handle, g_frameSerial });
}

void deferTexture(unsigned id) { queueDeferred(0, id); }
void deferBuffer(unsigned id) { queueDeferred(1, id); }
void deferFbo(unsigned id) { queueDeferred(2, id); }
void deferVkBuffer(VkBuffer raw) { queueDeferred(3, (uint64_t)(uintptr_t)raw); }
void deferVkPipeline(VkPipeline raw) { queueDeferred(4, (uint64_t)(uintptr_t)raw); }
void deferVkMemory(VkDeviceMemory raw) { queueDeferred(5, (uint64_t)(uintptr_t)raw); }
void deferRawFramebuffer(VkFramebuffer raw) { queueDeferred(6, (uint64_t)(uintptr_t)raw); }
void deferRawRenderPass(VkRenderPass raw) { queueDeferred(7, (uint64_t)(uintptr_t)raw); }
void deferRawImageView(VkImageView raw) { queueDeferred(8, (uint64_t)(uintptr_t)raw); }
void deferRawSampler(VkSampler raw) { queueDeferred(9, (uint64_t)(uintptr_t)raw); }
void deferRawImage(VkImage raw) { queueDeferred(10, (uint64_t)(uintptr_t)raw); }

static void releaseItem(const DeferredItem& it) {
    if (it.kind == 0) destroyTexture((unsigned)it.handle);
    else if (it.kind == 1) destroyBuffer((unsigned)it.handle);
    else if (it.kind == 2) destroyFbo((unsigned)it.handle);
    else if (it.kind == 3) vkDestroyBuffer(g_ctx.device, (VkBuffer)(uintptr_t)it.handle, nullptr);
    else if (it.kind == 4) vkDestroyPipeline(g_ctx.device, (VkPipeline)(uintptr_t)it.handle, nullptr);
    else if (it.kind == 5) vkFreeMemory(g_ctx.device, (VkDeviceMemory)(uintptr_t)it.handle, nullptr);
    else if (it.kind == 6) vkDestroyFramebuffer(g_ctx.device, (VkFramebuffer)(uintptr_t)it.handle, nullptr);
    else if (it.kind == 7) vkDestroyRenderPass(g_ctx.device, (VkRenderPass)(uintptr_t)it.handle, nullptr);
    else if (it.kind == 8) vkDestroyImageView(g_ctx.device, (VkImageView)(uintptr_t)it.handle, nullptr);
    else if (it.kind == 9) vkDestroySampler(g_ctx.device, (VkSampler)(uintptr_t)it.handle, nullptr);
    else if (it.kind == 10) vkDestroyImage(g_ctx.device, (VkImage)(uintptr_t)it.handle, nullptr);
}

void releaseDeferred() {
    ++g_frameSerial;
    for (size_t i = 0; i < g_deferred.size();) {
        if (g_frameSerial - g_deferred[i].frame <= (uint64_t)kMaxFramesInFlight) {
            ++i;
            continue;
        }
        DeferredItem it = g_deferred[i];
        g_deferred[i] = g_deferred.back();
        g_deferred.pop_back();
        releaseItem(it);
    }
}

void destroy() {
    if (g_ctx.device) {
        waitIdle();
        for (size_t i = 0; i < g_deferred.size(); ++i) releaseItem(g_deferred[i]);
        g_deferred.clear();
        for (size_t i = 0; i < g_ctx.inFlight.size(); ++i)
            if (g_ctx.inFlight[i]) vkDestroyFence(g_ctx.device, g_ctx.inFlight[i], nullptr);
        for (size_t i = 0; i < g_ctx.imgAvailable.size(); ++i)
            if (g_ctx.imgAvailable[i]) vkDestroySemaphore(g_ctx.device, g_ctx.imgAvailable[i], nullptr);
        g_ctx.inFlight.clear();
        g_ctx.imgAvailable.clear();
        destroySwapchainResources();
        if (g_ctx.commandPool) vkDestroyCommandPool(g_ctx.device, g_ctx.commandPool, nullptr);
        vkDestroyDevice(g_ctx.device, nullptr);
    }
    if (g_ctx.surface) vkDestroySurfaceKHR(g_ctx.instance, g_ctx.surface, nullptr);
    if (g_ctx.instance) vkDestroyInstance(g_ctx.instance, nullptr);
    g_ctx = VkContext{};
}

}
}
