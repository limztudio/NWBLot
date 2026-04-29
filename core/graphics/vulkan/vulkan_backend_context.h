// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#pragma once


#include "vulkan_backend.h"
#include "vulkan_backend_queries.h"


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
        RayTracingLinearSweptSpheres,
        MeshShader,
        FragmentShadingRate,
        MutableDescriptorType,
        DescriptorHeap,
        Count,
    };
};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


class BackendContext final : public IGraphicsBackend, public IBackendQueries{
private:
    using DeviceExtensionMap = HashMap<AString, DeviceExtensionFeature::Enum, Hasher<AString>, EqualTo<AString>, Alloc::CustomAllocator<Pair<const AString, DeviceExtensionFeature::Enum>>>;

    struct ExtEntry{
        const char* name;
        DeviceExtensionFeature::Enum feature = DeviceExtensionFeature::None;
    };

    struct VulkanExtensionSet{
        HashSet<AString, Hasher<AString>, EqualTo<AString>, Alloc::CustomAllocator<AString>> instance;
        HashSet<AString, Hasher<AString>, EqualTo<AString>, Alloc::CustomAllocator<AString>> layers;
        DeviceExtensionMap device;

        explicit VulkanExtensionSet(Alloc::CustomArena& arena)
            : instance(0, Hasher<AString>(), EqualTo<AString>(), Alloc::CustomAllocator<AString>(arena))
            , layers(0, Hasher<AString>(), EqualTo<AString>(), Alloc::CustomAllocator<AString>(arena))
            , device(0, Hasher<AString>(), EqualTo<AString>(), Alloc::CustomAllocator<Pair<const AString, DeviceExtensionFeature::Enum>>(arena))
        {}
    };

