// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "backend_context.h"
#include "backend_context_detail.h"
#include "swap_chain_lifecycle.h"

#include <global/atomic_identity.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_VULKAN_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


b8 BackendContext::createInstance(){
    {
        ScopedLock lifecycleLock(m_swapChainLifecycle.m_swapChainLifecycleMutex);
        if(m_swapChainLifecycle.m_swapChainLifecycleState == SwapChainLifecycleState::Destroyed){
            if(m_vulkanInstance || m_vulkanDevice || m_swapChain || m_rhiDevice)
                return false;
            m_swapChainLifecycle.m_swapChainLifecycleState = SwapChainLifecycleState::Ready;
            m_swapChainLifecycle.m_lifecycleDrainActive = false;
            ScopedLock presentationLock(m_framePresentation.m_framePresentationMutex);
            m_framePresentation.m_framePresentationClaimsEnabled = true;
        }
        else if(m_swapChainLifecycle.m_swapChainLifecycleState != SwapChainLifecycleState::Ready){
            return false;
        }
    }
    initDefaultExtensions();

    if(m_deviceParams.enableDebugRuntime){
        m_enabledExtensions.layers.emplace(VulkanDetail::s_ValidationLayerName, m_arena);
    }

    Alloc::ScratchArena scratchArena(VulkanArenaScope::s_InstanceCreateArena, s_DeviceSetupScratchArenaBytes);
    return createVulkanInstance(scratchArena);
}

