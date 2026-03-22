#include "vk_context.hpp"
#include <cstring>
#include <iostream>
#include <stdexcept>
#include <vector>

// ─── Validation layer callback ────────────────────────────────────────────────

static VKAPI_ATTR VkBool32 VKAPI_CALL debugCallback(
    VkDebugUtilsMessageSeverityFlagBitsEXT      severity,
    VkDebugUtilsMessageTypeFlagsEXT             /*type*/,
    const VkDebugUtilsMessengerCallbackDataEXT* data,
    void* /*userdata*/)
{
    if (severity >= VK_DEBUG_UTILS_MESSAGE_SEVERITY_WARNING_BIT_EXT)
        std::cerr << "[Vulkan] " << data->pMessage << '\n';
    return VK_FALSE;
}

// ─── VulkanContext ────────────────────────────────────────────────────────────

VulkanContext::VulkanContext(bool enableValidation) {
    createInstance(enableValidation);
    pickPhysicalDevice();
    createDevice();
}

VulkanContext::~VulkanContext() {
    if (device_ != VK_NULL_HANDLE)
        vkDestroyDevice(device_, nullptr);

    if (debugMessenger_ != VK_NULL_HANDLE) {
        auto fn = reinterpret_cast<PFN_vkDestroyDebugUtilsMessengerEXT>(
            vkGetInstanceProcAddr(instance_, "vkDestroyDebugUtilsMessengerEXT"));
        if (fn) fn(instance_, debugMessenger_, nullptr);
    }

    if (instance_ != VK_NULL_HANDLE)
        vkDestroyInstance(instance_, nullptr);
}

void VulkanContext::createInstance(bool enableValidation) {
    VkApplicationInfo appInfo{};
    appInfo.sType              = VK_STRUCTURE_TYPE_APPLICATION_INFO;
    appInfo.pApplicationName   = "Alchemation";
    appInfo.applicationVersion = VK_MAKE_VERSION(1, 0, 0);
    appInfo.apiVersion         = VK_API_VERSION_1_3;

    std::vector<const char*> extensions;
    std::vector<const char*> layers;

    if (enableValidation) {
        extensions.push_back(VK_EXT_DEBUG_UTILS_EXTENSION_NAME);
        layers.push_back("VK_LAYER_KHRONOS_validation");
    }

    VkInstanceCreateInfo ci{};
    ci.sType                   = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO;
    ci.pApplicationInfo        = &appInfo;
    ci.enabledExtensionCount   = static_cast<uint32_t>(extensions.size());
    ci.ppEnabledExtensionNames = extensions.data();
    ci.enabledLayerCount       = static_cast<uint32_t>(layers.size());
    ci.ppEnabledLayerNames     = layers.data();

    if (vkCreateInstance(&ci, nullptr, &instance_) != VK_SUCCESS)
        throw std::runtime_error("VulkanContext: vkCreateInstance failed");

    if (enableValidation) {
        VkDebugUtilsMessengerCreateInfoEXT dbg{};
        dbg.sType           = VK_STRUCTURE_TYPE_DEBUG_UTILS_MESSENGER_CREATE_INFO_EXT;
        dbg.messageSeverity = VK_DEBUG_UTILS_MESSAGE_SEVERITY_WARNING_BIT_EXT |
                              VK_DEBUG_UTILS_MESSAGE_SEVERITY_ERROR_BIT_EXT;
        dbg.messageType     = VK_DEBUG_UTILS_MESSAGE_TYPE_GENERAL_BIT_EXT |
                              VK_DEBUG_UTILS_MESSAGE_TYPE_VALIDATION_BIT_EXT;
        dbg.pfnUserCallback = debugCallback;

        auto fn = reinterpret_cast<PFN_vkCreateDebugUtilsMessengerEXT>(
            vkGetInstanceProcAddr(instance_, "vkCreateDebugUtilsMessengerEXT"));
        if (fn) fn(instance_, &dbg, nullptr, &debugMessenger_);
    }
}

