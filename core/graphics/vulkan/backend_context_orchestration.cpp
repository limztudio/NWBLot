// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "backend_context.h"
#include "backend_context_detail.h"


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_VULKAN_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


bool BackendContext::createInstance(){
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
        const auto optionalIt = m_optionalExtensions.device.find(name);
        if(optionalIt != m_optionalExtensions.device.end() && optionalIt.value() != DeviceExtensionFeature::None)
            return optionalIt.value();

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
    deviceDesc.graphicsQueue = m_graphicsQueue;
    deviceDesc.graphicsQueueIndex = m_graphicsQueueFamily;
    if(m_asyncComputeLaneEnabled){
        deviceDesc.computeQueue = m_computeQueue;
        deviceDesc.computeQueueIndex = m_computeQueueFamily;
    }
    if(m_transferQueueEnabled){
        deviceDesc.transferQueue = m_transferQueue;
        deviceDesc.transferQueueIndex = m_transferQueueFamily;
    }
    deviceDesc.asyncComputeLaneEnabled = m_asyncComputeLaneEnabled;
    deviceDesc.transferQueueEnabled = m_transferQueueEnabled;
    uint32_t physicalQueueFamilyCount = 0u;
    vkGetPhysicalDeviceQueueFamilyProperties(m_vulkanPhysicalDevice, &physicalQueueFamilyCount, nullptr);
    Vector<VkQueueFamilyProperties, Alloc::ScratchArena> physicalQueueFamilies(physicalQueueFamilyCount, scratchArena);
    vkGetPhysicalDeviceQueueFamilyProperties(
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
    Vector<VulkanPhysicalQueueDesc, Alloc::ScratchArena> physicalQueues{scratchArena};
    physicalQueues.reserve(1u + m_sameClassQueues.size() + 2u);
    const auto appendSameClassQueues = [this, &physicalQueues](const CommandQueue::Enum queueClass){
        for(const VulkanPhysicalQueueDesc& queue : m_sameClassQueues){
            if(queue.queueClass == queueClass)
                physicalQueues.push_back(queue);
        }
    };
    physicalQueues.push_back(VulkanPhysicalQueueDesc{
        .queue = m_graphicsQueue,
        .queueClass = CommandQueue::Graphics,
        .capabilities = graphicsQueueCapabilities,
        .familyIndex = static_cast<u32>(m_graphicsQueueFamily),
        .queueIndex = s_GraphicsQueueIndex,
        .timestampValidBits = timestampValidBitsForFamily(m_graphicsQueueFamily),
        .dedicated = false,
        .primaryForClass = true,
    });
    appendSameClassQueues(CommandQueue::Graphics);
    if(m_asyncComputeLaneEnabled){
        physicalQueues.push_back(VulkanPhysicalQueueDesc{
            .queue = m_computeQueue,
            .queueClass = CommandQueue::Compute,
            .capabilities = computeQueueCapabilities,
            .familyIndex = static_cast<u32>(m_computeQueueFamily),
            .queueIndex = s_ComputeQueueIndex,
            .timestampValidBits = timestampValidBitsForFamily(m_computeQueueFamily),
            .dedicated = true,
            .primaryForClass = true,
        });
        appendSameClassQueues(CommandQueue::Compute);
    }
    if(m_transferQueueEnabled){
        physicalQueues.push_back(VulkanPhysicalQueueDesc{
            .queue = m_transferQueue,
            .queueClass = CommandQueue::Transfer,
            .capabilities = transferQueueCapabilities,
            .familyIndex = static_cast<u32>(m_transferQueueFamily),
            .queueIndex = s_TransferQueueIndex,
            .timestampValidBits = timestampValidBitsForFamily(m_transferQueueFamily),
            .dedicated = true,
            .primaryForClass = true,
        });
        appendSameClassQueues(CommandQueue::Transfer);
    }
    deviceDesc.physicalQueues = physicalQueues.data();
    deviceDesc.physicalQueueCount = physicalQueues.size();
    deviceDesc.instanceExtensions = vecInstanceExt.data();
    deviceDesc.numInstanceExtensions = vecInstanceExt.size();
    deviceDesc.deviceExtensions = vecDeviceExt.data();
    deviceDesc.numDeviceExtensions = vecDeviceExt.size();
    deviceDesc.bufferDeviceAddressSupported = m_bufferDeviceAddressSupported;
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

#if defined(NWB_GPU_FAULT_INJECTION)
    // Test-only device fault injection after creation.
    u64 faultDeviceAddress = 0u;
    if(VulkanDetail::ReadGpuFaultInjectionValue(faultDeviceAddress))
        m_rhiDevice->debugTriggerGpuFault(faultDeviceAddress);
#endif

    return true;
}

bool BackendContext::createSwapChain(){
    if(!createVulkanSwapChain())
        return false;

    usize const numPresentSemaphores = m_swapChainImages.size();
    if(!recreateSemaphores(m_presentSemaphores, numPresentSemaphores, "create present semaphores")){
        destroySwapChain();
        return false;
    }

    usize const numAcquireSemaphores = Max(static_cast<usize>(m_maxFramesInFlight), m_swapChainImages.size());
    if(!recreateSemaphores(m_acquireSemaphores, numAcquireSemaphores, "create acquire semaphores")){
        clearSemaphores(m_presentSemaphores);
        destroySwapChain();
        return false;
    }

    if(!createFrameSyncQueries()){
        clearSemaphores(m_presentSemaphores);
        clearSemaphores(m_acquireSemaphores);
        destroySwapChain();
        return false;
    }

    m_swapChainIndex = Limit<u32>::s_Max;
    m_acquireSemaphoreIndex = 0;
    resetFramePresentationSignal();
    m_frameAcquired = false;
    m_frameAbandonmentComplete = false;

    return true;
}

void BackendContext::destroy(){
    if(m_rhiDevice)
        m_rhiDevice->waitForIdle();

    // destroy() already joined all device work, and its semaphore pools are released below. Do not allocate a
    // replacement for an abandoned graph signal on this terminal path.
    resetFramePresentationSignal();
    m_frameAcquired = false;
    m_frameAbandonmentComplete = false;

    while(!m_framesInFlight.empty())
        m_framesInFlight.pop();
    m_queryPool.clear();

    destroySwapChain();

    clearSemaphores(m_presentSemaphores);
    clearSemaphores(m_acquireSemaphores);

    m_rhiDevice = nullptr;
    m_rendererString.clear();

    if(m_vulkanDevice){
        vkDestroyDevice(m_vulkanDevice, nullptr);
        m_vulkanDevice = VK_NULL_HANDLE;
    }

    if(m_windowSurface){
        NWB_ASSERT(m_vulkanInstance);
        vkDestroySurfaceKHR(m_vulkanInstance, m_windowSurface, nullptr);
        m_windowSurface = VK_NULL_HANDLE;
    }

    if(m_debugUtilsMessenger){
        if(vkDestroyDebugUtilsMessengerEXT)
            vkDestroyDebugUtilsMessengerEXT(m_vulkanInstance, m_debugUtilsMessenger, nullptr);
        m_debugUtilsMessenger = VK_NULL_HANDLE;
    }

    if(m_vulkanInstance){
        vkDestroyInstance(m_vulkanInstance, nullptr);
        m_vulkanInstance = VK_NULL_HANDLE;
    }

    Aftermath::Shutdown();
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Frame management


AcquiredBackBuffer BackendContext::beginFrame(const BackBufferResizeCallbacks& callbacks){
    VkResult res = VK_SUCCESS;
    VkSemaphore semaphore = VK_NULL_HANDLE;
    u32 acquiredIndex = Limit<u32>::s_Max;

    if(m_frameAcquired){
        NWB_LOGGER_ERROR(NWB_TEXT("Cannot begin a Vulkan frame while the previous acquired swap-chain image remains unresolved"));
        return {};
    }

    cancelFramePresentationSignal();
    m_swapChainIndex = Limit<u32>::s_Max;
    m_frameAbandonmentComplete = false;

    if(!m_swapChain || m_acquireSemaphores.empty()){
        NWB_LOGGER_WARNING(NWB_TEXT("Vulkan: beginFrame skipped because swap chain or acquire semaphores are not ready."));
        return {};
    }

    for(usize attempt = 0; attempt < s_MaxRetryCountAcquireNextImage; ++attempt){
        if(!m_swapChain || m_acquireSemaphores.empty()){
            NWB_LOGGER_WARNING(NWB_TEXT("Vulkan: beginFrame aborted because swap chain or acquire semaphores are unavailable."));
            return {};
        }

        if(m_acquireSemaphoreIndex >= m_acquireSemaphores.size())
            m_acquireSemaphoreIndex = 0;

        semaphore = m_acquireSemaphores[m_acquireSemaphoreIndex];

        res = vkAcquireNextImageKHR(
            m_vulkanDevice,
            m_swapChain,
            UINT64_MAX,
            semaphore,
            VK_NULL_HANDLE,
            &acquiredIndex
        );

        // Render acquired suboptimal images before recreating the swapchain.
        if(res == VK_ERROR_OUT_OF_DATE_KHR && attempt < s_MaxRetryCountAcquireNextImage - 1){
            if(callbacks.beforeResize)
                callbacks.beforeResize(callbacks.userData);

            VkSurfaceCapabilitiesKHR surfaceCaps;
            res = vkGetPhysicalDeviceSurfaceCapabilitiesKHR(m_vulkanPhysicalDevice, m_windowSurface, &surfaceCaps);
            if(res != VK_SUCCESS){
                NWB_LOGGER_WARNING(NWB_TEXT("Vulkan: Failed to query surface capabilities during resize. {}"), ResultToString(res));
                return {};
            }

            if(surfaceCaps.currentExtent.width != UINT32_MAX && surfaceCaps.currentExtent.height != UINT32_MAX){
                m_swapChainState.backBufferWidth = surfaceCaps.currentExtent.width;
                m_swapChainState.backBufferHeight = surfaceCaps.currentExtent.height;
            }
            else{
                m_swapChainState.backBufferWidth = Max(surfaceCaps.minImageExtent.width, Min(surfaceCaps.maxImageExtent.width, m_swapChainState.backBufferWidth));
                m_swapChainState.backBufferHeight = Max(surfaceCaps.minImageExtent.height, Min(surfaceCaps.maxImageExtent.height, m_swapChainState.backBufferHeight));
            }

            resizeSwapChain();
            if(callbacks.afterResize)
                callbacks.afterResize(callbacks.userData);
            acquiredIndex = Limit<u32>::s_Max;
        }
        else
            break;
    }

    if(m_acquireSemaphores.empty()){
        NWB_LOGGER_WARNING(NWB_TEXT("Vulkan: beginFrame aborted because acquire semaphore pool became empty."));
        return {};
    }

    if(res == VK_SUCCESS || res == VK_SUBOPTIMAL_KHR){
        if(acquiredIndex >= m_swapChainImages.size() || !m_swapChainImages[acquiredIndex].rhiHandle){
            NWB_LOGGER_ERROR(NWB_TEXT("Vulkan: Acquired swap-chain image index does not identify a live back buffer."));
            NWB_ASSERT_MSG(false, NWB_TEXT("Vulkan returned a successful swap-chain acquisition with an invalid image identity"));

            // A successful acquire has queued a WSI signal even when its returned identity is unusable. Drain that
            // exact binary semaphore before retiring the pools; device idle alone does not consume a WSI signal.
            m_frameAcquired = true;
            m_rhiDevice->queueWaitForSemaphore(CommandQueue::Graphics, semaphore, 0u);
            const GpuPhysicalQueueId primaryGraphicsQueue = m_rhiDevice->getPrimaryPhysicalQueue(CommandQueue::Graphics);
            QueueSubmissionToken drainToken;
            if(primaryGraphicsQueue.valid()){
                const QueueSubmissionDesc drainSubmitDesc;
                drainToken = m_rhiDevice->executeCommandLists(nullptr, 0u, primaryGraphicsQueue, drainSubmitDesc);
            }
            if(!drainToken.valid()){
                NWB_LOGGER_CRITICAL_WARNING(NWB_TEXT("Vulkan: Failed to submit the invalid acquired-image semaphore drain; forcing device teardown."));
                m_rhiDevice->captureGpuCrash("invalid acquired-image semaphore drain");
                return {};
            }
            if(!m_rhiDevice->waitForIdle()){
                NWB_LOGGER_CRITICAL_WARNING(NWB_TEXT("Vulkan: Failed to join the acquired-image semaphore drain; forcing device teardown."));
                m_rhiDevice->captureGpuCrash("invalid acquired-image semaphore drain idle");
                return {};
            }

            if(callbacks.beforeResize)
                callbacks.beforeResize(callbacks.userData);

            clearSemaphores(m_presentSemaphores);
            clearSemaphores(m_acquireSemaphores);
            resizeSwapChain();

            if(callbacks.afterResize)
                callbacks.afterResize(callbacks.userData);
            const usize requiredAcquireSemaphoreCount = Max<usize>(m_maxFramesInFlight, m_swapChainImages.size());
            const bool rebuildReady =
                m_swapChain
                && !m_swapChainImages.empty()
                && m_presentSemaphores.size() == m_swapChainImages.size()
                && m_acquireSemaphores.size() == requiredAcquireSemaphoreCount
                && !m_frameAcquired
                && !m_frameAbandonmentComplete
                && m_swapChainIndex == Limit<u32>::s_Max
                && m_framePresentationSignalState == FramePresentationSignalState::Idle
            ;
            if(!rebuildReady){
                NWB_LOGGER_CRITICAL_WARNING(NWB_TEXT("Vulkan: Invalid acquired-image recovery left presentation incomplete; forcing device teardown."));
                m_rhiDevice->captureGpuCrash("invalid acquired-image recovery rebuild");
            }
            return {};
        }

        m_acquireSemaphoreIndex = (m_acquireSemaphoreIndex + 1) % static_cast<uint32_t>(m_acquireSemaphores.size());
        m_rhiDevice->queueWaitForSemaphore(CommandQueue::Graphics, semaphore, 0);
        const SwapChainImage& swapChainImage = m_swapChainImages[acquiredIndex];
        AcquiredBackBuffer backBuffer;
        backBuffer.texture = swapChainImage.rhiHandle;
        backBuffer.nativeInitialState = swapChainImage.presentationState.nativeInitialState();
        backBuffer.index = acquiredIndex;
        NWB_ASSERT(backBuffer.valid());

        m_swapChainIndex = acquiredIndex;
        m_frameAcquired = true;
        m_frameAbandonmentComplete = false;
        return backBuffer;
    }

    if(res == VK_ERROR_DEVICE_LOST && m_rhiDevice)
        m_rhiDevice->captureGpuCrash("acquire next image");
    NWB_LOGGER_WARNING(NWB_TEXT("Vulkan: Failed to acquire next swap chain image. {}"), ResultToString(res));
    return {};
}

bool BackendContext::abandonAcquiredFrame()noexcept{
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
    if(m_rhiDevice->isDeviceLost()){
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
    ;

    // Invalidate presentation identity before touching signal state. Keeping m_frameAcquired set quarantines the
    // WSI image even if retirement or draining fails, so no later beginFrame, claim, or present can reuse it.
    m_swapChainIndex = Limit<u32>::s_Max;
    if(!abandonmentReady){
        NWB_LOGGER_CRITICAL_WARNING(NWB_TEXT("Vulkan: Acquired-frame abandonment preconditions failed; forcing device teardown."));
        m_rhiDevice->captureGpuCrash("acquired-frame abandonment precondition");
        return false;
    }

    if(presentationSignalNeedsIdle && !m_rhiDevice->waitForIdle()){
        NWB_LOGGER_CRITICAL_WARNING(NWB_TEXT("Vulkan: Failed to join the abandoned presentation signal; forcing device teardown."));
        m_rhiDevice->captureGpuCrash("acquired-frame abandonment presentation idle");
        return false;
    }
    if(presentationSignalNeedsIdle && !replaceFramePresentationSemaphoreAfterIdle()){
        NWB_LOGGER_CRITICAL_WARNING(NWB_TEXT("Vulkan: Failed to quarantine the abandoned presentation signal; forcing device teardown."));
        m_rhiDevice->captureGpuCrash("acquired-frame abandonment presentation replacement");
        return false;
    }
    resetFramePresentationSignal();

    const GpuPhysicalQueueId primaryGraphicsQueue = m_rhiDevice->getPrimaryPhysicalQueue(CommandQueue::Graphics);
    QueueSubmissionToken drainToken;
    if(primaryGraphicsQueue.valid()){
        QueueSubmissionDesc drainSubmitDesc;
        drainSubmitDesc.forceNativeSubmission = true;
        drainToken = m_rhiDevice->executeCommandLists(nullptr, 0u, primaryGraphicsQueue, drainSubmitDesc);
    }
    if(!drainToken.valid()){
        NWB_LOGGER_CRITICAL_WARNING(NWB_TEXT("Vulkan: Failed to submit the acquired-frame wait drain; forcing device teardown."));
        m_rhiDevice->captureGpuCrash("acquired-frame abandonment drain");
        return false;
    }
    if(!m_rhiDevice->waitForIdle()){
        NWB_LOGGER_CRITICAL_WARNING(NWB_TEXT("Vulkan: Failed to join the acquired-frame wait drain; forcing device teardown."));
        m_rhiDevice->captureGpuCrash("acquired-frame abandonment drain idle");
        return false;
    }

    m_frameAbandonmentComplete = true;
    return true;
}

bool BackendContext::present(){
    VkResult res = VK_SUCCESS;

    if(!m_rhiDevice || !m_frameAcquired || !m_swapChain || m_presentSemaphores.empty() || m_swapChainImages.empty()){
        NWB_LOGGER_WARNING(NWB_TEXT("Vulkan: present skipped because its device, acquired frame, or swap-chain resources are not ready."));
        cancelFramePresentationSignal();
        return false;
    }

    if(m_swapChainIndex >= m_presentSemaphores.size() || m_swapChainIndex >= m_swapChainImages.size()){
        cancelFramePresentationSignal();
        NWB_LOGGER_ERROR(NWB_TEXT("Cannot present Vulkan swap-chain image because its acquired index is invalid"));
        return false;
    }

    const VkSemaphore& semaphore = m_presentSemaphores[m_swapChainIndex];

    bool frameSignalAccepted =
        m_framePresentationSignalState == FramePresentationSignalState::Accepted
        && m_framePresentationSwapChainIndex == m_swapChainIndex
        && m_framePresentationSemaphore == semaphore
    ;

    if(!frameSignalAccepted){
        cancelFramePresentationSignal();

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
            cancelFramePresentationSignal();
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
            cancelFramePresentationSignal();
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
            cancelFramePresentationSignal();
            NWB_LOGGER_ERROR(NWB_TEXT("Vulkan: Compatibility presentation transition/signal submission was rejected."));
            return false;
        }
        if(!confirmFramePresentationSignal(fallbackToken)){
            cancelFramePresentationSignal();
            NWB_LOGGER_ERROR(NWB_TEXT("Vulkan: Accepted compatibility presentation submission failed signal confirmation/tracking."));
            return false;
        }

        frameSignalAccepted = true;
    }

    VkPresentInfoKHR presentInfo = {};
    presentInfo.sType = VK_STRUCTURE_TYPE_PRESENT_INFO_KHR;
    presentInfo.waitSemaphoreCount = 1;
    presentInfo.pWaitSemaphores = &semaphore;
    presentInfo.swapchainCount = 1;
    presentInfo.pSwapchains = &m_swapChain;
    presentInfo.pImageIndices = &m_swapChainIndex;

    res = vkQueuePresentKHR(m_presentQueue, &presentInfo);
    const VulkanDetail::QueuePresentWaitDisposition::Enum presentWaitDisposition =
        VulkanDetail::ClassifyQueuePresentWaitDisposition(res);
    m_swapChainImages[m_swapChainIndex].presentationState.observeQueuePresentWaitDisposition(presentWaitDisposition);
    if(presentWaitDisposition != VulkanDetail::QueuePresentWaitDisposition::Consumed){
        // Host/device OOM and unknown failures do not prove the accepted binary signal was consumed. Preserve its
        // tracking until successful idle-and-replacement quarantine or terminal teardown.
        if(presentWaitDisposition == VulkanDetail::QueuePresentWaitDisposition::DeviceLost){
            if(m_rhiDevice)
                m_rhiDevice->captureGpuCrash("present");
            NWB_LOGGER_WARNING(NWB_TEXT("Vulkan: Queue present failed. {}"), ResultToString(res));
            return false;
        }
        if(!frameSignalAccepted || !m_rhiDevice){
            NWB_LOGGER_CRITICAL_WARNING(
                NWB_TEXT("Vulkan: Queue present lost its accepted signal tracking; forcing device teardown.")
            );
            if(m_rhiDevice)
                m_rhiDevice->captureGpuCrash("present semaphore tracking");
            return false;
        }
        if(!m_rhiDevice->waitForIdle()){
            NWB_LOGGER_CRITICAL_WARNING(
                NWB_TEXT("Vulkan: Failed to join the unconsumed presentation signal; forcing device teardown.")
            );
            m_rhiDevice->captureGpuCrash("present semaphore idle");
            return false;
        }
        if(!replaceFramePresentationSemaphoreAfterIdle()){
            m_rhiDevice->captureGpuCrash("present semaphore replacement");
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
    if(!(res == VK_SUCCESS || res == VK_ERROR_OUT_OF_DATE_KHR || res == VK_SUBOPTIMAL_KHR)){
        NWB_LOGGER_WARNING(NWB_TEXT("Vulkan: Queue present consumed synchronization but requires recreation. {}")
            , ResultToString(res)
        );
        return false;
    }

    while(m_framesInFlight.size() >= m_maxFramesInFlight){
        auto query = m_framesInFlight.front();
        m_framesInFlight.pop();
        m_rhiDevice->waitEventQuery(query.get());
        m_queryPool.push_back(query);
    }

    EventQueryHandle query;
    if(!m_queryPool.empty()){
        query = m_queryPool.back();
        m_queryPool.pop_back();
    }
    else{
        NWB_ASSERT_MSG(false, NWB_TEXT("Vulkan: frame synchronization query pool was exhausted"));
        NWB_LOGGER_WARNING(NWB_TEXT("Vulkan: frame synchronization query pool was exhausted; continuing without frame fence throttling."));
        return true;
    }

    if(!query){
        NWB_LOGGER_WARNING(NWB_TEXT("Vulkan: Failed to acquire frame synchronization query; continuing without frame fence throttling."));
        return true;
    }

    m_rhiDevice->setEventQuery(query.get(), CommandQueue::Graphics);
    m_framesInFlight.push(query);

    return true;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_VULKAN_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

