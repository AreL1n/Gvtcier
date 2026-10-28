#include "Gvtcier.h"
#include <windows.h>
#include <cstdio>
#include <cstring>
#include <string>
#include <unordered_map>
#include <vector>

// Vulkan 渲染后端（RHI 的第二种实现）。
//
// 当前状态：**已封禁**。存在未定位的内存泄漏（运行中显存持续上涨，
// 曾观察到每秒约 1GB、涨满后画面变红），因此 渲染抽象.cpp 里强制回退到 GL，
// 设置环境变量 GVTCIER_RHI=vk 也只会打印一行提示然后继续走 GL。
//
// 保留这份代码是为了将来修复后重新启用。它实现了与 GL 后端相同的 RHI_TABLE 接口，
// 上层渲染器不需要改动即可切换后端。
// 已知可疑点：BufferData 的缓冲复用策略、每帧重建资源的时机。
namespace rhi {

namespace {

using namespace vk;

// 管线状态缓存：只在状态变化时才重建 VkPipeline，避免每帧重建
struct PipeState {
    unsigned depthFunc = 123;
    bool depthTest = true;
    bool depthMask = true;
    unsigned cullMode = 112;
    unsigned frontFace = 113;
    bool blend = false;
    unsigned blendS = 142;
    unsigned blendD = 143;
    unsigned polygonMode = 132;
    float polyFactor = 0.0f;
    float polyUnit = 0.0f;
    unsigned topology = 154;
};

PipeState g_state;
unsigned g_program = 0;
unsigned g_fbo = 0;
int g_activeUnit = 0;
unsigned g_texUnit[16] = {};
bool g_offscreen = false;

uint64_t hashCombine(uint64_t h, uint64_t v) {
    h ^= v + 0x9e3779b97f4a7c15ULL + (h << 6) + (h >> 2);
    return h;
}

VkPrimitiveTopology toTopology(unsigned mode) {
    switch (mode) {
        case 150: return VK_PRIMITIVE_TOPOLOGY_POINT_LIST;
        case 151: return VK_PRIMITIVE_TOPOLOGY_LINE_LIST;
        case 153: return VK_PRIMITIVE_TOPOLOGY_LINE_STRIP;
        case 155: return VK_PRIMITIVE_TOPOLOGY_TRIANGLE_STRIP;
        case 156: return VK_PRIMITIVE_TOPOLOGY_TRIANGLE_FAN;
        default: return VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
    }
}

VkCompareOp toCompareOp(unsigned fn) {
    switch (fn) {
        case 120: return VK_COMPARE_OP_NEVER;
        case 121: return VK_COMPARE_OP_LESS;
        case 122: return VK_COMPARE_OP_EQUAL;
        case 123: return VK_COMPARE_OP_LESS_OR_EQUAL;
        case 124: return VK_COMPARE_OP_GREATER;
        case 125: return VK_COMPARE_OP_NOT_EQUAL;
        case 126: return VK_COMPARE_OP_GREATER_OR_EQUAL;
        case 127: return VK_COMPARE_OP_ALWAYS;
    }
    return VK_COMPARE_OP_LESS_OR_EQUAL;
}

VkBlendFactor toBlend(unsigned v) {
    switch (v) {
        case 140: return VK_BLEND_FACTOR_ZERO;
        case 141: return VK_BLEND_FACTOR_ONE;
        case 142: return VK_BLEND_FACTOR_SRC_ALPHA;
        case 143: return VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA;
        case 144: return VK_BLEND_FACTOR_SRC_COLOR;
        case 145: return VK_BLEND_FACTOR_ONE_MINUS_SRC_COLOR;
        case 146: return VK_BLEND_FACTOR_DST_ALPHA;
        case 147: return VK_BLEND_FACTOR_ONE_MINUS_DST_ALPHA;
    }
    return VK_BLEND_FACTOR_ONE;
}

uint32_t attribComponentSize(unsigned type) {
    switch (type) {
        case 200: case 201: return 1;
        case 202: case 203: case 208: return 2;
        case 204: case 205: case 206: return 4;
    }
    return 4;
}

VkVertexInputAttributeDescription toAttribDesc(const VkAttrib& a, uint32_t binding) {
    VkVertexInputAttributeDescription d{};
    d.location = a.index;
    d.binding = binding;
    d.offset = (uint32_t)a.offset;
    int comps = a.size;
    uint32_t cs = attribComponentSize(a.type);
    switch (comps) {
        case 1: d.format = cs == 1 ? VK_FORMAT_R8_UNORM : (cs == 2 ? VK_FORMAT_R16_UNORM : VK_FORMAT_R32_SFLOAT); break;
        case 2: d.format = cs == 1 ? VK_FORMAT_R8G8_UNORM : (cs == 2 ? VK_FORMAT_R16G16_UNORM : VK_FORMAT_R32G32_SFLOAT); break;
        case 3: d.format = cs == 1 ? VK_FORMAT_R8G8B8_UNORM : (cs == 2 ? VK_FORMAT_R16G16B16_UNORM : VK_FORMAT_R32G32B32_SFLOAT); break;
        default: d.format = cs == 1 ? VK_FORMAT_R8G8B8A8_UNORM : (cs == 2 ? VK_FORMAT_R16G16B16A16_UNORM : VK_FORMAT_R32G32B32A32_SFLOAT); break;
    }
    return d;
}

std::unordered_map<uint64_t, VkPipeline> g_pipelines;
VkCommandBuffer g_cmd = VK_NULL_HANDLE;

VkRenderPass currentRenderPass() {
    if (g_offscreen) {
        VkFboObj* f = fboAt(g_fbo);
        if (f && f->renderPass) return f->renderPass;
    }
    return ctx().swapRenderPass;
}

unsigned colorAttachmentCount(VkRenderPass rp) {
    if (rp && rp == ctx().swapRenderPass) return 1;
    VkFboObj* f = fboAt(g_fbo);
    if (f && rp == f->renderPass && !f->colors.empty()) return (unsigned)f->colors.size();
    return 1;
}

VkPipeline makePipeline(unsigned program, VkRenderPass rp) {
    VkProgram* p = programOf(program);
    if (!p || !p->pipeLayout) return VK_NULL_HANDLE;

    VkPipelineShaderStageCreateInfo stages[2]{};
    uint32_t stageCount = 0;
    if (p->vert) {
        stages[stageCount].sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
        stages[stageCount].stage = VK_SHADER_STAGE_VERTEX_BIT;
        stages[stageCount].module = p->vert;
        stages[stageCount].pName = "main";
        ++stageCount;
    }
    if (p->frag) {
        stages[stageCount].sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
        stages[stageCount].stage = VK_SHADER_STAGE_FRAGMENT_BIT;
        stages[stageCount].module = p->frag;
        stages[stageCount].pName = "main";
        ++stageCount;
    }

    std::vector<VkVertexInputAttributeDescription> attrs;
    std::vector<VkVertexInputBindingDescription> bindings;
    VkVaoObj* v = vaoAt(boundVaoId());
    if (v) {
        int vtxStride = 0;
        std::vector<int> instStrides;
        for (size_t i = 0; i < v->attribs.size(); ++i) {
            const VkAttrib& a = v->attribs[i];
            uint32_t slot;
            if (a.divisor == 0) {
                slot = 0;
                if (a.stride > vtxStride) vtxStride = a.stride;
            } else {
                int found = -1;
                for (size_t k = 0; k < instStrides.size(); ++k)
                    if (instStrides[k] == a.stride) { found = (int)k; break; }
                if (found < 0) { instStrides.push_back(a.stride); found = (int)instStrides.size() - 1; }
                slot = (uint32_t)(1 + found);
            }
            attrs.push_back(toAttribDesc(a, slot));
        }
        if (!attrs.empty() && vtxStride > 0) {
            VkVertexInputBindingDescription b{};
            b.binding = 0;
            b.stride = (uint32_t)vtxStride;
            b.inputRate = VK_VERTEX_INPUT_RATE_VERTEX;
            bindings.push_back(b);
        }
        for (size_t k = 0; k < instStrides.size(); ++k) {
            VkVertexInputBindingDescription b{};
            b.binding = (uint32_t)(1 + k);
            b.stride = (uint32_t)instStrides[k];
            b.inputRate = VK_VERTEX_INPUT_RATE_INSTANCE;
            bindings.push_back(b);
        }
    }

    VkPipelineVertexInputStateCreateInfo vi{};
    vi.sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO;
    vi.vertexBindingDescriptionCount = (uint32_t)bindings.size();
    vi.pVertexBindingDescriptions = bindings.empty() ? nullptr : bindings.data();
    vi.vertexAttributeDescriptionCount = (uint32_t)attrs.size();
    vi.pVertexAttributeDescriptions = attrs.empty() ? nullptr : attrs.data();

    VkPipelineInputAssemblyStateCreateInfo ia{};
    ia.sType = VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO;
    ia.topology = toTopology(g_state.topology);

    VkPipelineViewportStateCreateInfo vp{};
    vp.sType = VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO;
    vp.viewportCount = 1;
    vp.scissorCount = 1;

    VkPipelineRasterizationStateCreateInfo rs{};
    rs.sType = VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO;
    rs.polygonMode = (g_state.polygonMode == 131) ? VK_POLYGON_MODE_LINE : VK_POLYGON_MODE_FILL;
    rs.cullMode = (g_state.cullMode == 111) ? VK_CULL_MODE_FRONT_BIT
                                            : (g_state.cullMode == 112 ? VK_CULL_MODE_BACK_BIT : VK_CULL_MODE_NONE);
    rs.frontFace = (g_state.frontFace == 114) ? VK_FRONT_FACE_CLOCKWISE : VK_FRONT_FACE_COUNTER_CLOCKWISE;
    rs.lineWidth = 1.0f;
    rs.depthBiasEnable = (g_state.polyFactor != 0.0f || g_state.polyUnit != 0.0f) ? VK_TRUE : VK_FALSE;
    rs.depthBiasSlopeFactor = g_state.polyFactor;
    rs.depthBiasConstantFactor = g_state.polyUnit;

    VkPipelineMultisampleStateCreateInfo ms{};
    ms.sType = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO;
    ms.rasterizationSamples = VK_SAMPLE_COUNT_1_BIT;

    VkPipelineDepthStencilStateCreateInfo ds{};
    ds.sType = VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO;
    ds.depthTestEnable = g_state.depthTest ? VK_TRUE : VK_FALSE;
    ds.depthWriteEnable = g_state.depthMask ? VK_TRUE : VK_FALSE;
    ds.depthCompareOp = toCompareOp(g_state.depthFunc);

    VkPipelineColorBlendAttachmentState cb{};
    cb.colorWriteMask = VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT |
                        VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT;
    cb.blendEnable = g_state.blend ? VK_TRUE : VK_FALSE;
    cb.srcColorBlendFactor = toBlend(g_state.blendS);
    cb.dstColorBlendFactor = toBlend(g_state.blendD);
    cb.colorBlendOp = VK_BLEND_OP_ADD;
    cb.srcAlphaBlendFactor = toBlend(g_state.blendS);
    cb.dstAlphaBlendFactor = toBlend(g_state.blendD);
    cb.alphaBlendOp = VK_BLEND_OP_ADD;

    std::vector<VkPipelineColorBlendAttachmentState> cbsAtts(colorAttachmentCount(rp), cb);

    VkPipelineColorBlendStateCreateInfo cbs{};
    cbs.sType = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO;
    cbs.attachmentCount = (uint32_t)cbsAtts.size();
    cbs.pAttachments = cbsAtts.empty() ? nullptr : cbsAtts.data();

    VkDynamicState dyn[] = { VK_DYNAMIC_STATE_VIEWPORT, VK_DYNAMIC_STATE_SCISSOR };
    VkPipelineDynamicStateCreateInfo dsi{};
    dsi.sType = VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO;
    dsi.dynamicStateCount = 2;
    dsi.pDynamicStates = dyn;

    VkGraphicsPipelineCreateInfo ci{};
    ci.sType = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO;
    ci.stageCount = stageCount;
    ci.pStages = stages;
    ci.pVertexInputState = &vi;
    ci.pInputAssemblyState = &ia;
    ci.pViewportState = &vp;
    ci.pRasterizationState = &rs;
    ci.pMultisampleState = &ms;
    ci.pDepthStencilState = &ds;
    ci.pColorBlendState = &cbs;
    ci.pDynamicState = &dsi;
    ci.layout = p->pipeLayout;
    ci.renderPass = rp;
    ci.subpass = 0;

    VkPipeline pipeline = VK_NULL_HANDLE;
    VkResult r = vkCreateGraphicsPipelines(ctx().device, VK_NULL_HANDLE, 1, &ci, nullptr, &pipeline);
    if (r != VK_SUCCESS) {
        printf("[vk] pipeline create failed %d\n", (int)r);
        return VK_NULL_HANDLE;
    }
    return pipeline;
}

VkPipeline currentPipeline() {
    if (!g_program) return VK_NULL_HANDLE;
    VkRenderPass rp = currentRenderPass();
    if (!rp) return VK_NULL_HANDLE;
    uint64_t h = 1469598103934665603ULL;
    h = hashCombine(h, (uint64_t)g_program);
    h = hashCombine(h, boundVaoId());
    h = hashCombine(h, (uint64_t)rp);
    h = hashCombine(h, g_state.depthFunc);
    h = hashCombine(h, g_state.depthTest ? 1 : 0);
    h = hashCombine(h, g_state.depthMask ? 1 : 0);
    h = hashCombine(h, g_state.cullMode);
    h = hashCombine(h, g_state.frontFace);
    h = hashCombine(h, g_state.blend ? 1 : 0);
    h = hashCombine(h, g_state.blendS);
    h = hashCombine(h, g_state.blendD);
    h = hashCombine(h, g_state.polygonMode);
    h = hashCombine(h, (uint64_t&)g_state.polyFactor);
    h = hashCombine(h, (uint64_t&)g_state.polyUnit);
    h = hashCombine(h, g_state.topology);
    auto it = g_pipelines.find(h);
    if (it != g_pipelines.end()) return it->second;
    VkPipeline pipe = makePipeline(g_program, rp);
    g_pipelines[h] = pipe;
    return pipe;
}

void clearPipelineCache() {
    vk::waitIdle();
    for (auto& kv : g_pipelines)
        if (kv.second) vkDestroyPipeline(ctx().device, kv.second, nullptr);
    g_pipelines.clear();
}

int activeUnitSlot() {
    if (g_activeUnit < 0 || g_activeUnit >= 16) return 0;
    return g_activeUnit;
}

int g_vpX = 0, g_vpY = 0, g_vpW = 0, g_vpH = 0;
bool g_vpSet = false;

void applyViewport() {
    if (!g_cmd) return;
    int tw = (int)ctx().swapExtent.width;
    int th = (int)ctx().swapExtent.height;
    VkFboObj* f = fboAt(g_fbo);
    if (f && f->width > 0 && f->height > 0) { tw = f->width; th = f->height; }
    if (tw <= 0 || th <= 0) return;
    int x = g_vpSet ? g_vpX : 0;
    int y = g_vpSet ? g_vpY : 0;
    int w = (g_vpSet && g_vpW > 0) ? g_vpW : tw;
    int h = (g_vpSet && g_vpH > 0) ? g_vpH : th;
    VkViewport vp{};
    vp.x = (float)x;
    vp.y = (float)(th - y);
    vp.width = (float)w;
    vp.height = -(float)h;
    vp.minDepth = 0.0f;
    vp.maxDepth = 1.0f;
    vkCmdSetViewport(g_cmd, 0, 1, &vp);
    VkRect2D sc{};
    sc.offset.x = x;
    int sy = th - y - h;
    sc.offset.y = sy > 0 ? sy : 0;
    sc.extent.width = (uint32_t)w;
    sc.extent.height = (uint32_t)h;
    vkCmdSetScissor(g_cmd, 0, 1, &sc);
}

void recordDraw(unsigned mode, int count, unsigned type, const void* indices, int instances) {
    if (!ctx().frameActive || !g_program || !g_cmd) return;
    g_state.topology = mode;
    VkPipeline pipe = currentPipeline();
    if (!pipe) return;

    unsigned texIds[16];
    for (int i = 0; i < 16; ++i) texIds[i] = g_texUnit[i];
    updateDescriptors(g_program, texIds, 16);
    VkDescriptorSet ds = descriptorSetOf(g_program);
    VkPipelineLayout pl = pipelineLayoutOf(g_program);
    if (!pl) return;
    uint32_t dynOffset = 0;
    bool hasDyn = vk::programHasUbo(g_program);
    if (hasDyn) dynOffset = vk::uniformSlotOffset(g_program);
    if (ds) {
        VkDescriptorSet sets[2] = { ds, vk::uboSetOf(g_program) };
        vkCmdBindDescriptorSets(g_cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, pl, 0, hasDyn ? 2 : 1, sets,
                                hasDyn ? 1 : 0, hasDyn ? &dynOffset : nullptr);
    }
    vkCmdBindPipeline(g_cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, pipe);
    applyViewport();

    VkVaoObj* vao = vaoAt(boundVaoId());
    if (vao && vao->vbo) {
        VkBufferObj* vb = bufferAt(vao->vbo);
        if (vb && vb->buffer) {
            VkDeviceSize off = vb->slotBase;
            vkCmdBindVertexBuffers(g_cmd, 0, 1, &vb->buffer, &off);
        }
    }
    if (vao && vao->instBuffer) {
        VkBufferObj* ib = bufferAt(vao->instBuffer);
        if (ib && ib->buffer) {
            VkDeviceSize off = ib->slotBase;
            vkCmdBindVertexBuffers(g_cmd, 1, 1, &ib->buffer, &off);
        }
    }

    unsigned eboId = (vao && vao->ebo) ? vao->ebo : boundIndexBuffer();
    bool indexed = (indices != nullptr) || eboId != 0;
    if (indexed) {
        VkBufferObj* ib = bufferAt(eboId);
        if (!ib || !ib->buffer) return;
        VkIndexType it = (type == 203 || type == 202) ? VK_INDEX_TYPE_UINT16 : VK_INDEX_TYPE_UINT32;
        vkCmdBindIndexBuffer(g_cmd, ib->buffer, ib->slotBase, it);
        vkCmdDrawIndexed(g_cmd, (uint32_t)count, instances > 0 ? (uint32_t)instances : 1, 0, 0, 0);
    } else {
        vkCmdDraw(g_cmd, (uint32_t)count, instances > 0 ? (uint32_t)instances : 1, 0, 0);
    }
}

void endActivePass() {
    if (!g_cmd) return;
    if (ctx().swapPassActive) {
        vkCmdEndRenderPass(g_cmd);
        ctx().swapPassActive = false;
    }
    if (g_offscreen) {
        vkCmdEndRenderPass(g_cmd);
        g_offscreen = false;
    }
}

void beginSwapchainPass() {
    if (!ctx().frameActive || !g_cmd) return;
    endActivePass();
    VkRenderPass rp = ctx().swapRenderPass;
    VkFramebuffer fb = ctx().swapFramebuffers.empty() ? VK_NULL_HANDLE : ctx().swapFramebuffers[ctx().imageIndex];
    if (!rp || !fb) return;
    VkClearValue clears[2]{};
    clears[0].color.float32[0] = ctx().clearR;
    clears[0].color.float32[1] = ctx().clearG;
    clears[0].color.float32[2] = ctx().clearB;
    clears[0].color.float32[3] = ctx().clearA;
    clears[1].depthStencil.depth = ctx().clearDepth;
    clears[1].depthStencil.stencil = 0;
    VkRenderPassBeginInfo bi{};
    bi.sType = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO;
    bi.renderPass = rp;
    bi.framebuffer = fb;
    bi.renderArea.offset.x = 0;
    bi.renderArea.offset.y = 0;
    bi.renderArea.extent = ctx().swapExtent;
    bi.clearValueCount = 2;
    bi.pClearValues = clears;
    vkCmdBeginRenderPass(g_cmd, &bi, VK_SUBPASS_CONTENTS_INLINE);
    ctx().swapPassActive = true;
}

void beginFboPass(unsigned fboId) {
    if (!fboBuild(fboId)) return;
    VkFboObj* f = fboAt(fboId);
    if (!f || !f->renderPass || !f->framebuffer) return;
    endActivePass();
    for (size_t i = 0; i < f->colors.size(); ++i) {
        VkTextureObj* t = textureAt(f->colors[i].texture);
        if (t && t->sampled) ensureImageLayout(t, VK_IMAGE_LAYOUT_GENERAL);
    }
    if (f->depth) {
        VkTextureObj* dt = textureAt(f->depth);
        if (dt) ensureImageLayout(dt, VK_IMAGE_LAYOUT_GENERAL);
    }
    VkClearValue clears[9]{};
    uint32_t clearCount = 0;
    for (size_t i = 0; i < f->colors.size() && clearCount < 9; ++i) {
        clears[clearCount].color.float32[0] = ctx().clearR;
        clears[clearCount].color.float32[1] = ctx().clearG;
        clears[clearCount].color.float32[2] = ctx().clearB;
        clears[clearCount].color.float32[3] = ctx().clearA;
        ++clearCount;
    }
    if (f->depth && clearCount < 9) {
        clears[clearCount].depthStencil.depth = ctx().clearDepth;
        clears[clearCount].depthStencil.stencil = 0;
        ++clearCount;
    }
    VkRenderPassBeginInfo rp{};
    rp.sType = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO;
    rp.renderPass = f->renderPass;
    rp.framebuffer = f->framebuffer;
    rp.renderArea.offset.x = 0;
    rp.renderArea.offset.y = 0;
    rp.renderArea.extent.width = (uint32_t)f->width;
    rp.renderArea.extent.height = (uint32_t)f->height;
    rp.clearValueCount = clearCount;
    rp.pClearValues = clearCount ? clears : nullptr;
    vkCmdBeginRenderPass(g_cmd, &rp, VK_SUBPASS_CONTENTS_INLINE);
    g_offscreen = true;
}

void endOffscreenPass() {
    if (!g_offscreen) return;
    vkCmdEndRenderPass(g_cmd);
    g_offscreen = false;
}

void t_present() {
    if (ctx().frameActive) {
        endActivePass();
        vk::endFrame();
    }
}

void t_setVsync(bool v) { ctx().vsync = v; }

void t_resize(int w, int h) {
    if (w <= 0 || h <= 0) return;
    ctx().width = (uint32_t)w;
    ctx().height = (uint32_t)h;
    if ((uint32_t)w != ctx().swapExtent.width || (uint32_t)h != ctx().swapExtent.height) {
        ctx().pendingResize = true;
        ctx().pendingWidth = (uint32_t)w;
        ctx().pendingHeight = (uint32_t)h;
    }
}

void t_shutdown() {
    vk::waitIdle();
    clearPipelineCache();
    shaderShutdown();
    resourceShutdown();
    vk::destroy();
}

const char* t_Gvtcier_Rhi_GetString(unsigned name) {
    static char versionBuf[64];
    switch (name) {
        case 320:
            snprintf(versionBuf, sizeof(versionBuf), "%u.%u",
                     VK_API_VERSION_MAJOR(ctx().apiVersion), VK_API_VERSION_MINOR(ctx().apiVersion));
            return versionBuf;
        case 321: return ctx().deviceName[0] ? ctx().deviceName : "Vulkan Device";
        case 322: return "Gvtcier";
        case 323: return "SPIR-V";
    }
    return "Gvtcier Vulkan";
}

void t_Gvtcier_Rhi_GetIntegerv(unsigned pname, int* d) {
    if (!d) return;
    switch (pname) {
        case 324: *d = 1; break;
        case 325: *d = 1; break;
        case 326: *d = 8192; break;
        case 327: *d = 1; break;
        default: *d = 0; break;
    }
}

unsigned t_Gvtcier_Rhi_GetError() { return 0; }

void t_Gvtcier_Rhi_ClearColor(float r, float g, float b, float a) {
    ctx().clearR = r;
    ctx().clearG = g;
    ctx().clearB = b;
    ctx().clearA = a;
}

void t_Gvtcier_Rhi_ClearDepth(double d) { ctx().clearDepth = (float)d; }

void t_Gvtcier_Rhi_Clear(unsigned mask) {
    (void)mask;
    static bool s_resizePending = false;
    if (ctx().pendingResize) {
        s_resizePending = true;
        vk::waitIdle();
        clearPipelineCache();
        ctx().pendingResize = false;
        vk::recreateSwapchain();
        if (g_fbo) fboInvalidate(g_fbo);
    }
    (void)s_resizePending;
    if (!ctx().frameActive) {
        if (!vk::beginFrame()) return;
        g_cmd = ctx().cmd[ctx().frameIndex];
    }
    if (!g_cmd) return;
    if (g_fbo) {
        beginFboPass(g_fbo);
        if (g_offscreen) {
            VkFboObj* f = fboAt(g_fbo);
            if (f) {
                std::vector<VkClearAttachment> cas;
                if (mask & ColorBit) {
                    for (size_t i = 0; i < f->colors.size(); ++i) {
                        VkClearAttachment ca{};
                        ca.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
                        ca.colorAttachment = (uint32_t)i;
                        ca.clearValue.color.float32[0] = ctx().clearR;
                        ca.clearValue.color.float32[1] = ctx().clearG;
                        ca.clearValue.color.float32[2] = ctx().clearB;
                        ca.clearValue.color.float32[3] = ctx().clearA;
                        cas.push_back(ca);
                    }
                }
                if ((mask & DepthBit) && f->depth) {
                    VkClearAttachment ca{};
                    ca.aspectMask = VK_IMAGE_ASPECT_DEPTH_BIT;
                    ca.clearValue.depthStencil.depth = ctx().clearDepth;
                    ca.clearValue.depthStencil.stencil = 0;
                    cas.push_back(ca);
                }
                if (!cas.empty()) {
                    VkClearRect cr{};
                    cr.rect.extent.width = (uint32_t)f->width;
                    cr.rect.extent.height = (uint32_t)f->height;
                    cr.baseArrayLayer = 0;
                    cr.layerCount = 1;
                    vkCmdClearAttachments(g_cmd, (uint32_t)cas.size(), cas.data(), 1, &cr);
                }
            }
        }
        return;
    }
    beginSwapchainPass();
}

void t_Gvtcier_Rhi_Viewport(int x, int y, int w, int h) {
    g_vpX = x; g_vpY = y; g_vpW = w; g_vpH = h;
    g_vpSet = true;
}

void t_Gvtcier_Rhi_SetRasterState(const RasterState* s) {
    if (!s) return;
    g_state.depthTest = s->depthTest;
    g_state.depthMask = s->depthWrite;
    g_state.depthFunc = s->depthFunc;
    g_state.cullMode = s->cull ? s->cullMode : 0;
    g_state.frontFace = s->frontFace;
    g_state.blend = s->blend;
    g_state.blendS = s->blendSrc;
    g_state.blendD = s->blendDst;
    g_state.polygonMode = s->polygonMode;
    if (g_state.polyFactor != s->polyFactor || g_state.polyUnit != s->polyUnit) {
        g_state.polyFactor = s->polyFactor;
        g_state.polyUnit = s->polyUnit;
        clearPipelineCache();
    }
}
void t_Gvtcier_Rhi_DrawBuffer(unsigned) {}
void t_Gvtcier_Rhi_DrawBuffers(int, const unsigned*) {}
void t_Gvtcier_Rhi_ReadBuffer(unsigned) {}
void t_Gvtcier_Rhi_ReadPixels(int, int, int, int, unsigned, unsigned, void*) {}
void t_Gvtcier_Rhi_ReadBufferTex(unsigned, unsigned, unsigned) {}

void t_Gvtcier_Rhi_GenTextures(int n, Texture* out) {
    for (int i = 0; i < n; ++i) out[i] = createTexture();
}
void t_Gvtcier_Rhi_BindTexture(unsigned, Texture t) {
    g_texUnit[activeUnitSlot()] = t;
}
void t_Gvtcier_Rhi_DeleteTextures(int n, const Texture* t) {
    for (int i = 0; i < n; ++i)
        if (t[i]) vk::deferTexture(t[i]);
}
void t_Gvtcier_Rhi_TexImage2D(unsigned target, int, int fmt, int w, int h,
                              unsigned pixelFmt, unsigned, const void* data) {
    VkTextureObj* t = textureAt(g_texUnit[activeUnitSlot()]);
    if (!t) return;
    t->target = target;
    createImageFor(t, w, h, 1, fmt);
    if (data) vk::uploadImageData(t, 0, 0, w, h, data, pixelFmt);
}
void t_Gvtcier_Rhi_TexSubImage2D(unsigned target, int, int x, int y, int w, int h,
                                 unsigned pixelFmt, unsigned, const void* data) {
    (void)target;
    VkTextureObj* t = textureAt(g_texUnit[activeUnitSlot()]);
    if (!t || !data) return;
    vk::uploadImageData(t, x, y, w, h, data, pixelFmt);
}
void t_Gvtcier_Rhi_TexImage3D(unsigned target, int, int fmt, int w, int h, int d,
                              unsigned, unsigned, const void*) {
    VkTextureObj* t = textureAt(g_texUnit[activeUnitSlot()]);
    if (!t) return;
    t->target = target;
    if (target == 251) {
        t->layers = d;
        createImageFor(t, w, h, 1, fmt);
    } else {
        createImageFor(t, w, h, d, fmt);
    }
}
void t_Gvtcier_Rhi_TexSubImage3D(unsigned, int, int, int, int, int, int, int, unsigned, unsigned, const void*) {}
void t_Gvtcier_Rhi_GenerateMipmap(unsigned) {}
void t_Gvtcier_Rhi_TexParameteri(unsigned target, unsigned pname, int value) {
    VkTextureObj* t = textureAt(g_texUnit[activeUnitSlot()]);
    if (!t || !t->sampler) return;
    if (target == 253 || target == 259) return;
    unsigned oldMin = t->minFilter, oldMag = t->magFilter;
    unsigned oldS = t->wrapS, oldT = t->wrapT;
    if (pname == 260) t->minFilter = value;
    else if (pname == 261) t->magFilter = value;
    else if (pname == 262) t->wrapS = value;
    else if (pname == 263) t->wrapT = value;
    if (oldMin == t->minFilter && oldMag == t->magFilter && oldS == t->wrapS && oldT == t->wrapT) return;
    VkSamplerCreateInfo si{};
    si.sType = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO;
    si.magFilter = (t->magFilter == 270) ? VK_FILTER_NEAREST : VK_FILTER_LINEAR;
    si.minFilter = (t->minFilter == 270 || t->minFilter == 272) ? VK_FILTER_NEAREST : VK_FILTER_LINEAR;
    si.mipmapMode = (t->minFilter == 270 || t->minFilter == 271) ? VK_SAMPLER_MIPMAP_MODE_NEAREST
                                                                 : VK_SAMPLER_MIPMAP_MODE_LINEAR;
    VkSamplerAddressMode amS = (t->wrapS == 276) ? VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE
                                                 : (t->wrapS == 278 ? VK_SAMPLER_ADDRESS_MODE_MIRRORED_REPEAT
                                                                    : VK_SAMPLER_ADDRESS_MODE_REPEAT);
    VkSamplerAddressMode amT = (t->wrapT == 276) ? VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE
                                                 : (t->wrapT == 278 ? VK_SAMPLER_ADDRESS_MODE_MIRRORED_REPEAT
                                                                    : VK_SAMPLER_ADDRESS_MODE_REPEAT);
    si.addressModeU = amS;
    si.addressModeV = amT;
    si.addressModeW = amS;
    si.maxLod = 1.0f;
    vkDestroySampler(ctx().device, t->sampler, nullptr);
    t->sampler = VK_NULL_HANDLE;
    vkCreateSampler(ctx().device, &si, nullptr, &t->sampler);
    for (unsigned pi = 1; pi <= 64; ++pi) {
        VkProgram* pr = programOf(pi);
        if (pr) pr->descDirty = true;
    }
}
void t_Gvtcier_Rhi_TexParameterf(unsigned, unsigned, float) {}
void t_Gvtcier_Rhi_SetAnisotropy(int) {}
int t_Gvtcier_Rhi_Anisotropy() { return 1; }
void t_Gvtcier_Rhi_BindTextureUnit(int unit, unsigned, Texture t) {
    if (unit < 0 || unit >= 16) return;
    g_activeUnit = unit;
    g_texUnit[unit] = t;
}

void t_Gvtcier_Rhi_GenVertexArrays(int n, Vao* out) {
    for (int i = 0; i < n; ++i) out[i] = createVao();
}
void t_Gvtcier_Rhi_BindVertexArray(Vao v) { bindVao(v); }
void t_Gvtcier_Rhi_DeleteVertexArrays(int n, const Vao* v) {
    for (int i = 0; i < n; ++i) destroyVao(v[i]);
}

void t_Gvtcier_Rhi_GenBuffers(int n, Buffer* out) {
    for (int i = 0; i < n; ++i) out[i] = createBuffer();
}
void t_Gvtcier_Rhi_BindBuffer(unsigned target, Buffer b) { bindBuffer(target, b); }
void t_Gvtcier_Rhi_BufferData(unsigned target, size_t size, const void* data, unsigned) {
    unsigned id = currentBuffer(target);
    VkBufferObj* bo = bufferAt(id);
    if (!bo) return;
    size_t need = size ? size : 4;
    size_t align = (need + 255) & ~(size_t)255;
    VkBufferSlot& s = bo->slots[0];
    size_t want = bo->writeOfs + align;
    if (s.buffer == VK_NULL_HANDLE || s.capacity < want) {
        size_t cap = s.capacity > 0 ? s.capacity : 65536;
        while (cap < want) cap *= 2;
        if (s.buffer) { vk::deferVkBuffer(s.buffer); s.buffer = VK_NULL_HANDLE; }
        if (s.memory) { vk::deferVkMemory(s.memory); s.memory = VK_NULL_HANDLE; s.mapped = nullptr; }
        VkBufferCreateInfo bi{};
        bi.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
        bi.size = cap;
        bi.usage = VK_BUFFER_USAGE_VERTEX_BUFFER_BIT | VK_BUFFER_USAGE_INDEX_BUFFER_BIT |
                   VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT | VK_BUFFER_USAGE_STORAGE_BUFFER_BIT |
                   VK_BUFFER_USAGE_TRANSFER_DST_BIT;
        bi.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
        if (vkCreateBuffer(ctx().device, &bi, nullptr, &s.buffer) != VK_SUCCESS) { s.buffer = VK_NULL_HANDLE; return; }
        VkMemoryRequirements req{};
        vkGetBufferMemoryRequirements(ctx().device, s.buffer, &req);
        VkMemoryAllocateInfo ai{};
        ai.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
        ai.allocationSize = req.size;
        ai.memoryTypeIndex = findMemoryType(req.memoryTypeBits, VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT |
                                                                   VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);
        if (vkAllocateMemory(ctx().device, &ai, nullptr, &s.memory) != VK_SUCCESS) {
            vk::deferVkBuffer(s.buffer);
            s.buffer = VK_NULL_HANDLE;
            s.memory = VK_NULL_HANDLE;
            return;
        }
        vkBindBufferMemory(ctx().device, s.buffer, s.memory, 0);
        vkMapMemory(ctx().device, s.memory, 0, cap, 0, &s.mapped);
        s.capacity = cap;
    }
    bo->buffer = s.buffer;
    bo->memory = s.memory;
    bo->size = need;
    bo->target = target;
    bo->slotBase = (uint32_t)bo->writeOfs;
    bo->mapped = s.mapped ? ((unsigned char*)s.mapped + bo->writeOfs) : nullptr;
    if (data && size && bo->mapped) memcpy(bo->mapped, data, size);
    bo->writeOfs += align;
}
void t_Gvtcier_Rhi_BufferSubData(unsigned target, ptrdiff_t offset, size_t size, const void* data) {
    VkBufferObj* bo = bufferAt(currentBuffer(target));
    if (!bo || !bo->mapped || !data) return;
    memcpy((unsigned char*)bo->mapped + offset, data, size);
}
void t_Gvtcier_Rhi_BindBufferBase(unsigned target, unsigned index, Buffer b) {
    VkBufferObj* bo = bufferAt((unsigned)b);
    if (!bo || !bo->buffer) return;
    VkProgram* p = programOf(g_program);
    if (!p) return;
    for (size_t i = 0; i < p->ssbos.size(); ++i) {
        if (p->ssbos[i].binding == index) {
            p->ssbos[i].buffer = bo->buffer;
            p->ssbos[i].offset = bo->slotBase;
            p->ssbos[i].range = (uint32_t)bo->size;
            break;
        }
    }
    bindStorageBuffer(g_program, index, bo->buffer, bo->slotBase, (uint32_t)bo->size);
}
unsigned t_Gvtcier_Rhi_GetUniformBlockIndex(Program, const char*) { return InvalidIndex; }
void t_Gvtcier_Rhi_UniformBlockBinding(Program, unsigned, unsigned) {}
void t_Gvtcier_Rhi_GetActiveUniformBlockiv(Program, unsigned, unsigned, int* p) { if (p) *p = 0; }
void t_Gvtcier_Rhi_GetUniformIndices(Program, int, const char* const*, unsigned*) {}
void t_Gvtcier_Rhi_GetActiveUniformsiv(Program, int, const unsigned*, unsigned, int*) {}
void t_Gvtcier_Rhi_GetActiveUniformName(Program, unsigned, int, int* len, char* name) {
    if (len) *len = 0;
    if (name) name[0] = '\0';
}
void t_Gvtcier_Rhi_DispatchCompute(unsigned, unsigned, unsigned) {}
void t_Gvtcier_Rhi_MemoryBarrier(unsigned) {}
bool t_Gvtcier_Rhi_HasCompute() { return false; }
bool t_Gvtcier_Rhi_HasInstancing() { return true; }
void t_Gvtcier_Rhi_DeleteBuffers(int n, const Buffer* b) {
    for (int i = 0; i < n; ++i) vk::deferBuffer(b[i]);
}
void t_Gvtcier_Rhi_VertexAttribPointer(unsigned index, int size, unsigned type, bool normalized,
                                       int stride, const void* ptr) {
    VkVaoObj* v = vaoAt(boundVaoId());
    if (!v) return;
    VkAttrib a;
    a.index = index;
    a.size = size;
    a.type = type;
    a.normalized = normalized;
    a.stride = stride > 0 ? stride : size * (int)attribComponentSize(type);
    a.offset = (uint64_t)(uintptr_t)ptr;
    unsigned cur = currentBuffer(160);
    if (cur && !v->vbo) v->vbo = cur;
    for (size_t i = 0; i < v->attribs.size(); ++i) {
        if (v->attribs[i].index == index) {
            a.divisor = v->attribs[i].divisor;
            v->attribs[i] = a;
            return;
        }
    }
    v->attribs.push_back(a);
}
void t_Gvtcier_Rhi_EnableAttrib(unsigned) {}
void t_Gvtcier_Rhi_VertexAttribDivisor(unsigned index, unsigned divisor) {
    VkVaoObj* v = vaoAt(boundVaoId());
    if (!v) return;
    for (size_t i = 0; i < v->attribs.size(); ++i) {
        if (v->attribs[i].index == index) {
            v->attribs[i].divisor = divisor;
            if (divisor > 0) {
                unsigned cur = currentBuffer(160);
                if (cur) v->instBuffer = cur;
            }
            return;
        }
    }
}

Stage t_Gvtcier_Rhi_CreateShader(unsigned type) { return createStage((int)type); }
void t_Gvtcier_Rhi_ShaderSource(Stage, int, const char* const*, const int*) {}
void t_Gvtcier_Rhi_ShaderSourceBinary(Stage s, const void* data, int len) {
    setStageSpv(s, data, (size_t)len);
}
void t_Gvtcier_Rhi_CompileShader(Stage) {}
void t_Gvtcier_Rhi_GetShaderiv(Stage, unsigned pname, int* p) {
    if (p) *p = (pname == 290) ? 1 : 0;
}
void t_Gvtcier_Rhi_GetShaderInfoLog(Stage, int, int* len, char* log) {
    if (len) *len = 0;
    if (log) log[0] = '\0';
}
void t_Gvtcier_Rhi_DeleteShader(Stage s) { destroyStage(s); }
Program t_Gvtcier_Rhi_CreateProgram() { return createProgram(); }
void t_Gvtcier_Rhi_SetProgramName(Program p, const char* name) {
    if (name) setProgramName(p, name);
}
void t_Gvtcier_Rhi_AttachShader(Program p, Stage s) { attachShader(p, s); }
void t_Gvtcier_Rhi_LinkProgram(Program p) {
    linkProgram(p, std::string());
}
void t_Gvtcier_Rhi_GetProgramiv(Program, unsigned pname, int* p) {
    if (p) *p = (pname == 291) ? 1 : 0;
}
void t_Gvtcier_Rhi_GetProgramInfoLog(Program, int, int* len, char* log) {
    if (len) *len = 0;
    if (log) log[0] = '\0';
}
void t_Gvtcier_Rhi_DeleteProgram(Program p) { destroyProgram(p); }
void t_Gvtcier_Rhi_UseProgram(Program p) { g_program = p; }
Location t_Gvtcier_Rhi_UniformLocation(Program p, const char* name) {
    return (Location)uniformOffset(p, name);
}
void t_Gvtcier_Rhi_Uniform1i(Location l, int v) {
    writeUniform(g_program, l, &v, 4);
    vk::setSamplerUnitByOffset(g_program, l, v);
}
void t_Gvtcier_Rhi_Uniform1f(Location l, float v) { writeUniform(g_program, l, &v, 4); }
void t_Gvtcier_Rhi_Uniform2f(Location l, float x, float y) {
    float t[2] = { x, y };
    writeUniform(g_program, l, t, 8);
}
void t_Gvtcier_Rhi_Uniform3f(Location l, float x, float y, float z) {
    float t[4] = { x, y, z, 0.0f };
    writeUniform(g_program, l, t, 12);
}
void t_Gvtcier_Rhi_Uniform4f(Location l, float x, float y, float z, float w) {
    float t[4] = { x, y, z, w };
    writeUniform(g_program, l, t, 16);
}
void t_Gvtcier_Rhi_UniformMatrix4fv(Location l, int count, bool, const float* v) {
    for (int i = 0; i < count; ++i) writeUniformMat4(g_program, l + i * 64, v + i * 16);
}

void t_Gvtcier_Rhi_DrawArrays(unsigned mode, int, int count) {
    recordDraw(mode, count, 205, nullptr, 0);
}
void t_Gvtcier_Rhi_DrawElements(unsigned mode, int count, unsigned type, const void* indices) {
    recordDraw(mode, count, type, indices, 0);
}
void t_Gvtcier_Rhi_DrawElementsInstanced(unsigned mode, int count, unsigned type,
                                         const void* indices, int instanceCount) {
    recordDraw(mode, count, type, indices, instanceCount);
}

void t_Gvtcier_Rhi_GenFramebuffers(int n, Fbo* out) {
    for (int i = 0; i < n; ++i) out[i] = createFbo();
}
void t_Gvtcier_Rhi_BindFramebuffer(unsigned, Fbo f) {
    if (f == g_fbo) return;
    if (g_offscreen) {
        vkCmdEndRenderPass(g_cmd);
        g_offscreen = false;
    } else if (ctx().swapPassActive) {
        vkCmdEndRenderPass(g_cmd);
        ctx().swapPassActive = false;
    }
    g_fbo = f;
    if (!ctx().frameActive || !g_cmd) return;
    if (f) {
        beginFboPass(f);
    } else {
        beginSwapchainPass();
    }
}
void t_Gvtcier_Rhi_DeleteFramebuffers(int n, const Fbo* f) {
    for (int i = 0; i < n; ++i)
        if (f[i]) vk::deferFbo(f[i]);
}
void t_Gvtcier_Rhi_FramebufferTexture2D(unsigned, unsigned attachment, unsigned, Texture t, int) {
    VkFboObj* f = fboAt(g_fbo);
    if (!f) return;
    VkAttachment a;
    a.texture = t;
    a.attachment = attachment;
    a.layer = -1;
    if (attachment == 302 || attachment == 303 || attachment == 305) {
        f->depth = t;
        f->depthAttachment = attachment;
    } else {
        bool replaced = false;
        for (size_t i = 0; i < f->colors.size(); ++i) {
            if (f->colors[i].attachment == attachment) {
                f->colors[i] = a;
                replaced = true;
                break;
            }
        }
        if (!replaced) f->colors.push_back(a);
    }
    fboInvalidate(g_fbo);
}
void t_Gvtcier_Rhi_FramebufferTextureLayer(unsigned, unsigned attachment, Texture t, int, int layer) {
    VkFboObj* f = fboAt(g_fbo);
    if (!f) return;
    VkAttachment a;
    a.texture = t;
    a.attachment = attachment;
    a.layer = layer;
    if (attachment == 302 || attachment == 303 || attachment == 305) {
        f->depth = t;
        f->depthAttachment = attachment;
    } else {
        bool replaced = false;
        for (size_t i = 0; i < f->colors.size(); ++i) {
            if (f->colors[i].attachment == attachment) {
                f->colors[i] = a;
                replaced = true;
                break;
            }
        }
        if (!replaced) f->colors.push_back(a);
    }
    fboInvalidate(g_fbo);
}
unsigned t_Gvtcier_Rhi_CheckFramebufferStatus(unsigned) { return FramebufferComplete; }
void t_Gvtcier_Rhi_GenRenderbuffers(int n, Rbo* out) {
    for (int i = 0; i < n; ++i) out[i] = createRbo();
}
void t_Gvtcier_Rhi_BindRenderbuffer(unsigned, Rbo) {}
void t_Gvtcier_Rhi_DeleteRenderbuffers(int n, const Rbo* r) {
    for (int i = 0; i < n; ++i) vk::deferTexture(r[i]);
}
void t_Gvtcier_Rhi_RenderbufferStorage(unsigned target, unsigned fmt, int w, int h) {
    unsigned id = createRbo();
    VkTextureObj* t = textureAt(id);
    if (!t) return;
    t->target = target;
    createImageFor(t, w, h, 1, (int)fmt);
}
void t_Gvtcier_Rhi_FramebufferRenderbuffer(unsigned, unsigned, unsigned, Rbo) {}
bool t_Gvtcier_Rhi_LegacyCreate() { return false; }
bool t_Gvtcier_Rhi_LegacyBind() { return false; }
bool t_Gvtcier_Rhi_CoreBind() { return false; }

RhiTable g_vkTable = {
    t_present,
    t_setVsync,
    t_resize,
    t_shutdown,

#define RHI_TABLE_ENTRY(ret, name, params, args) t_##name,
    RHI_TABLE_ENTRIES(RHI_TABLE_ENTRY)
#undef RHI_TABLE_ENTRY
};

}

RhiTable* vkTable(HWND hwnd) {
    if (!vk::create(hwnd)) return nullptr;
    vk::resourceInit();
    vk::shaderInit();
    return &g_vkTable;
}

}
