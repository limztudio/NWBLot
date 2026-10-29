// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#pragma once


#include "backend.h"
#include "device_extension_policy.h"

#include <core/task/cpu/scheduler.h>

#include "swap_chain_presentation.h"


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_VULKAN_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


class BackendContext final{
private:
    struct ExtensionNameHasher{
        using is_transparent = void;


    public:
        [[nodiscard]] usize operator()(const AStringView value)const noexcept{ return Hasher<AStringView>{}(value); }
    };
    struct ExtensionNameEqualTo{
        using is_transparent = void;


    public:
        [[nodiscard]] b8 operator()(const AStringView lhs, const AStringView rhs)const noexcept{ return lhs == rhs; }
    };


private:
    using ExtensionStringSet = HashSet<GraphicsString, GraphicsArena, ExtensionNameHasher, ExtensionNameEqualTo>;
    using DeviceExtensionMap = HashMap<GraphicsString, DeviceExtensionFeature::Enum, GraphicsArena, ExtensionNameHasher, ExtensionNameEqualTo>;
    using ExtEntry = DeviceExtensionEntry;


private:
    struct VulkanExtensionSet{
        ExtensionStringSet instance;
        ExtensionStringSet layers;
        DeviceExtensionMap device;


    public:
        explicit VulkanExtensionSet(Alloc::GlobalArena& arena)
            : instance(0, ExtensionNameHasher(), ExtensionNameEqualTo(), arena)
            , layers(0, ExtensionNameHasher(), ExtensionNameEqualTo(), arena)
            , device(0, ExtensionNameHasher(), ExtensionNameEqualTo(), arena)
        {}
    };
    struct SwapChainImage{
        VkImage image = VK_NULL_HANDLE;
        TextureHandle rhiHandle;
        VulkanDetail::SwapChainImagePresentationState presentationState;
    };


private:
    using SemaphoreVector = GraphicsVector<VkSemaphore>;


private:
    struct AcquireSyncSlotState{
        enum Enum : u8{
            Idle,
            AcquirePending,
            ConsumerAccepted,
            Unconsumed,
        };
    };
    struct AcquireSyncSlot{
        VkSemaphore semaphore = VK_NULL_HANDLE;
        VkFence fence = VK_NULL_HANDLE;
        QueueSubmissionToken consumerToken;
        AcquireSyncSlotState::Enum state = AcquireSyncSlotState::Idle;
    };


private:
    using AcquireSyncSlotVector = GraphicsVector<AcquireSyncSlot>;


private:
    struct FramePresentationSignalState{
        enum Enum : u8{
            Idle,
            Claimed,
            Queued,
            Rejected,
            Accepted,
            Failed,
            Retiring,
        };
    };
    struct SwapChainLifecycleState{
        enum Enum : u8{
            Ready,
            RetiringPresentation,
            Preparing,
            PreparedResize,
            PreparedDestroy,
            NeedsDestroy,
            Destroyed,
        };
    };


private:
    static constexpr StringView s_EnabledInstanceExts[] = { VK_KHR_GET_PHYSICAL_DEVICE_PROPERTIES_2_EXTENSION_NAME, };
    static constexpr StringView s_DebugRequiredInstanceExts[] = { VK_EXT_DEBUG_UTILS_EXTENSION_NAME, };
    static constexpr ExtEntry s_EnabledDeviceExts[] = {
        { VK_KHR_MAINTENANCE1_EXTENSION_NAME, DeviceExtensionFeature::None },
        // Descriptor buffers are the renderer's only descriptor transport.
        // A device without this extension does not meet the renderer's minimum requirements.
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
        // UASTC HDR 4x4 blocks are directly valid ASTC HDR blocks.
        // Keep this optional so consumers can select BC6H or RGBA16_FLOAT on hardware without it.
        { VK_EXT_TEXTURE_COMPRESSION_ASTC_HDR_EXTENSION_NAME, DeviceExtensionFeature::TextureCompressionAstcHdr },
        { VK_AMD_BUFFER_MARKER_EXTENSION_NAME, DeviceExtensionFeature::None },
    };


private:
    [[nodiscard]] static Expected<QueueSubmissionNativeSignal> InvokeFramePresentationSignalPreparation(void* context, u64 identity, const GpuPhysicalQueueId& executionQueue)noexcept;
    [[nodiscard]] static b8 InvokeFramePresentationSignalResolution(void* context, u64 identity, const QueueSubmissionToken& token)noexcept;


public:
    BackendContext(const DeviceCreationParameters& params, SwapChainRuntimeState& swapChainState, GraphicsAllocator& allocator, CpuTaskScheduler& cpuScheduler);


public:
    [[nodiscard]] Device* getDevice()const noexcept{ return m_rhiDevice.get(); }

