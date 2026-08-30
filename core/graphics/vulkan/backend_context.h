// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#pragma once


#include "backend.h"
#include "swapchain_presentation.h"


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
        DescriptorBuffer,
        DeviceFault,
        TextureCompressionAstcHdr,
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
        VkImage image = VK_NULL_HANDLE;
        TextureHandle rhiHandle;
        VulkanDetail::SwapChainImagePresentationState presentationState;
    };
    using SemaphoreVector = GraphicsVector<VkSemaphore>;

    enum class FramePresentationSignalState : u8{
        Idle,
        Claimed,
        Queued,
        Accepted,
    };


private:
    static constexpr StringView s_EnabledInstanceExts[] = {
        VK_KHR_GET_PHYSICAL_DEVICE_PROPERTIES_2_EXTENSION_NAME,
    };
    static constexpr StringView s_DebugRequiredInstanceExts[] = {
        VK_EXT_DEBUG_UTILS_EXTENSION_NAME,
    };

private:
    static constexpr ExtEntry s_EnabledDeviceExts[] = {
        { VK_KHR_MAINTENANCE1_EXTENSION_NAME, DeviceExtensionFeature::None },
        // Descriptor buffers are the renderer's only descriptor transport. A device without this extension
        // does not meet the renderer's minimum requirements.
        { VK_EXT_DESCRIPTOR_BUFFER_EXTENSION_NAME, DeviceExtensionFeature::DescriptorBuffer },
    };
    static constexpr ExtEntry s_OptionalDeviceExts[] = {
        { VK_KHR_BUFFER_DEVICE_ADDRESS_EXTENSION_NAME, DeviceExtensionFeature::None },
        { VK_KHR_CALIBRATED_TIMESTAMPS_EXTENSION_NAME, DeviceExtensionFeature::None },
        { VK_EXT_CALIBRATED_TIMESTAMPS_EXTENSION_NAME, DeviceExtensionFeature::None },
        { VK_KHR_FRAGMENT_SHADING_RATE_EXTENSION_NAME, DeviceExtensionFeature::FragmentShadingRate },
        { VK_KHR_DYNAMIC_RENDERING_EXTENSION_NAME, DeviceExtensionFeature::None },
        { VK_KHR_MAINTENANCE_4_EXTENSION_NAME, DeviceExtensionFeature::None },
        { VK_EXT_SAMPLER_FILTER_MINMAX_EXTENSION_NAME, DeviceExtensionFeature::None },
        { VK_KHR_SWAPCHAIN_MUTABLE_FORMAT_EXTENSION_NAME, DeviceExtensionFeature::None },
        { VK_EXT_HDR_METADATA_EXTENSION_NAME, DeviceExtensionFeature::None },
        { VK_KHR_SYNCHRONIZATION_2_EXTENSION_NAME, DeviceExtensionFeature::None },
        { VK_EXT_MESH_SHADER_EXTENSION_NAME, DeviceExtensionFeature::MeshShader },
        { VK_NV_COOPERATIVE_VECTOR_EXTENSION_NAME, DeviceExtensionFeature::None },
        { VK_EXT_DEVICE_FAULT_EXTENSION_NAME, DeviceExtensionFeature::DeviceFault },
        // UASTC HDR 4x4 blocks are directly valid ASTC HDR blocks. Keep this optional so
        // the texture loader can select BC6H or RGBA16_FLOAT on hardware without it.
        { VK_EXT_TEXTURE_COMPRESSION_ASTC_HDR_EXTENSION_NAME, DeviceExtensionFeature::TextureCompressionAstcHdr },
        { VK_AMD_BUFFER_MARKER_EXTENSION_NAME, DeviceExtensionFeature::None },
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
    [[nodiscard]] bool getSelectedAdapterInfo(AdapterInfo& outAdapter)const;
    [[nodiscard]] bool isValidationMessageIdIgnored(i32 messageId)const;

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

    Texture* getBackBuffer(u32 index)const;
    u32 getBackBufferCount()const{ return static_cast<u32>(m_swapChainImages.size()); }

    void setPlatformFrameParam(const Common::FrameParam& frameParam){ m_platformFrameParam = frameParam; }
    bool createInstance();
    bool createDevice();
    bool createSwapChain();
    void destroy();
    void resizeSwapChain();
    [[nodiscard]] AcquiredBackBuffer beginFrame(const BackBufferResizeCallbacks& callbacks);
    // Idempotently retires synchronization for a healthy aborted frame. The acquired WSI image stays quarantined
    // until swap-chain or device teardown; an already-resolved frame is a successful no-op.
    [[nodiscard]] bool abandonAcquiredFrame()noexcept;
    bool present();
    // Claims the acquired image's completion semaphore for one exact graph packet. A null hook leaves the
    // compatibility transition-submit path in present() active.
    [[nodiscard]] QueueSubmissionPreSubmitHook claimFramePresentationSignal()noexcept;
    // The renderer confirms only the terminal packet token accepted by the graph submission transaction.
    [[nodiscard]] bool confirmFramePresentationSignal(const QueueSubmissionToken& token)noexcept;
    // A hook that reached a rejected or abandoned submission cannot be reused blindly; retire that binary signal
    // before the next frame instead of allowing it to leak into another present.
    void cancelFramePresentationSignal()noexcept;
    void reportLiveObjects()const{}

private:
    void initDefaultExtensions();
    bool createVulkanInstance();
    bool createWindowSurface();
    void installDebugMessenger();
    bool pickPhysicalDevice();
    bool findQueueFamilies(VkPhysicalDevice physicalDevice);
    bool createVulkanDevice();
    void logVulkanDeviceConfiguration(
        Alloc::ScratchArena& scratchArena,
        const VkPhysicalDeviceProperties& physicalDeviceProperties,
        bool maintenance4Enabled,
        const VkPhysicalDeviceMaintenance4Features& maintenance4Features,
        bool createAsyncComputeQueue,
        bool createCrossFamilySecondaryComputeQueue,
        i32 secondaryComputeQueueFamily,
        bool createDedicatedTransferQueue,
        bool createCrossFamilySecondaryTransferQueue,
        i32 secondaryTransferQueueFamily
    );
    bool createVulkanSwapChain();
    void destroySwapChain();
    void clearSemaphores(SemaphoreVector& semaphores);
    bool recreateSemaphores(SemaphoreVector& semaphores, usize count, AStringView operationName);
    [[nodiscard]] bool createFrameSyncQueries();
    [[nodiscard]] static bool PrepareFramePresentationSignal(
        void* context,
        const GpuPhysicalQueueId& executionQueue,
        QueueSubmissionNativeSignal& outSignal
    );
    [[nodiscard]] bool prepareFramePresentationSignal(
        const GpuPhysicalQueueId& executionQueue,
        QueueSubmissionNativeSignal& outSignal
    )noexcept;
    [[nodiscard]] bool replaceFramePresentationSemaphoreAfterIdle()noexcept;
    void resetFramePresentationSignal()noexcept;


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
    VkDebugUtilsMessengerEXT m_debugUtilsMessenger = VK_NULL_HANDLE;

    VkPhysicalDevice m_vulkanPhysicalDevice = VK_NULL_HANDLE;
    i32 m_graphicsQueueFamily = s_InvalidQueueFamilyIndex;
    // Family selected for the optional auxiliary Graphics queue; it differs from the primary only for explicitly
    // enabled cross-family routing.
    i32 m_secondaryGraphicsQueueFamily = s_InvalidQueueFamilyIndex;
    i32 m_computeQueueFamily = s_InvalidQueueFamilyIndex;
    // Dedicated Compute and Transfer paths may likewise expose one cross-family auxiliary transport. Device
    // registration is opt-in, and task-level scheduling must separately select the resulting route.
    i32 m_secondaryComputeQueueFamily = s_InvalidQueueFamilyIndex;
    i32 m_transferQueueFamily = s_InvalidQueueFamilyIndex;
    i32 m_secondaryTransferQueueFamily = s_InvalidQueueFamilyIndex;
    i32 m_presentQueueFamily = s_InvalidQueueFamilyIndex;

    VkDevice m_vulkanDevice = VK_NULL_HANDLE;
    VkQueue m_graphicsQueue = VK_NULL_HANDLE;
    // Optional auxiliary Graphics VkQueue. It may come from the primary family or, under the separate cross-family
    // opt-in, a distinct Graphics-capable family. It is exposed only through the physical registry; legacy
    // CommandQueue::Graphics callers continue to use m_graphicsQueue.
    VkQueue m_secondaryGraphicsQueue = VK_NULL_HANDLE;
    VkQueue m_computeQueue = VK_NULL_HANDLE;
    VkQueue m_secondaryComputeQueue = VK_NULL_HANDLE;
    VkQueue m_transferQueue = VK_NULL_HANDLE;
    VkQueue m_secondaryTransferQueue = VK_NULL_HANDLE;
    VkQueue m_presentQueue = VK_NULL_HANDLE;

    VkSurfaceKHR m_windowSurface = VK_NULL_HANDLE;

    VkSurfaceFormatKHR m_swapChainFormat = {};
    VkSwapchainKHR m_swapChain = VK_NULL_HANDLE;

    GraphicsVector<SwapChainImage> m_swapChainImages;
    DeviceHandle m_rhiDevice;
    // Exact opt-in same-class transports registered after their class primary. The first entry for each class
    // keeps the legacy secondary fields above populated, while graph packets use this complete physical registry.
    GraphicsVector<VulkanPhysicalQueueDesc> m_sameClassQueues;

    SemaphoreVector m_acquireSemaphores;
    SemaphoreVector m_presentSemaphores;

    VkSemaphore m_framePresentationSemaphore = VK_NULL_HANDLE;
    GpuPhysicalQueueId m_framePresentationQueue;
    u32 m_framePresentationSwapChainIndex = Limit<u32>::s_Max;
    FramePresentationSignalState m_framePresentationSignalState = FramePresentationSignalState::Idle;
    bool m_frameAcquired = false;
    bool m_frameAbandonmentComplete = false;

    ::Queue<EventQueryHandle, Alloc::GlobalArena> m_framesInFlight;
    Vector<EventQueryHandle, Alloc::GlobalArena> m_queryPool;

    u32 m_swapChainIndex = Limit<u32>::s_Max;
    u32 m_acquireSemaphoreIndex = 0;
    u32 m_maxFramesInFlight = s_MaxFramesInFlight;

    bool m_swapChainMutableFormatSupported = false;
    bool m_hdr10ColorSpaceExtensionEnabled = false;
    bool m_bufferDeviceAddressSupported = false;
    bool m_textureCompressionBcFeatureEnabled = false;
    bool m_textureCompressionAstcLdrFeatureEnabled = false;
    bool m_textureCompressionAstcHdrFeatureEnabled = false;
    bool m_dynamicRenderingSupported = false;
    bool m_synchronization2Supported = false;
    bool m_independentBlendFeatureEnabled = false;
    bool m_fullDrawIndexUint32FeatureEnabled = false;
    bool m_multiDrawIndirectFeatureEnabled = false;
    bool m_drawIndirectFirstInstanceFeatureEnabled = false;
    bool m_meshShaderFeatureEnabled = false;
    bool m_accelerationStructureFeatureEnabled = false;
    bool m_rayTracingPipelineFeatureEnabled = false;
    bool m_rayQueryFeatureEnabled = false;
    bool m_opacityMicromapFeatureEnabled = false;
    bool m_clusterAccelerationStructureFeatureEnabled = false;
    bool m_rayTracingInvocationReorderFeatureEnabled = false;
    bool m_rayTracingInvocationReorderExtFeatureEnabled = false;
    bool m_cooperativeVectorFeatureEnabled = false;
    bool m_cooperativeVectorTrainingFeatureEnabled = false;
    bool m_sameClassGraphicsQueueEnabled = false;
    bool m_sameClassComputeQueueEnabled = false;
    bool m_sameClassTransferQueueEnabled = false;
    bool m_asyncComputeLaneEnabled = false;
    bool m_transferQueueEnabled = false;
    bool m_meshTaskShaderSupported = false;
    bool m_rayTracingSpheresSupported = false;
    bool m_rayTracingLinearSweptSpheresSupported = false;
};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_VULKAN_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

