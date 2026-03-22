#pragma once

#include "vk_context.hpp"
#include <string>
#include <vector>
#include <cstdint>

// ─── ComputePipeline ──────────────────────────────────────────────────────────
//
// Wraps a single Vulkan compute pipeline.
//
// Every compute shader used by the simulation follows the same descriptor layout:
//   binding 0..N  — storage buffers (VK_DESCRIPTOR_TYPE_STORAGE_BUFFER)
//
// The caller binds actual buffers via bindBuffers() before each dispatch.
// A new descriptor set is written each time bindBuffers() is called, which is
// fine at our dispatch rate (one per system per tick).
//
// Push constants carry per-dispatch parameters (entity count, current tick, etc.)
// up to pushConstantSize bytes, which the caller sets via pushConstants().

class ComputePipeline {
public:
    ComputePipeline(const VulkanContext& ctx,
                    const std::string&   spvPath,
                    uint32_t             bindingCount,
                    uint32_t             pushConstantSize = 0);
    ~ComputePipeline();

    ComputePipeline(const ComputePipeline&)            = delete;
    ComputePipeline& operator=(const ComputePipeline&) = delete;

    // Bind storage buffers for the next dispatch (one per binding, in order).
    // Also records the push-constant data if pushData != nullptr.
    void bindBuffers(VkCommandBuffer              cmd,
                     const std::vector<VkBuffer>& buffers,
                     const void*                  pushData = nullptr);

    // Issue vkCmdDispatch. groupsX = ceil(entityCount / localSizeX).
    void dispatch(VkCommandBuffer cmd, uint32_t groupsX,
                  uint32_t groupsY = 1, uint32_t groupsZ = 1);

    VkPipeline       pipeline()       const { return pipeline_; }
    VkPipelineLayout pipelineLayout() const { return pipelineLayout_; }

private:
    const VulkanContext& ctx_;
    uint32_t             bindingCount_;
    uint32_t             pushConstantSize_;

    VkDescriptorSetLayout descSetLayout_   = VK_NULL_HANDLE;
    VkDescriptorPool      descPool_        = VK_NULL_HANDLE;
    VkDescriptorSet       descSet_         = VK_NULL_HANDLE;
    VkPipelineLayout      pipelineLayout_  = VK_NULL_HANDLE;
    VkPipeline            pipeline_        = VK_NULL_HANDLE;

    static std::vector<uint32_t> loadSpv(const std::string& path);
};
