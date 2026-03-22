#pragma once

#include "vk_context.hpp"
#include <cstring>
#include <stdexcept>
#include <vector>

// ─── GpuBuffer<T> ─────────────────────────────────────────────────────────────
//
// Typed wrapper around a Vulkan storage buffer.
//
// Usage pattern:
//   GpuBuffer<float> buf(ctx, 1024, GpuBuffer<float>::HostVisible);
//   buf.upload(hostVec.data(), hostVec.size());
//   // ... dispatch compute shader that binds buf.buffer() ...
//   buf.download(hostVec.data(), hostVec.size());
//
// Two memory strategies:
//   HostVisible  — buffer mapped permanently; upload/download are memcpy.
//                  Use for staging or small buffers that CPU reads every tick.
//   DeviceLocal  — fast VRAM; requires an explicit staging buffer for upload/download.
//                  Use for large entity buffers that only GPU touches mid-tick.
//
// For Phase 25a / 25b we use HostVisible everywhere for simplicity. Once the
// pipeline is proven correct, hot paths can be promoted to DeviceLocal + staging.

template<typename T>
class GpuBuffer {
public:
    enum Strategy { HostVisible, DeviceLocal };

    GpuBuffer(const VulkanContext& ctx, uint32_t capacity, Strategy strategy = HostVisible)
        : ctx_(ctx), capacity_(capacity), strategy_(strategy)
    {
        VkBufferCreateInfo bci{};
        bci.sType       = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
        bci.size        = static_cast<VkDeviceSize>(capacity) * sizeof(T);
        bci.usage       = VK_BUFFER_USAGE_STORAGE_BUFFER_BIT |
                          VK_BUFFER_USAGE_TRANSFER_SRC_BIT   |
                          VK_BUFFER_USAGE_TRANSFER_DST_BIT;
        bci.sharingMode = VK_SHARING_MODE_EXCLUSIVE;

        if (vkCreateBuffer(ctx.device(), &bci, nullptr, &buffer_) != VK_SUCCESS)
            throw std::runtime_error("GpuBuffer: vkCreateBuffer failed");

        VkMemoryPropertyFlags memFlags =
            (strategy == HostVisible)
                ? (VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT)
                : VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT;

        memory_ = ctx.allocate(buffer_, memFlags);

        if (strategy == HostVisible) {
            if (vkMapMemory(ctx.device(), memory_, 0, bci.size, 0,
                            reinterpret_cast<void**>(&mapped_)) != VK_SUCCESS)
                throw std::runtime_error("GpuBuffer: vkMapMemory failed");
        }
    }

    ~GpuBuffer() {
        if (mapped_)
            vkUnmapMemory(ctx_.device(), memory_);
        if (buffer_ != VK_NULL_HANDLE)
            vkDestroyBuffer(ctx_.device(), buffer_, nullptr);
        if (memory_ != VK_NULL_HANDLE)
            vkFreeMemory(ctx_.device(), memory_, nullptr);
    }

    GpuBuffer(const GpuBuffer&)            = delete;
    GpuBuffer& operator=(const GpuBuffer&) = delete;

    // Upload count elements from src into the buffer (HostVisible only).
    void upload(const T* src, uint32_t count) {
        if (strategy_ != HostVisible)
            throw std::logic_error("GpuBuffer::upload: use staging for DeviceLocal buffers");
        if (count > capacity_)
            throw std::out_of_range("GpuBuffer::upload: count exceeds capacity");
        std::memcpy(mapped_, src, count * sizeof(T));
    }

    // Download count elements from the buffer into dst (HostVisible only).
    void download(T* dst, uint32_t count) const {
        if (strategy_ != HostVisible)
            throw std::logic_error("GpuBuffer::download: use staging for DeviceLocal buffers");
        if (count > capacity_)
            throw std::out_of_range("GpuBuffer::download: count exceeds capacity");
        std::memcpy(dst, mapped_, count * sizeof(T));
    }

    // Convenience overloads for std::vector.
    void upload(const std::vector<T>& v)   { upload(v.data(), static_cast<uint32_t>(v.size())); }
    void download(std::vector<T>& v) const { download(v.data(), static_cast<uint32_t>(v.size())); }

    VkBuffer  buffer()   const { return buffer_; }
    uint32_t  capacity() const { return capacity_; }
    VkDeviceSize byteSize() const { return static_cast<VkDeviceSize>(capacity_) * sizeof(T); }

private:
    const VulkanContext& ctx_;
    uint32_t             capacity_;
    Strategy             strategy_;
    VkBuffer             buffer_ = VK_NULL_HANDLE;
    VkDeviceMemory       memory_ = VK_NULL_HANDLE;
    T*                   mapped_ = nullptr;
};
