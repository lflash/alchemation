#include "gpu_runner.hpp"
#include <stdexcept>

GpuRunner::GpuRunner(const VulkanContext& ctx) : ctx_(ctx) {
    VkCommandPoolCreateInfo cpci{};
    cpci.sType            = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO;
    cpci.queueFamilyIndex = ctx.computeFamily();
    cpci.flags            = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT;
    if (vkCreateCommandPool(ctx.device(), &cpci, nullptr, &cmdPool_) != VK_SUCCESS)
        throw std::runtime_error("GpuRunner: vkCreateCommandPool failed");

    VkCommandBufferAllocateInfo cbai{};
    cbai.sType              = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
    cbai.commandPool        = cmdPool_;
    cbai.level              = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
    cbai.commandBufferCount = 1;
    if (vkAllocateCommandBuffers(ctx.device(), &cbai, &cmd_) != VK_SUCCESS)
        throw std::runtime_error("GpuRunner: vkAllocateCommandBuffers failed");

    VkFenceCreateInfo fci{};
    fci.sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO;
    if (vkCreateFence(ctx.device(), &fci, nullptr, &fence_) != VK_SUCCESS)
        throw std::runtime_error("GpuRunner: vkCreateFence failed");
}

GpuRunner::~GpuRunner() {
    VkDevice dev = ctx_.device();
    if (fence_)   vkDestroyFence(dev, fence_, nullptr);
    if (cmdPool_) vkDestroyCommandPool(dev, cmdPool_, nullptr);
    // cmd_ is implicitly freed with the pool
}

void GpuRunner::submit(const std::function<void(VkCommandBuffer)>& record) {
    // Reset and record.
    vkResetCommandBuffer(cmd_, 0);

    VkCommandBufferBeginInfo bi{};
    bi.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
    bi.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
    if (vkBeginCommandBuffer(cmd_, &bi) != VK_SUCCESS)
        throw std::runtime_error("GpuRunner: vkBeginCommandBuffer failed");

    record(cmd_);

    if (vkEndCommandBuffer(cmd_) != VK_SUCCESS)
        throw std::runtime_error("GpuRunner: vkEndCommandBuffer failed");

    // Submit and wait.
    vkResetFences(ctx_.device(), 1, &fence_);

    VkSubmitInfo si{};
    si.sType              = VK_STRUCTURE_TYPE_SUBMIT_INFO;
    si.commandBufferCount = 1;
    si.pCommandBuffers    = &cmd_;
    if (vkQueueSubmit(ctx_.computeQueue(), 1, &si, fence_) != VK_SUCCESS)
        throw std::runtime_error("GpuRunner: vkQueueSubmit failed");

    vkWaitForFences(ctx_.device(), 1, &fence_, VK_TRUE, UINT64_MAX);
}

void GpuRunner::barrier(VkCommandBuffer cmd) {
    VkMemoryBarrier mb{};
    mb.sType         = VK_STRUCTURE_TYPE_MEMORY_BARRIER;
    mb.srcAccessMask = VK_ACCESS_SHADER_WRITE_BIT;
    mb.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;

    vkCmdPipelineBarrier(cmd,
                         VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
                         VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
                         0, 1, &mb, 0, nullptr, 0, nullptr);
}
