// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#pragma once


#include "global.h"

#include <global/core/graphics/api.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_VULKAN_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


inline constexpr u32 s_DefaultUploadSuballocationAlignment = s_ConstantBufferOffsetSizeAlignment;
inline constexpr u64 s_AccelerationStructureAlignment = s_ConstantBufferOffsetSizeAlignment;

namespace ObjectTypes{
    inline constexpr ObjectType VK_Queue                               = 0x00030004;
    inline constexpr ObjectType VK_DeviceMemory                        = 0x00030006;
    inline constexpr ObjectType VK_Buffer                              = 0x00030007;
    inline constexpr ObjectType VK_Image                               = 0x00030008;
    inline constexpr ObjectType VK_ImageView                           = 0x00030009;
    inline constexpr ObjectType VK_AccelerationStructureKHR            = 0x0003000a;
    inline constexpr ObjectType VK_Pipeline                            = 0x00030013;
};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


class Device;
typedef Handle<Device> DeviceHandle;

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

    GraphicsAllocator& allocator;
    Alloc::ThreadPool& threadPool;

    VkAllocationCallbacks* allocationCallbacks = nullptr;

    const char** instanceExtensions = nullptr;
    usize numInstanceExtensions = 0;

    const char** deviceExtensions = nullptr;
    usize numDeviceExtensions = 0;

    // Indicates if VkPhysicalDeviceVulkan12Features::bufferDeviceAddress was set to 'true' at device creation time
    bool bufferDeviceAddressSupported = false;
    // Indicates if dynamic rendering was enabled at device creation time (via Vulkan 1.3 core or KHR extension)
    bool dynamicRenderingSupported = false;
    // Indicates if synchronization2 was enabled at device creation time (via Vulkan 1.3 core or KHR extension)
    bool synchronization2Supported = false;
    // Indicates if VK_EXT_mesh_shader taskShader was enabled at device creation time
    bool meshTaskShaderSupported = false;
    // Indicates if VK_NV_ray_tracing_linear_swept_spheres spheres was enabled at device creation time
    bool rayTracingSpheresSupported = false;
    // Indicates if VK_NV_ray_tracing_linear_swept_spheres linearSweptSpheres was enabled at device creation time
    bool rayTracingLinearSweptSpheresSupported = false;
    bool gpuCrashDiagnosticsEnabled = false;
    bool logBufferLifetime = false;

    GraphicsString vulkanLibraryName;
    Path pipelineCacheDirectory;


    explicit DeviceDesc(GraphicsAllocator& allocatorRef, Alloc::ThreadPool& threadPoolRef)
        : allocator(allocatorRef)
        , threadPool(threadPoolRef)
        , vulkanLibraryName(allocatorRef.getObjectArena())
        , pipelineCacheDirectory(allocatorRef.getObjectArena())
    {}
};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


extern DeviceHandle CreateDevice(const DeviceDesc& desc);

extern VkFormat ConvertFormat(Format::Enum format);

extern const tchar* ResultToString(VkResult result);


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_VULKAN_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

