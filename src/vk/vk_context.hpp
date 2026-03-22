#pragma once

#include <vulkan/vulkan.h>
#include <stdexcept>
#include <string>
#include <vector>
#include <cstdint>

// ─── VulkanContext ────────────────────────────────────────────────────────────
//
// Owns the Vulkan instance, physical device, logical device, and compute queue.
// One instance is created at startup and lives for the duration of the program.
//
// Compute-only: no surface, swapchain, or graphics queue. SDL2 continues to
// handle all rendering via its own path.

class VulkanContext {
public:
    explicit VulkanContext(bool enableValidation = false);
    ~VulkanContext();

    // Non-copyable, non-movable — owns raw Vulkan handles.
    VulkanContext(const VulkanContext&)            = delete;
    VulkanContext& operator=(const VulkanContext&) = delete;

    VkDevice         device()         const { return device_; }
    VkPhysicalDevice physicalDevice() const { return physDevice_; }
    VkQueue          computeQueue()   const { return computeQueue_; }
    uint32_t         computeFamily()  const { return computeFamily_; }

    // GPU name for diagnostics.
    const std::string& deviceName() const { return deviceName_; }

    // Allocate device-local memory for a storage buffer.
    // Returns the allocated VkDeviceMemory; caller binds it to the buffer.
    VkDeviceMemory allocate(VkBuffer buffer, VkMemoryPropertyFlags props) const;

    // Find a memory type index satisfying both the type filter and property flags.
    uint32_t findMemoryType(uint32_t typeFilter, VkMemoryPropertyFlags props) const;

private:
    VkInstance       instance_     = VK_NULL_HANDLE;
    VkPhysicalDevice physDevice_   = VK_NULL_HANDLE;
    VkDevice         device_       = VK_NULL_HANDLE;
    VkQueue          computeQueue_ = VK_NULL_HANDLE;
    uint32_t         computeFamily_ = 0;
    std::string      deviceName_;

    VkDebugUtilsMessengerEXT debugMessenger_ = VK_NULL_HANDLE;

    void createInstance(bool enableValidation);
    void pickPhysicalDevice();
    void createDevice();
};
