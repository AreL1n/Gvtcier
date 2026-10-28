#include "Rhidriver/sg/device_sg.h"
#include "Rhidriver/sg/mapping_sg.h"
#include "Rhidriver/vk/vk.h"

namespace rhi {
namespace sg {

Device& Dev() {
    static Device d;
    return d;
}

// sokol 要的 Vulkan 句柄全从我们自己的 vk 上下文拿，
// 这样设备层不用重写，sokol 只是接在这套上下文上面的绘制通道。
static void FillVulkanEnvironment(sg_environment& env) {
    vk::VkContext& c = vk::ctx();
    env.vulkan.instance = (const void*)c.instance;
    env.vulkan.physical_device = (const void*)c.physical;
    env.vulkan.device = (const void*)c.device;
    env.vulkan.queue = (const void*)c.graphicsQueue;
    env.vulkan.queue_family_index = c.graphicsFamily;
}

bool CreateDevice(void* hwnd, int width, int height) {
    Device& d = Dev();
    if (d.ready) return true;

    // 交换链要挂到真实窗口上，句柄没了连 surface 都建不出来
    LogLine("[sg] 开始建 Vulkan 上下文 hwnd=%p", hwnd);
    if (!vk::create((HWND)hwnd)) {
        LogLine("[sg] Vulkan 上下文创建失败");
        return false;
    }
    d.hwnd = hwnd;

    FillVulkanEnvironment(d.env);

    sg_desc desc{};
    desc.environment = d.env;
    // sokol 的 panic 是 log_level 0，之前只放行 >=2 等于把最致命的那条吞了。
    // 它 panic 之后是直接 abort()，日志里必须留住这一句
    desc.logger.func = [](const char* tag, uint32_t log_level, uint32_t log_item, const char* message,
                          uint32_t line, const char* file, void*) {
        LogLine("[sokol:%s] lv=%u item=%u %s:%u %s",
                tag ? tag : "?", log_level, log_item,
                file ? file : "?", line, message ? message : "(无描述)");
    };
    // sokol 这一步会去要描述符缓冲那一整组扩展函数，缺一个就直接 abort，
    // 所以进来之前先把话说清楚，崩了也好定位
    LogLine("[sg] 调用 sg_setup，需要在 descriptor_buffer 扩展上通过");
    sg_setup(&desc);
    if (!sg_isvalid()) {
        LogLine("[sg] sokol_gfx 初始化失败");
        vk::destroy();
        return false;
    }
    LogLine("[sg] sokol_gfx 就绪");

    d.width = width;
    d.height = height;
    d.ready = true;
    return true;
}

void DestroyDevice() {
    Device& d = Dev();
    if (!d.ready) return;
    // sokol 放资源的时候 GPU 可能还在跑上一帧，先让队列停下来再动它手里的句柄
    vk::waitIdle();
    sg_shutdown();
    // sokol 走干净了才轮到 vk 收尾，顺序反了就是拿野句柄去销毁，退出时必崩
    vk::destroy();
    d.ready = false;
}

// 每帧开始前把当前交换链的图与信号量交给 sokol，它才知道往哪画
void BeginFrame() {
    Device& d = Dev();
    if (!d.ready) return;
    vk::VkContext& c = vk::ctx();
    if (!vk::beginFrame()) return;

    d.swapchain = sg_swapchain{};
    d.swapchain.width = (int)c.swapExtent.width;
    d.swapchain.height = (int)c.swapExtent.height;
    d.swapchain.sample_count = 1;
    d.swapchain.color_format = ToSwapchainColor();
    d.swapchain.depth_format = ToSwapchainDepth();
    d.swapchain.vulkan.render_image = (const void*)c.swapImages[c.imageIndex];
    d.swapchain.vulkan.render_view = (const void*)c.swapViews[c.imageIndex];
    d.swapchain.vulkan.depth_stencil_image = (const void*)c.defaultDepthImage;
    d.swapchain.vulkan.depth_stencil_view = (const void*)c.defaultDepthView;
    // 信号量索引跟引擎保持一致：等待按帧序号，示完成按图像序号
    d.swapchain.vulkan.render_finished_semaphore = (const void*)c.renderFinished[c.imageIndex];
    d.swapchain.vulkan.present_complete_semaphore = (const void*)c.imgAvailable[c.frameIndex];
    d.width = (int)c.swapExtent.width;
    d.height = (int)c.swapExtent.height;
}

void EndFrame() {
    Device& d = Dev();
    if (!d.ready) return;
    sg_commit();
    vk::endFrame();
}

}
}
