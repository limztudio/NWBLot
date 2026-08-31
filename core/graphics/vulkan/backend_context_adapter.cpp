// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "backend_context.h"
#include "backend_context_detail.h"


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_VULKAN_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


bool BackendContext::findQueueFamilies(VkPhysicalDevice physicalDevice){
    uint32_t queueFamilyCount = 0;
    m_instanceDispatch.vkGetPhysicalDeviceQueueFamilyProperties(physicalDevice, &queueFamilyCount, nullptr);

    Alloc::ScratchArena scratchArena(VulkanArenaScope::s_QueueFamilyQueryArena);

    Vector<VkQueueFamilyProperties, Alloc::ScratchArena> props(queueFamilyCount, scratchArena);
    m_instanceDispatch.vkGetPhysicalDeviceQueueFamilyProperties(physicalDevice, &queueFamilyCount, props.data());

    m_graphicsQueueFamily = s_InvalidQueueFamilyIndex;
    m_secondaryGraphicsQueueFamily = s_InvalidQueueFamilyIndex;
    m_computeQueueFamily = s_InvalidQueueFamilyIndex;
    m_secondaryComputeQueueFamily = s_InvalidQueueFamilyIndex;
    m_transferQueueFamily = s_InvalidQueueFamilyIndex;
    m_secondaryTransferQueueFamily = s_InvalidQueueFamilyIndex;
    m_presentQueueFamily = s_InvalidQueueFamilyIndex;

    const bool requirePresentQueue = !m_deviceParams.headlessDevice;
    // Continue scanning for a dedicated async-compute family.
    const bool searchAsyncComputeQueue = m_deviceParams.enableAsyncComputeLane;
    // Transfer fallback is resolved by the task graph. Only expose an independently useful transfer-only family.
    const bool searchDedicatedTransferQueue = m_deviceParams.enableTransferQueue;

    for(i32 i = 0; i < static_cast<i32>(props.size()); ++i){
        const auto& queueFamily = props[i];

        if(m_graphicsQueueFamily == s_InvalidQueueFamilyIndex){
            if(
                queueFamily.queueCount > 0
                && (queueFamily.queueFlags & VK_QUEUE_GRAPHICS_BIT)
                && (queueFamily.queueFlags & VK_QUEUE_COMPUTE_BIT)
            )
                m_graphicsQueueFamily = i;
        }

        if(m_computeQueueFamily == s_InvalidQueueFamilyIndex){
            if(
                queueFamily.queueCount > 0
                && (queueFamily.queueFlags & VK_QUEUE_COMPUTE_BIT)
                && !(queueFamily.queueFlags & VK_QUEUE_GRAPHICS_BIT)
            )
                m_computeQueueFamily = i;
        }

        if(m_transferQueueFamily == s_InvalidQueueFamilyIndex){
            if(
                queueFamily.queueCount > 0
                && (queueFamily.queueFlags & VK_QUEUE_TRANSFER_BIT)
                && !(queueFamily.queueFlags & VK_QUEUE_GRAPHICS_BIT)
                && !(queueFamily.queueFlags & VK_QUEUE_COMPUTE_BIT)
            )
                m_transferQueueFamily = i;
        }

#ifdef NWB_PLATFORM_WINDOWS
        if(requirePresentQueue && m_presentQueueFamily == s_InvalidQueueFamilyIndex){
            if(queueFamily.queueCount > 0){
                VkBool32 supported = m_instanceDispatch.vkGetPhysicalDeviceWin32PresentationSupportKHR(physicalDevice, i);
                if(supported)
                    m_presentQueueFamily = i;
            }
        }
#elif defined(NWB_PLATFORM_LINUX)
        VkResult res = VK_SUCCESS;
        if(requirePresentQueue && m_presentQueueFamily == s_InvalidQueueFamilyIndex && m_windowSurface){
            if(queueFamily.queueCount > 0){
                VkBool32 supported = VK_FALSE;
                res = m_instanceDispatch.vkGetPhysicalDeviceSurfaceSupportKHR(physicalDevice, i, m_windowSurface, &supported);
                if(res == VK_SUCCESS && supported)
                    m_presentQueueFamily = i;
            }
        }
#endif

        if(
            m_graphicsQueueFamily != s_InvalidQueueFamilyIndex
            && (!requirePresentQueue || m_presentQueueFamily != s_InvalidQueueFamilyIndex)
            && (!searchAsyncComputeQueue || m_computeQueueFamily != s_InvalidQueueFamilyIndex)
            && (!searchDedicatedTransferQueue || m_transferQueueFamily != s_InvalidQueueFamilyIndex)
        )
            break;
    }

    if(
        m_graphicsQueueFamily == s_InvalidQueueFamilyIndex
        || (m_presentQueueFamily == s_InvalidQueueFamilyIndex && requirePresentQueue)
    )
        return false;

    // Every active primary family may expose its extra queues through the low-overhead same-class route. The
    // cross-family opt-in may also register one deterministic alternate family for every already-enabled class;
    // task-level scheduling still has to opt in before compiler routing can cross its ownership boundary.
    if(
        m_deviceParams.enableSameClassMultiQueue
        && m_deviceParams.enableCrossFamilySameClassQueueRouting
    ){
        for(i32 i = 0; i < static_cast<i32>(props.size()); ++i){
            const VkQueueFamilyProperties& queueFamily = props[i];
            if(
                m_secondaryGraphicsQueueFamily == s_InvalidQueueFamilyIndex
                && i != m_graphicsQueueFamily
                && queueFamily.queueCount > 0u
                && (queueFamily.queueFlags & VK_QUEUE_GRAPHICS_BIT)
            ){
                m_secondaryGraphicsQueueFamily = i;
            }
            if(
                m_secondaryComputeQueueFamily == s_InvalidQueueFamilyIndex
                && m_computeQueueFamily != s_InvalidQueueFamilyIndex
                && i != m_computeQueueFamily
                && queueFamily.queueCount > 0u
                && (queueFamily.queueFlags & VK_QUEUE_COMPUTE_BIT)
                && !(queueFamily.queueFlags & VK_QUEUE_GRAPHICS_BIT)
            )
                m_secondaryComputeQueueFamily = i;
            if(
                m_secondaryTransferQueueFamily == s_InvalidQueueFamilyIndex
                && m_transferQueueFamily != s_InvalidQueueFamilyIndex
                && i != m_transferQueueFamily
                && queueFamily.queueCount > 0u
                && (queueFamily.queueFlags & VK_QUEUE_TRANSFER_BIT)
                && !(queueFamily.queueFlags & VK_QUEUE_GRAPHICS_BIT)
                && !(queueFamily.queueFlags & VK_QUEUE_COMPUTE_BIT)
            )
                m_secondaryTransferQueueFamily = i;
        }
    }

    return true;
}

