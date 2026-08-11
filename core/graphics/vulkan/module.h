// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#pragma once


#include "global.h"

#include <core/graphics/api.h>


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

// One real VkQueue exposed to the RHI. The registry may contain more than one entry with the same broad queue
// class; `primaryForClass` only preserves legacy CommandQueue-based callers while graph packets use the physical
// ID assigned by Device.
struct VulkanPhysicalQueueDesc{
    VkQueue queue = VK_NULL_HANDLE;
    CommandQueue::Enum queueClass = CommandQueue::kCount;
    GpuQueueCapability::Mask capabilities = GpuQueueCapability::None;
    u32 familyIndex = Limit<u32>::s_Max;
    u32 queueIndex = 0u;
    bool dedicated = false;
    bool primaryForClass = false;
};

struct DeviceDesc{
    VkInstance instance = VK_NULL_HANDLE;
    VkPhysicalDevice physicalDevice = VK_NULL_HANDLE;
    VkDevice device = VK_NULL_HANDLE;

    // Queue handles grouped together, then their integer indices packed back-to-back, to avoid the
    // 4-byte padding that an interleaved handle/index layout would otherwise introduce between each pair.
    VkQueue graphicsQueue = VK_NULL_HANDLE;
    VkQueue computeQueue = VK_NULL_HANDLE;
    VkQueue transferQueue = VK_NULL_HANDLE;
    i32 graphicsQueueIndex = s_InvalidQueueFamilyIndex;
    i32 computeQueueIndex = s_InvalidQueueFamilyIndex;
    i32 transferQueueIndex = s_InvalidQueueFamilyIndex;
    // True only when the renderer requested AsyncCompute and a distinct dedicated Compute family was created.
    bool asyncComputeLaneEnabled = false;
    // True only when the renderer requested Transfer and a distinct dedicated transfer-only family was created.
    bool transferQueueEnabled = false;

    // Preferred native registry input. Existing grouped fields above remain as a source-compatible fallback for
    // older creation code; new creation paths should enumerate every active VkQueue here.
    const VulkanPhysicalQueueDesc* physicalQueues = nullptr;
    usize physicalQueueCount = 0u;

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
    // Enabled feature-chain state for optional ray-tracing capabilities.  Keep this distinct from extension-name
    // presence so runtime feature queries do not advertise an extension whose required feature bit was not enabled.
    bool accelerationStructureFeatureEnabled = false;
    bool rayTracingPipelineFeatureEnabled = false;
    bool rayQueryFeatureEnabled = false;
    bool opacityMicromapFeatureEnabled = false;
    bool clusterAccelerationStructureFeatureEnabled = false;
    bool rayTracingInvocationReorderFeatureEnabled = false;
    bool rayTracingInvocationReorderExtFeatureEnabled = false;
    // Indicates if VK_NV_cooperative_vector feature bits were enabled at device creation time.
    bool cooperativeVectorFeatureEnabled = false;
    bool cooperativeVectorTrainingFeatureEnabled = false;
    // Indicates if VK_EXT_mesh_shader taskShader was enabled at device creation time
    bool meshTaskShaderSupported = false;
    // Indicates if VK_NV_ray_tracing_linear_swept_spheres spheres was enabled at device creation time
    bool rayTracingSpheresSupported = false;
    // Indicates if VK_NV_ray_tracing_linear_swept_spheres linearSweptSpheres was enabled at device creation time
    bool rayTracingLinearSweptSpheresSupported = false;
    bool gpuCrashDiagnosticsEnabled = false;
    bool logBufferLifetime = false;

    GpuDescriptorHeapAbi bindlessHeapAbi;
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