    [[nodiscard]] TStringView getRendererString()const noexcept{ return m_rendererString; }

    [[nodiscard]] Expected<GraphicsVector<AdapterInfo>> enumerateAdapters();
    [[nodiscard]] Expected<AdapterInfo> getSelectedAdapterInfo()const;
    [[nodiscard]] b8 isValidationMessageIdIgnored(i32 messageId)const noexcept;

    [[nodiscard]] b8 isInstanceExtensionEnabled(AStringView extensionName)const noexcept{
        return m_enabledExtensions.instance.find(extensionName) != m_enabledExtensions.instance.end();
    }

    [[nodiscard]] b8 isDeviceExtensionEnabled(AStringView extensionName)const noexcept{
        return m_enabledExtensions.device.find(extensionName) != m_enabledExtensions.device.end();
    }

    [[nodiscard]] b8 isLayerEnabled(AStringView layerName)const noexcept{
        return m_enabledExtensions.layers.find(layerName) != m_enabledExtensions.layers.end();
    }

    Texture* getBackBuffer(u32 index)const noexcept;

    u32 getBackBufferCount()const noexcept{ return static_cast<u32>(m_swapChainImages.size()); }

    void setPlatformFrameParam(const Common::FrameParam& frameParam)noexcept{ m_platformFrameParam = frameParam; }

