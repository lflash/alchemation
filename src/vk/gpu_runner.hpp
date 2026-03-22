#pragma once

#include "vk_context.hpp"
#include <cstdint>
#include <functional>

// ─── GpuRunner ────────────────────────────────────────────────────────────────
//
// Owns the command pool and a reusable command buffer for the compute queue.
// Wraps the record → submit → wait pattern so callers don't touch raw Vulkan
// synchronisation.
//
// Usage:
//   runner.submit([&](VkCommandBuffer cmd) {
//       pipeline.bindBuffers(cmd, {buf.buffer()}, &pushConst);
//       pipeline.dispatch(cmd, groups);
//   });
//   // CPU blocks until the GPU work completes.

class GpuRunner {
public:
    explicit GpuRunner(const VulkanContext& ctx);
    ~GpuRunner();

    GpuRunner(const GpuRunner&)            = delete;
    GpuRunner& operator=(const GpuRunner&) = delete;

    // Record commands via the provided lambda, submit, and wait for completion.
    void submit(const std::function<void(VkCommandBuffer)>& record);

    // Convenience: add a global memory barrier between pipeline stages.
    static void barrier(VkCommandBuffer cmd);

private:
    const VulkanContext& ctx_;
    VkCommandPool        cmdPool_ = VK_NULL_HANDLE;
    VkCommandBuffer      cmd_     = VK_NULL_HANDLE;
    VkFence              fence_   = VK_NULL_HANDLE;
};