    struct SwapChainImage{
        VkImage image;
        TextureHandle rhiHandle;
    };
    using SemaphoreVector = Vector<VkSemaphore, Alloc::CustomAllocator<VkSemaphore>>;


private:
    static constexpr const char* s_EnabledInstanceExts[] = {
        VK_KHR_GET_PHYSICAL_DEVICE_PROPERTIES_2_EXTENSION_NAME,
    };
    static constexpr const char* s_OptionalInstanceExts[] = {
        VK_EXT_DEBUG_UTILS_EXTENSION_NAME,
    };

private:
    static constexpr ExtEntry s_EnabledDeviceExts[] = {
        { VK_KHR_MAINTENANCE1_EXTENSION_NAME, DeviceExtensionFeature::None },
    };
    static constexpr ExtEntry s_OptionalDeviceExts[] = {
        { VK_EXT_DEBUG_MARKER_EXTENSION_NAME, DeviceExtensionFeature::None },
        { VK_EXT_DESCRIPTOR_INDEXING_EXTENSION_NAME, DeviceExtensionFeature::None },
        { VK_EXT_DESCRIPTOR_HEAP_EXTENSION_NAME, DeviceExtensionFeature::DescriptorHeap },
        { VK_KHR_BUFFER_DEVICE_ADDRESS_EXTENSION_NAME, DeviceExtensionFeature::None },
        { VK_KHR_FRAGMENT_SHADING_RATE_EXTENSION_NAME, DeviceExtensionFeature::FragmentShadingRate },
        { VK_KHR_DYNAMIC_RENDERING_EXTENSION_NAME, DeviceExtensionFeature::None },
        { VK_KHR_MAINTENANCE_4_EXTENSION_NAME, DeviceExtensionFeature::None },
        { VK_EXT_SAMPLER_FILTER_MINMAX_EXTENSION_NAME, DeviceExtensionFeature::None },
        { VK_KHR_SWAPCHAIN_MUTABLE_FORMAT_EXTENSION_NAME, DeviceExtensionFeature::None },
        { VK_KHR_SYNCHRONIZATION_2_EXTENSION_NAME, DeviceExtensionFeature::None },
        { VK_EXT_MESH_SHADER_EXTENSION_NAME, DeviceExtensionFeature::MeshShader },
        { VK_EXT_MUTABLE_DESCRIPTOR_TYPE_EXTENSION_NAME, DeviceExtensionFeature::MutableDescriptorType },
    };
    static constexpr ExtEntry s_RayTracingExts[] = {
        { VK_KHR_ACCELERATION_STRUCTURE_EXTENSION_NAME, DeviceExtensionFeature::AccelerationStructure },
        { VK_KHR_DEFERRED_HOST_OPERATIONS_EXTENSION_NAME, DeviceExtensionFeature::None },
        { VK_KHR_PIPELINE_LIBRARY_EXTENSION_NAME, DeviceExtensionFeature::None },
        { VK_KHR_RAY_QUERY_EXTENSION_NAME, DeviceExtensionFeature::RayQuery },
        { VK_KHR_RAY_TRACING_PIPELINE_EXTENSION_NAME, DeviceExtensionFeature::RayTracingPipeline },
        { VK_EXT_OPACITY_MICROMAP_EXTENSION_NAME, DeviceExtensionFeature::OpacityMicromap },
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
    [[nodiscard]] virtual IDevice* getDevice()const override{ return m_rhiDevice.get(); }
    [[nodiscard]] virtual GraphicsAPI::Enum getGraphicsAPI()const override{ return GraphicsAPI::VULKAN; }
    [[nodiscard]] virtual const tchar* getRendererString()const override{ return m_rendererString.c_str(); }
    [[nodiscard]] virtual void* queryInterface(GraphicsBackendInterfaceID interfaceID)override;
    [[nodiscard]] virtual const void* queryInterface(GraphicsBackendInterfaceID interfaceID)const override;

    virtual bool enumerateAdapters(Vector<AdapterInfo>& outAdapters)override;
    [[nodiscard]] bool isValidationMessageLocationIgnored(usize location)const;

    virtual bool isInstanceExtensionEnabled(const char* extensionName)const override{
        return m_enabledExtensions.instance.find(extensionName) != m_enabledExtensions.instance.end();
    }
    virtual bool isDeviceExtensionEnabled(const char* extensionName)const override{
        return m_enabledExtensions.device.find(extensionName) != m_enabledExtensions.device.end();
    }
    virtual bool isLayerEnabled(const char* layerName)const override{
        return m_enabledExtensions.layers.find(layerName) != m_enabledExtensions.layers.end();
    }
    virtual void getEnabledInstanceExtensions(Vector<AString>& extensions)const override;
    virtual void getEnabledDeviceExtensions(Vector<AString>& extensions)const override;
    virtual void getEnabledLayers(Vector<AString>& layers)const override;

    virtual ITexture* getCurrentBackBuffer()override;
    virtual ITexture* getBackBuffer(u32 index)override;
    virtual u32 getCurrentBackBufferIndex()override{ return m_swapChainIndex; }
    virtual u32 getBackBufferCount()override{ return static_cast<u32>(m_swapChainImages.size()); }

    virtual void setPlatformFrameParam(const Common::FrameParam& frameParam)override{ m_platformFrameParam = frameParam; }
    virtual bool createInstance()override;
    virtual bool createDevice()override;
    virtual bool createSwapChain()override;
    virtual void destroy()override;
    virtual void resizeSwapChain()override;
    virtual bool beginFrame(const BackBufferResizeCallbacks& callbacks)override;
    virtual bool present()override;

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
    Alloc::CustomArena& m_arena;
    Common::FrameParam m_platformFrameParam = {};

private:
    VulkanExtensionSet m_enabledExtensions;
    VulkanExtensionSet m_optionalExtensions;

    DeviceExtensionMap m_rayTracingExtensions;

    TString m_rendererString;

    VkInstance m_vulkanInstance = VK_NULL_HANDLE;
    VkDebugReportCallbackEXT m_debugReportCallback = VK_NULL_HANDLE;

    VkPhysicalDevice m_vulkanPhysicalDevice = VK_NULL_HANDLE;
    i32 m_graphicsQueueFamily = -1;
    i32 m_computeQueueFamily = -1;
    i32 m_transferQueueFamily = -1;
    i32 m_presentQueueFamily = -1;

    VkDevice m_vulkanDevice = VK_NULL_HANDLE;
    VkQueue m_graphicsQueue = VK_NULL_HANDLE;
    VkQueue m_computeQueue = VK_NULL_HANDLE;
    VkQueue m_transferQueue = VK_NULL_HANDLE;
    VkQueue m_presentQueue = VK_NULL_HANDLE;

    VkSurfaceKHR m_windowSurface = VK_NULL_HANDLE;

    VkSurfaceFormatKHR m_swapChainFormat = {};
    VkSwapchainKHR m_swapChain = VK_NULL_HANDLE;
    bool m_swapChainMutableFormatSupported = false;

    Vector<SwapChainImage, Alloc::CustomAllocator<SwapChainImage>> m_swapChainImages;
    u32 m_swapChainIndex = static_cast<u32>(-1);

    DeviceHandle m_rhiDevice;

    SemaphoreVector m_acquireSemaphores;
    SemaphoreVector m_presentSemaphores;
    u32 m_acquireSemaphoreIndex = 0;

    ::Queue<EventQueryHandle, Alloc::CustomAllocator<EventQueryHandle>> m_framesInFlight;
    Vector<EventQueryHandle, Alloc::CustomAllocator<EventQueryHandle>> m_queryPool;
    u32 m_maxFramesInFlight = s_MaxFramesInFlight;

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

