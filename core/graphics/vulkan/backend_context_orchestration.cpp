// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "backend_context.h"
#include "backend_context_detail.h"


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_VULKAN_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


bool BackendContext::createInstance(){
    {
        ScopedLock lifecycleLock(m_swapChainLifecycleMutex);
        if(m_swapChainLifecycleState == SwapChainLifecycleState::Destroyed){
            if(m_vulkanInstance || m_vulkanDevice || m_swapChain || m_rhiDevice)
                return false;
            m_swapChainLifecycleState = SwapChainLifecycleState::Ready;
            m_lifecycleDrainActive = false;
            ScopedLock presentationLock(m_framePresentationMutex);
            m_framePresentationClaimsEnabled = true;
        }
        else if(m_swapChainLifecycleState != SwapChainLifecycleState::Ready){
            return false;
        }
    }
    initDefaultExtensions();

    if(m_deviceParams.enableDebugRuntime){
        m_enabledExtensions.layers.emplace("VK_LAYER_KHRONOS_validation", m_arena);
    }

    return createVulkanInstance();
}

bool BackendContext::createDevice(){
    if(m_deviceParams.enableDebugRuntime)
        installDebugMessenger();

    m_maxFramesInFlight = m_deviceParams.maxFramesInFlight;
    if(m_maxFramesInFlight == 0){
        NWB_LOGGER_CRITICAL_WARNING(NWB_TEXT("Vulkan: maxFramesInFlight was 0; clamping to 1."));
        m_maxFramesInFlight = 1;
    }

    auto resolveDeviceExtensionFeature = [this](const GraphicsString& name)->DeviceExtensionFeature::Enum{
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

    auto registerDeviceExtension = [](DeviceExtensionMap& extensions, const GraphicsString& name, const DeviceExtensionFeature::Enum feature){
        auto [it, inserted] = extensions.emplace(name, feature);
        if(!inserted && it.value() == DeviceExtensionFeature::None && feature != DeviceExtensionFeature::None)
            it.value() = feature;
    };

    for(const auto& name : m_deviceParams.requiredBackendDeviceExtensions)
        registerDeviceExtension(m_enabledExtensions.device, name, resolveDeviceExtensionFeature(name));
    for(const auto& name : m_deviceParams.optionalBackendDeviceExtensions)
        registerDeviceExtension(m_optionalExtensions.device, name, resolveDeviceExtensionFeature(name));
    if(m_deviceParams.enableGpuCrashDiagnostics){
        m_optionalExtensions.device.emplace(GraphicsString(VK_NV_DEVICE_DIAGNOSTIC_CHECKPOINTS_EXTENSION_NAME, m_arena), DeviceExtensionFeature::None);
        m_optionalExtensions.device.emplace(GraphicsString(VK_NV_DEVICE_DIAGNOSTICS_CONFIG_EXTENSION_NAME, m_arena), DeviceExtensionFeature::None);
    }

    m_swapChainState.backBufferFormat = VulkanDetail::GetBackBufferFormat(m_deviceParams);
    m_swapChainState.outputMode = SwapChainOutputMode::SDR;
    if(!m_deviceParams.headlessDevice)
        m_enabledExtensions.device.emplace(GraphicsString(VK_KHR_SWAPCHAIN_EXTENSION_NAME, m_arena), DeviceExtensionFeature::None);
    if(!m_deviceParams.headlessDevice){
        if(!createWindowSurface())
            return false;
    }
    if(!pickPhysicalDevice())
        return false;
    if(!findQueueFamilies(m_vulkanPhysicalDevice))
        return false;
    if(!createVulkanDevice())
        return false;

    Alloc::ScratchArena scratchArena(VulkanArenaScope::s_DeviceExtensionSetupArena, s_DeviceSetupScratchArenaBytes);

    auto vecInstanceExt = VulkanDetail::StringSetToVector(m_enabledExtensions.instance, scratchArena);
    auto vecDeviceExt = VulkanDetail::StringMapKeysToVector(m_enabledExtensions.device, scratchArena);

    DeviceDesc deviceDesc(m_allocator, m_threadPool);
    deviceDesc.instance = m_vulkanInstance;
    deviceDesc.physicalDevice = m_vulkanPhysicalDevice;
    deviceDesc.device = m_vulkanDevice;
    deviceDesc.getInstanceProcAddr = m_getInstanceProcAddr;
    deviceDesc.instanceDispatch = m_instanceDispatch;
    deviceDesc.deviceDispatch = m_deviceDispatch;
    uint32_t physicalQueueFamilyCount = 0u;
    m_instanceDispatch.vkGetPhysicalDeviceQueueFamilyProperties(m_vulkanPhysicalDevice, &physicalQueueFamilyCount, nullptr);
    Vector<VkQueueFamilyProperties, Alloc::ScratchArena> physicalQueueFamilies(physicalQueueFamilyCount, scratchArena);
    m_instanceDispatch.vkGetPhysicalDeviceQueueFamilyProperties(
        m_vulkanPhysicalDevice,
        &physicalQueueFamilyCount,
        physicalQueueFamilies.data()
    );
    const auto queueCapabilitiesForFamily = [&physicalQueueFamilies](const i32 queueFamily){
        if(
            queueFamily == s_InvalidQueueFamilyIndex
            || static_cast<usize>(queueFamily) >= physicalQueueFamilies.size()
        )
            return GpuQueueCapability::None;
        return VulkanDetail::QueueCapabilitiesForQueueFlags(
            physicalQueueFamilies[static_cast<usize>(queueFamily)].queueFlags
        );
    };
    const auto timestampValidBitsForFamily = [&physicalQueueFamilies](const i32 queueFamily){
        if(
            queueFamily == s_InvalidQueueFamilyIndex
            || static_cast<usize>(queueFamily) >= physicalQueueFamilies.size()
        )
            return 0u;
        return physicalQueueFamilies[static_cast<usize>(queueFamily)].timestampValidBits;
    };
    const GpuQueueCapability::Mask graphicsQueueCapabilities = queueCapabilitiesForFamily(m_graphicsQueueFamily);
    const GpuQueueCapability::Mask computeQueueCapabilities = queueCapabilitiesForFamily(m_computeQueueFamily);
    const GpuQueueCapability::Mask transferQueueCapabilities = queueCapabilitiesForFamily(m_transferQueueFamily);
    const GpuQueueCapability::Mask requiredGraphicsQueueCapabilities = static_cast<GpuQueueCapability::Mask>(
        static_cast<u8>(GpuQueueCapability::Graphics)
        | static_cast<u8>(GpuQueueCapability::Compute)
        | static_cast<u8>(GpuQueueCapability::Transfer)
    );
    if(
        (static_cast<u8>(graphicsQueueCapabilities) & static_cast<u8>(requiredGraphicsQueueCapabilities))
        != static_cast<u8>(requiredGraphicsQueueCapabilities)
    ){
        NWB_LOGGER_ERROR(NWB_TEXT("Vulkan: selected primary Graphics queue family does not support Graphics, Compute, and Transfer."));
        return false;
    }
    const u32 graphicsNativeQueueIndex = findNativeQueueIndex(
        static_cast<u32>(m_graphicsQueueFamily),
        s_GraphicsQueueIndex
    );
    const u32 computeNativeQueueIndex = findNativeQueueIndex(
        static_cast<u32>(m_computeQueueFamily),
        s_ComputeQueueIndex
    );
    const u32 transferNativeQueueIndex = findNativeQueueIndex(
        static_cast<u32>(m_transferQueueFamily),
        s_TransferQueueIndex
    );
    if(
        graphicsNativeQueueIndex == Limit<u32>::s_Max
        || (m_asyncComputeLaneEnabled && computeNativeQueueIndex == Limit<u32>::s_Max)
        || (m_transferQueueEnabled && transferNativeQueueIndex == Limit<u32>::s_Max)
    ){
        NWB_LOGGER_ERROR(NWB_TEXT("Vulkan: Scheduler queue projection references a missing native queue."));
        return false;
    }
    Vector<VulkanPhysicalQueueDesc, Alloc::ScratchArena> physicalQueues{scratchArena};
    physicalQueues.reserve(1u + m_sameClassQueues.size() + 2u);
    const auto appendSameClassQueues = [this, &physicalQueues](const CommandQueue::Enum queueClass){
        for(const VulkanPhysicalQueueDesc& queue : m_sameClassQueues){
            if(queue.queueClass == queueClass)
                physicalQueues.push_back(queue);
        }
    };
    physicalQueues.push_back(VulkanPhysicalQueueDesc{
        .nativeQueueIndex = graphicsNativeQueueIndex,
        .queueClass = CommandQueue::Graphics,
        .capabilities = graphicsQueueCapabilities,
        .timestampValidBits = timestampValidBitsForFamily(m_graphicsQueueFamily),
        .dedicated = false,
        .primaryForClass = true,
    });
    appendSameClassQueues(CommandQueue::Graphics);
    if(m_asyncComputeLaneEnabled){
        physicalQueues.push_back(VulkanPhysicalQueueDesc{
            .nativeQueueIndex = computeNativeQueueIndex,
            .queueClass = CommandQueue::Compute,
            .capabilities = computeQueueCapabilities,
            .timestampValidBits = timestampValidBitsForFamily(m_computeQueueFamily),
            .dedicated = true,
            .primaryForClass = true,
        });
        appendSameClassQueues(CommandQueue::Compute);
    }
    if(m_transferQueueEnabled){
        physicalQueues.push_back(VulkanPhysicalQueueDesc{
            .nativeQueueIndex = transferNativeQueueIndex,
            .queueClass = CommandQueue::Transfer,
            .capabilities = transferQueueCapabilities,
            .timestampValidBits = timestampValidBitsForFamily(m_transferQueueFamily),
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
    deviceDesc.logBufferLifetime = m_deviceParams.logBufferLifetime;
    deviceDesc.bindlessHeapAbi = m_deviceParams.bindlessHeapAbi;
    deviceDesc.vulkanLibraryName = m_deviceParams.backendLibraryName;
    deviceDesc.pipelineCacheDirectory = m_deviceParams.pipelineCacheDirectory;

    m_rhiDevice = CreateDevice(deviceDesc);
    if(!m_rhiDevice){
        NWB_LOGGER_ERROR(NWB_TEXT("Vulkan: Failed to create RHI device wrapper."));
        return false;
    }
    if(!m_rhiDevice->getDescriptorHeap().isInitialized()){
        NWB_LOGGER_ERROR(NWB_TEXT("Vulkan: Required descriptor-buffer heap initialization failed."));
        m_rhiDevice = nullptr;
        return false;
    }

    return true;
}

bool BackendContext::createSwapChainResources(){
    if(!createVulkanSwapChain())
        return false;

    usize const numPresentSemaphores = m_swapChainImages.size();
    if(!recreateSemaphores(m_presentSemaphores, numPresentSemaphores, "create present semaphores")){
        if(!destroySwapChainPrepared())
            NWB_LOGGER_CRITICAL_WARNING(NWB_TEXT("Vulkan: Failed to destroy swapchain after present-semaphore creation failure."));
        return false;
    }

    const usize numAcquireSyncSlots = Max(static_cast<usize>(m_maxFramesInFlight), m_swapChainImages.size());
    if(!recreateAcquireSyncSlots(numAcquireSyncSlots)){
        clearSemaphores(m_presentSemaphores);
        if(!destroySwapChainPrepared())
            NWB_LOGGER_CRITICAL_WARNING(NWB_TEXT("Vulkan: Failed to destroy swapchain after acquire-slot creation failure."));
        return false;
    }

    if(!createFrameSyncQueries()){
        clearSemaphores(m_presentSemaphores);
        clearAcquireSyncSlots();
        if(!destroySwapChainPrepared())
            NWB_LOGGER_CRITICAL_WARNING(NWB_TEXT("Vulkan: Failed to destroy swapchain after frame-query creation failure."));
        return false;
    }

    m_swapChainIndex = Limit<u32>::s_Max;
    m_acquireSyncSlotIndex = 0u;
    m_activeAcquireSyncSlotIndex = Limit<u32>::s_Max;
    {
        ScopedLock presentationLock(m_framePresentationMutex);
        resetFramePresentationSignal();
        m_framePresentationClaimsEnabled = true;
    }
    m_frameAcquired = false;
    m_frameAbandonmentComplete = false;

    return true;
}

bool BackendContext::createSwapChain(){
    ScopedLock lifecycleLock(m_swapChainLifecycleMutex);
    if(
        m_swapChainLifecycleState != SwapChainLifecycleState::Ready
        || m_swapChain
        || !m_swapChainImages.empty()
    ){
        NWB_LOGGER_ERROR(NWB_TEXT("Vulkan: Swapchain creation requires an empty ready lifecycle."));
        return false;
    }

    if(createSwapChainResources())
        return true;

    if(m_rhiDevice && !m_lifecycleDrainActive){
        m_lifecycleDrainActive = m_rhiDevice->beginLifecycleDrain();
        if(!m_lifecycleDrainActive)
            NWB_LOGGER_CRITICAL_WARNING(NWB_TEXT("Vulkan: Failed to close submissions after swapchain creation failure."));
    }
    {
        ScopedLock presentationLock(m_framePresentationMutex);
        m_framePresentationClaimsEnabled = false;
    }
    m_swapChainLifecycleState = SwapChainLifecycleState::NeedsDestroy;
    return false;
}

bool BackendContext::validPreparedTicket(
    const SwapChainTransitionTicket& ticket,
    const SwapChainTransitionKind::Enum kind
)const noexcept{
    const SwapChainLifecycleState requiredState = kind == SwapChainTransitionKind::Resize
        ? SwapChainLifecycleState::PreparedResize
        : SwapChainLifecycleState::PreparedDestroy
    ;
    return ticket.valid()
        && ticket.owner == this
        && ticket.kind == kind
        && ticket.epoch == m_swapChainLifecycleEpoch
        && m_swapChainLifecycleState == requiredState
    ;
}

bool BackendContext::prepareSwapChainTransition(
    const SwapChainTransitionKind::Enum kind,
    SwapChainTransitionTicket& outTicket
){
    ScopedLock lifecycleLock(m_swapChainLifecycleMutex);
    outTicket = {};
    if(kind >= SwapChainTransitionKind::kCount)
        return false;
    if(m_swapChainLifecycleState == SwapChainLifecycleState::Destroyed){
        if(kind != SwapChainTransitionKind::Destroy || m_swapChainLifecycleEpoch == 0u)
            return false;
        outTicket.owner = this;
        outTicket.epoch = m_swapChainLifecycleEpoch;
        outTicket.kind = kind;
        return true;
    }
    if(
        m_swapChainLifecycleState == SwapChainLifecycleState::PreparedResize
        || m_swapChainLifecycleState == SwapChainLifecycleState::PreparedDestroy
    ){
        const bool preparedForRequestedKind =
            (m_swapChainLifecycleState == SwapChainLifecycleState::PreparedResize && kind == SwapChainTransitionKind::Resize)
            || (m_swapChainLifecycleState == SwapChainLifecycleState::PreparedDestroy && kind == SwapChainTransitionKind::Destroy)
        ;
        if(preparedForRequestedKind){
            outTicket.owner = this;
            outTicket.epoch = m_swapChainLifecycleEpoch;
            outTicket.kind = kind;
            return true;
        }
        if(m_swapChainLifecycleState != SwapChainLifecycleState::PreparedResize || kind != SwapChainTransitionKind::Destroy)
            return false;
        if(m_rhiDevice && !m_rhiDevice->sealLifecycleDrainForDestruction()){
            m_swapChainLifecycleState = SwapChainLifecycleState::NeedsDestroy;
            NWB_LOGGER_CRITICAL_WARNING(NWB_TEXT("Vulkan: Failed to seal an already prepared resize for destruction."));
            return false;
        }

        ++m_swapChainLifecycleEpoch;
        if(m_swapChainLifecycleEpoch == 0u)
            ++m_swapChainLifecycleEpoch;
        m_swapChainLifecycleState = SwapChainLifecycleState::PreparedDestroy;
        outTicket.owner = this;
        outTicket.epoch = m_swapChainLifecycleEpoch;
        outTicket.kind = kind;
        return true;
    }
    if(
        m_swapChainLifecycleState == SwapChainLifecycleState::Preparing
        || (m_swapChainLifecycleState == SwapChainLifecycleState::NeedsDestroy && kind != SwapChainTransitionKind::Destroy)
    )
        return false;

    if(m_rhiDevice && !m_lifecycleDrainActive){
        if(m_rhiDevice->submissionOperationActiveOnCurrentThread()){
            NWB_LOGGER_ERROR(NWB_TEXT("Vulkan: A submission callback cannot synchronously prepare a swapchain lifecycle transition."));
            return false;
        }
        if(!m_rhiDevice->beginLifecycleDrain()){
            m_swapChainLifecycleState = SwapChainLifecycleState::NeedsDestroy;
            NWB_LOGGER_CRITICAL_WARNING(NWB_TEXT("Vulkan: Failed to close the submission gate for a swapchain transition."));
            return false;
        }
        m_lifecycleDrainActive = true;
    }
    {
        ScopedLock presentationLock(m_framePresentationMutex);
        m_framePresentationClaimsEnabled = false;
    }
    m_swapChainLifecycleState = SwapChainLifecycleState::Preparing;

    if(!preflightSwapChainImageRevocation()){
        m_swapChainLifecycleState = SwapChainLifecycleState::NeedsDestroy;
        return false;
    }

    const bool acquireProofsComplete = waitAcquireSyncSlotsForLifecycle();
    if(!acquireProofsComplete && (!m_rhiDevice || !m_rhiDevice->isDeviceLost())){
        m_swapChainLifecycleState = SwapChainLifecycleState::NeedsDestroy;
        NWB_LOGGER_CRITICAL_WARNING(NWB_TEXT("Vulkan: Swapchain transition could not prove WSI acquire completion."));
        return false;
    }

    if(m_rhiDevice){
        const bool deviceIdle = m_rhiDevice->waitForIdle();
        if(!deviceIdle && !m_rhiDevice->isDeviceLost()){
            m_swapChainLifecycleState = SwapChainLifecycleState::NeedsDestroy;
            NWB_LOGGER_CRITICAL_WARNING(NWB_TEXT("Vulkan: Swapchain transition could not prove device idle."));
            return false;
        }
        if(kind == SwapChainTransitionKind::Resize && m_rhiDevice->requiresRecreation()){
            m_swapChainLifecycleState = SwapChainLifecycleState::NeedsDestroy;
            return false;
        }
        if(kind == SwapChainTransitionKind::Destroy && !m_rhiDevice->sealLifecycleDrainForDestruction()){
            m_swapChainLifecycleState = SwapChainLifecycleState::NeedsDestroy;
            NWB_LOGGER_CRITICAL_WARNING(NWB_TEXT("Vulkan: Failed to seal the prepared device destruction state."));
            return false;
        }
    }

    ++m_swapChainLifecycleEpoch;
    if(m_swapChainLifecycleEpoch == 0u)
        ++m_swapChainLifecycleEpoch;
    m_swapChainLifecycleState = kind == SwapChainTransitionKind::Resize
        ? SwapChainLifecycleState::PreparedResize
        : SwapChainLifecycleState::PreparedDestroy
    ;
    outTicket.epoch = m_swapChainLifecycleEpoch;
    outTicket.owner = this;
    outTicket.kind = kind;
    return true;
}

bool BackendContext::commitSwapChainResize(SwapChainTransitionTicket&& ticket){
    ScopedLock lifecycleLock(m_swapChainLifecycleMutex);
    if(!validPreparedTicket(ticket, SwapChainTransitionKind::Resize))
        return false;

    if(!destroySwapChainPrepared()){
        m_swapChainLifecycleState = SwapChainLifecycleState::NeedsDestroy;
        return false;
    }
    while(!m_framesInFlight.empty())
        m_framesInFlight.pop();
    m_queryPool.clear();
    clearSemaphores(m_presentSemaphores);
    clearAcquireSyncSlots();

    if(!createSwapChainResources()){
        m_swapChainLifecycleState = SwapChainLifecycleState::NeedsDestroy;
        return false;
    }

    NWB_ASSERT(m_rhiDevice && m_lifecycleDrainActive);
    m_swapChainLifecycleState = SwapChainLifecycleState::Ready;
    {
        ScopedLock presentationLock(m_framePresentationMutex);
        m_framePresentationClaimsEnabled = true;
    }
    m_rhiDevice->endLifecycleDrain();
    m_lifecycleDrainActive = false;
    ticket = {};
    return true;
}

bool BackendContext::commitDestroy(SwapChainTransitionTicket&& ticket)noexcept{
    ScopedLock lifecycleLock(m_swapChainLifecycleMutex);
    if(m_swapChainLifecycleState == SwapChainLifecycleState::Destroyed){
        const bool validNoOpTicket = ticket.valid()
            && ticket.owner == this
            && ticket.kind == SwapChainTransitionKind::Destroy
            && ticket.epoch == m_swapChainLifecycleEpoch
        ;
        ticket = {};
        return validNoOpTicket;
    }
    if(!validPreparedTicket(ticket, SwapChainTransitionKind::Destroy))
        return false;

    if(!destroySwapChainPrepared()){
        m_swapChainLifecycleState = SwapChainLifecycleState::NeedsDestroy;
        return false;
    }

    while(!m_framesInFlight.empty())
        m_framesInFlight.pop();
    m_queryPool.clear();

    clearSemaphores(m_presentSemaphores);
    clearAcquireSyncSlots();

    m_rhiDevice = nullptr;
    m_lifecycleDrainActive = false;
    m_sameClassQueues.clear();
    m_nativeQueues.clear();
    m_presentNativeQueueIndex = Limit<u32>::s_Max;
    m_rendererString.clear();

    if(m_vulkanDevice){
        m_deviceDispatch.vkDestroyDevice(m_vulkanDevice, nullptr);
        m_vulkanDevice = VK_NULL_HANDLE;
    }

    if(m_windowSurface){
        NWB_ASSERT(m_vulkanInstance);
        m_instanceDispatch.vkDestroySurfaceKHR(m_vulkanInstance, m_windowSurface, nullptr);
        m_windowSurface = VK_NULL_HANDLE;
    }

    if(m_debugUtilsMessenger){
        if(m_instanceDispatch.vkDestroyDebugUtilsMessengerEXT)
            m_instanceDispatch.vkDestroyDebugUtilsMessengerEXT(m_vulkanInstance, m_debugUtilsMessenger, nullptr);
        m_debugUtilsMessenger = VK_NULL_HANDLE;
    }

    if(m_vulkanInstance){
        m_instanceDispatch.vkDestroyInstance(m_vulkanInstance, nullptr);
        m_vulkanInstance = VK_NULL_HANDLE;
    }

    Aftermath::Shutdown();
    m_swapChainLifecycleState = SwapChainLifecycleState::Destroyed;
    ticket = {};
    return true;
}

bool BackendContext::destroy(){
    {
        ScopedLock lifecycleLock(m_swapChainLifecycleMutex);
        if(m_swapChainLifecycleState == SwapChainLifecycleState::Destroyed)
            return true;
    }

    SwapChainTransitionTicket ticket;
    if(!prepareSwapChainTransition(SwapChainTransitionKind::Destroy, ticket))
        return false;
    return commitDestroy(Move(ticket));
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Frame management


BeginFrameResult BackendContext::beginFrame(){
    ScopedLock lifecycleLock(m_swapChainLifecycleMutex);
    BeginFrameResult result;

    if(m_swapChainLifecycleState != SwapChainLifecycleState::Ready || m_frameAcquired){
        NWB_LOGGER_ERROR(NWB_TEXT("Cannot begin a Vulkan frame while the previous acquired swap-chain image remains unresolved"));
        return result;
    }

    if(!cancelFramePresentationSignal()){
        m_swapChainLifecycleState = SwapChainLifecycleState::NeedsDestroy;
        return result;
    }
    m_swapChainIndex = Limit<u32>::s_Max;
    m_frameAbandonmentComplete = false;

    if(!m_rhiDevice || !m_swapChain || m_acquireSyncSlots.empty()){
        NWB_LOGGER_WARNING(NWB_TEXT("Vulkan: beginFrame skipped because swap chain or acquire synchronization is not ready."));
        return result;
    }

    if(m_acquireSyncSlotIndex >= m_acquireSyncSlots.size())
        m_acquireSyncSlotIndex = 0u;
    AcquireSyncSlot& slot = m_acquireSyncSlots[m_acquireSyncSlotIndex];
    if(!prepareAcquireSyncSlot(slot)){
        NWB_LOGGER_CRITICAL_WARNING(NWB_TEXT("Vulkan: Failed to prepare the next acquire synchronization slot."));
        m_rhiDevice->quarantineDevice();
        m_swapChainLifecycleState = SwapChainLifecycleState::NeedsDestroy;
        return result;
    }

    u32 acquiredIndex = Limit<u32>::s_Max;
    const VkResult acquireResult = m_deviceDispatch.vkAcquireNextImageKHR(
        m_vulkanDevice,
        m_swapChain,
        UINT64_MAX,
        slot.semaphore,
        slot.fence,
        &acquiredIndex
    );
    if(acquireResult == VK_ERROR_OUT_OF_DATE_KHR){
        VkSurfaceCapabilitiesKHR surfaceCaps = {};
        const VkResult capabilitiesResult = m_instanceDispatch.vkGetPhysicalDeviceSurfaceCapabilitiesKHR(
            m_vulkanPhysicalDevice,
            m_windowSurface,
            &surfaceCaps
        );
        if(capabilitiesResult != VK_SUCCESS){
            NWB_LOGGER_WARNING(NWB_TEXT("Vulkan: Failed to query surface capabilities for a requested resize. {}")
                , ResultToString(capabilitiesResult)
            );
            return result;
        }
        if(surfaceCaps.currentExtent.width != UINT32_MAX && surfaceCaps.currentExtent.height != UINT32_MAX){
            result.suggestedWidth = surfaceCaps.currentExtent.width;
            result.suggestedHeight = surfaceCaps.currentExtent.height;
        }
        else{
            result.suggestedWidth = Max(
                surfaceCaps.minImageExtent.width,
                Min(surfaceCaps.maxImageExtent.width, m_swapChainState.backBufferWidth)
            );
            result.suggestedHeight = Max(
                surfaceCaps.minImageExtent.height,
                Min(surfaceCaps.maxImageExtent.height, m_swapChainState.backBufferHeight)
            );
        }
        result.status = BeginFrameStatus::ResizeRequired;
        return result;
    }
    if(acquireResult != VK_SUCCESS && acquireResult != VK_SUBOPTIMAL_KHR){
        if(acquireResult == VK_ERROR_DEVICE_LOST)
            m_rhiDevice->captureDeviceLoss("acquire next image");
        NWB_LOGGER_WARNING(NWB_TEXT("Vulkan: Failed to acquire next swap chain image. {}"), ResultToString(acquireResult));
        return result;
    }

    slot.state = AcquireSyncSlotState::AcquirePending;
    m_activeAcquireSyncSlotIndex = m_acquireSyncSlotIndex;
    m_acquireSyncSlotIndex = (m_acquireSyncSlotIndex + 1u) % static_cast<u32>(m_acquireSyncSlots.size());
    m_frameAcquired = true;
    slot.consumerToken = m_rhiDevice->consumeAcquiredImageSemaphore(slot.semaphore);
    if(!slot.consumerToken.valid()){
        slot.state = AcquireSyncSlotState::Unconsumed;
        m_rhiDevice->quarantineDevice();
        m_swapChainLifecycleState = SwapChainLifecycleState::NeedsDestroy;
        NWB_LOGGER_CRITICAL_WARNING(NWB_TEXT("Vulkan: Failed to bridge an acquired WSI semaphore into a queue completion token."));
        return result;
    }
    slot.state = AcquireSyncSlotState::ConsumerAccepted;

    if(acquiredIndex >= m_swapChainImages.size() || !m_swapChainImages[acquiredIndex].rhiHandle){
        NWB_LOGGER_CRITICAL_WARNING(NWB_TEXT("Vulkan: Acquired swap-chain image index does not identify a live back buffer."));
        m_rhiDevice->quarantineDevice();
        m_swapChainLifecycleState = SwapChainLifecycleState::NeedsDestroy;
        return result;
    }

    const SwapChainImage& swapChainImage = m_swapChainImages[acquiredIndex];
    result.backBuffer.texture = swapChainImage.rhiHandle;
    result.backBuffer.availabilityCompletion = slot.consumerToken;
    result.backBuffer.nativeInitialState = swapChainImage.presentationState.nativeInitialState();
    result.backBuffer.index = acquiredIndex;
    result.status = BeginFrameStatus::Acquired;
    NWB_ASSERT(result.acquired());

    m_swapChainIndex = acquiredIndex;
    m_frameAbandonmentComplete = false;
    return result;
}

bool BackendContext::abandonAcquiredFrame()noexcept{
    ScopedLock lifecycleLock(m_swapChainLifecycleMutex);
    UniqueLock<Futex> presentationLock(m_framePresentationMutex);
    m_framePresentationCondition.wait(presentationLock, [this](){
        return m_framePresentationSignalState != FramePresentationSignalState::Queued;
    });
    if(!m_frameAcquired)
        return true;
    if(m_frameAbandonmentComplete){
        NWB_ASSERT(m_swapChainIndex == Limit<u32>::s_Max);
        NWB_ASSERT(m_framePresentationSignalState == FramePresentationSignalState::Idle);
        return true;
    }
    if(!m_rhiDevice){
        NWB_LOGGER_CRITICAL_WARNING(NWB_TEXT("Vulkan: Cannot abandon an acquired frame without a live RHI device."));
        NWB_ASSERT_MSG(false, NWB_TEXT("A valid acquired frame must retain its RHI device"));
        m_swapChainIndex = Limit<u32>::s_Max;
        return false;
    }
    if(m_rhiDevice->requiresRecreation()){
        m_swapChainIndex = Limit<u32>::s_Max;
        return false;
    }

    const bool currentImageReady =
        m_swapChain
        && m_swapChainIndex < m_swapChainImages.size()
        && m_swapChainIndex < m_presentSemaphores.size()
        && m_swapChainImages[m_swapChainIndex].rhiHandle
        && m_presentSemaphores[m_swapChainIndex] != VK_NULL_HANDLE
    ;
    const bool presentationSignalIdle =
        m_framePresentationSignalState == FramePresentationSignalState::Idle
        && m_framePresentationSemaphore == VK_NULL_HANDLE
        && !m_framePresentationQueue.valid()
        && m_framePresentationSwapChainIndex == Limit<u32>::s_Max
    ;
    const bool presentationSignalTracked =
        currentImageReady
        && m_framePresentationSignalState != FramePresentationSignalState::Idle
        && m_framePresentationSemaphore == m_presentSemaphores[m_swapChainIndex]
        && m_framePresentationSwapChainIndex == m_swapChainIndex
    ;
    const bool abandonmentReady = currentImageReady && (presentationSignalIdle || presentationSignalTracked);
    const bool presentationSignalNeedsIdle =
        m_framePresentationSignalState == FramePresentationSignalState::Queued
        || m_framePresentationSignalState == FramePresentationSignalState::Accepted
        || m_framePresentationSignalState == FramePresentationSignalState::Failed
    ;

    // Invalidate presentation identity before touching signal state. Keeping m_frameAcquired set quarantines the
    // WSI image even if retirement or draining fails, so no later beginFrame, claim, or present can reuse it.
    m_swapChainIndex = Limit<u32>::s_Max;
    if(!abandonmentReady){
        NWB_LOGGER_CRITICAL_WARNING(NWB_TEXT("Vulkan: Acquired-frame abandonment preconditions failed; forcing device teardown."));
        m_rhiDevice->quarantineDevice();
        return false;
    }

    if(presentationSignalNeedsIdle && !m_rhiDevice->waitForIdle()){
        NWB_LOGGER_CRITICAL_WARNING(NWB_TEXT("Vulkan: Failed to join the abandoned presentation signal; forcing device teardown."));
        m_rhiDevice->quarantineDevice();
        return false;
    }
    if(presentationSignalNeedsIdle && !replaceFramePresentationSemaphoreAfterIdle()){
        NWB_LOGGER_CRITICAL_WARNING(NWB_TEXT("Vulkan: Failed to quarantine the abandoned presentation signal; forcing device teardown."));
        m_rhiDevice->quarantineDevice();
        return false;
    }
    resetFramePresentationSignal();

    m_frameAbandonmentComplete = true;
    return true;
}

bool BackendContext::present(){
    ScopedLock lifecycleLock(m_swapChainLifecycleMutex);
    VkResult res = VK_SUCCESS;

    if(!m_rhiDevice || !m_frameAcquired || !m_swapChain || m_presentSemaphores.empty() || m_swapChainImages.empty()){
        NWB_LOGGER_WARNING(NWB_TEXT("Vulkan: present skipped because its device, acquired frame, or swap-chain resources are not ready."));
        if(!cancelFramePresentationSignal())
            NWB_LOGGER_CRITICAL_WARNING(NWB_TEXT("Vulkan: Failed to cancel presentation synchronization after an invalid present."));
        return false;
    }

    if(m_swapChainIndex >= m_presentSemaphores.size() || m_swapChainIndex >= m_swapChainImages.size()){
        if(!cancelFramePresentationSignal())
            NWB_LOGGER_CRITICAL_WARNING(NWB_TEXT("Vulkan: Failed to cancel presentation synchronization for an invalid image index."));
        NWB_LOGGER_ERROR(NWB_TEXT("Cannot present Vulkan swap-chain image because its acquired index is invalid"));
        return false;
    }

    const VkSemaphore& semaphore = m_presentSemaphores[m_swapChainIndex];

    bool frameSignalAccepted = false;
    {
        ScopedLock presentationLock(m_framePresentationMutex);
        frameSignalAccepted =
            m_framePresentationSignalState == FramePresentationSignalState::Accepted
            && m_framePresentationSwapChainIndex == m_swapChainIndex
            && m_framePresentationSemaphore == semaphore
        ;
    }

    if(!frameSignalAccepted){
        if(!cancelFramePresentationSignal()){
            NWB_LOGGER_CRITICAL_WARNING(NWB_TEXT("Vulkan: Failed to cancel the previous presentation-signal claim."));
            return false;
        }

        SwapChainImage& swapChainImage = m_swapChainImages[m_swapChainIndex];
        const VulkanDetail::CompatibilityPresentTransitionPolicy::Enum transitionPolicy =
            VulkanDetail::ResolveCompatibilityPresentTransitionPolicy(
                swapChainImage.presentationState.nativeInitialState()
            )
        ;
        const GpuPhysicalQueueId primaryGraphicsQueue = m_rhiDevice->getPrimaryPhysicalQueue(CommandQueue::Graphics);
        if(
            !swapChainImage.rhiHandle
            || transitionPolicy == VulkanDetail::CompatibilityPresentTransitionPolicy::Invalid
            || !primaryGraphicsQueue.valid()
        ){
            if(!cancelFramePresentationSignal())
                NWB_LOGGER_CRITICAL_WARNING(NWB_TEXT("Vulkan: Failed to cancel presentation synchronization after invalid compatibility state."));
            NWB_LOGGER_ERROR(NWB_TEXT("Vulkan: Compatibility presentation transition preconditions failed."));
            return false;
        }

        CommandListParameters commandListParams;
        commandListParams.setPhysicalQueue(primaryGraphicsQueue);
        CommandListHandle compatibilityCommandList = m_rhiDevice->createCommandList(commandListParams);
        if(!compatibilityCommandList){
            NWB_LOGGER_ERROR(NWB_TEXT("Vulkan: Failed to allocate the compatibility presentation command list."));
            return false;
        }

        compatibilityCommandList->open();
        if(
            !compatibilityCommandList->hasCommandBuffer()
            || !compatibilityCommandList->isRecording()
            || compatibilityCommandList->commandRecordingFailed()
        ){
            NWB_LOGGER_ERROR(NWB_TEXT("Vulkan: Failed to open the compatibility presentation command list."));
            return false;
        }

        if(transitionPolicy == VulkanDetail::CompatibilityPresentTransitionPolicy::PreservePresent){
            compatibilityCommandList->beginTrackingTextureState(
                swapChainImage.rhiHandle.get(),
                s_AllSubresources,
                ResourceStates::Present
            );
        }
        compatibilityCommandList->setTextureState(
            swapChainImage.rhiHandle.get(),
            s_AllSubresources,
            ResourceStates::Present
        );
        compatibilityCommandList->commitBarriers();
        if(
            !compatibilityCommandList->hasCommandBuffer()
            || !compatibilityCommandList->isRecording()
            || compatibilityCommandList->commandRecordingFailed()
        ){
            NWB_LOGGER_ERROR(NWB_TEXT("Vulkan: Failed to record the compatibility presentation transition."));
            return false;
        }

        compatibilityCommandList->close();
        if(
            !compatibilityCommandList->hasCommandBuffer()
            || compatibilityCommandList->isRecording()
            || compatibilityCommandList->commandRecordingFailed()
        ){
            NWB_LOGGER_ERROR(NWB_TEXT("Vulkan: Failed to close the compatibility presentation command list."));
            return false;
        }

        const QueueSubmissionPreSubmitHook presentationSignalHook = claimFramePresentationSignal();
        if(!presentationSignalHook.valid()){
            if(!cancelFramePresentationSignal())
                NWB_LOGGER_CRITICAL_WARNING(NWB_TEXT("Vulkan: Failed to cancel presentation synchronization after claim rejection."));
            NWB_LOGGER_ERROR(NWB_TEXT("Vulkan: Failed to claim compatibility presentation on the primary Graphics queue."));
            return false;
        }

        QueueSubmissionDesc submitDesc;
        submitDesc.setPreSubmitHook(presentationSignalHook);
        CommandList* const compatibilityCommandLists[] = { compatibilityCommandList.get() };
        const QueueSubmissionToken fallbackToken = m_rhiDevice->executeCommandLists(
            compatibilityCommandLists,
            LengthOf(compatibilityCommandLists),
            primaryGraphicsQueue,
            submitDesc
        );
        if(!fallbackToken.valid()){
            if(!cancelFramePresentationSignal(presentationSignalHook))
                NWB_LOGGER_CRITICAL_WARNING(NWB_TEXT("Vulkan: Failed to cancel presentation synchronization after submit rejection."));
            NWB_LOGGER_ERROR(NWB_TEXT("Vulkan: Compatibility presentation transition/signal submission was rejected."));
            return false;
        }
        if(!confirmFramePresentationSignal(presentationSignalHook, fallbackToken)){
            if(!cancelFramePresentationSignal(presentationSignalHook))
                NWB_LOGGER_CRITICAL_WARNING(NWB_TEXT("Vulkan: Failed to cancel presentation synchronization after confirmation rejection."));
            NWB_LOGGER_ERROR(NWB_TEXT("Vulkan: Accepted compatibility presentation submission failed signal confirmation/tracking."));
            return false;
        }

        frameSignalAccepted = true;
    }

    ScopedLock presentationLock(m_framePresentationMutex);
    frameSignalAccepted =
        m_framePresentationSignalState == FramePresentationSignalState::Accepted
        && m_framePresentationSwapChainIndex == m_swapChainIndex
        && m_framePresentationSemaphore == semaphore
    ;
    if(!frameSignalAccepted){
        NWB_LOGGER_CRITICAL_WARNING(NWB_TEXT("Vulkan: Presentation signal state changed before native present."));
        return false;
    }

    VkPresentInfoKHR presentInfo = {};
    presentInfo.sType = VK_STRUCTURE_TYPE_PRESENT_INFO_KHR;
    presentInfo.waitSemaphoreCount = 1;
    presentInfo.pWaitSemaphores = &semaphore;
    presentInfo.swapchainCount = 1;
    presentInfo.pSwapchains = &m_swapChain;
    presentInfo.pImageIndices = &m_swapChainIndex;

    if(!m_rhiDevice){
        NWB_LOGGER_ERROR(NWB_TEXT("Vulkan: Presentation requires a synchronized RHI device owner."));
        return false;
    }
    if(!m_rhiDevice->presentNativeQueue(m_presentNativeQueueIndex, presentInfo, res)){
        if(!cancelFramePresentationSignalLocked())
            NWB_LOGGER_CRITICAL_WARNING(NWB_TEXT("Vulkan: Failed to retire a presentation signal after native present rejection."));
        return false;
    }
    const VulkanDetail::QueuePresentWaitDisposition::Enum presentWaitDisposition =
        VulkanDetail::ClassifyQueuePresentWaitDisposition(res);
    m_swapChainImages[m_swapChainIndex].presentationState.observeQueuePresentWaitDisposition(presentWaitDisposition);
    if(presentWaitDisposition != VulkanDetail::QueuePresentWaitDisposition::Consumed){
        // Host/device OOM and unknown failures do not prove the accepted binary signal was consumed. Preserve its
        // tracking until successful idle-and-replacement quarantine or terminal teardown.
        if(presentWaitDisposition == VulkanDetail::QueuePresentWaitDisposition::DeviceLost){
            if(m_rhiDevice)
                m_rhiDevice->captureDeviceLoss("present");
            NWB_LOGGER_WARNING(NWB_TEXT("Vulkan: Queue present failed. {}"), ResultToString(res));
            return false;
        }
        if(!frameSignalAccepted || !m_rhiDevice){
            NWB_LOGGER_CRITICAL_WARNING(
                NWB_TEXT("Vulkan: Queue present lost its accepted signal tracking; forcing device teardown.")
            );
            if(m_rhiDevice)
                m_rhiDevice->quarantineDevice();
            return false;
        }
        if(!m_rhiDevice->waitForIdle()){
            NWB_LOGGER_CRITICAL_WARNING(
                NWB_TEXT("Vulkan: Failed to join the unconsumed presentation signal; forcing device teardown.")
            );
            m_rhiDevice->quarantineDevice();
            return false;
        }
        if(!replaceFramePresentationSemaphoreAfterIdle()){
            m_rhiDevice->quarantineDevice();
            return false;
        }
        resetFramePresentationSignal();
        NWB_LOGGER_WARNING(NWB_TEXT("Vulkan: Queue present failed. {}"), ResultToString(res));
        return false;
    }

    // Every consumed disposition has retired the binary wait even when the surface itself must be recreated.
    resetFramePresentationSignal();
    m_frameAcquired = false;
    m_frameAbandonmentComplete = false;
    m_swapChainIndex = Limit<u32>::s_Max;
    m_activeAcquireSyncSlotIndex = Limit<u32>::s_Max;
    if(!(res == VK_SUCCESS || res == VK_ERROR_OUT_OF_DATE_KHR || res == VK_SUBOPTIMAL_KHR)){
        NWB_LOGGER_WARNING(NWB_TEXT("Vulkan: Queue present consumed synchronization but requires recreation. {}")
            , ResultToString(res)
        );
        return false;
    }

    while(m_framesInFlight.size() >= m_maxFramesInFlight){
        auto query = m_framesInFlight.front();
        if(!m_rhiDevice->waitEventQuery(query.get())){
            if(!m_rhiDevice->isDeviceLost())
                m_rhiDevice->quarantineDevice();
            return false;
        }
        m_framesInFlight.pop();
        m_queryPool.push_back(query);
    }

    EventQueryHandle query;
    if(!m_queryPool.empty()){
        query = m_queryPool.back();
        m_queryPool.pop_back();
    }
    else{
        NWB_ASSERT_MSG(false, NWB_TEXT("Vulkan: frame synchronization query pool was exhausted"));
        NWB_LOGGER_CRITICAL_WARNING(NWB_TEXT("Vulkan: Frame synchronization query pool was exhausted; forcing device teardown."));
        m_rhiDevice->quarantineDevice();
        return false;
    }

    if(!query){
        NWB_LOGGER_CRITICAL_WARNING(NWB_TEXT("Vulkan: Failed to acquire a frame synchronization query; forcing device teardown."));
        m_rhiDevice->quarantineDevice();
        return false;
    }

    if(!m_rhiDevice->setEventQuery(query.get(), CommandQueue::Graphics)){
        m_queryPool.push_back(query);
        if(!m_rhiDevice->isDeviceLost())
            m_rhiDevice->quarantineDevice();
        return false;
    }
    m_framesInFlight.push(query);

    return true;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_VULKAN_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

