#pragma once
#include "Rhidriver/vk/vk.h"
#include <string>
#include <unordered_map>
#include <vector>

namespace rhi {
namespace vk {

struct UniformMember {
    std::string name;
    uint32_t offset = 0;
    uint32_t size = 0;
    uint32_t elem = 0;
    uint32_t count = 1;
};

struct SamplerBinding {
    uint32_t binding = 0;
    std::string name;
    int unit = -1;
};

struct SsboBinding {
    uint32_t binding = 0;
    std::string name;
    VkBuffer buffer = VK_NULL_HANDLE;
    uint32_t offset = 0;
    uint32_t range = 0;
};

struct VkProgram {
    VkShaderModule vert = VK_NULL_HANDLE;
    VkShaderModule frag = VK_NULL_HANDLE;
    VkShaderModule comp = VK_NULL_HANDLE;
    VkDescriptorSetLayout setLayout = VK_NULL_HANDLE;
    VkDescriptorSetLayout uboLayout = VK_NULL_HANDLE;
    VkPipelineLayout pipeLayout = VK_NULL_HANDLE;
    VkBuffer ubo = VK_NULL_HANDLE;
    VkDeviceMemory uboMem = VK_NULL_HANDLE;
    void* uboMapped = nullptr;
    uint32_t uboSize = 0;
    uint32_t uboSlotStride = 0;
    uint32_t uboSlotCount = 1;
    uint32_t uboWriteSlot = 0;
    uint32_t uboDrawSlot = 0;
    bool uboSlotUsed = false;
    int uboBinding = -1;
    VkDescriptorPool descPool = VK_NULL_HANDLE;
    VkDescriptorSet descSet = VK_NULL_HANDLE;
    VkDescriptorPool uboPool = VK_NULL_HANDLE;
    VkDescriptorSet uboSet = VK_NULL_HANDLE;
    std::vector<UniformMember> members;
    std::vector<SamplerBinding> samplers;
    std::vector<SsboBinding> ssbos;
    unsigned lastTex[16] = {};
    bool descDirty = true;
    bool compute = false;
};

struct VkShaderStage {
    int type = 0;
    std::vector<unsigned char> spv;
};

void shaderInit();
void shaderShutdown();
VkProgram* programOf(unsigned id);
VkShaderStage* stageOf(unsigned id);

unsigned createStage(int type);
void setStageSpv(unsigned id, const void* data, size_t len);
unsigned createProgram();
void attachShader(unsigned program, unsigned stage);
bool linkProgram(unsigned program, const std::string& nameKey);
void setProgramName(unsigned program, const std::string& nameKey);
void destroyProgram(unsigned program);
void destroyStage(unsigned id);

int uniformOffset(unsigned program, const char* name);
void writeUniform(unsigned program, int offset, const void* data, size_t size);
void writeUniformMat4(unsigned program, int offset, const float* m);
void advanceUniformSlot(unsigned program);
bool programHasUbo(unsigned program);
uint32_t uniformSlotOffset(unsigned program);

void updateDescriptors(unsigned program, const unsigned* textureIds, int count);
bool bindStorageBuffer(unsigned program, unsigned binding, VkBuffer buffer, uint32_t offset, uint32_t range);
void invalidateDescriptors(unsigned program);
void setSamplerUnitByOffset(unsigned program, int offset, int unit);
VkDescriptorSet descriptorSetOf(unsigned program);
VkDescriptorSet uboSetOf(unsigned program);
VkPipelineLayout pipelineLayoutOf(unsigned program);

}
}
