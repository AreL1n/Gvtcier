#include "Gvtcier.h"
#include "json.hpp"
#include <cstdio>
#include <cstring>
#include <deque>

extern "C" const void* res_get(const char* name, int* outSize);

// Vulkan 着色器管理：加载 SPIR-V 二进制、创建管线、映射 uniform。
// 属于已封禁的 VK 后端（原因见 Vulkan后端.cpp 顶部说明）。
// 与 GL 后端不同，这里必须先编译出 .spv 产物（构建时由 Shader/SPIRV/build_vk.py 生成），
// 运行时通过 res_get("SHADER_n_SPV") 读取，资源名带 _SPV 后缀。
// uniform 的偏移信息来自构建期从 SPIR-V 反射生成的 json（shaders.json / spvhash.json）。
namespace rhi {
namespace vk {

static void loadMeta(VkProgram* p, const char* name);

namespace {

std::deque<VkShaderStage> g_stages;   // 已加载的着色器阶段对象
std::deque<VkProgram> g_programs;

bool g_jsonLoaded = false;
nlohmann::json g_json;
nlohmann::json g_hash;

void ensureJson() {
    if (g_jsonLoaded) return;
    g_jsonLoaded = true;
    int sz = 0;
    const char* data = (const char*)res_get("shaders_json", &sz);
    if (!data || sz <= 0) {
        printf("[vk] shaders_json missing\n");
        return;
    }
    try {
        g_json = nlohmann::json::parse(std::string(data, (size_t)sz));
        printf("[vk] shaders.json loaded, %u shaders\n", (unsigned)g_json.size());
    } catch (...) {
        printf("[vk] shaders.json parse failed\n");
    }
    int sz2 = 0;
    const char* data2 = (const char*)res_get("s2d_shaders_json", &sz2);
    if (data2 && sz2 > 0) {
        try {
            nlohmann::json j2 = nlohmann::json::parse(std::string(data2, (size_t)sz2));
            for (auto it = j2.begin(); it != j2.end(); ++it)
                if (g_json.find(it.key()) == g_json.end()) g_json[it.key()] = it.value();
            printf("[vk] s2d shaders merged, %u total\n", (unsigned)g_json.size());
        } catch (...) {
            printf("[vk] s2d shaders parse failed\n");
        }
    }
    int hz = 0;
    const char* hdata = (const char*)res_get("spvhash_json", &hz);
    if (hdata && hz > 0) {
        try {
            g_hash = nlohmann::json::parse(std::string(hdata, (size_t)hz));
        } catch (...) {
            printf("[vk] spvhash.json parse failed\n");
        }
    }
    int hz2 = 0;
    const char* hdata2 = (const char*)res_get("s2d_spvhash_json", &hz2);
    if (hdata2 && hz2 > 0) {
        try {
            nlohmann::json h2 = nlohmann::json::parse(std::string(hdata2, (size_t)hz2));
            for (auto it = h2.begin(); it != h2.end(); ++it)
                if (g_hash.find(it.key()) == g_hash.end()) g_hash[it.key()] = it.value();
        } catch (...) {
            printf("[vk] s2d spvhash parse failed\n");
        }
    }
}

std::string fnv1aHex(const void* data, size_t len) {
    const unsigned char* p = (const unsigned char*)data;
    uint64_t h = 0xcbf29ce484222325ull;
    for (size_t i = 0; i < len; ++i) {
        h ^= (uint64_t)p[i];
        h *= 0x100000001b3ull;
    }
    char buf[32];
    snprintf(buf, sizeof(buf), "%016llx", (unsigned long long)h);
    return buf;
}

std::string nameOfSpv(const std::vector<unsigned char>& spv) {
    if (spv.empty() || g_hash.empty()) return std::string();
    std::string h = fnv1aHex(spv.data(), spv.size());
    auto it = g_hash.find(h);
    if (it == g_hash.end()) return std::string();
    return it->get<std::string>();
}

VkShaderModule makeModule(const std::vector<unsigned char>& code) {
    if (code.empty()) return VK_NULL_HANDLE;
    VkShaderModuleCreateInfo ci{};
    ci.sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO;
    ci.codeSize = code.size();
    ci.pCode = (const uint32_t*)code.data();
    VkShaderModule m = VK_NULL_HANDLE;
    if (vkCreateShaderModule(ctx().device, &ci, nullptr, &m) != VK_SUCCESS) return VK_NULL_HANDLE;
    return m;
}

}

void shaderInit() {
    g_stages.clear();
    g_programs.clear();
    ensureJson();
}

void shaderShutdown() {
    for (size_t i = 0; i < g_programs.size(); ++i) {
        VkProgram& p = g_programs[i];
        if (p.vert) vkDestroyShaderModule(ctx().device, p.vert, nullptr);
        if (p.frag) vkDestroyShaderModule(ctx().device, p.frag, nullptr);
        if (p.comp) vkDestroyShaderModule(ctx().device, p.comp, nullptr);
        if (p.descPool) vkDestroyDescriptorPool(ctx().device, p.descPool, nullptr);
        if (p.uboPool) vkDestroyDescriptorPool(ctx().device, p.uboPool, nullptr);
        if (p.pipeLayout) vkDestroyPipelineLayout(ctx().device, p.pipeLayout, nullptr);
        if (p.setLayout) vkDestroyDescriptorSetLayout(ctx().device, p.setLayout, nullptr);
        if (p.uboLayout) vkDestroyDescriptorSetLayout(ctx().device, p.uboLayout, nullptr);
        if (p.ubo) vk::deferVkBuffer(p.ubo);
        if (p.uboMem) vk::deferVkMemory(p.uboMem);
    }
    g_programs.clear();
    g_stages.clear();
}

VkProgram* programOf(unsigned id) {
    if (id == 0 || id > g_programs.size()) return nullptr;
    return &g_programs[id - 1];
}

VkShaderStage* stageOf(unsigned id) {
    if (id == 0 || id > g_stages.size()) return nullptr;
    return &g_stages[id - 1];
}

unsigned createStage(int type) {
    VkShaderStage s;
    s.type = type;
    g_stages.push_back(s);
    return (unsigned)g_stages.size();
}

void setStageSpv(unsigned id, const void* data, size_t len) {
    VkShaderStage* s = stageOf(id);
    if (!s) return;
    if (!data || len == 0) { s->spv.clear(); return; }
    const unsigned char* p = (const unsigned char*)data;
    s->spv.assign(p, p + len);
}

unsigned createProgram() {
    g_programs.push_back(VkProgram{});
    return (unsigned)g_programs.size();
}

void attachShader(unsigned program, unsigned stage) {
    VkProgram* p = programOf(program);
    VkShaderStage* s = stageOf(stage);
    if (!p || !s) return;
    if (s->spv.empty()) {
        rlog::once((std::string("vk_empty_") + std::to_string(program) + "_" + std::to_string(s->type)).c_str(),
                   "[vk] attach prog=%u stage=%u: spv empty\n", program, s->type);
        return;
    }
    std::string n = nameOfSpv(s->spv);
    if (n.empty()) printf("[vk] attach stage %u: spv hash miss, %u bytes\n", s->type, (unsigned)s->spv.size());
    else loadMeta(p, n.c_str());
    rlog::once((std::string("vk_ok_") + std::to_string(program) + "_" + std::to_string(s->type)).c_str(),
               "[vk] attach prog=%u stage=%u spv=%u bytes name=%s\n",
               program, s->type, (unsigned)s->spv.size(), n.empty() ? "?" : n.c_str());
    if (s->type == 280) p->vert = makeModule(s->spv);
    else if (s->type == 281) p->frag = makeModule(s->spv);
    else if (s->type == 283) { p->comp = makeModule(s->spv); p->compute = true; }
}

void destroyProgram(unsigned program) {
    VkProgram* p = programOf(program);
    if (!p) return;
    if (p->vert) vkDestroyShaderModule(ctx().device, p->vert, nullptr);
    if (p->frag) vkDestroyShaderModule(ctx().device, p->frag, nullptr);
    if (p->comp) vkDestroyShaderModule(ctx().device, p->comp, nullptr);
    if (p->descPool) vkDestroyDescriptorPool(ctx().device, p->descPool, nullptr);
    if (p->uboPool) vkDestroyDescriptorPool(ctx().device, p->uboPool, nullptr);
    if (p->pipeLayout) vkDestroyPipelineLayout(ctx().device, p->pipeLayout, nullptr);
    if (p->setLayout) vkDestroyDescriptorSetLayout(ctx().device, p->setLayout, nullptr);
    if (p->uboLayout) vkDestroyDescriptorSetLayout(ctx().device, p->uboLayout, nullptr);
    if (p->ubo) vk::deferVkBuffer(p->ubo);
    if (p->uboMem) vk::deferVkMemory(p->uboMem);
    *p = VkProgram{};
}

void destroyStage(unsigned id) {
    VkShaderStage* s = stageOf(id);
    if (s) s->spv.clear();
}

static void loadMeta(VkProgram* p, const char* name) {
    if (!name) return;
    auto it = g_json.find(name);
    if (it == g_json.end()) return;
    const auto& j = *it;
    auto itSize = j.find("uboSize");
    if (itSize != j.end()) p->uboSize = itSize->get<uint32_t>();
    auto itUB = j.find("uboBinding");
    if (itUB != j.end()) p->uboBinding = itUB->get<int>();
    auto itU = j.find("uniforms");
    if (itU != j.end()) {
        for (const auto& u : *itU) {
            std::string nm = u.value("name", std::string());
            bool dup = false;
            for (size_t i = 0; i < p->members.size(); ++i)
                if (p->members[i].name == nm) { dup = true; break; }
            if (dup) continue;
            UniformMember m;
            m.name = nm;
            m.offset = u.value("offset", 0u);
            m.size = u.value("size", 0u);
            m.elem = u.value("elem", 0u);
            m.count = u.value("count", 1u);
            p->members.push_back(m);
        }
    }
    auto itS = j.find("samplers");
    if (itS != j.end()) {
        for (const auto& s : *itS) {
            uint32_t b = s.value("binding", 0u);
            bool dup = false;
            for (size_t i = 0; i < p->samplers.size(); ++i)
                if (p->samplers[i].binding == b) { dup = true; break; }
            if (dup) continue;
            SamplerBinding sb;
            sb.binding = b;
            sb.name = s.value("name", std::string());
            p->samplers.push_back(sb);
        }
    }
    auto itB = j.find("ssbos");
    if (itB != j.end()) {
        for (const auto& s : *itB) {
            uint32_t b = s.value("binding", 0u);
            bool dup = false;
            for (size_t i = 0; i < p->ssbos.size(); ++i)
                if (p->ssbos[i].binding == b) { dup = true; break; }
            if (dup) continue;
            SsboBinding sb;
            sb.binding = b;
            sb.name = s.value("name", std::string());
            p->ssbos.push_back(sb);
        }
    }
}

bool linkProgram(unsigned program, const std::string& nameKey) {
    VkProgram* p = programOf(program);
    if (!p) return false;
    if (!nameKey.empty()) loadMeta(p, nameKey.c_str());

    uint32_t stageFlags = p->compute ? VK_SHADER_STAGE_COMPUTE_BIT
                                     : (VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT);

    bool hasUbo = !p->members.empty();
    if (hasUbo && p->uboSize == 0) p->uboSize = 16;

    std::vector<VkDescriptorSetLayoutBinding> bindings;
    std::vector<VkDescriptorBindingFlags> bindFlags;
    for (size_t i = 0; i < p->ssbos.size(); ++i) {
        VkDescriptorSetLayoutBinding sb{};
        sb.binding = p->ssbos[i].binding;
        sb.descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
        sb.descriptorCount = 1;
        sb.stageFlags = stageFlags;
        bindings.push_back(sb);
        bindFlags.push_back(VK_DESCRIPTOR_BINDING_UPDATE_AFTER_BIND_BIT);
    }
    for (size_t i = 0; i < p->samplers.size(); ++i) {
        VkDescriptorSetLayoutBinding sb{};
        sb.binding = p->samplers[i].binding;
        sb.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
        sb.descriptorCount = 1;
        sb.stageFlags = VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT | VK_SHADER_STAGE_COMPUTE_BIT;
        bindings.push_back(sb);
        bindFlags.push_back(VK_DESCRIPTOR_BINDING_UPDATE_AFTER_BIND_BIT);
    }
    if (bindings.empty()) {
        VkDescriptorSetLayoutBinding sb{};
        sb.binding = 0u;
        sb.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
        sb.descriptorCount = 1;
        sb.stageFlags = stageFlags;
        bindings.push_back(sb);
        bindFlags.push_back(VK_DESCRIPTOR_BINDING_UPDATE_AFTER_BIND_BIT);
    }

    VkDescriptorSetLayoutBindingFlagsCreateInfo bf{};
    VkDescriptorSetLayoutCreateInfo sl{};
    sl.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
    bf.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_BINDING_FLAGS_CREATE_INFO;
    bf.bindingCount = (uint32_t)bindFlags.size();
    bf.pBindingFlags = bindFlags.data();
    sl.pNext = &bf;
    sl.flags = VK_DESCRIPTOR_SET_LAYOUT_CREATE_UPDATE_AFTER_BIND_POOL_BIT;
    sl.bindingCount = (uint32_t)bindings.size();
    sl.pBindings = bindings.data();
    if (vkCreateDescriptorSetLayout(ctx().device, &sl, nullptr, &p->setLayout) != VK_SUCCESS) return false;

    if (hasUbo) {
        VkDescriptorSetLayoutBinding ub{};
        ub.binding = (p->uboBinding >= 0) ? (uint32_t)p->uboBinding : 0u;
        ub.descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER_DYNAMIC;
        ub.descriptorCount = 1;
        ub.stageFlags = stageFlags;
        VkDescriptorSetLayoutCreateInfo usl{};
        usl.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
        usl.bindingCount = 1;
        usl.pBindings = &ub;
        if (vkCreateDescriptorSetLayout(ctx().device, &usl, nullptr, &p->uboLayout) != VK_SUCCESS) return false;
    }

    VkDescriptorSetLayout layouts[2] = { p->setLayout, p->uboLayout };
    VkPipelineLayoutCreateInfo pl{};
    pl.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
    pl.setLayoutCount = hasUbo ? 2 : 1;
    pl.pSetLayouts = layouts;
    if (vkCreatePipelineLayout(ctx().device, &pl, nullptr, &p->pipeLayout) != VK_SUCCESS) return false;

    if (hasUbo) {
        VkPhysicalDeviceProperties props{};
        vkGetPhysicalDeviceProperties(ctx().physical, &props);
        uint32_t alignMask = (uint32_t)props.limits.minUniformBufferOffsetAlignment;
        if (alignMask < 1) alignMask = 1;
        p->uboSlotStride = (p->uboSize + alignMask - 1) / alignMask * alignMask;
        p->uboSlotCount = (1u << 20) / p->uboSlotStride;
        if (p->uboSlotCount < 64) p->uboSlotCount = 64;
        if (p->uboSlotCount > 4096) p->uboSlotCount = 4096;
        p->uboWriteSlot = 0;
        p->uboDrawSlot = 0;
        p->uboSlotUsed = false;

        VkBufferCreateInfo bi{};
        bi.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
        bi.size = (VkDeviceSize)p->uboSlotStride * p->uboSlotCount;
        bi.usage = VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT;
        bi.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
        if (vkCreateBuffer(ctx().device, &bi, nullptr, &p->ubo) != VK_SUCCESS) return false;

        VkMemoryRequirements req{};
        vkGetBufferMemoryRequirements(ctx().device, p->ubo, &req);
        VkMemoryAllocateInfo ai{};
        ai.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
        ai.allocationSize = req.size;
        ai.memoryTypeIndex = findMemoryType(req.memoryTypeBits, VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT |
                                                                   VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);
        if (vkAllocateMemory(ctx().device, &ai, nullptr, &p->uboMem) != VK_SUCCESS) return false;
        vkBindBufferMemory(ctx().device, p->ubo, p->uboMem, 0);
        vkMapMemory(ctx().device, p->uboMem, 0, bi.size, 0, &p->uboMapped);
        memset(p->uboMapped, 0, (size_t)bi.size);
    }

    std::vector<VkDescriptorPoolSize> sizes;
    if (!p->ssbos.empty()) sizes.push_back({ VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, (uint32_t)p->ssbos.size() });
    if (!p->samplers.empty()) sizes.push_back({ VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, (uint32_t)p->samplers.size() });
    if (sizes.empty()) sizes.push_back({ VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 1 });
    VkDescriptorPoolCreateInfo dpi{};
    dpi.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
    dpi.flags = VK_DESCRIPTOR_POOL_CREATE_UPDATE_AFTER_BIND_BIT;
    dpi.maxSets = 1;
    dpi.poolSizeCount = (uint32_t)sizes.size();
    dpi.pPoolSizes = sizes.data();
    if (vkCreateDescriptorPool(ctx().device, &dpi, nullptr, &p->descPool) != VK_SUCCESS) return false;

    VkDescriptorSetAllocateInfo dsa{};
    dsa.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
    dsa.descriptorPool = p->descPool;
    dsa.descriptorSetCount = 1;
    dsa.pSetLayouts = &p->setLayout;
    if (vkAllocateDescriptorSets(ctx().device, &dsa, &p->descSet) != VK_SUCCESS) return false;

    if (p->uboLayout) {
        VkDescriptorPoolSize us{ VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER_DYNAMIC, 1 };
        VkDescriptorPoolCreateInfo upi{};
        upi.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
        upi.maxSets = 1;
        upi.poolSizeCount = 1;
        upi.pPoolSizes = &us;
        if (vkCreateDescriptorPool(ctx().device, &upi, nullptr, &p->uboPool) != VK_SUCCESS) return false;
        VkDescriptorSetAllocateInfo ua{};
        ua.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
        ua.descriptorPool = p->uboPool;
        ua.descriptorSetCount = 1;
        ua.pSetLayouts = &p->uboLayout;
        if (vkAllocateDescriptorSets(ctx().device, &ua, &p->uboSet) != VK_SUCCESS) return false;

        VkDescriptorBufferInfo bi{};
        bi.buffer = p->ubo;
        bi.offset = 0;
        bi.range = p->uboSlotStride;
        VkWriteDescriptorSet w0{};
        w0.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        w0.dstSet = p->uboSet;
        w0.dstBinding = (p->uboBinding >= 0) ? (uint32_t)p->uboBinding : 0u;
        w0.descriptorCount = 1;
        w0.descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER_DYNAMIC;
        w0.pBufferInfo = &bi;
        vkUpdateDescriptorSets(ctx().device, 1, &w0, 0, nullptr);
    }

    p->descDirty = true;
    updateDescriptors(program, nullptr, 0);
    return true;
}

void updateDescriptors(unsigned program, const unsigned* textureIds, int count) {
    VkProgram* p = programOf(program);
    if (!p || !p->descSet) return;

    bool same = !p->descDirty;
    if (same) {
        for (int i = 0; i < 16; ++i) {
            unsigned want = textureIds ? textureIds[i] : 0u;
            if (p->lastTex[i] != want) { same = false; break; }
        }
    }
    if (same) return;
    for (int i = 0; i < 16; ++i)
        p->lastTex[i] = textureIds ? textureIds[i] : 0u;
    p->descDirty = false;

    std::vector<VkWriteDescriptorSet> writes;
    std::vector<VkDescriptorBufferInfo> sbis(p->ssbos.size());
    for (size_t i = 0; i < p->ssbos.size(); ++i) {
        bool useDummy = !p->ssbos[i].buffer;
        sbis[i].buffer = useDummy ? dummyBuffer() : p->ssbos[i].buffer;
        if (useDummy) { sbis[i].offset = 0; sbis[i].range = VK_WHOLE_SIZE; }
        else {
            sbis[i].offset = p->ssbos[i].offset;
            sbis[i].range = p->ssbos[i].range ? p->ssbos[i].range : VK_WHOLE_SIZE;
        }
        VkWriteDescriptorSet w{};
        w.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        w.dstSet = p->descSet;
        w.dstBinding = p->ssbos[i].binding;
        w.descriptorCount = 1;
        w.descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
        w.pBufferInfo = &sbis[i];
        writes.push_back(w);
    }
    std::vector<VkDescriptorImageInfo> imgs(p->samplers.size());
    for (size_t i = 0; i < p->samplers.size(); ++i) {
        int unit = p->samplers[i].unit;
        if (unit < 0) unit = (int)p->samplers[i].binding - 10;
        if (unit < 0) unit = (int)i;
        unsigned texId = (textureIds && unit < count) ? textureIds[unit] : 0;
        if (!textureReady(texId)) texId = dummyTexture();
        if (!textureReady(texId)) continue;
        VkTextureObj* to = textureAt(texId);
        if (to && to->sampled) ensureImageLayout(to, VK_IMAGE_LAYOUT_GENERAL);
        imgs[i].imageLayout = VK_IMAGE_LAYOUT_GENERAL;
        imgs[i].imageView = imageViewOf(texId);
        imgs[i].sampler = samplerOf(texId);
        if (!imgs[i].imageView || !imgs[i].sampler) continue;
        VkWriteDescriptorSet w{};
        w.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        w.dstSet = p->descSet;
        w.dstBinding = p->samplers[i].binding;
        w.descriptorCount = 1;
        w.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
        w.pImageInfo = &imgs[i];
        writes.push_back(w);
    }
    if (!writes.empty())
        vkUpdateDescriptorSets(ctx().device, (uint32_t)writes.size(), writes.data(), 0, nullptr);
}

bool bindStorageBuffer(unsigned program, unsigned binding, VkBuffer buffer, uint32_t offset, uint32_t range) {
    VkProgram* p = programOf(program);
    if (!p || !p->descSet || !buffer) return false;
    bool declared = false;
    for (size_t i = 0; i < p->ssbos.size(); ++i) {
        if (p->ssbos[i].binding == binding) { declared = true; break; }
    }
    if (!declared) return false;
    VkDescriptorBufferInfo sbi{};
    sbi.buffer = buffer;
    sbi.offset = offset;
    sbi.range = range ? range : VK_WHOLE_SIZE;
    VkWriteDescriptorSet w{};
    w.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
    w.dstSet = p->descSet;
    w.dstBinding = binding;
    w.descriptorCount = 1;
    w.descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
    w.pBufferInfo = &sbi;
    vkUpdateDescriptorSets(ctx().device, 1, &w, 0, nullptr);
    return true;
}

void invalidateDescriptors(unsigned program) {
    VkProgram* p = programOf(program);
    if (p) p->descDirty = true;
}

void setSamplerUnitByOffset(unsigned program, int offset, int unit) {
    VkProgram* p = programOf(program);
    if (!p || offset < 0) return;
    for (size_t i = 0; i < p->members.size(); ++i) {
        if ((int)p->members[i].offset == offset) {
            for (size_t k = 0; k < p->samplers.size(); ++k) {
                if (p->samplers[k].name == p->members[i].name) {
                    if (p->samplers[k].unit != unit) {
                        p->samplers[k].unit = unit;
                        p->descDirty = true;
                    }
                    return;
                }
            }
            return;
        }
    }
}

VkDescriptorSet descriptorSetOf(unsigned program) {
    VkProgram* p = programOf(program);
    return p ? p->descSet : VK_NULL_HANDLE;
}

VkDescriptorSet uboSetOf(unsigned program) {
    VkProgram* p = programOf(program);
    return p ? p->uboSet : VK_NULL_HANDLE;
}

VkPipelineLayout pipelineLayoutOf(unsigned program) {
    VkProgram* p = programOf(program);
    return p ? p->pipeLayout : VK_NULL_HANDLE;
}

void setProgramName(unsigned program, const std::string& nameKey) {
    VkProgram* p = programOf(program);
    if (p) loadMeta(p, nameKey.c_str());
}

int uniformOffset(unsigned program, const char* name) {
    VkProgram* p = programOf(program);
    if (!p || !name) return -1;
    for (size_t i = 0; i < p->members.size(); ++i) {
        const std::string& n = p->members[i].name;
        if (n == name) return (int)p->members[i].offset;
        size_t len = n.size();
        if (len < strlen(name) && name[len] == '[') {
            bool pre = true;
            for (size_t k = 0; k < len; ++k) {
                if (n[k] != name[k]) { pre = false; break; }
            }
            if (!pre) continue;
            const char* q = name + len + 1;
            if (*q < '0' || *q > '9') continue;
            int idx = 0;
            while (*q >= '0' && *q <= '9') { idx = idx * 10 + (*q - '0'); ++q; }
            if (*q != ']' || q[1] != '\0') continue;
            if (p->members[i].count <= 1 || p->members[i].elem == 0) continue;
            if (idx < 0 || (uint32_t)idx >= p->members[i].count) continue;
            return (int)(p->members[i].offset + (uint32_t)idx * p->members[i].elem);
        }
    }
    return -1;
}

void advanceUniformSlot(unsigned program) {
    VkProgram* p = programOf(program);
    if (!p || !p->ubo) return;
    if (p->uboSlotUsed) {
        p->uboWriteSlot = (p->uboWriteSlot + 1) % p->uboSlotCount;
        p->uboSlotUsed = false;
    }
}

bool programHasUbo(unsigned program) {
    VkProgram* p = programOf(program);
    return p && p->ubo != VK_NULL_HANDLE && p->uboSlotStride > 0;
}

uint32_t uniformSlotOffset(unsigned program) {
    VkProgram* p = programOf(program);
    if (!p || !p->ubo) return 0;
    p->uboDrawSlot = p->uboWriteSlot;
    p->uboSlotUsed = true;
    return p->uboDrawSlot * p->uboSlotStride;
}

void writeUniform(unsigned program, int offset, const void* data, size_t size) {
    VkProgram* p = programOf(program);
    if (!p || !p->uboMapped || offset < 0) return;
    if ((uint32_t)offset + size > p->uboSize) return;
    advanceUniformSlot(program);
    unsigned char* base = (unsigned char*)p->uboMapped + (size_t)p->uboWriteSlot * p->uboSlotStride;
    memcpy(base + offset, data, size);
}

void writeUniformMat4(unsigned program, int offset, const float* m) {
    writeUniform(program, offset, m, 64);
}

}
}