b8 BackendContext::createDevice(){
    if(!IsValidHardwareRayTracingPolicy(m_deviceParams.hardwareRayTracingPolicy)){
        NWB_LOGGER_ERROR(FOUNDATION_TEXT("Vulkan: Invalid hardware ray tracing device policy."));
        return false;
    }

    if(m_deviceParams.enableDebugRuntime)
        installDebugMessenger();

    m_maxFramesInFlight = m_deviceParams.maxFramesInFlight;
    if(m_maxFramesInFlight == 0){
        NWB_LOGGER_CRITICAL_WARNING(FOUNDATION_TEXT("Vulkan: maxFramesInFlight was 0; clamping to 1."));
        m_maxFramesInFlight = 1;
    }

    auto resolveDeviceExtensionFeature = [this](const AStringView name)noexcept->DeviceExtensionFeature::Enum{
        // Feature metadata remains canonical even when policy removes an extension from the default optional set.
        // A caller that names that extension explicitly must still receive coherent feature query/enable handling.
        for(const ExtEntry& entry : s_OptionalDeviceExts){
            if(name == entry.name)
                return entry.feature;
        }

        const auto rayTracingIt = m_rayTracingExtensions.find(name);
        if(rayTracingIt != m_rayTracingExtensions.end())
            return rayTracingIt.value();

        return DeviceExtensionFeature::None;
    };

    auto registerDeviceExtension = [this](DeviceExtensionMap& extensions, const AStringView name, const DeviceExtensionFeature::Enum feature){
        auto it = extensions.find(name);
        if(it != extensions.end()){
            if(it.value() == DeviceExtensionFeature::None && feature != DeviceExtensionFeature::None)
                it.value() = feature;
            return;
        }
        extensions.emplace(GraphicsString(name, m_arena), feature);
    };

    for(const auto& name : m_deviceParams.requiredBackendDeviceExtensions){
        if(ResolveDeviceExtensionRequest(m_deviceParams.hardwareRayTracingPolicy, name, true) == DeviceExtensionRequestAction::Reject){
            NWB_LOGGER_ERROR(FOUNDATION_TEXT("Vulkan: Required device extension '{}' conflicts with the disabled hardware ray tracing policy."), ConvertString(name));
            return false;
        }
        registerDeviceExtension(m_enabledExtensions.device, name, resolveDeviceExtensionFeature(name));
    }
    for(const auto& name : m_deviceParams.optionalBackendDeviceExtensions){
        if(ResolveDeviceExtensionRequest(m_deviceParams.hardwareRayTracingPolicy, name, false) == DeviceExtensionRequestAction::Omit)
            continue;
        registerDeviceExtension(m_optionalExtensions.device, name, resolveDeviceExtensionFeature(name));
    }
    if(m_deviceParams.enableGpuCrashDiagnostics){
        registerDeviceExtension(m_optionalExtensions.device, VK_NV_DEVICE_DIAGNOSTIC_CHECKPOINTS_EXTENSION_NAME, DeviceExtensionFeature::None);
        registerDeviceExtension(m_optionalExtensions.device, VK_NV_DEVICE_DIAGNOSTICS_CONFIG_EXTENSION_NAME, DeviceExtensionFeature::None);
    }

    m_swapChainState.backBufferFormat = VulkanDetail::GetBackBufferFormat(m_deviceParams);
    m_swapChainState.outputMode = SwapChainOutputMode::SDR;
    if(!m_deviceParams.headlessDevice)
        registerDeviceExtension(m_enabledExtensions.device, VK_KHR_SWAPCHAIN_EXTENSION_NAME, DeviceExtensionFeature::None);
    if(!m_deviceParams.headlessDevice){
        if(!createWindowSurface())
            return false;
    }
    Alloc::ScratchArena scratchArena(VulkanArenaScope::s_DeviceExtensionSetupArena, s_DeviceSetupScratchArenaBytes);

    if(!pickPhysicalDevice(scratchArena))
        return false;
    if(!findQueueFamilies(scratchArena, m_vulkanPhysicalDevice))
        return false;
    if(!createVulkanDevice(scratchArena))
        return false;

    Vector<AStringView, Alloc::ScratchArena> vecInstanceExt(scratchArena);
    Vector<AStringView, Alloc::ScratchArena> vecDeviceExt(scratchArena);
    vecInstanceExt.reserve(m_enabledExtensions.instance.size());
    vecDeviceExt.reserve(m_enabledExtensions.device.size());
    for(const auto& extension : m_enabledExtensions.instance)
        vecInstanceExt.emplace_back(extension);
    for(const auto& extension : m_enabledExtensions.device)
        vecDeviceExt.emplace_back(extension.first);

    DeviceDesc deviceDesc(m_allocator, m_cpuScheduler);
    deviceDesc.instance = m_vulkanInstance;
    deviceDesc.physicalDevice = m_vulkanPhysicalDevice;
    deviceDesc.device = m_vulkanDevice;
    deviceDesc.getInstanceProcAddr = m_getInstanceProcAddr;
    deviceDesc.instanceDispatch = m_instanceDispatch;
    deviceDesc.deviceDispatch = m_deviceDispatch;
    uint32_t physicalQueueFamilyCount = 0u;
    m_instanceDispatch.vkGetPhysicalDeviceQueueFamilyProperties(m_vulkanPhysicalDevice, &physicalQueueFamilyCount, nullptr);
    Vector<VkQueueFamilyProperties, Alloc::ScratchArena> physicalQueueFamilies(physicalQueueFamilyCount, scratchArena);
    m_instanceDispatch.vkGetPhysicalDeviceQueueFamilyProperties(m_vulkanPhysicalDevice, &physicalQueueFamilyCount, physicalQueueFamilies.data());
    const auto queueCapabilitiesForFamily = [&physicalQueueFamilies](const i32 queueFamily)noexcept{
        if(queueFamily == s_InvalidQueueFamilyIndex || static_cast<usize>(queueFamily) >= physicalQueueFamilies.size())
            return GpuQueueCapability::None;
        return VulkanDetail::QueueCapabilitiesForQueueFlags(physicalQueueFamilies[static_cast<usize>(queueFamily)].queueFlags);
    };
    const auto timestampValidBitsForFamily = [&physicalQueueFamilies](const i32 queueFamily)noexcept{
        if(queueFamily == s_InvalidQueueFamilyIndex || static_cast<usize>(queueFamily) >= physicalQueueFamilies.size())
            return 0u;
        return physicalQueueFamilies[static_cast<usize>(queueFamily)].timestampValidBits;
    };
    const GpuQueueCapability::Mask graphicsQueueCapabilities = queueCapabilitiesForFamily(m_graphicsQueueFamily);
    const i32 schedulerComputeQueueFamily = m_asyncComputeQueueFamily != s_InvalidQueueFamilyIndex ? m_asyncComputeQueueFamily : m_computeQueueFamily;
    const GpuQueueCapability::Mask requiredComputeQueueCapabilities = queueCapabilitiesForFamily(m_computeQueueFamily);
    const GpuQueueCapability::Mask computeQueueCapabilities = queueCapabilitiesForFamily(schedulerComputeQueueFamily);
    const GpuQueueCapability::Mask transferQueueCapabilities = queueCapabilitiesForFamily(m_transferQueueFamily);
    const GpuQueueCapability::Mask requiredGraphicsQueueCapabilities = static_cast<GpuQueueCapability::Mask>(static_cast<u8>(GpuQueueCapability::Graphics) | static_cast<u8>(GpuQueueCapability::Transfer));
    const GpuQueueCapability::Mask requiredComputeQueueCapabilitiesMask = static_cast<GpuQueueCapability::Mask>(static_cast<u8>(GpuQueueCapability::Compute) | static_cast<u8>(GpuQueueCapability::Transfer));
    if((static_cast<u8>(graphicsQueueCapabilities) & static_cast<u8>(requiredGraphicsQueueCapabilities)) != static_cast<u8>(requiredGraphicsQueueCapabilities)){
        NWB_LOGGER_ERROR(FOUNDATION_TEXT("Vulkan: selected required Graphics queue family does not support Graphics and Transfer."));
        return false;
    }
    if((static_cast<u8>(requiredComputeQueueCapabilities) & static_cast<u8>(requiredComputeQueueCapabilitiesMask)) != static_cast<u8>(requiredComputeQueueCapabilitiesMask)){
        NWB_LOGGER_ERROR(FOUNDATION_TEXT("Vulkan: selected required Compute queue family does not support Compute and Transfer."));
        return false;
    }
    const u32 graphicsNativeQueueIndex = findNativeQueueIndex(static_cast<u32>(m_graphicsQueueFamily), s_GraphicsQueueIndex);
    const u32 computeNativeQueueIndex = findNativeQueueIndex(static_cast<u32>(schedulerComputeQueueFamily), s_ComputeQueueIndex);
    const u32 transferNativeQueueIndex = findNativeQueueIndex(static_cast<u32>(m_transferQueueFamily), s_TransferQueueIndex);
    if(graphicsNativeQueueIndex == Limit<u32>::s_Max || (m_computeQueueEnabled && computeNativeQueueIndex == Limit<u32>::s_Max) || (m_transferQueueEnabled && transferNativeQueueIndex == Limit<u32>::s_Max)){
        NWB_LOGGER_ERROR(FOUNDATION_TEXT("Vulkan: Scheduler queue projection references a missing native queue."));
        return false;
    }
    constexpr u32 s_ComputeAndTransferQueueCount = 2u;
    Vector<VulkanPhysicalQueueDesc, Alloc::ScratchArena> physicalQueues{scratchArena};
    physicalQueues.reserve(1u + m_sameClassQueues.size() + s_ComputeAndTransferQueueCount);
    const auto appendSameClassQueues = [this, &physicalQueues](const CommandQueue::Enum queueClass)noexcept{
        for(const VulkanPhysicalQueueDesc& queue : m_sameClassQueues){
            if(queue.queueClass == queueClass)
                physicalQueues.push_back(queue);
        }
    };
    physicalQueues.push_back(VulkanPhysicalQueueDesc{
        .nativeQueueIndex = graphicsNativeQueueIndex,
        .timestampValidBits = timestampValidBitsForFamily(m_graphicsQueueFamily),
        .queueClass = CommandQueue::Graphics,
        .capabilities = graphicsQueueCapabilities,
        .dedicated = false,
        .primaryForClass = true,
    });
    appendSameClassQueues(CommandQueue::Graphics);
    if(m_computeQueueEnabled){
        physicalQueues.push_back(VulkanPhysicalQueueDesc{
            .nativeQueueIndex = computeNativeQueueIndex,
            .timestampValidBits = timestampValidBitsForFamily(schedulerComputeQueueFamily),
            .queueClass = CommandQueue::Compute,
            .capabilities = computeQueueCapabilities,
            .dedicated = true,
            .primaryForClass = true,
        });
        appendSameClassQueues(CommandQueue::Compute);
    }
    if(m_transferQueueEnabled){
        physicalQueues.push_back(VulkanPhysicalQueueDesc{
            .nativeQueueIndex = transferNativeQueueIndex,
            .timestampValidBits = timestampValidBitsForFamily(m_transferQueueFamily),
            .queueClass = CommandQueue::Transfer,
            .capabilities = transferQueueCapabilities,
            .dedicated = true,
            .primaryForClass = true,
        });
        appendSameClassQueues(CommandQueue::Transfer);
    }
    deviceDesc.nativeQueues = m_nativeQueues.data();
    deviceDesc.nativeQueueCount = m_nativeQueues.size();
    deviceDesc.physicalQueues = physicalQueues.data();
    deviceDesc.physicalQueueCount = physicalQueues.size();
    deviceDesc.instanceExtensions = vecInstanceExt.data();
    deviceDesc.numInstanceExtensions = vecInstanceExt.size();
    deviceDesc.deviceExtensions = vecDeviceExt.data();
    deviceDesc.numDeviceExtensions = vecDeviceExt.size();
    deviceDesc.bufferDeviceAddressSupported = m_bufferDeviceAddressSupported;
    deviceDesc.hostQueryResetFeatureEnabled = m_hostQueryResetFeatureEnabled;
    deviceDesc.textureCompressionBcFeatureEnabled = m_textureCompressionBcFeatureEnabled;
    deviceDesc.textureCompressionAstcLdrFeatureEnabled = m_textureCompressionAstcLdrFeatureEnabled;
    deviceDesc.textureCompressionAstcHdrFeatureEnabled = m_textureCompressionAstcHdrFeatureEnabled;
    deviceDesc.dynamicRenderingSupported = m_dynamicRenderingSupported;
    deviceDesc.synchronization2Supported = m_synchronization2Supported;
    deviceDesc.independentBlendFeatureEnabled = m_independentBlendFeatureEnabled;
    deviceDesc.fullDrawIndexUint32FeatureEnabled = m_fullDrawIndexUint32FeatureEnabled;
    deviceDesc.multiDrawIndirectFeatureEnabled = m_multiDrawIndirectFeatureEnabled;
    deviceDesc.drawIndirectFirstInstanceFeatureEnabled = m_drawIndirectFirstInstanceFeatureEnabled;
    deviceDesc.meshShaderFeatureEnabled = m_meshShaderFeatureEnabled;
    deviceDesc.accelerationStructureFeatureEnabled = m_accelerationStructureFeatureEnabled;
    deviceDesc.rayTracingPipelineFeatureEnabled = m_rayTracingPipelineFeatureEnabled;
    deviceDesc.rayQueryFeatureEnabled = m_rayQueryFeatureEnabled;
    deviceDesc.opacityMicromapFeatureEnabled = m_opacityMicromapFeatureEnabled;
    deviceDesc.clusterAccelerationStructureFeatureEnabled = m_clusterAccelerationStructureFeatureEnabled;
    deviceDesc.rayTracingInvocationReorderFeatureEnabled = m_rayTracingInvocationReorderFeatureEnabled;
    deviceDesc.rayTracingInvocationReorderExtFeatureEnabled = m_rayTracingInvocationReorderExtFeatureEnabled;
    deviceDesc.cooperativeVectorFeatureEnabled = m_cooperativeVectorFeatureEnabled;
    deviceDesc.cooperativeVectorTrainingFeatureEnabled = m_cooperativeVectorTrainingFeatureEnabled;
    deviceDesc.meshTaskShaderSupported = m_meshTaskShaderSupported;
    deviceDesc.rayTracingSpheresSupported = m_rayTracingSpheresSupported;
    deviceDesc.rayTracingLinearSweptSpheresSupported = m_rayTracingLinearSweptSpheresSupported;
    deviceDesc.gpuCrashDiagnosticsEnabled = m_deviceParams.enableGpuCrashDiagnostics;
    deviceDesc.bindlessHeapAbi = m_deviceParams.bindlessHeapAbi;
    deviceDesc.pipelineCacheDirectory = m_deviceParams.pipelineCacheDirectory;
    deviceDesc.filesystemFactory = m_deviceParams.filesystemFactory;

    auto deviceResult = CreateDevice(deviceDesc);
    if(!deviceResult){
        m_rhiDevice = nullptr;
        NWB_LOGGER_ERROR(FOUNDATION_TEXT("Vulkan: Failed to create RHI device wrapper."));
        return false;
    }
    m_rhiDevice = Move(*deviceResult);
    if(!m_rhiDevice->getDescriptorHeap().isInitialized()){
        NWB_LOGGER_ERROR(FOUNDATION_TEXT("Vulkan: Required descriptor-buffer heap initialization failed."));
        m_rhiDevice = nullptr;
        return false;
    }

    if(m_deviceParams.hardwareRayTracingPolicy == HardwareRayTracingPolicy::Disabled){
        const b8 rayQuery = m_rhiDevice->queryFeatureSupport(Feature::RayQuery);
        const b8 rayTracingPipeline = m_rhiDevice->queryFeatureSupport(Feature::RayTracingPipeline);
        const b8 accelerationStructure = m_rhiDevice->queryFeatureSupport(Feature::RayTracingAccelStruct);
        const GpuDescriptorHeapLifecycleStatistics heap = m_rhiDevice->getDescriptorHeap().lifecycleStatistics();
        const b8 accelerationStructureLayout = m_rhiDevice->getDescriptorHeap().hasAccelStructLayout();
        if(rayQuery || rayTracingPipeline || accelerationStructure || heap.accelStructCapacity != 0u || accelerationStructureLayout){
            NWB_LOGGER_ERROR(FOUNDATION_TEXT("Vulkan: Disabled hardware ray tracing policy produced incompatible device capabilities."));
            m_rhiDevice = nullptr;
            return false;
        }
        NWB_LOGGER_ESSENTIAL_INFO(FOUNDATION_TEXT("Vulkan: hardware ray tracing policy=disabled; RayQuery={} RayTracingPipeline={} RayTracingAccelStruct={} AccelStructDescriptors={} AccelStructLayout={}")
            , static_cast<u32>(rayQuery)
            , static_cast<u32>(rayTracingPipeline)
            , static_cast<u32>(accelerationStructure)
            , heap.accelStructCapacity
            , static_cast<u32>(accelerationStructureLayout)
        );
    }

    return true;
}

b8 BackendContext::createSwapChain(){
    return m_swapChainLifecycle.createSwapChain();
}

b8 BackendContext::destroy(){
    return m_swapChainLifecycle.destroy();
}

Expected<SwapChainTransitionTicket> BackendContext::prepareSwapChainTransition(const SwapChainTransitionKind::Enum kind){
    return m_swapChainLifecycle.prepareSwapChainTransition(kind);
}

b8 BackendContext::commitSwapChainResize(SwapChainTransitionTicket&& ticket){
    return m_swapChainLifecycle.commitSwapChainResize(Move(ticket));
}

b8 BackendContext::commitDestroy(SwapChainTransitionTicket&& ticket)noexcept{
    return m_swapChainLifecycle.commitDestroy(Move(ticket));
}



////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_VULKAN_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

