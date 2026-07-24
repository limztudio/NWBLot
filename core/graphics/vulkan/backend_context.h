// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#pragma once


#include "backend.h"


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_VULKAN_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace DeviceExtensionFeature{
    enum Enum : u8{
        None = 0,
        AccelerationStructure,
        RayTracingPipeline,
        RayQuery,
        OpacityMicromap,
        ClusterAccelerationStructure,
        RayTracingInvocationReorder,
        RayTracingInvocationReorderExt,
        RayTracingLinearSweptSpheres,
        MeshShader,
        FragmentShadingRate,
        MutableDescriptorType,
        DescriptorHeap,
        DescriptorBuffer,
        DeviceFault,
        Count,
    };
};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


class BackendContext final{
private:
    using ExtensionStringSet = HashSet<GraphicsString, Hasher<GraphicsString>, EqualTo<GraphicsString>, GraphicsArena>;
    using DeviceExtensionMap = HashMap<GraphicsString, DeviceExtensionFeature::Enum, Hasher<GraphicsString>, EqualTo<GraphicsString>, GraphicsArena>;

    struct ExtEntry{
        const char* name;
        DeviceExtensionFeature::Enum feature = DeviceExtensionFeature::None;
    };

    struct VulkanExtensionSet{
        ExtensionStringSet instance;
        ExtensionStringSet layers;
        DeviceExtensionMap device;

        explicit VulkanExtensionSet(Alloc::GlobalArena& arena)
            : instance(0, Hasher<GraphicsString>(), EqualTo<GraphicsString>(), arena)
            , layers(0, Hasher<GraphicsString>(), EqualTo<GraphicsString>(), arena)
            , device(0, Hasher<GraphicsString>(), EqualTo<GraphicsString>(), arena)
        {}
    };

    struct SwapChainImage{
        VkImage image;
        TextureHandle rhiHandle;
    };
    using SemaphoreVector = GraphicsVector<VkSemaphore>;


private:
    static constexpr const char* s_EnabledInstanceExts[] = {
        VK_KHR_GET_PHYSICAL_DEVICE_PROPERTIES_2_EXTENSION_NAME,
    };
    static constexpr const char* s_DebugInstanceExts[] = {
        VK_EXT_DEBUG_UTILS_EXTENSION_NAME,
    };

private:
    static constexpr ExtEntry s_EnabledDeviceExts[] = {
        { VK_KHR_MAINTENANCE1_EXTENSION_NAME, DeviceExtensionFeature::None },
    };
    static constexpr ExtEntry s_OptionalDeviceExts[] = {
        { VK_EXT_DESCRIPTOR_INDEXING_EXTENSION_NAME, DeviceExtensionFeature::None },
        { VK_EXT_DESCRIPTOR_HEAP_EXTENSION_NAME, DeviceExtensionFeature::DescriptorHeap },
        { VK_EXT_DESCRIPTOR_BUFFER_EXTENSION_NAME, DeviceExtensionFeature::DescriptorBuffer },
        { VK_KHR_BUFFER_DEVICE_ADDRESS_EXTENSION_NAME, DeviceExtensionFeature::None },
        { VK_KHR_FRAGMENT_SHADING_RATE_EXTENSION_NAME, DeviceExtensionFeature::FragmentShadingRate },
        { VK_KHR_DYNAMIC_RENDERING_EXTENSION_NAME, DeviceExtensionFeature::None },
        { VK_KHR_MAINTENANCE_4_EXTENSION_NAME, DeviceExtensionFeature::None },
        { VK_EXT_SAMPLER_FILTER_MINMAX_EXTENSION_NAME, DeviceExtensionFeature::None },
        { VK_KHR_SWAPCHAIN_MUTABLE_FORMAT_EXTENSION_NAME, DeviceExtensionFeature::None },
        { VK_KHR_SYNCHRONIZATION_2_EXTENSION_NAME, DeviceExtensionFeature::None },
        { VK_EXT_MESH_SHADER_EXTENSION_NAME, DeviceExtensionFeature::MeshShader },
        { VK_EXT_MUTABLE_DESCRIPTOR_TYPE_EXTENSION_NAME, DeviceExtensionFeature::MutableDescriptorType },
        { VK_NV_COOPERATIVE_VECTOR_EXTENSION_NAME, DeviceExtensionFeature::None },
        { VK_EXT_DEVICE_FAULT_EXTENSION_NAME, DeviceExtensionFeature::DeviceFault },
        { VK_AMD_BUFFER_MARKER_EXTENSION_NAME, DeviceExtensionFeature::None },
    };
    static constexpr ExtEntry s_DebugDeviceExts[] = {
        { VK_EXT_DEBUG_MARKER_EXTENSION_NAME, DeviceExtensionFeature::None },
    };
    static constexpr ExtEntry s_RayTracingExts[] = {
        { VK_KHR_ACCELERATION_STRUCTURE_EXTENSION_NAME, DeviceExtensionFeature::AccelerationStructure },
        { VK_KHR_DEFERRED_HOST_OPERATIONS_EXTENSION_NAME, DeviceExtensionFeature::None },
        { VK_KHR_PIPELINE_LIBRARY_EXTENSION_NAME, DeviceExtensionFeature::None },
        { VK_KHR_RAY_QUERY_EXTENSION_NAME, DeviceExtensionFeature::RayQuery },
        { VK_KHR_RAY_TRACING_PIPELINE_EXTENSION_NAME, DeviceExtensionFeature::RayTracingPipeline },
        { VK_EXT_OPACITY_MICROMAP_EXTENSION_NAME, DeviceExtensionFeature::OpacityMicromap },
        { VK_EXT_RAY_TRACING_INVOCATION_REORDER_EXTENSION_NAME, DeviceExtensionFeature::RayTracingInvocationReorderExt },
        { VK_NV_CLUSTER_ACCELERATION_STRUCTURE_EXTENSION_NAME, DeviceExtensionFeature::ClusterAccelerationStructure },
        { VK_NV_RAY_TRACING_INVOCATION_REORDER_EXTENSION_NAME, DeviceExtensionFeature::RayTracingInvocationReorder },
        { VK_NV_RAY_TRACING_LINEAR_SWEPT_SPHERES_EXTENSION_NAME, DeviceExtensionFeature::RayTracingLinearSweptSpheres },
    };


public:
    BackendContext(
        const DeviceCreationParameters& params,
        SwapChainRuntimeState& swapChainState,
        GraphicsAllocator& allocator,
        Alloc::ThreadPool& threadPool
    );


public:
    [[nodiscard]] Device* getDevice()const{ return m_rhiDevice.get(); }
    [[nodiscard]] const tchar* getRendererString()const{ return m_rendererString.c_str(); }
    bool enumerateAdapters(GraphicsVector<AdapterInfo>& outAdapters);
    [[nodiscard]] bool isValidationMessageLocationIgnored(usize location)const;

