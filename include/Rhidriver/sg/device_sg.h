#pragma once

#include "Rhidriver/rhi.h"
#include "Rhidriver/sg/rhi_sg.h"

namespace rhi {
namespace sg {

// sokol 只管怎么发绘制命令，Vulkan 的实例、设备、交换链得由我们自己的
// vk 上下文提供，这里把它包装成 sokol 要的形状。
struct Device {
    sg_environment env{};
    sg_swapchain swapchain{};
    void* hwnd = nullptr;
    bool ready = false;
    bool vsync = true;
    int width = 0;
    int height = 0;
};

Device& Dev();

// hwnd 是交换链要挂的那个窗口，没有它建不出 surface
bool CreateDevice(void* hwnd, int width, int height);
void DestroyDevice();
void BeginFrame();
void EndFrame();

}
}
