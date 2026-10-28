// sokol_gfx 是单头文件库，必须有一个翻译单元负责把实现体实例化出来。
// Vulkan 后端要能看到 vulkan 的类型与函数，所以先包含 SDK 的头再开实现。
#define SOKOL_IMPL
#define SOKOL_VULKAN
#include <vulkan/vulkan.h>
#include "sokol_gfx.h"
