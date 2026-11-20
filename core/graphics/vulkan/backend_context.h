// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#pragma once


#include "backend.h"
#include "device_extension_policy.h"

#include <core/common/frame_data.h>
#include <core/task/cpu/scheduler.h>

#include "swap_chain_presentation.h"
#include "frame_presentation_coordinator.h"
#include "swap_chain_lifecycle.h"


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_VULKAN_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


class BackendContext final{
    friend class SwapChainLifecycle;
    friend class FramePresentationCoordinator;
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
    static constexpr Array<StringView, 1> s_EnabledInstanceExts = {{ VK_KHR_GET_PHYSICAL_DEVICE_PROPERTIES_2_EXTENSION_NAME }};
    static constexpr Array<StringView, 1> s_DebugRequiredInstanceExts = {{ VK_EXT_DEBUG_UTILS_EXTENSION_NAME }};
    static constexpr Array<ExtEntry, 2> s_EnabledDeviceExts = {{
        { VK_KHR_MAINTENANCE1_EXTENSION_NAME, DeviceExtensionFeature::None },
        { VK_EXT_DESCRIPTOR_BUFFER_EXTENSION_NAME, DeviceExtensionFeature::DescriptorBuffer },
    }};
    static constexpr Array<ExtEntry, 15> s_OptionalDeviceExts = {{
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
        { VK_EXT_TEXTURE_COMPRESSION_ASTC_HDR_EXTENSION_NAME, DeviceExtensionFeature::TextureCompressionAstcHdr },
        { VK_AMD_BUFFER_MARKER_EXTENSION_NAME, DeviceExtensionFeature::None },
    }};


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
    [[nodiscard]] b8 abandonAcquiredFrame();
    [[nodiscard]] Expected<b8, PresentationFailure> present();
    [[nodiscard]] QueueSubmissionPreSubmitHook claimFramePresentationSignal()noexcept;
    [[nodiscard]] b8 confirmFramePresentationSignal(const QueueSubmissionPreSubmitHook& claim, const QueueSubmissionToken& token)noexcept;
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
    [[nodiscard]] b8 prepareSwapChainImageRevocation();
    void commitPreparedSwapChainDestruction()noexcept;
    [[nodiscard]] b8 destroySwapChainAfterCreateFailure();
    void clearSemaphores(SemaphoreVector& semaphores)noexcept;
    b8 recreateSemaphores(SemaphoreVector& semaphores, usize count, AStringView operationName);
    void clearAcquireSyncSlots()noexcept;
    [[nodiscard]] b8 recreateAcquireSyncSlots(usize count);
    [[nodiscard]] b8 prepareAcquireSyncSlot(AcquireSyncSlot& slot);
    [[nodiscard]] b8 waitAcquireSyncSlotsForLifecycle();
    [[nodiscard]] b8 ensureDirectPresentCommandList(const GpuPhysicalQueueId& executionQueue);
    void resetDirectPresentCommandList()noexcept;
    [[nodiscard]] b8 recordDirectPresentTransition(const VulkanDetail::DirectPresentTransitionPolicy::Enum transitionPolicy, Texture* backbufferTexture);
    [[nodiscard]] Expected<QueueSubmissionNativeSignal> prepareFramePresentationSignal(u64 identity, const GpuPhysicalQueueId& executionQueue)noexcept;
    [[nodiscard]] b8 resolveFramePresentationSignal(u64 identity, const QueueSubmissionToken& token)noexcept;
    [[nodiscard]] b8 replaceFramePresentationSemaphoreAfterIdle();
    void resetFramePresentationSignal()noexcept;
    [[nodiscard]] b8 cancelFramePresentationSignalDeferred(const QueueSubmissionPreSubmitHook* claim, UniqueLock<Futex>& lifecycleLock);


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
    i32 m_secondaryGraphicsQueueFamily = s_InvalidQueueFamilyIndex;
    i32 m_computeQueueFamily = s_InvalidQueueFamilyIndex;
    i32 m_asyncComputeQueueFamily = s_InvalidQueueFamilyIndex;
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
    GraphicsVector<VulkanNativeQueueDesc> m_nativeQueues;
    DeviceHandle m_rhiDevice;
    GraphicsVector<VulkanPhysicalQueueDesc> m_sameClassQueues;
    AcquireSyncSlotVector m_acquireSyncSlots;
    SemaphoreVector m_presentSemaphores;
    CommandListHandle m_directPresentCommandList;
    GpuPhysicalQueueId m_directPresentQueue;
    ::Queue<QueueSubmissionToken, Alloc::GlobalArena> m_framesInFlight;
    u32 m_swapChainIndex = Limit<u32>::s_Max;
    u32 m_acquireSyncSlotIndex = 0u;
    u32 m_maxFramesInFlight = s_MaxFramesInFlight;
    u32 m_presentNativeQueueIndex = Limit<u32>::s_Max;
    SwapChainLifecycle m_swapChainLifecycle;
    FramePresentationCoordinator m_framePresentation;
    b8 m_frameAcquired = false;
    b8 m_frameAbandonmentComplete = false;
    b8 m_swapChainMutableFormatSupported = false;
    b8 m_hdr10ColorSpaceExtensionEnabled = false;
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

