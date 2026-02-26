// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#pragma once


#include "global.h"


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_VULKAN_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


class IDevice : public Core::IDevice{
    using Core::IDevice::IDevice;


public:
    // Additional Vulkan-specific public methods
    virtual VkSemaphore getQueueSemaphore(CommandQueue::Enum queue) = 0;
    virtual void queueWaitForSemaphore(CommandQueue::Enum waitQueue, VkSemaphore semaphore, u64 value) = 0;
    virtual void queueSignalSemaphore(CommandQueue::Enum executionQueue, VkSemaphore semaphore, u64 value) = 0;
    virtual u64 queueGetCompletedInstance(CommandQueue::Enum queue) = 0;
};
typedef RefCountPtr<IDevice, ArenaRefDeleter<IDevice>> DeviceHandle;

struct DeviceDesc{
    VkInstance instance = VK_NULL_HANDLE;
    VkPhysicalDevice physicalDevice = VK_NULL_HANDLE;
    VkDevice device = VK_NULL_HANDLE;

    VkQueue graphicsQueue = VK_NULL_HANDLE;
    i32 graphicsQueueIndex = -1;
    VkQueue transferQueue = VK_NULL_HANDLE;
    i32 transferQueueIndex = -1;
    VkQueue computeQueue = VK_NULL_HANDLE;
    i32 computeQueueIndex = -1;

    const SystemMemoryAllocator* systemMemoryAllocator = nullptr;

    GraphicsAllocator& allocator;
    Alloc::ThreadPool& threadPool;

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


    explicit DeviceDesc(GraphicsAllocator& allocatorRef, Alloc::ThreadPool& threadPoolRef)
        : allocator(allocatorRef)
        , threadPool(threadPoolRef)
    {}
};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


extern DeviceHandle CreateDevice(const DeviceDesc& desc);

extern VkFormat ConvertFormat(Format::Enum format);

extern const tchar* ResultToString(VkResult result);


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_VULKAN_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