    [[nodiscard]] bool isInstanceExtensionEnabled(const char* extensionName)const{
        const GraphicsString lookup(extensionName, m_arena);
        return m_enabledExtensions.instance.find(lookup) != m_enabledExtensions.instance.end();
    }
    [[nodiscard]] bool isDeviceExtensionEnabled(const char* extensionName)const{
        const GraphicsString lookup(extensionName, m_arena);
        return m_enabledExtensions.device.find(lookup) != m_enabledExtensions.device.end();
    }
    [[nodiscard]] bool isLayerEnabled(const char* layerName)const{
        const GraphicsString lookup(layerName, m_arena);
        return m_enabledExtensions.layers.find(lookup) != m_enabledExtensions.layers.end();
    }

    Texture* getCurrentBackBuffer()const;
    Texture* getBackBuffer(u32 index)const;
    u32 getCurrentBackBufferIndex()const{ return m_swapChainIndex; }
    u32 getBackBufferCount()const{ return static_cast<u32>(m_swapChainImages.size()); }

    void setPlatformFrameParam(const Common::FrameParam& frameParam){ m_platformFrameParam = frameParam; }
    bool createInstance();
    bool createDevice();
    bool createSwapChain();
    void destroy();
    void resizeSwapChain();
    bool beginFrame(const BackBufferResizeCallbacks& callbacks);
    bool present();
    void reportLiveObjects()const{}

private:
    void initDefaultExtensions();
    bool createVulkanInstance();
    bool createWindowSurface();
    void installDebugCallback();
    bool pickPhysicalDevice();
    bool findQueueFamilies(VkPhysicalDevice physicalDevice);
    bool createVulkanDevice();
    bool createVulkanSwapChain();
    void destroySwapChain();
    void clearSemaphores(SemaphoreVector& semaphores);
    bool recreateSemaphores(SemaphoreVector& semaphores, usize count, AStringView operationName);


private:
    const DeviceCreationParameters& m_deviceParams;
    SwapChainRuntimeState& m_swapChainState;
    GraphicsAllocator& m_allocator;
    Alloc::ThreadPool& m_threadPool;
    Alloc::GlobalArena& m_arena;
    Common::FrameParam m_platformFrameParam = {};

private:
    VulkanExtensionSet m_enabledExtensions;
    VulkanExtensionSet m_optionalExtensions;

    DeviceExtensionMap m_rayTracingExtensions;

    GraphicsTString m_rendererString;

    VkInstance m_vulkanInstance = VK_NULL_HANDLE;
    VkDebugReportCallbackEXT m_debugReportCallback = VK_NULL_HANDLE;

    VkPhysicalDevice m_vulkanPhysicalDevice = VK_NULL_HANDLE;
    i32 m_graphicsQueueFamily = s_InvalidQueueFamilyIndex;
    i32 m_computeQueueFamily = s_InvalidQueueFamilyIndex;
    i32 m_transferQueueFamily = s_InvalidQueueFamilyIndex;
    i32 m_presentQueueFamily = s_InvalidQueueFamilyIndex;

    VkDevice m_vulkanDevice = VK_NULL_HANDLE;
    VkQueue m_graphicsQueue = VK_NULL_HANDLE;
    VkQueue m_computeQueue = VK_NULL_HANDLE;
    VkQueue m_transferQueue = VK_NULL_HANDLE;
    VkQueue m_presentQueue = VK_NULL_HANDLE;

    VkSurfaceKHR m_windowSurface = VK_NULL_HANDLE;

    VkSurfaceFormatKHR m_swapChainFormat = {};
    VkSwapchainKHR m_swapChain = VK_NULL_HANDLE;

    GraphicsVector<SwapChainImage> m_swapChainImages;
    DeviceHandle m_rhiDevice;

    SemaphoreVector m_acquireSemaphores;
    SemaphoreVector m_presentSemaphores;

    ::Queue<EventQueryHandle, Alloc::GlobalArena> m_framesInFlight;
    Vector<EventQueryHandle, Alloc::GlobalArena> m_queryPool;

    u32 m_swapChainIndex = static_cast<u32>(-1);
    u32 m_acquireSemaphoreIndex = 0;
    u32 m_maxFramesInFlight = s_MaxFramesInFlight;

    bool m_swapChainMutableFormatSupported = false;
    bool m_bufferDeviceAddressSupported = false;
    bool m_dynamicRenderingSupported = false;
    bool m_synchronization2Supported = false;
    bool m_meshTaskShaderSupported = false;
    bool m_rayTracingSpheresSupported = false;
    bool m_rayTracingLinearSweptSpheresSupported = false;
};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_VULKAN_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