void VulkanContext::pickPhysicalDevice() {
    uint32_t count = 0;
    vkEnumeratePhysicalDevices(instance_, &count, nullptr);
    if (count == 0)
        throw std::runtime_error("VulkanContext: no Vulkan-capable GPU found");

    std::vector<VkPhysicalDevice> devices(count);
    vkEnumeratePhysicalDevices(instance_, &count, devices.data());

    // Prefer a discrete GPU; fall back to the first available device.
    VkPhysicalDevice fallback = devices[0];
    for (VkPhysicalDevice dev : devices) {
        VkPhysicalDeviceProperties props;
        vkGetPhysicalDeviceProperties(dev, &props);
        if (props.deviceType == VK_PHYSICAL_DEVICE_TYPE_DISCRETE_GPU) {
            physDevice_ = dev;
            deviceName_ = props.deviceName;
            break;
        }
    }
    if (physDevice_ == VK_NULL_HANDLE) {
        physDevice_ = fallback;
        VkPhysicalDeviceProperties props;
        vkGetPhysicalDeviceProperties(physDevice_, &props);
        deviceName_ = props.deviceName;
    }
}

void VulkanContext::createDevice() {
    // Find a queue family that supports compute.
    uint32_t qCount = 0;
    vkGetPhysicalDeviceQueueFamilyProperties(physDevice_, &qCount, nullptr);
    std::vector<VkQueueFamilyProperties> families(qCount);
    vkGetPhysicalDeviceQueueFamilyProperties(physDevice_, &qCount, families.data());

    computeFamily_ = UINT32_MAX;
    for (uint32_t i = 0; i < qCount; ++i) {
        if (families[i].queueFlags & VK_QUEUE_COMPUTE_BIT) {
            computeFamily_ = i;
            break;
        }
    }
    if (computeFamily_ == UINT32_MAX)
        throw std::runtime_error("VulkanContext: no compute queue family found");

    float priority = 1.0f;
    VkDeviceQueueCreateInfo qci{};
    qci.sType            = VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO;
    qci.queueFamilyIndex = computeFamily_;
    qci.queueCount       = 1;
    qci.pQueuePriorities = &priority;

    VkDeviceCreateInfo dci{};
    dci.sType            = VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO;
    dci.queueCreateInfoCount = 1;
    dci.pQueueCreateInfos    = &qci;

    if (vkCreateDevice(physDevice_, &dci, nullptr, &device_) != VK_SUCCESS)
        throw std::runtime_error("VulkanContext: vkCreateDevice failed");

    vkGetDeviceQueue(device_, computeFamily_, 0, &computeQueue_);
}

uint32_t VulkanContext::findMemoryType(uint32_t typeFilter,
                                       VkMemoryPropertyFlags props) const {
    VkPhysicalDeviceMemoryProperties memProps;
    vkGetPhysicalDeviceMemoryProperties(physDevice_, &memProps);

    for (uint32_t i = 0; i < memProps.memoryTypeCount; ++i) {
        if ((typeFilter & (1u << i)) &&
            (memProps.memoryTypes[i].propertyFlags & props) == props)
            return i;
    }
    throw std::runtime_error("VulkanContext: no suitable memory type found");
}

VkDeviceMemory VulkanContext::allocate(VkBuffer buffer,
                                       VkMemoryPropertyFlags props) const {
    VkMemoryRequirements req;
    vkGetBufferMemoryRequirements(device_, buffer, &req);

    VkMemoryAllocateInfo ai{};
    ai.sType           = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
    ai.allocationSize  = req.size;
    ai.memoryTypeIndex = findMemoryType(req.memoryTypeBits, props);

    VkDeviceMemory mem = VK_NULL_HANDLE;
    if (vkAllocateMemory(device_, &ai, nullptr, &mem) != VK_SUCCESS)
        throw std::runtime_error("VulkanContext: vkAllocateMemory failed");
    vkBindBufferMemory(device_, buffer, mem, 0);
    return mem;
}
