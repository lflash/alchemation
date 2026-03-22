#include "compute_pipeline.hpp"
#include <fstream>
#include <stdexcept>

// ─── SPIR-V loader ────────────────────────────────────────────────────────────

std::vector<uint32_t> ComputePipeline::loadSpv(const std::string& path) {
    std::ifstream f(path, std::ios::binary | std::ios::ate);
    if (!f)
        throw std::runtime_error("ComputePipeline: cannot open SPIR-V: " + path);

    auto size = static_cast<size_t>(f.tellg());
    if (size % 4 != 0)
        throw std::runtime_error("ComputePipeline: SPIR-V size not 4-byte aligned: " + path);

    f.seekg(0);
    std::vector<uint32_t> code(size / 4);
    f.read(reinterpret_cast<char*>(code.data()), static_cast<std::streamsize>(size));
    return code;
}

// ─── ComputePipeline ─────────────────────────────────────────────────────────

ComputePipeline::ComputePipeline(const VulkanContext& ctx,
                                  const std::string&   spvPath,
                                  uint32_t             bindingCount,
                                  uint32_t             pushConstantSize)
    : ctx_(ctx), bindingCount_(bindingCount), pushConstantSize_(pushConstantSize)
{
    VkDevice dev = ctx.device();

    // ── Descriptor set layout: N storage-buffer bindings ─────────────────────
    std::vector<VkDescriptorSetLayoutBinding> bindings(bindingCount);
    for (uint32_t i = 0; i < bindingCount; ++i) {
        bindings[i].binding            = i;
        bindings[i].descriptorType     = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
        bindings[i].descriptorCount    = 1;
        bindings[i].stageFlags         = VK_SHADER_STAGE_COMPUTE_BIT;
        bindings[i].pImmutableSamplers = nullptr;
    }

    VkDescriptorSetLayoutCreateInfo dslci{};
    dslci.sType        = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
    dslci.bindingCount = bindingCount;
    dslci.pBindings    = bindings.data();
    if (vkCreateDescriptorSetLayout(dev, &dslci, nullptr, &descSetLayout_) != VK_SUCCESS)
        throw std::runtime_error("ComputePipeline: vkCreateDescriptorSetLayout failed");

    // ── Descriptor pool + set ─────────────────────────────────────────────────
    VkDescriptorPoolSize poolSize{};
    poolSize.type            = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
    poolSize.descriptorCount = bindingCount;

    VkDescriptorPoolCreateInfo dpci{};
    dpci.sType         = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
    dpci.poolSizeCount = 1;
    dpci.pPoolSizes    = &poolSize;
    dpci.maxSets       = 1;
    if (vkCreateDescriptorPool(dev, &dpci, nullptr, &descPool_) != VK_SUCCESS)
        throw std::runtime_error("ComputePipeline: vkCreateDescriptorPool failed");

    VkDescriptorSetAllocateInfo dsai{};
    dsai.sType              = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
    dsai.descriptorPool     = descPool_;
    dsai.descriptorSetCount = 1;
    dsai.pSetLayouts        = &descSetLayout_;
    if (vkAllocateDescriptorSets(dev, &dsai, &descSet_) != VK_SUCCESS)
        throw std::runtime_error("ComputePipeline: vkAllocateDescriptorSets failed");

    // ── Pipeline layout ───────────────────────────────────────────────────────
    VkPushConstantRange pcRange{};
    pcRange.stageFlags = VK_SHADER_STAGE_COMPUTE_BIT;
    pcRange.offset     = 0;
    pcRange.size       = pushConstantSize_ > 0 ? pushConstantSize_ : 4; // min 4 bytes

    VkPipelineLayoutCreateInfo plci{};
    plci.sType                  = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
    plci.setLayoutCount         = 1;
    plci.pSetLayouts            = &descSetLayout_;
    plci.pushConstantRangeCount = 1;
    plci.pPushConstantRanges    = &pcRange;
    if (vkCreatePipelineLayout(dev, &plci, nullptr, &pipelineLayout_) != VK_SUCCESS)
        throw std::runtime_error("ComputePipeline: vkCreatePipelineLayout failed");

    // ── Shader module ─────────────────────────────────────────────────────────
    auto code = loadSpv(spvPath);

    VkShaderModuleCreateInfo smci{};
    smci.sType    = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO;
    smci.codeSize = code.size() * sizeof(uint32_t);
    smci.pCode    = code.data();

    VkShaderModule shaderModule = VK_NULL_HANDLE;
    if (vkCreateShaderModule(dev, &smci, nullptr, &shaderModule) != VK_SUCCESS)
        throw std::runtime_error("ComputePipeline: vkCreateShaderModule failed for " + spvPath);

    // ── Compute pipeline ──────────────────────────────────────────────────────
    VkComputePipelineCreateInfo cpci{};
    cpci.sType              = VK_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO;
    cpci.stage.sType        = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    cpci.stage.stage        = VK_SHADER_STAGE_COMPUTE_BIT;
    cpci.stage.module       = shaderModule;
    cpci.stage.pName        = "main";
    cpci.layout             = pipelineLayout_;

    VkResult r = vkCreateComputePipelines(dev, VK_NULL_HANDLE, 1, &cpci, nullptr, &pipeline_);
    vkDestroyShaderModule(dev, shaderModule, nullptr); // safe to destroy after pipeline creation
    if (r != VK_SUCCESS)
        throw std::runtime_error("ComputePipeline: vkCreateComputePipelines failed for " + spvPath);
}

