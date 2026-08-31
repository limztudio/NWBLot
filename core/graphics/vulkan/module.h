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

// Canonical native queue construction identity. Enumerate every queue created by VkDeviceQueueCreateInfo exactly
// once, including queues used only for presentation. Device materializes stable synchronized states from this table,
// and scheduler-visible physical queues reference those states by index.
struct VulkanNativeQueueDesc{
    VkQueue queue = VK_NULL_HANDLE;
    u32 familyIndex = Limit<u32>::s_Max;
    u32 queueIndex = Limit<u32>::s_Max;
};

// One canonical native queue exposed to the RHI scheduler. The projection may contain more than one entry with the
// same broad class; `primaryForClass` only preserves legacy CommandQueue-based callers while graph packets use the
// physical ID assigned by Device.
struct VulkanPhysicalQueueDesc{
    u32 nativeQueueIndex = Limit<u32>::s_Max;
    CommandQueue::Enum queueClass = CommandQueue::kCount;
    GpuQueueCapability::Mask capabilities = GpuQueueCapability::None;
    u32 timestampValidBits = 0u;
    bool dedicated = false;
    bool primaryForClass = false;
};

struct DeviceDesc{
    VkInstance instance = VK_NULL_HANDLE;
    VkPhysicalDevice physicalDevice = VK_NULL_HANDLE;
    VkDevice device = VK_NULL_HANDLE;
    PFN_vkGetInstanceProcAddr getInstanceProcAddr = nullptr;
    VolkInstanceTable instanceDispatch = {};
    VolkDeviceTable deviceDispatch = {};

    // Required canonical registry inputs, synchronously consumed by CreateDevice. The native table includes queues
    // that are intentionally absent from physical task-graph topology, such as a distinct present-only queue.
    const VulkanNativeQueueDesc* nativeQueues = nullptr;
    usize nativeQueueCount = 0u;
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
    // Retains the exact VkPhysicalDeviceVulkan12Features::hostQueryReset bit enabled at device creation time.
    bool hostQueryResetFeatureEnabled = false;
    // Retains the texture-compression features enabled through vkCreateDevice.
    bool textureCompressionBcFeatureEnabled = false;
    bool textureCompressionAstcLdrFeatureEnabled = false;
    bool textureCompressionAstcHdrFeatureEnabled = false;
    // Indicates if dynamic rendering was enabled at device creation time (via Vulkan 1.3 core or KHR extension)
    bool dynamicRenderingSupported = false;
    // Indicates if synchronization2 was enabled at device creation time (via Vulkan 1.3 core or KHR extension)
    bool synchronization2Supported = false;
    bool independentBlendFeatureEnabled = false;
    bool fullDrawIndexUint32FeatureEnabled = false;
    bool multiDrawIndirectFeatureEnabled = false;
    bool drawIndirectFirstInstanceFeatureEnabled = false;
    bool meshShaderFeatureEnabled = false;
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