    b8 createInstance();
    b8 createDevice();
    b8 createSwapChain();
    [[nodiscard]] b8 destroy();
    [[nodiscard]] Expected<SwapChainTransitionTicket> prepareSwapChainTransition(SwapChainTransitionKind::Enum kind);
    [[nodiscard]] b8 commitSwapChainResize(SwapChainTransitionTicket&& ticket);
    [[nodiscard]] b8 commitDestroy(SwapChainTransitionTicket&& ticket)noexcept;
    [[nodiscard]] BeginFrameResult beginFrame();
    // Idempotently retires synchronization for a healthy aborted frame.
    // The acquired WSI image stays quarantined until swap-chain or device teardown; an already-resolved frame is a successful no-op.
    [[nodiscard]] b8 abandonAcquiredFrame();
    // The return value retains continuation/retirement status.
    // Output reports native present acceptance alone: true even if later sync fails, false for out-of-date surfaces.
    [[nodiscard]] Expected<b8, PresentationFailure> present();
    // Claims the acquired image's completion semaphore for one exact graph packet.
    // A null hook leaves the direct transition-submit path in present() active.
    [[nodiscard]] QueueSubmissionPreSubmitHook claimFramePresentationSignal()noexcept;
    // The renderer confirms only the terminal packet token accepted by the graph submission transaction.
    [[nodiscard]] b8 confirmFramePresentationSignal(const QueueSubmissionPreSubmitHook& claim, const QueueSubmissionToken& token)noexcept;
    // A hook from a rejected/abandoned submit is unusable; retire its signal before the next frame.
    [[nodiscard]] b8 cancelFramePresentationSignal(const QueueSubmissionPreSubmitHook& claim);


private:
    void initDefaultExtensions();
    b8 createVulkanInstance(Alloc::ScratchArena& scratchArena);
    b8 createWindowSurface();
    void installDebugMessenger();
    b8 pickPhysicalDevice(Alloc::ScratchArena& scratchArena);
    b8 findQueueFamilies(Alloc::ScratchArena& scratchArena, VkPhysicalDevice physicalDevice);
    [[nodiscard]] u32 findNativeQueueIndex(u32 familyIndex, u32 queueIndex)const noexcept;
    b8 createVulkanDevice(Alloc::ScratchArena& scratchArena);
    void logVulkanDeviceConfiguration(
        Alloc::ScratchArena& scratchArena,
        const VkPhysicalDeviceProperties& physicalDeviceProperties,
        b8 maintenance4Enabled,
        const VkPhysicalDeviceMaintenance4Features& maintenance4Features,
        b8 createComputeQueue,
        b8 createCrossFamilySecondaryComputeQueue,
        i32 secondaryComputeQueueFamily,
        b8 createDedicatedTransferQueue,
        b8 createCrossFamilySecondaryTransferQueue,
        i32 secondaryTransferQueueFamily
    );
    b8 createVulkanSwapChain(Alloc::ScratchArena& scratchArena);
    [[nodiscard]] b8 createSwapChainResources(Alloc::ScratchArena& scratchArena);
    [[nodiscard]] b8 prepareSwapChainImageRevocation();
    void commitPreparedSwapChainDestruction()noexcept;
    [[nodiscard]] b8 destroySwapChainAfterCreateFailure();
    void clearSemaphores(SemaphoreVector& semaphores)noexcept;
    b8 recreateSemaphores(SemaphoreVector& semaphores, usize count, AStringView operationName);
    void clearAcquireSyncSlots()noexcept;
    [[nodiscard]] b8 recreateAcquireSyncSlots(usize count);
    [[nodiscard]] b8 prepareAcquireSyncSlot(AcquireSyncSlot& slot);
    [[nodiscard]] b8 waitAcquireSyncSlotsForLifecycle();
    [[nodiscard]] b8 createFrameSyncQueries();
    [[nodiscard]] b8 ensureDirectPresentCommandList(const GpuPhysicalQueueId& executionQueue);
    void resetDirectPresentCommandList()noexcept;
    [[nodiscard]] b8 recordDirectPresentTransition(const VulkanDetail::DirectPresentTransitionPolicy::Enum transitionPolicy, Texture* backbufferTexture);
    [[nodiscard]] Expected<QueueSubmissionNativeSignal> prepareFramePresentationSignal(u64 identity, const GpuPhysicalQueueId& executionQueue)noexcept;
    [[nodiscard]] b8 resolveFramePresentationSignal(u64 identity, const QueueSubmissionToken& token)noexcept;
    [[nodiscard]] b8 replaceFramePresentationSemaphoreAfterIdle();
    void resetFramePresentationSignal()noexcept;
    [[nodiscard]] b8 cancelFramePresentationSignalDeferred(const QueueSubmissionPreSubmitHook* claim, UniqueLock<Futex>& lifecycleLock);
    [[nodiscard]] b8 validPreparedTicket(const SwapChainTransitionTicket& ticket, SwapChainTransitionKind::Enum kind)const noexcept;


private:
    const DeviceCreationParameters& m_deviceParams;
    SwapChainRuntimeState& m_swapChainState;
    GraphicsAllocator& m_allocator;
    CpuTaskScheduler& m_cpuScheduler;
    Alloc::GlobalArena& m_arena;
    Common::FrameParam m_platformFrameParam = {};
    VulkanExtensionSet m_enabledExtensions;
    VulkanExtensionSet m_optionalExtensions;
    DeviceExtensionMap m_rayTracingExtensions;
    GraphicsTString m_rendererString;
    VkInstance m_vulkanInstance = VK_NULL_HANDLE;
    PFN_vkGetInstanceProcAddr m_getInstanceProcAddr = nullptr;
    VolkInstanceTable m_instanceDispatch = {};
    VkDebugUtilsMessengerEXT m_debugUtilsMessenger = VK_NULL_HANDLE;
    VkPhysicalDevice m_vulkanPhysicalDevice = VK_NULL_HANDLE;
    i32 m_graphicsQueueFamily = s_InvalidQueueFamilyIndex;
    // Family selected for the optional auxiliary Graphics queue; it differs from the primary only for explicitly enabled cross-family routing.
    i32 m_secondaryGraphicsQueueFamily = s_InvalidQueueFamilyIndex;
    i32 m_computeQueueFamily = s_InvalidQueueFamilyIndex;
    // Offload family differs from the Compute role, which aliases Graphics on universal hardware.
    i32 m_asyncComputeQueueFamily = s_InvalidQueueFamilyIndex;
    // Dedicated Compute and Transfer paths may likewise expose one cross-family auxiliary transport.
    // Device registration is opt-in, and task-level scheduling must separately select the resulting route.
    i32 m_secondaryComputeQueueFamily = s_InvalidQueueFamilyIndex;
    i32 m_transferQueueFamily = s_InvalidQueueFamilyIndex;
    i32 m_secondaryTransferQueueFamily = s_InvalidQueueFamilyIndex;
    i32 m_presentQueueFamily = s_InvalidQueueFamilyIndex;
    VkDevice m_vulkanDevice = VK_NULL_HANDLE;
    VolkDeviceTable m_deviceDispatch = {};
    VkSurfaceKHR m_windowSurface = VK_NULL_HANDLE;
    VkSurfaceFormatKHR m_swapChainFormat = {};
    VkSwapchainKHR m_swapChain = VK_NULL_HANDLE;
    GraphicsVector<SwapChainImage> m_swapChainImages;
    // One construction entry per VkQueue created from m_vulkanDevice.
    // Device owns the corresponding synchronized states; physical scheduling queues and the present role retain only canonical indices.
    GraphicsVector<VulkanNativeQueueDesc> m_nativeQueues;
    DeviceHandle m_rhiDevice;
    // Exact opt-in same-class transports registered after their class primary.
    // The first entry for each class populates the per-class auxiliary family fields above; graph packets use this complete physical registry.
    GraphicsVector<VulkanPhysicalQueueDesc> m_sameClassQueues;
    AcquireSyncSlotVector m_acquireSyncSlots;
    SemaphoreVector m_presentSemaphores;
    // Swap-chain-lifetime direct transition list.
    // Created once with swap-chain resources and reopened per use; present() never creates a command list so the frame path stays allocation-free.
    CommandListHandle m_directPresentCommandList;
    GpuPhysicalQueueId m_directPresentQueue;
    GpuPhysicalQueueId m_framePresentationQueue;
    VkSemaphore m_framePresentationSemaphore = VK_NULL_HANDLE;
    QueueSubmissionToken m_framePresentationSubmission;
    u64 m_nextFramePresentationClaimIdentity = 0u;
    u64 m_framePresentationClaimIdentity = 0u;
    ::Queue<EventQueryHandle, Alloc::GlobalArena> m_framesInFlight;
    Vector<EventQueryHandle, Alloc::GlobalArena> m_queryPool;
    u32 m_swapChainIndex = Limit<u32>::s_Max;
    u32 m_acquireSyncSlotIndex = 0u;
    u32 m_maxFramesInFlight = s_MaxFramesInFlight;
    u32 m_framePresentationSwapChainIndex = Limit<u32>::s_Max;
    u32 m_presentNativeQueueIndex = Limit<u32>::s_Max;
    Futex m_swapChainLifecycleMutex;
    // Presentation hooks run inside queue submission and therefore cannot take the lifecycle mutex recursively.
    // This narrower lock protects their state, while the claims-enabled gate closes before lifecycle joins begin.
    Futex m_framePresentationMutex;
    u64 m_swapChainLifecycleEpoch = 0u;
    ConditionVariableAny m_framePresentationCondition;
    b8 m_framePresentationClaimIdentityExhausted = false;
    b8 m_frameAcquired = false;
    b8 m_frameAbandonmentComplete = false;
    FramePresentationSignalState::Enum m_framePresentationSignalState = FramePresentationSignalState::Idle;
    SwapChainLifecycleState::Enum m_swapChainLifecycleState = SwapChainLifecycleState::Ready;
    b8 m_lifecycleDrainActive = false;
    b8 m_swapChainMutableFormatSupported = false;
    b8 m_hdr10ColorSpaceExtensionEnabled = false;
    b8 m_framePresentationClaimsEnabled = true;
    b8 m_bufferDeviceAddressSupported = false;
    b8 m_hostQueryResetFeatureEnabled = false;
    b8 m_textureCompressionBcFeatureEnabled = false;
    b8 m_textureCompressionAstcLdrFeatureEnabled = false;
    b8 m_textureCompressionAstcHdrFeatureEnabled = false;
    b8 m_dynamicRenderingSupported = false;
    b8 m_synchronization2Supported = false;
    b8 m_independentBlendFeatureEnabled = false;
    b8 m_fullDrawIndexUint32FeatureEnabled = false;
    b8 m_multiDrawIndirectFeatureEnabled = false;
    b8 m_drawIndirectFirstInstanceFeatureEnabled = false;
    b8 m_meshShaderFeatureEnabled = false;
    b8 m_accelerationStructureFeatureEnabled = false;
    b8 m_rayTracingPipelineFeatureEnabled = false;
    b8 m_rayQueryFeatureEnabled = false;
    b8 m_opacityMicromapFeatureEnabled = false;
    b8 m_clusterAccelerationStructureFeatureEnabled = false;
    b8 m_rayTracingInvocationReorderFeatureEnabled = false;
    b8 m_rayTracingInvocationReorderExtFeatureEnabled = false;
    b8 m_cooperativeVectorFeatureEnabled = false;
    b8 m_cooperativeVectorTrainingFeatureEnabled = false;
    b8 m_sameClassGraphicsQueueEnabled = false;
    b8 m_sameClassComputeQueueEnabled = false;
    b8 m_sameClassTransferQueueEnabled = false;
    b8 m_computeQueueEnabled = false;
    b8 m_asyncComputeLaneEnabled = false;
    b8 m_transferQueueEnabled = false;
    b8 m_meshTaskShaderSupported = false;
    b8 m_rayTracingSpheresSupported = false;
    b8 m_rayTracingLinearSweptSpheresSupported = false;
};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_VULKAN_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