ComputePipeline::~ComputePipeline() {
    VkDevice dev = ctx_.device();
    if (pipeline_)       vkDestroyPipeline(dev, pipeline_, nullptr);
    if (pipelineLayout_) vkDestroyPipelineLayout(dev, pipelineLayout_, nullptr);
    if (descPool_)       vkDestroyDescriptorPool(dev, descPool_, nullptr);
    if (descSetLayout_)  vkDestroyDescriptorSetLayout(dev, descSetLayout_, nullptr);
}

void ComputePipeline::bindBuffers(VkCommandBuffer              cmd,
                                   const std::vector<VkBuffer>& buffers,
                                   const void*                  pushData) {
    if (buffers.size() != bindingCount_)
        throw std::invalid_argument("ComputePipeline::bindBuffers: wrong buffer count");

    // Write descriptor set with the provided buffer handles.
    std::vector<VkDescriptorBufferInfo> bufInfos(bindingCount_);
    std::vector<VkWriteDescriptorSet>   writes(bindingCount_);

    for (uint32_t i = 0; i < bindingCount_; ++i) {
        bufInfos[i].buffer = buffers[i];
        bufInfos[i].offset = 0;
        bufInfos[i].range  = VK_WHOLE_SIZE;

        writes[i].sType           = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        writes[i].dstSet          = descSet_;
        writes[i].dstBinding      = i;
        writes[i].descriptorCount = 1;
        writes[i].descriptorType  = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
        writes[i].pBufferInfo     = &bufInfos[i];
    }
    vkUpdateDescriptorSets(ctx_.device(),
                           bindingCount_, writes.data(),
                           0, nullptr);

    vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, pipeline_);
    vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_COMPUTE,
                            pipelineLayout_, 0, 1, &descSet_, 0, nullptr);

    if (pushData && pushConstantSize_ > 0)
        vkCmdPushConstants(cmd, pipelineLayout_,
                           VK_SHADER_STAGE_COMPUTE_BIT,
                           0, pushConstantSize_, pushData);
}

void ComputePipeline::dispatch(VkCommandBuffer cmd,
                                uint32_t groupsX, uint32_t groupsY, uint32_t groupsZ) {
    vkCmdDispatch(cmd, groupsX, groupsY, groupsZ);
}
