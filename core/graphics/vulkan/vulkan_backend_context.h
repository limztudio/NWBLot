// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#pragma once


#include "vulkan_backend.h"
#include "vulkan_backend_queries.h"


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_VULKAN_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


enum class DeviceExtensionFeature : u8{
    None = 0,
    AccelerationStructure,
    RayTracingPipeline,
    RayQuery,
    MeshShader,
    FragmentShadingRate,
    MutableDescriptorType,
    DescriptorHeap,
    Count,
};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


class BackendContext final : public IGraphicsBackend, public IBackendQueries{
private:
    using DeviceExtensionMap = HashMap<AString, DeviceExtensionFeature, Hasher<AString>, EqualTo<AString>, Alloc::CustomAllocator<Pair<const AString, DeviceExtensionFeature>>>;

    struct ExtEntry{
        const char* name;
        DeviceExtensionFeature feature = DeviceExtensionFeature::None;
    };

    struct VulkanExtensionSet{
        HashSet<AString, Hasher<AString>, EqualTo<AString>, Alloc::CustomAllocator<AString>> instance;
        HashSet<AString, Hasher<AString>, EqualTo<AString>, Alloc::CustomAllocator<AString>> layers;
        DeviceExtensionMap device;

        explicit VulkanExtensionSet(Alloc::CustomArena& arena)
            : instance(0, Hasher<AString>(), EqualTo<AString>(), Alloc::CustomAllocator<AString>(arena))
            , layers(0, Hasher<AString>(), EqualTo<AString>(), Alloc::CustomAllocator<AString>(arena))
            , device(0, Hasher<AString>(), EqualTo<AString>(), Alloc::CustomAllocator<Pair<const AString, DeviceExtensionFeature>>(arena))
        {}
    };

    struct SwapChainImage{
        VkImage image;
        TextureHandle rhiHandle;
    };
    using SemaphoreVector = Vector<VkSemaphore, Alloc::CustomAllocator<VkSemaphore>>;


public:
    BackendContext(
        const DeviceCreationParameters& params,
        SwapChainRuntimeState& swapChainState,
        GraphicsAllocator& allocator,
        Alloc::ThreadPool& threadPool
    );


public:
    [[nodiscard]] IDevice* getDevice()const override;
    [[nodiscard]] GraphicsAPI::Enum getGraphicsAPI()const override{ return GraphicsAPI::VULKAN; }
    [[nodiscard]] const tchar* getRendererString()const override;
    [[nodiscard]] void* queryInterface(GraphicsBackendInterfaceID interfaceID)override;
    [[nodiscard]] const void* queryInterface(GraphicsBackendInterfaceID interfaceID)const override;

    bool enumerateAdapters(Vector<AdapterInfo>& outAdapters)override;
    [[nodiscard]] bool isValidationMessageLocationIgnored(usize location)const;

    bool isInstanceExtensionEnabled(const char* extensionName)const override;
    bool isDeviceExtensionEnabled(const char* extensionName)const override;
    bool isLayerEnabled(const char* layerName)const override;
    void getEnabledInstanceExtensions(Vector<AString>& extensions)const override;
    void getEnabledDeviceExtensions(Vector<AString>& extensions)const override;
    void getEnabledLayers(Vector<AString>& layers)const override;

    ITexture* getCurrentBackBuffer()override;
    ITexture* getBackBuffer(u32 index)override;
    u32 getCurrentBackBufferIndex()override;
    u32 getBackBufferCount()override;

    void setPlatformFrameParam(const Common::FrameParam& frameParam)override{ m_platformFrameParam = frameParam; }
    bool createInstance()override;
    bool createDevice()override;
    bool createSwapChain()override;
    void destroy()override;
    void resizeSwapChain()override;
    bool beginFrame(const BackBufferResizeCallbacks& callbacks)override;
    bool present()override;

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
    static constexpr const char* s_EnabledInstanceExts[] = {
        VK_KHR_GET_PHYSICAL_DEVICE_PROPERTIES_2_EXTENSION_NAME,
    };
    static constexpr const char* s_OptionalInstanceExts[] = {
        VK_EXT_DEBUG_UTILS_EXTENSION_NAME,
        VK_EXT_SAMPLER_FILTER_MINMAX_EXTENSION_NAME,
    };


private:
    const ExtEntry m_enabledDeviceExts[1] = {
        { VK_KHR_MAINTENANCE1_EXTENSION_NAME },
    };
    const ExtEntry m_optionalDeviceExts[11] = {
        { VK_EXT_DEBUG_MARKER_EXTENSION_NAME },
        { VK_EXT_DESCRIPTOR_INDEXING_EXTENSION_NAME },
        { VK_EXT_DESCRIPTOR_HEAP_EXTENSION_NAME, DeviceExtensionFeature::DescriptorHeap },
        { VK_KHR_BUFFER_DEVICE_ADDRESS_EXTENSION_NAME },
        { VK_KHR_FRAGMENT_SHADING_RATE_EXTENSION_NAME, DeviceExtensionFeature::FragmentShadingRate },
        { VK_KHR_DYNAMIC_RENDERING_EXTENSION_NAME },
        { VK_KHR_MAINTENANCE_4_EXTENSION_NAME },
        { VK_KHR_SWAPCHAIN_MUTABLE_FORMAT_EXTENSION_NAME },
        { VK_KHR_SYNCHRONIZATION_2_EXTENSION_NAME },
        { VK_EXT_MESH_SHADER_EXTENSION_NAME, DeviceExtensionFeature::MeshShader },
        { VK_EXT_MUTABLE_DESCRIPTOR_TYPE_EXTENSION_NAME, DeviceExtensionFeature::MutableDescriptorType },
    };
    const ExtEntry m_rayTracingExts[5] = {
        { VK_KHR_ACCELERATION_STRUCTURE_EXTENSION_NAME, DeviceExtensionFeature::AccelerationStructure },
        { VK_KHR_DEFERRED_HOST_OPERATIONS_EXTENSION_NAME },
        { VK_KHR_PIPELINE_LIBRARY_EXTENSION_NAME },
        { VK_KHR_RAY_QUERY_EXTENSION_NAME, DeviceExtensionFeature::RayQuery },
        { VK_KHR_RAY_TRACING_PIPELINE_EXTENSION_NAME, DeviceExtensionFeature::RayTracingPipeline },
    };


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
};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_VULKAN_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