bool BackendContext::pickPhysicalDevice(){
    const Format::Enum requestedSdrFormat = VulkanDetail::GetBackBufferFormat(m_deviceParams);
    if(!m_deviceParams.headlessDevice && VulkanDetail::ConvertFormat(requestedSdrFormat) == VK_FORMAT_UNDEFINED){
        NWB_LOGGER_ERROR(NWB_TEXT("Vulkan: Requested SDR swapchain format is unsupported"));
        return false;
    }
    const bool hdr10Allowed =
        m_deviceParams.enableHDR10Output
        && m_hdr10ColorSpaceExtensionEnabled
    ;
    VkExtent2D requestedExtent = { m_swapChainState.backBufferWidth, m_swapChainState.backBufferHeight };

    VkResult res = VK_SUCCESS;

    uint32_t deviceCount = 0;
    res = m_instanceDispatch.vkEnumeratePhysicalDevices(m_vulkanInstance, &deviceCount, nullptr);
    if(res != VK_SUCCESS){
        NWB_LOGGER_ERROR(NWB_TEXT("Vulkan: Failed to enumerate physical device count. {}"), ResultToString(res));
        return false;
    }

    Alloc::ScratchArena scratchArena(VulkanArenaScope::s_PhysicalDeviceSelectArena, s_DeviceSetupScratchArenaBytes);

    Vector<VkPhysicalDevice, Alloc::ScratchArena> devices(deviceCount, scratchArena);
    res = m_instanceDispatch.vkEnumeratePhysicalDevices(m_vulkanInstance, &deviceCount, devices.data());
    if(res != VK_SUCCESS){
        NWB_LOGGER_ERROR(NWB_TEXT("Vulkan: Failed to enumerate physical devices. {}"), ResultToString(res));
        return false;
    }

    i32 adapterIndex = m_deviceParams.adapterIndex;
    i32 firstDevice = 0;
    i32 lastDevice = static_cast<i32>(devices.size()) - 1;
    if(adapterIndex >= 0){
        if(adapterIndex > lastDevice){
            NWB_LOGGER_ERROR(NWB_TEXT("Vulkan: The specified physical device {} does not exist."), adapterIndex);
            return false;
        }
        firstDevice = adapterIndex;
        lastDevice = adapterIndex;
    }

    auto errorStream = VulkanDetail::MakeScratchStringStream(scratchArena);
    errorStream << "Cannot find a Vulkan device that supports all the required extensions and properties.";

    struct DeviceSelection{
        VkPhysicalDevice device = VK_NULL_HANDLE;
        i32 graphicsQueueFamily = s_InvalidQueueFamilyIndex;
        i32 secondaryGraphicsQueueFamily = s_InvalidQueueFamilyIndex;
        i32 computeQueueFamily = s_InvalidQueueFamilyIndex;
        i32 secondaryComputeQueueFamily = s_InvalidQueueFamilyIndex;
        i32 transferQueueFamily = s_InvalidQueueFamilyIndex;
        i32 secondaryTransferQueueFamily = s_InvalidQueueFamilyIndex;
        i32 presentQueueFamily = s_InvalidQueueFamilyIndex;
    };
    auto captureCurrentSelection = [this](VkPhysicalDevice device){
        DeviceSelection selection;
        selection.device = device;
        selection.graphicsQueueFamily = m_graphicsQueueFamily;
        selection.secondaryGraphicsQueueFamily = m_secondaryGraphicsQueueFamily;
        selection.computeQueueFamily = m_computeQueueFamily;
        selection.secondaryComputeQueueFamily = m_secondaryComputeQueueFamily;
        selection.transferQueueFamily = m_transferQueueFamily;
        selection.secondaryTransferQueueFamily = m_secondaryTransferQueueFamily;
        selection.presentQueueFamily = m_presentQueueFamily;
        return selection;
    };
    auto applySelection = [this](const DeviceSelection& selection){
        m_vulkanPhysicalDevice = selection.device;
        m_graphicsQueueFamily = selection.graphicsQueueFamily;
        m_secondaryGraphicsQueueFamily = selection.secondaryGraphicsQueueFamily;
        m_computeQueueFamily = selection.computeQueueFamily;
        m_secondaryComputeQueueFamily = selection.secondaryComputeQueueFamily;
        m_transferQueueFamily = selection.transferQueueFamily;
        m_secondaryTransferQueueFamily = selection.secondaryTransferQueueFamily;
        m_presentQueueFamily = selection.presentQueueFamily;
    };
    DeviceSelection fallbackSelection;
    bool skippedWindowedCpuDevice = false;
    bool sawWindowedGpuCandidate = false;

    for(i32 deviceIndex = firstDevice; deviceIndex <= lastDevice; ++deviceIndex){
        VkPhysicalDevice dev = devices[deviceIndex];
        VkPhysicalDeviceProperties prop;
        m_instanceDispatch.vkGetPhysicalDeviceProperties(dev, &prop);

        errorStream << "\n" << prop.deviceName << ":";
        if(!m_deviceParams.headlessDevice && prop.deviceType == VK_PHYSICAL_DEVICE_TYPE_CPU){
            errorStream << "\n  - CPU Vulkan devices are not supported for windowed rendering";
            skippedWindowedCpuDevice = true;
            continue;
        }
        if(!m_deviceParams.headlessDevice)
            sawWindowedGpuCandidate = true;

        VulkanDetail::ScratchStringSet requiredExtensions(0, Hasher<VulkanDetail::ScratchString>(), EqualTo<VulkanDetail::ScratchString>(), scratchArena);
        requiredExtensions.reserve(m_enabledExtensions.device.size());
        for(const auto& [name, _] : m_enabledExtensions.device)
            requiredExtensions.insert(VulkanDetail::MakeScratchString(scratchArena, AStringView(name)));
        uint32_t extCount = 0;
        res = m_instanceDispatch.vkEnumerateDeviceExtensionProperties(dev, nullptr, &extCount, nullptr);
        if(res != VK_SUCCESS){
            errorStream << "\n  - failed to enumerate device extension count";
            continue;
        }
        Vector<VkExtensionProperties, Alloc::ScratchArena> deviceExtensions(extCount, scratchArena);
        res = m_instanceDispatch.vkEnumerateDeviceExtensionProperties(dev, nullptr, &extCount, deviceExtensions.data());
        if(res != VK_SUCCESS){
            errorStream << "\n  - failed to enumerate device extensions";
            continue;
        }
        for(const auto& ext : deviceExtensions)
            requiredExtensions.erase(VulkanDetail::MakeScratchString(scratchArena, ext.extensionName));

        bool deviceIsGood = true;

        if(!requiredExtensions.empty()){
            for(const auto& ext : requiredExtensions)
                errorStream << "\n  - missing " << ext;
            deviceIsGood = false;
        }

        VkPhysicalDeviceFeatures deviceFeatures;
        m_instanceDispatch.vkGetPhysicalDeviceFeatures(dev, &deviceFeatures);
        if(!deviceFeatures.samplerAnisotropy){
            errorStream << "\n  - does not support samplerAnisotropy";
            deviceIsGood = false;
        }
        if(!findQueueFamilies(dev)){
            errorStream << "\n  - does not support the necessary queue types";
            deviceIsGood = false;
        }

        if(deviceIsGood && m_windowSurface){
            VkBool32 surfaceSupported = VK_FALSE;
            res = m_instanceDispatch.vkGetPhysicalDeviceSurfaceSupportKHR(dev, m_presentQueueFamily, m_windowSurface, &surfaceSupported);
            if(res != VK_SUCCESS){
                errorStream << "\n  - failed to query surface support";
                deviceIsGood = false;
            }
            else if(!surfaceSupported){
                errorStream << "\n  - does not support the window surface";
                deviceIsGood = false;
            }
            else{
                VkSurfaceCapabilitiesKHR surfaceCaps;
                res = m_instanceDispatch.vkGetPhysicalDeviceSurfaceCapabilitiesKHR(dev, m_windowSurface, &surfaceCaps);
                if(res != VK_SUCCESS){
                    errorStream << "\n  - failed to query surface capabilities";
                    continue;
                }

                uint32_t fmtCount = 0;
                res = m_instanceDispatch.vkGetPhysicalDeviceSurfaceFormatsKHR(dev, m_windowSurface, &fmtCount, nullptr);
                if(res != VK_SUCCESS){
                    errorStream << "\n  - failed to query surface format count";
                    continue;
                }
                Vector<VkSurfaceFormatKHR, Alloc::ScratchArena> surfaceFmts(fmtCount, scratchArena);
                res = m_instanceDispatch.vkGetPhysicalDeviceSurfaceFormatsKHR(dev, m_windowSurface, &fmtCount, surfaceFmts.data());
                if(res != VK_SUCCESS){
                    errorStream << "\n  - failed to query surface formats";
                    continue;
                }

                if(
                    surfaceCaps.minImageCount > m_deviceParams.swapChainBufferCount
                    || (surfaceCaps.maxImageCount < m_deviceParams.swapChainBufferCount && surfaceCaps.maxImageCount > 0)
                ){
                    errorStream << "\n  - cannot support the requested swap chain image count";
                    deviceIsGood = false;
                }

                if(
                    surfaceCaps.minImageExtent.width > requestedExtent.width
                    || surfaceCaps.minImageExtent.height > requestedExtent.height
                    || surfaceCaps.maxImageExtent.width < requestedExtent.width
                    || surfaceCaps.maxImageExtent.height < requestedExtent.height
                ){
                    errorStream << "\n  - cannot support the requested swap chain size";
                    deviceIsGood = false;
                }

                VulkanDetail::SwapChainSurfaceFormatSelection surfaceFormatSelection;
                if(!VulkanDetail::SelectSurfaceFormat(
                    surfaceFmts.data(),
                    static_cast<u32>(surfaceFmts.size()),
                    requestedSdrFormat,
                    hdr10Allowed,
                    surfaceFormatSelection
                )){
                    errorStream << "\n  - does not support HDR10 or the requested SDR swap chain format";
                    deviceIsGood = false;
                }
            }
        }

        if(!deviceIsGood)
            continue;

        if(prop.deviceType == VK_PHYSICAL_DEVICE_TYPE_DISCRETE_GPU){
            applySelection(captureCurrentSelection(dev));
            return true;
        }

        if(fallbackSelection.device == VK_NULL_HANDLE)
            fallbackSelection = captureCurrentSelection(dev);
    }

    if(fallbackSelection.device != VK_NULL_HANDLE){
        applySelection(fallbackSelection);
        return true;
    }

    if(skippedWindowedCpuDevice && !sawWindowedGpuCandidate){
        NWB_LOGGER_CRITICAL_WARNING(NWB_TEXT("Vulkan: {}"), StringConvert(errorStream.str()));
        return false;
    }

    NWB_LOGGER_ERROR(NWB_TEXT("Vulkan: {}"), StringConvert(errorStream.str()));
    return false;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Adapter enumeration


bool BackendContext::enumerateAdapters(GraphicsVector<AdapterInfo>& outAdapters){
    VkResult res = VK_SUCCESS;

    if(!m_vulkanInstance){
        NWB_LOGGER_ERROR(NWB_TEXT("Vulkan: Failed to enumerate adapters: instance is null"));
        return false;
    }

    uint32_t deviceCount = 0;
    res = m_instanceDispatch.vkEnumeratePhysicalDevices(m_vulkanInstance, &deviceCount, nullptr);
    if(res != VK_SUCCESS){
        NWB_LOGGER_ERROR(NWB_TEXT("Vulkan: Failed to enumerate adapter count. {}"), ResultToString(res));
        return false;
    }

    if(deviceCount == 0){
        outAdapters.clear();
        return true;
    }

    Alloc::ScratchArena scratchArena(VulkanArenaScope::s_AdapterEnumerateArena);

    Vector<VkPhysicalDevice, Alloc::ScratchArena> devices(deviceCount, scratchArena);
    res = m_instanceDispatch.vkEnumeratePhysicalDevices(m_vulkanInstance, &deviceCount, devices.data());
    if(res != VK_SUCCESS){
        NWB_LOGGER_ERROR(NWB_TEXT("Vulkan: Failed to enumerate adapters. {}"), ResultToString(res));
        return false;
    }

    outAdapters.clear();
    outAdapters.reserve(deviceCount);
    for(usize i = 0; i < static_cast<usize>(deviceCount); ++i)
        outAdapters.emplace_back(m_arena);

    auto fillAdapterInfo = [&](usize i){
        AdapterInfo adapterInfo(m_arena);
        VulkanDetail::PopulateAdapterInfo(m_instanceDispatch, devices[i], adapterInfo);
        outAdapters[i] = Move(adapterInfo);
    };

    if(m_threadPool.isParallelEnabled() && deviceCount >= s_ParallelAdapterThreshold)
        m_threadPool.parallelFor(static_cast<usize>(0), static_cast<usize>(deviceCount), fillAdapterInfo);
    else{
        for(usize i = 0; i < static_cast<usize>(deviceCount); ++i)
            fillAdapterInfo(i);
    }

    return true;
}

bool BackendContext::getSelectedAdapterInfo(AdapterInfo& outAdapter)const{
    if(!m_rhiDevice || !m_vulkanPhysicalDevice)
        return false;

    VulkanDetail::PopulateAdapterInfo(m_instanceDispatch, m_vulkanPhysicalDevice, outAdapter);
    return true;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_VULKAN_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

