// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#pragma once


#include "global.h"


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_VULKAN_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


class IDevice : public Core::IDevice{
public:
    // Additional Vulkan-specific public methods
    virtual VkSemaphore getQueueSemaphore(CommandQueue::Enum queue) = 0;
    virtual void queueWaitForSemaphore(CommandQueue::Enum waitQueue, VkSemaphore semaphore, u64 value) = 0;
    virtual void queueSignalSemaphore(CommandQueue::Enum executionQueue, VkSemaphore semaphore, u64 value) = 0;
    virtual u64 queueGetCompletedInstance(CommandQueue::Enum queue) = 0;
};
typedef RefCountPtr<IDevice, BlankDeleter<IDevice>> DeviceHandle;

struct DeviceDesc{
    VkInstance instance;
    VkPhysicalDevice physicalDevice;
    VkDevice device;

    VkQueue graphicsQueue;
    i32 graphicsQueueIndex = -1;
    VkQueue transferQueue;
    i32 transferQueueIndex = -1;
    VkQueue computeQueue;
    i32 computeQueueIndex = -1;

    VkAllocationCallbacks* allocationCallbacks = nullptr;

    const char** instanceExtensions = nullptr;
    usize numInstanceExtensions = 0;

    const char** deviceExtensions = nullptr;
    usize numDeviceExtensions = 0;

    u32 maxTimerQueries = 256;

    // Indicates if VkPhysicalDeviceVulkan12Features::bufferDeviceAddress was set to 'true' at device creation time
    bool bufferDeviceAddressSupported = false;
    bool aftermathEnabled = false;
    bool logBufferLifetime = false;

    AString vulkanLibraryName;
};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


extern DeviceHandle CreateDevice(const DeviceDesc& desc);

extern VkFormat ConvertFormat(Format::Enum format);

extern const tchar* ResultToString(VkResult result);


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_VULKAN_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

