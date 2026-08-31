// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "backend_context.h"
#include "backend_context_detail.h"
#include "device_detail.h"


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_VULKAN_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


u32 BackendContext::findNativeQueueIndex(const u32 familyIndex, const u32 queueIndex)const noexcept{
    for(usize nativeQueueIndex = 0u; nativeQueueIndex < m_nativeQueues.size(); ++nativeQueueIndex){
        const VulkanNativeQueueDesc& nativeQueue = m_nativeQueues[nativeQueueIndex];
        if(nativeQueue.familyIndex == familyIndex && nativeQueue.queueIndex == queueIndex)
            return static_cast<u32>(nativeQueueIndex);
    }
    return Limit<u32>::s_Max;
}


bool BackendContext::createVulkanDevice(){
    VkResult res = VK_SUCCESS;

    Alloc::ScratchArena scratchArena(VulkanArenaScope::s_DeviceCreateArena, s_DeviceSetupScratchArenaBytes);
    m_bufferDeviceAddressSupported = false;
    m_textureCompressionBcFeatureEnabled = false;
    m_textureCompressionAstcLdrFeatureEnabled = false;
    m_textureCompressionAstcHdrFeatureEnabled = false;
    m_dynamicRenderingSupported = false;
    m_synchronization2Supported = false;
    m_independentBlendFeatureEnabled = false;
    m_fullDrawIndexUint32FeatureEnabled = false;
    m_multiDrawIndirectFeatureEnabled = false;
    m_drawIndirectFirstInstanceFeatureEnabled = false;
    m_meshShaderFeatureEnabled = false;
    m_accelerationStructureFeatureEnabled = false;
    m_rayTracingPipelineFeatureEnabled = false;
    m_rayQueryFeatureEnabled = false;
    m_opacityMicromapFeatureEnabled = false;
    m_clusterAccelerationStructureFeatureEnabled = false;
    m_rayTracingInvocationReorderFeatureEnabled = false;
    m_rayTracingInvocationReorderExtFeatureEnabled = false;
    m_cooperativeVectorFeatureEnabled = false;
    m_cooperativeVectorTrainingFeatureEnabled = false;
    m_meshTaskShaderSupported = false;
    m_rayTracingSpheresSupported = false;
    m_rayTracingLinearSweptSpheresSupported = false;

    uint32_t extCount = 0;
    res = vkEnumerateDeviceExtensionProperties(m_vulkanPhysicalDevice, nullptr, &extCount, nullptr);
    if(res != VK_SUCCESS){
        NWB_LOGGER_ERROR(NWB_TEXT("Vulkan: Failed to enumerate device extension count. {}"), ResultToString(res));
        return false;
    }
    Vector<VkExtensionProperties, Alloc::ScratchArena> deviceExtensions(extCount, scratchArena);
    res = vkEnumerateDeviceExtensionProperties(m_vulkanPhysicalDevice, nullptr, &extCount, deviceExtensions.data());
    if(res != VK_SUCCESS){
        NWB_LOGGER_ERROR(NWB_TEXT("Vulkan: Failed to enumerate device extensions. {}"), ResultToString(res));
        return false;
    }

    const bool swapchainEnabled = isDeviceExtensionEnabled(VK_KHR_SWAPCHAIN_EXTENSION_NAME);
    for(const auto& ext : deviceExtensions){
        GraphicsString name(ext.extensionName, m_arena);
        bool enableExtension = false;
        DeviceExtensionFeature::Enum enabledFeature = DeviceExtensionFeature::None;

        auto optIt = m_optionalExtensions.device.find(name);
        if(optIt != m_optionalExtensions.device.end()){
            if(
                !swapchainEnabled
                && (
                    name == VK_KHR_SWAPCHAIN_MUTABLE_FORMAT_EXTENSION_NAME
                    || name == VK_EXT_HDR_METADATA_EXTENSION_NAME
                )
            )
                continue;
            enableExtension = true;
            enabledFeature = optIt.value();
        }

        if(!enableExtension && m_deviceParams.enableRayTracingExtensions){
            auto rtIt = m_rayTracingExtensions.find(name);
            if(rtIt != m_rayTracingExtensions.end()){
                enableExtension = true;
                enabledFeature = rtIt.value();
            }
        }

        if(enableExtension){
            auto [it, inserted] = m_enabledExtensions.device.emplace(Move(name), enabledFeature);
            if(!inserted && it.value() == DeviceExtensionFeature::None && enabledFeature != DeviceExtensionFeature::None)
                it.value() = enabledFeature;
        }
    }

    VkPhysicalDeviceProperties physicalDeviceProperties;
    vkGetPhysicalDeviceProperties(m_vulkanPhysicalDevice, &physicalDeviceProperties);

#ifdef NWB_UNICODE
    {
        const char* deviceName = physicalDeviceProperties.deviceName;
        const usize len = NWB_STRNLEN(deviceName, VK_MAX_PHYSICAL_DEVICE_NAME_SIZE);
        m_rendererString = StringConvert(m_arena, AStringView(deviceName, len));
    }
#else
    m_rendererString = physicalDeviceProperties.deviceName;
#endif

    const bool apiSupportsVulkan13 = physicalDeviceProperties.apiVersion >= VK_API_VERSION_1_3;
    const bool coopVecExtensionEnabled = isDeviceExtensionEnabled(VK_NV_COOPERATIVE_VECTOR_EXTENSION_NAME);
    const bool dynamicRenderingExtensionEnabled = isDeviceExtensionEnabled(VK_KHR_DYNAMIC_RENDERING_EXTENSION_NAME);
    const bool synchronization2ExtensionEnabled = isDeviceExtensionEnabled(VK_KHR_SYNCHRONIZATION_2_EXTENSION_NAME);
    const bool maintenance4ExtensionEnabled = isDeviceExtensionEnabled(VK_KHR_MAINTENANCE_4_EXTENSION_NAME);

    const bool diagnosticsConfigExtensionEnabled = isDeviceExtensionEnabled(VK_NV_DEVICE_DIAGNOSTICS_CONFIG_EXTENSION_NAME);
    const bool aftermathRequested = m_deviceParams.enableGpuCrashDiagnostics && physicalDeviceProperties.vendorID == s_NvidiaVendorId;
    // Configure process-global NVIDIA Aftermath before device creation.
    const bool aftermathActive = aftermathRequested && Aftermath::Initialize();
    if(aftermathRequested && !aftermathActive)
        NWB_LOGGER_INFO(NWB_TEXT("Vulkan: NVIDIA Aftermath GPU crash dumps unavailable; using vendor-neutral GPU diagnostics only."));

    m_swapChainMutableFormatSupported = isDeviceExtensionEnabled(VK_KHR_SWAPCHAIN_MUTABLE_FORMAT_EXTENSION_NAME);

    constexpr usize kOptionalDeviceFeatureCount = static_cast<usize>(DeviceExtensionFeature::Count);

    void* pNext = nullptr;

    auto physicalDeviceFeatures2 = VulkanDetail::MakeVkStruct<VkPhysicalDeviceFeatures2>(VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FEATURES_2);

    VulkanDetail::OptionalDeviceFeatureSet requestedOptionalFeatures = VulkanDetail::MakeRequestedOptionalDeviceFeatures();
    if(!m_deviceParams.enableNativeMeshShaders)
        requestedOptionalFeatures.meshShader.meshShader = VK_FALSE;
    VulkanDetail::OptionalDeviceFeatureSet supportedOptionalFeatures;

    VkPhysicalDeviceVulkan11Features supportedVulkan11Features = VulkanDetail::MakeVkFeatureStruct<VkPhysicalDeviceVulkan11Features>(VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_1_FEATURES);
    VulkanDetail::AppendFeatureStruct(pNext, &supportedVulkan11Features);

    VkPhysicalDeviceVulkan12Features supportedVulkan12Features = VulkanDetail::MakeVkFeatureStruct<VkPhysicalDeviceVulkan12Features>(VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_2_FEATURES);
    VulkanDetail::AppendFeatureStruct(pNext, &supportedVulkan12Features);

    VkPhysicalDeviceVulkan13Features supportedVulkan13Features = VulkanDetail::MakeVkFeatureStruct<
        VkPhysicalDeviceVulkan13Features
    >(
        VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_3_FEATURES
    );
    if(apiSupportsVulkan13)
        VulkanDetail::AppendFeatureStruct(pNext, &supportedVulkan13Features);

    VkPhysicalDeviceSynchronization2Features synchronization2Features = VulkanDetail::MakeVkFeatureStruct<VkPhysicalDeviceSynchronization2Features>(VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_SYNCHRONIZATION_2_FEATURES);
    if(!apiSupportsVulkan13 && synchronization2ExtensionEnabled)
        VulkanDetail::AppendFeatureStruct(pNext, &synchronization2Features);

    VkPhysicalDeviceMaintenance4Features maintenance4Features = VulkanDetail::MakeVkFeatureStruct<VkPhysicalDeviceMaintenance4Features>(VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_MAINTENANCE_4_FEATURES);
    if(!apiSupportsVulkan13 && maintenance4ExtensionEnabled)
        VulkanDetail::AppendFeatureStruct(pNext, &maintenance4Features);

    VkPhysicalDeviceDynamicRenderingFeatures dynamicRenderingFeatures = VulkanDetail::MakeVkFeatureStruct<VkPhysicalDeviceDynamicRenderingFeatures>(VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_DYNAMIC_RENDERING_FEATURES);
    if(!apiSupportsVulkan13 && dynamicRenderingExtensionEnabled)
        VulkanDetail::AppendFeatureStruct(pNext, &dynamicRenderingFeatures);

    VkPhysicalDeviceCooperativeVectorFeaturesNV cooperativeVectorFeatures = VulkanDetail::MakeVkFeatureStruct<VkPhysicalDeviceCooperativeVectorFeaturesNV>(VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_COOPERATIVE_VECTOR_FEATURES_NV);
    if(coopVecExtensionEnabled)
        VulkanDetail::AppendFeatureStruct(pNext, &cooperativeVectorFeatures);

    bool queriedOptionalFeatures[kOptionalDeviceFeatureCount] = {};
    for(const auto& [_, feature] : m_enabledExtensions.device){
        if(apiSupportsVulkan13 && feature == DeviceExtensionFeature::TextureCompressionAstcHdr)
            continue;
        VulkanDetail::AppendOptionalDeviceFeature(pNext, supportedOptionalFeatures, feature, queriedOptionalFeatures);
    }

    physicalDeviceFeatures2.pNext = pNext;
    vkGetPhysicalDeviceFeatures2(m_vulkanPhysicalDevice, &physicalDeviceFeatures2);

    if(apiSupportsVulkan13){
        synchronization2Features.synchronization2 = supportedVulkan13Features.synchronization2;
        maintenance4Features.maintenance4 = supportedVulkan13Features.maintenance4;
        dynamicRenderingFeatures.dynamicRendering = supportedVulkan13Features.dynamicRendering;
    }

    GraphicsVector<GraphicsString> unsupportedFeatureExtensions{ m_arena };
    unsupportedFeatureExtensions.reserve(m_enabledExtensions.device.size());
    for(const auto& [name, feature] : m_enabledExtensions.device){
        if(feature == DeviceExtensionFeature::None)
            continue;
        if(
            apiSupportsVulkan13
            && feature == DeviceExtensionFeature::TextureCompressionAstcHdr
            && supportedVulkan13Features.textureCompressionASTC_HDR == VK_TRUE
        )
            continue;
        if(VulkanDetail::SupportsRequestedOptionalDeviceFeature(requestedOptionalFeatures, supportedOptionalFeatures, feature))
            continue;
        unsupportedFeatureExtensions.push_back(name);
    }

    for(const auto& name : unsupportedFeatureExtensions){
        const auto extensionIt = m_enabledExtensions.device.find(name);
        if(extensionIt != m_enabledExtensions.device.end() && extensionIt.value() == DeviceExtensionFeature::DescriptorBuffer){
            NWB_LOGGER_ERROR(NWB_TEXT("Vulkan: Required device extension '{}' lacks the descriptorBuffer feature."), StringConvert(name));
            return false;
        }
        NWB_LOGGER_INFO(NWB_TEXT("Vulkan: Disabling device extension '{}' because the selected GPU does not support its required feature set."), StringConvert(name));
        m_enabledExtensions.device.erase(name);
    }

    {
        const GraphicsString samplerFilterMinmaxExtensionName(VK_EXT_SAMPLER_FILTER_MINMAX_EXTENSION_NAME, m_arena);
        const auto samplerFilterMinmaxIt = m_enabledExtensions.device.find(samplerFilterMinmaxExtensionName);
        if(samplerFilterMinmaxIt != m_enabledExtensions.device.end() && supportedVulkan12Features.samplerFilterMinmax != VK_TRUE){
            NWB_LOGGER_INFO(NWB_TEXT("Vulkan: Disabling device extension '{}' because samplerFilterMinmax is not supported."), StringConvert(samplerFilterMinmaxExtensionName));
            m_enabledExtensions.device.erase(samplerFilterMinmaxExtensionName);
        }
    }

    {
        const GraphicsString meshShaderExtensionName(VK_EXT_MESH_SHADER_EXTENSION_NAME, m_arena);
        const auto meshShaderIt = m_enabledExtensions.device.find(meshShaderExtensionName);
        if(
            meshShaderIt != m_enabledExtensions.device.end()
            && physicalDeviceProperties.deviceType == VK_PHYSICAL_DEVICE_TYPE_CPU
        ){
            NWB_LOGGER_INFO(NWB_TEXT("Vulkan: Disabling device extension '{}' on CPU Vulkan device '{}' so renderer uses compute emulation instead of native mesh shaders.")
                , StringConvert(meshShaderExtensionName)
                , StringConvert(physicalDeviceProperties.deviceName)
            );
            m_enabledExtensions.device.erase(meshShaderExtensionName);
        }
    }

    const bool synchronization2Enabled = apiSupportsVulkan13 || isDeviceExtensionEnabled(VK_KHR_SYNCHRONIZATION_2_EXTENSION_NAME);
    const bool maintenance4Enabled = apiSupportsVulkan13 || isDeviceExtensionEnabled(VK_KHR_MAINTENANCE_4_EXTENSION_NAME);
    const bool dynamicRenderingEnabled = apiSupportsVulkan13 || isDeviceExtensionEnabled(VK_KHR_DYNAMIC_RENDERING_EXTENSION_NAME);
    const bool textureCompressionAstcHdrFeatureEnabled = VulkanDetail::ShouldEnableAstcHdrFeature({
        .apiSupportsVulkan13 = apiSupportsVulkan13,
        .vulkan13FeatureSupported = supportedVulkan13Features.textureCompressionASTC_HDR == VK_TRUE,
        .extensionEnabled = isDeviceExtensionEnabled(VK_EXT_TEXTURE_COMPRESSION_ASTC_HDR_EXTENSION_NAME),
        .extensionFeatureSupported = supportedOptionalFeatures.textureCompressionAstcHdr.textureCompressionASTC_HDR == VK_TRUE,
    });

    {
        auto ss = VulkanDetail::MakeScratchStringStream(scratchArena);
        ss << "Vulkan: Enabled device extensions:";
        for(const auto& [name, _] : m_enabledExtensions.device)
            ss << "\n    " << name;
        NWB_LOGGER_INFO(StringConvert(ss.str()));
    }

    auto requireFeature = [&](const VkBool32 supported, const AStringView featureName)->bool{
        if(supported == VK_TRUE)
            return true;

        NWB_LOGGER_ERROR(NWB_TEXT("Vulkan: Required device feature '{}' is not supported by the selected GPU."), StringConvert(featureName));
        return false;
    };

    const VkPhysicalDeviceFeatures& supportedCoreFeatures = physicalDeviceFeatures2.features;
    if(
        !requireFeature(supportedCoreFeatures.shaderImageGatherExtended, "shaderImageGatherExtended")
        || !requireFeature(supportedCoreFeatures.samplerAnisotropy, "samplerAnisotropy")
        || !requireFeature(supportedCoreFeatures.tessellationShader, "tessellationShader")
        || !requireFeature(supportedCoreFeatures.geometryShader, "geometryShader")
        || !requireFeature(supportedCoreFeatures.imageCubeArray, "imageCubeArray")
        || !requireFeature(supportedCoreFeatures.shaderInt16, "shaderInt16")
        || !requireFeature(supportedCoreFeatures.depthClamp, "depthClamp")
        || !requireFeature(supportedCoreFeatures.fillModeNonSolid, "fillModeNonSolid")
        || !requireFeature(supportedCoreFeatures.fragmentStoresAndAtomics, "fragmentStoresAndAtomics")
        || !requireFeature(supportedCoreFeatures.dualSrcBlend, "dualSrcBlend")
        || !requireFeature(supportedCoreFeatures.vertexPipelineStoresAndAtomics, "vertexPipelineStoresAndAtomics")
        || !requireFeature(supportedCoreFeatures.shaderInt64, "shaderInt64")
        || !requireFeature(supportedCoreFeatures.shaderStorageImageWriteWithoutFormat, "shaderStorageImageWriteWithoutFormat")
        || !requireFeature(supportedCoreFeatures.shaderStorageImageReadWithoutFormat, "shaderStorageImageReadWithoutFormat")
        || !requireFeature(supportedCoreFeatures.independentBlend, "independentBlend")
        || !requireFeature(supportedCoreFeatures.fullDrawIndexUint32, "fullDrawIndexUint32")
        || !requireFeature(supportedCoreFeatures.multiDrawIndirect, "multiDrawIndirect")
        || !requireFeature(supportedCoreFeatures.drawIndirectFirstInstance, "drawIndirectFirstInstance")
        || !requireFeature(supportedVulkan11Features.storageBuffer16BitAccess, "storageBuffer16BitAccess")
        || !requireFeature(supportedVulkan11Features.shaderDrawParameters, "shaderDrawParameters")
        || !requireFeature(supportedVulkan12Features.bufferDeviceAddress, "bufferDeviceAddress")
        || !requireFeature(supportedVulkan12Features.descriptorIndexing, "descriptorIndexing")
        || !requireFeature(supportedVulkan12Features.runtimeDescriptorArray, "runtimeDescriptorArray")
        || !requireFeature(supportedVulkan12Features.timelineSemaphore, "timelineSemaphore")
        || !requireFeature(supportedVulkan12Features.shaderFloat16, "shaderFloat16")
        || !requireFeature(supportedVulkan12Features.shaderSampledImageArrayNonUniformIndexing, "shaderSampledImageArrayNonUniformIndexing")
        // Bindless geometry uses non-uniform storage-buffer indexing.
        || !requireFeature(supportedVulkan12Features.shaderStorageBufferArrayNonUniformIndexing, "shaderStorageBufferArrayNonUniformIndexing")
        || !requireFeature(supportedVulkan12Features.shaderSubgroupExtendedTypes, "shaderSubgroupExtendedTypes")
        || !requireFeature(supportedVulkan12Features.scalarBlockLayout, "scalarBlockLayout")
        || !requireFeature(dynamicRenderingEnabled ? dynamicRenderingFeatures.dynamicRendering : VK_FALSE, "dynamicRendering")
        || !requireFeature(synchronization2Enabled ? synchronization2Features.synchronization2 : VK_FALSE, "synchronization2")
    )
        return false;

    m_dynamicRenderingSupported = true;
    m_synchronization2Supported = true;
    m_independentBlendFeatureEnabled = true;
    m_fullDrawIndexUint32FeatureEnabled = true;
    m_multiDrawIndirectFeatureEnabled = true;
    m_drawIndirectFirstInstanceFeatureEnabled = true;
    VulkanDetail::FinalizeOptionalDeviceFeatureEnablement(requestedOptionalFeatures, supportedOptionalFeatures);
    if(!m_deviceParams.enableNativeMeshShaders)
        requestedOptionalFeatures.meshShader.taskShader = VK_FALSE;
    m_meshShaderFeatureEnabled =
        m_deviceParams.enableNativeMeshShaders
        && isDeviceExtensionEnabled(VK_EXT_MESH_SHADER_EXTENSION_NAME)
        && requestedOptionalFeatures.meshShader.meshShader == VK_TRUE
    ;
    m_accelerationStructureFeatureEnabled =
        isDeviceExtensionEnabled(VK_KHR_ACCELERATION_STRUCTURE_EXTENSION_NAME)
        && requestedOptionalFeatures.accelerationStructure.accelerationStructure == VK_TRUE
    ;
    m_rayTracingPipelineFeatureEnabled =
        isDeviceExtensionEnabled(VK_KHR_RAY_TRACING_PIPELINE_EXTENSION_NAME)
        && requestedOptionalFeatures.rayTracingPipeline.rayTracingPipeline == VK_TRUE
    ;
    m_rayQueryFeatureEnabled =
        isDeviceExtensionEnabled(VK_KHR_RAY_QUERY_EXTENSION_NAME)
        && requestedOptionalFeatures.rayQuery.rayQuery == VK_TRUE
    ;
    m_opacityMicromapFeatureEnabled =
        isDeviceExtensionEnabled(VK_EXT_OPACITY_MICROMAP_EXTENSION_NAME)
        && requestedOptionalFeatures.opacityMicromap.micromap == VK_TRUE
    ;
    m_clusterAccelerationStructureFeatureEnabled =
        isDeviceExtensionEnabled(VK_NV_CLUSTER_ACCELERATION_STRUCTURE_EXTENSION_NAME)
        && requestedOptionalFeatures.clusterAccelerationStructure.clusterAccelerationStructure == VK_TRUE
    ;
    m_rayTracingInvocationReorderFeatureEnabled =
        isDeviceExtensionEnabled(VK_NV_RAY_TRACING_INVOCATION_REORDER_EXTENSION_NAME)
        && requestedOptionalFeatures.rayTracingInvocationReorder.rayTracingInvocationReorder == VK_TRUE
    ;
    m_rayTracingInvocationReorderExtFeatureEnabled =
        isDeviceExtensionEnabled(VK_EXT_RAY_TRACING_INVOCATION_REORDER_EXTENSION_NAME)
        && requestedOptionalFeatures.rayTracingInvocationReorderExt.rayTracingInvocationReorder == VK_TRUE
    ;
    m_meshTaskShaderSupported =
        m_meshShaderFeatureEnabled
        && requestedOptionalFeatures.meshShader.taskShader == VK_TRUE
    ;
    m_rayTracingSpheresSupported =
        isDeviceExtensionEnabled(VK_NV_RAY_TRACING_LINEAR_SWEPT_SPHERES_EXTENSION_NAME)
        && requestedOptionalFeatures.rayTracingLinearSweptSpheres.spheres == VK_TRUE
    ;
    m_rayTracingLinearSweptSpheresSupported =
        isDeviceExtensionEnabled(VK_NV_RAY_TRACING_LINEAR_SWEPT_SPHERES_EXTENSION_NAME)
        && requestedOptionalFeatures.rayTracingLinearSweptSpheres.linearSweptSpheres == VK_TRUE
    ;

    // Same-class routing may use every additional queue exposed by each active primary family. The separate
    // cross-family policy may additionally expose one distinct compatible family for each enabled queue class.
    // Query family counts here, after physical-device selection and before vkCreateDevice, so no unsupported queue
    // index is placed in the immutable Device registry.
    uint32_t physicalQueueFamilyCount = 0u;
    vkGetPhysicalDeviceQueueFamilyProperties(m_vulkanPhysicalDevice, &physicalQueueFamilyCount, nullptr);
    Vector<VkQueueFamilyProperties, Alloc::ScratchArena> physicalQueueFamilies(physicalQueueFamilyCount, scratchArena);
    vkGetPhysicalDeviceQueueFamilyProperties(
        m_vulkanPhysicalDevice,
        &physicalQueueFamilyCount,
        physicalQueueFamilies.data()
    );
    const bool createCrossFamilySecondaryGraphicsQueue =
        m_deviceParams.enableSameClassMultiQueue
        && m_deviceParams.enableCrossFamilySameClassQueueRouting
        && m_secondaryGraphicsQueueFamily != s_InvalidQueueFamilyIndex
        && m_secondaryGraphicsQueueFamily != m_graphicsQueueFamily
        && static_cast<usize>(m_secondaryGraphicsQueueFamily) < physicalQueueFamilies.size()
        && physicalQueueFamilies[static_cast<usize>(m_secondaryGraphicsQueueFamily)].queueCount > s_GraphicsQueueIndex
    ;
    const bool createAsyncComputeQueue =
        m_deviceParams.enableAsyncComputeLane
        && m_computeQueueFamily != s_InvalidQueueFamilyIndex
    ;
    const bool createCrossFamilySecondaryComputeQueue =
        createAsyncComputeQueue
        && m_deviceParams.enableSameClassMultiQueue
        && m_deviceParams.enableCrossFamilySameClassQueueRouting
        && m_secondaryComputeQueueFamily != s_InvalidQueueFamilyIndex
        && m_secondaryComputeQueueFamily != m_computeQueueFamily
        && static_cast<usize>(m_secondaryComputeQueueFamily) < physicalQueueFamilies.size()
        && physicalQueueFamilies[static_cast<usize>(m_secondaryComputeQueueFamily)].queueCount > s_ComputeQueueIndex
    ;
    const i32 secondaryComputeQueueFamily = createCrossFamilySecondaryComputeQueue
        ? m_secondaryComputeQueueFamily
        : s_InvalidQueueFamilyIndex
    ;
    const bool createDedicatedTransferQueue =
        m_deviceParams.enableTransferQueue
        && m_transferQueueFamily != s_InvalidQueueFamilyIndex
        && m_transferQueueFamily != m_graphicsQueueFamily
        && m_transferQueueFamily != m_computeQueueFamily
    ;
    const bool createCrossFamilySecondaryTransferQueue =
        createDedicatedTransferQueue
        && m_deviceParams.enableSameClassMultiQueue
        && m_deviceParams.enableCrossFamilySameClassQueueRouting
        && m_secondaryTransferQueueFamily != s_InvalidQueueFamilyIndex
        && m_secondaryTransferQueueFamily != m_transferQueueFamily
        && static_cast<usize>(m_secondaryTransferQueueFamily) < physicalQueueFamilies.size()
        && physicalQueueFamilies[static_cast<usize>(m_secondaryTransferQueueFamily)].queueCount > s_TransferQueueIndex
    ;
    const i32 secondaryTransferQueueFamily = createCrossFamilySecondaryTransferQueue
        ? m_secondaryTransferQueueFamily
        : s_InvalidQueueFamilyIndex
    ;

    struct SameClassQueueRequest{
        CommandQueue::Enum queueClass = CommandQueue::kCount;
        i32 family = s_InvalidQueueFamilyIndex;
        u32 queueIndex = 0u;
    };
    Vector<SameClassQueueRequest, Alloc::ScratchArena> sameClassQueueRequests{scratchArena};
    const auto appendSameClassQueue = [&sameClassQueueRequests](
        const CommandQueue::Enum queueClass,
        const i32 family,
        const u32 queueIndex
    ){
        for(const SameClassQueueRequest& existing : sameClassQueueRequests){
            if(existing.family == family && existing.queueIndex == queueIndex){
                NWB_ASSERT(existing.queueClass == queueClass);
                return;
            }
        }
        sameClassQueueRequests.push_back(SameClassQueueRequest{
            .queueClass = queueClass,
            .family = family,
            .queueIndex = queueIndex,
        });
    };
    const auto appendPrimaryFamilyQueues = [
        &appendSameClassQueue,
        &physicalQueueFamilies,
        this
    ](
        const CommandQueue::Enum queueClass,
        const i32 primaryFamily,
        const u32 firstAdditionalQueueIndex
    ){
        if(
            !m_deviceParams.enableSameClassMultiQueue
            || primaryFamily == s_InvalidQueueFamilyIndex
            || static_cast<usize>(primaryFamily) >= physicalQueueFamilies.size()
        )
            return;

        const u32 queueCount = physicalQueueFamilies[static_cast<usize>(primaryFamily)].queueCount;
        for(u32 queueIndex = firstAdditionalQueueIndex; queueIndex < queueCount; ++queueIndex)
            appendSameClassQueue(queueClass, primaryFamily, queueIndex);
    };

    // Preserve the old auxiliary identity when a cross-family route exists: it remains the first non-primary
    // entry for that class, while every extra queue from the primary family is still registered after it.
    if(createCrossFamilySecondaryGraphicsQueue)
        appendSameClassQueue(CommandQueue::Graphics, m_secondaryGraphicsQueueFamily, s_GraphicsQueueIndex);
    appendPrimaryFamilyQueues(
        CommandQueue::Graphics,
        m_graphicsQueueFamily,
        s_SecondaryGraphicsQueueIndex
    );

    if(createCrossFamilySecondaryComputeQueue)
        appendSameClassQueue(CommandQueue::Compute, secondaryComputeQueueFamily, s_ComputeQueueIndex);
    if(createAsyncComputeQueue){
        appendPrimaryFamilyQueues(
            CommandQueue::Compute,
            m_computeQueueFamily,
            s_ComputeQueueIndex + 1u
        );
    }

    if(createCrossFamilySecondaryTransferQueue)
        appendSameClassQueue(CommandQueue::Transfer, secondaryTransferQueueFamily, s_TransferQueueIndex);
    if(createDedicatedTransferQueue){
        appendPrimaryFamilyQueues(
            CommandQueue::Transfer,
            m_transferQueueFamily,
            s_TransferQueueIndex + 1u
        );
    }

    Vector<i32, Alloc::ScratchArena> uniqueQueueFamilies{scratchArena};
    const auto appendUniqueQueueFamily = [&uniqueQueueFamilies](const i32 queueFamily){
        if(queueFamily == s_InvalidQueueFamilyIndex)
            return;
        for(const i32 existingQueueFamily : uniqueQueueFamilies){
            if(existingQueueFamily == queueFamily)
                return;
        }
        uniqueQueueFamilies.push_back(queueFamily);
    };
    appendUniqueQueueFamily(m_graphicsQueueFamily);
    for(const SameClassQueueRequest& request : sameClassQueueRequests)
        appendUniqueQueueFamily(request.family);

    if(!m_deviceParams.headlessDevice)
        appendUniqueQueueFamily(m_presentQueueFamily);
    if(createAsyncComputeQueue)
        appendUniqueQueueFamily(m_computeQueueFamily);
    if(createCrossFamilySecondaryComputeQueue)
        appendUniqueQueueFamily(secondaryComputeQueueFamily);
    if(createDedicatedTransferQueue)
        appendUniqueQueueFamily(m_transferQueueFamily);
    if(createCrossFamilySecondaryTransferQueue)
        appendUniqueQueueFamily(secondaryTransferQueueFamily);

    u32 maxQueueCount = 1u;
    const auto queueCountForFamily = [&sameClassQueueRequests](const i32 queueFamily){
        u32 queueCount = 1u;
        for(const SameClassQueueRequest& request : sameClassQueueRequests){
            if(request.family == queueFamily && request.queueIndex >= queueCount)
                queueCount = request.queueIndex + 1u;
        }
        return queueCount;
    };
    for(const i32 queueFamily : uniqueQueueFamilies){
        const u32 queueCount = queueCountForFamily(queueFamily);
        if(queueCount > maxQueueCount)
            maxQueueCount = queueCount;
    }
    Vector<f32, Alloc::ScratchArena> queuePriorities(maxQueueCount, scratchArena);
    for(f32& priority : queuePriorities)
        priority = 1.f;
    Vector<VkDeviceQueueCreateInfo, Alloc::ScratchArena> queueDesc(uniqueQueueFamilies.size(), scratchArena);
    usize queueIndex = 0u;
    for(i32 queueFamily : uniqueQueueFamilies){
        VkDeviceQueueCreateInfo queueInfo = {};
        queueInfo.sType = VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO;
        queueInfo.queueFamilyIndex = static_cast<u32>(queueFamily);
        queueInfo.queueCount = queueCountForFamily(queueFamily);
        queueInfo.pQueuePriorities = queuePriorities.data();
        queueDesc[queueIndex] = queueInfo;
        ++queueIndex;
    }

    VkPhysicalDeviceVulkan13Features vulkan13features = VulkanDetail::MakeVkFeatureStruct<VkPhysicalDeviceVulkan13Features>(VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_3_FEATURES);
    vulkan13features.synchronization2 = synchronization2Features.synchronization2;
    vulkan13features.maintenance4 = maintenance4Features.maintenance4;
    vulkan13features.dynamicRendering = dynamicRenderingFeatures.dynamicRendering;
    vulkan13features.textureCompressionASTC_HDR = textureCompressionAstcHdrFeatureEnabled ? VK_TRUE : VK_FALSE;

    pNext = nullptr;
    bool enabledOptionalFeatures[kOptionalDeviceFeatureCount] = {};
    for(const auto& [_, feature] : m_enabledExtensions.device){
        // ASTC HDR is owned by VkPhysicalDeviceVulkan13Features after promotion into Vulkan 1.3.
        if(apiSupportsVulkan13 && feature == DeviceExtensionFeature::TextureCompressionAstcHdr)
            continue;
        VulkanDetail::AppendOptionalDeviceFeature(pNext, requestedOptionalFeatures, feature, enabledOptionalFeatures);
    }

    if(!apiSupportsVulkan13 && dynamicRenderingEnabled)
        VulkanDetail::AppendFeatureStruct(pNext, &dynamicRenderingFeatures);

    if(!apiSupportsVulkan13 && synchronization2Enabled)
        VulkanDetail::AppendFeatureStruct(pNext, &synchronization2Features);

    if(coopVecExtensionEnabled && cooperativeVectorFeatures.cooperativeVector)
        VulkanDetail::AppendFeatureStruct(pNext, &cooperativeVectorFeatures);

    m_cooperativeVectorFeatureEnabled = coopVecExtensionEnabled && cooperativeVectorFeatures.cooperativeVector == VK_TRUE;
    m_cooperativeVectorTrainingFeatureEnabled =
        m_cooperativeVectorFeatureEnabled
        && cooperativeVectorFeatures.cooperativeVectorTraining == VK_TRUE
    ;

    if(apiSupportsVulkan13)
        VulkanDetail::AppendFeatureStruct(pNext, &vulkan13features);
    else if(maintenance4Enabled && maintenance4Features.maintenance4 == VK_TRUE)
        VulkanDetail::AppendFeatureStruct(pNext, &maintenance4Features);

    // Aftermath configuration must outlive vkCreateDevice.
    VkPhysicalDeviceDiagnosticsConfigFeaturesNV diagnosticsConfigFeatures = VulkanDetail::MakeVkFeatureStruct<VkPhysicalDeviceDiagnosticsConfigFeaturesNV>(VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_DIAGNOSTICS_CONFIG_FEATURES_NV);
    VkDeviceDiagnosticsConfigCreateInfoNV diagnosticsConfigCreateInfo = VulkanDetail::MakeVkStruct<VkDeviceDiagnosticsConfigCreateInfoNV>(VK_STRUCTURE_TYPE_DEVICE_DIAGNOSTICS_CONFIG_CREATE_INFO_NV);
    if(aftermathActive && diagnosticsConfigExtensionEnabled){
        diagnosticsConfigFeatures.diagnosticsConfig = VK_TRUE;
        VulkanDetail::AppendFeatureStruct(pNext, &diagnosticsConfigFeatures);

        diagnosticsConfigCreateInfo.flags =
            VK_DEVICE_DIAGNOSTICS_CONFIG_ENABLE_SHADER_DEBUG_INFO_BIT_NV
            | VK_DEVICE_DIAGNOSTICS_CONFIG_ENABLE_RESOURCE_TRACKING_BIT_NV
            | VK_DEVICE_DIAGNOSTICS_CONFIG_ENABLE_AUTOMATIC_CHECKPOINTS_BIT_NV
            | VK_DEVICE_DIAGNOSTICS_CONFIG_ENABLE_SHADER_ERROR_REPORTING_BIT_NV
        ;
        VulkanDetail::AppendFeatureStruct(pNext, &diagnosticsConfigCreateInfo);
    }

    VkPhysicalDeviceFeatures coreDeviceFeatures = {};
    coreDeviceFeatures.shaderImageGatherExtended = supportedCoreFeatures.shaderImageGatherExtended;
    coreDeviceFeatures.samplerAnisotropy = supportedCoreFeatures.samplerAnisotropy;
    coreDeviceFeatures.tessellationShader = supportedCoreFeatures.tessellationShader;
    coreDeviceFeatures.textureCompressionBC = supportedCoreFeatures.textureCompressionBC;
    coreDeviceFeatures.textureCompressionASTC_LDR = supportedCoreFeatures.textureCompressionASTC_LDR;
    coreDeviceFeatures.geometryShader = supportedCoreFeatures.geometryShader;
    coreDeviceFeatures.imageCubeArray = supportedCoreFeatures.imageCubeArray;
    coreDeviceFeatures.shaderInt16 = supportedCoreFeatures.shaderInt16;
    coreDeviceFeatures.depthClamp = supportedCoreFeatures.depthClamp;
    coreDeviceFeatures.fillModeNonSolid = supportedCoreFeatures.fillModeNonSolid;
    coreDeviceFeatures.fragmentStoresAndAtomics = supportedCoreFeatures.fragmentStoresAndAtomics;
    coreDeviceFeatures.dualSrcBlend = supportedCoreFeatures.dualSrcBlend;
    coreDeviceFeatures.independentBlend = VK_TRUE;
    coreDeviceFeatures.vertexPipelineStoresAndAtomics = supportedCoreFeatures.vertexPipelineStoresAndAtomics;
    coreDeviceFeatures.shaderInt64 = supportedCoreFeatures.shaderInt64;
    coreDeviceFeatures.shaderStorageImageWriteWithoutFormat = supportedCoreFeatures.shaderStorageImageWriteWithoutFormat;
    coreDeviceFeatures.shaderStorageImageReadWithoutFormat = supportedCoreFeatures.shaderStorageImageReadWithoutFormat;
    coreDeviceFeatures.fullDrawIndexUint32 = supportedCoreFeatures.fullDrawIndexUint32;
    coreDeviceFeatures.multiDrawIndirect = supportedCoreFeatures.multiDrawIndirect;
    coreDeviceFeatures.drawIndirectFirstInstance = supportedCoreFeatures.drawIndirectFirstInstance;

    VkPhysicalDeviceVulkan11Features vulkan11features = VulkanDetail::MakeVkFeatureStruct<VkPhysicalDeviceVulkan11Features>(VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_1_FEATURES);
    vulkan11features.storageBuffer16BitAccess = supportedVulkan11Features.storageBuffer16BitAccess;
    vulkan11features.shaderDrawParameters = supportedVulkan11Features.shaderDrawParameters;
    vulkan11features.pNext = pNext;

    VkPhysicalDeviceVulkan12Features vulkan12features = VulkanDetail::MakeVkFeatureStruct<VkPhysicalDeviceVulkan12Features>(VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_2_FEATURES);
    vulkan12features.descriptorIndexing = supportedVulkan12Features.descriptorIndexing;
    vulkan12features.runtimeDescriptorArray = supportedVulkan12Features.runtimeDescriptorArray;
    vulkan12features.timelineSemaphore = supportedVulkan12Features.timelineSemaphore;
    vulkan12features.shaderFloat16 = supportedVulkan12Features.shaderFloat16;
    vulkan12features.shaderSampledImageArrayNonUniformIndexing = supportedVulkan12Features.shaderSampledImageArrayNonUniformIndexing;
    vulkan12features.shaderStorageBufferArrayNonUniformIndexing = supportedVulkan12Features.shaderStorageBufferArrayNonUniformIndexing;
    vulkan12features.bufferDeviceAddress = supportedVulkan12Features.bufferDeviceAddress;
    vulkan12features.shaderSubgroupExtendedTypes = supportedVulkan12Features.shaderSubgroupExtendedTypes;
    vulkan12features.scalarBlockLayout = supportedVulkan12Features.scalarBlockLayout;
    // Host-side timer-query resets require hostQueryReset.
    vulkan12features.hostQueryReset = supportedVulkan12Features.hostQueryReset;
    if(isDeviceExtensionEnabled(VK_EXT_SAMPLER_FILTER_MINMAX_EXTENSION_NAME))
        vulkan12features.samplerFilterMinmax = supportedVulkan12Features.samplerFilterMinmax;
    vulkan12features.pNext = &vulkan11features;

    auto extVec = VulkanDetail::StringMapKeysToVector(m_enabledExtensions.device, scratchArena);

    VkDeviceCreateInfo deviceCreateInfo = {};
    deviceCreateInfo.sType = VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO;
    deviceCreateInfo.pQueueCreateInfos = queueDesc.data();
    deviceCreateInfo.queueCreateInfoCount = static_cast<uint32_t>(queueDesc.size());
    deviceCreateInfo.pEnabledFeatures = &coreDeviceFeatures;
    deviceCreateInfo.enabledExtensionCount = static_cast<uint32_t>(extVec.size());
    deviceCreateInfo.ppEnabledExtensionNames = extVec.data();
    deviceCreateInfo.enabledLayerCount = 0;
    deviceCreateInfo.ppEnabledLayerNames = nullptr;
    deviceCreateInfo.pNext = &vulkan12features;

    res = vkCreateDevice(m_vulkanPhysicalDevice, &deviceCreateInfo, nullptr, &m_vulkanDevice);
    if(res != VK_SUCCESS){
        NWB_LOGGER_ERROR(NWB_TEXT("Vulkan: Failed to create logical device. {}"), ResultToString(res));
        return false;
    }

    volkLoadDevice(m_vulkanDevice);

    usize nativeQueueCount = 0u;
    for(const VkDeviceQueueCreateInfo& queueInfo : queueDesc)
        nativeQueueCount += queueInfo.queueCount;
    m_nativeQueues.clear();
    m_nativeQueues.reserve(nativeQueueCount);
    for(const VkDeviceQueueCreateInfo& queueInfo : queueDesc){
        for(u32 nativeQueueOffset = 0u; nativeQueueOffset < queueInfo.queueCount; ++nativeQueueOffset){
            VkQueue queue = VK_NULL_HANDLE;
            vkGetDeviceQueue(m_vulkanDevice, queueInfo.queueFamilyIndex, nativeQueueOffset, &queue);
            if(queue == VK_NULL_HANDLE){
                NWB_LOGGER_ERROR(NWB_TEXT("Vulkan: Created native queue could not be retrieved."));
                return false;
            }
            m_nativeQueues.push_back(VulkanNativeQueueDesc{
                .queue = queue,
                .familyIndex = queueInfo.queueFamilyIndex,
                .queueIndex = nativeQueueOffset,
            });
        }
    }

    m_sameClassGraphicsQueueEnabled = false;
    m_secondaryGraphicsQueueFamily = s_InvalidQueueFamilyIndex;
    m_sameClassQueues.clear();
    m_sameClassQueues.reserve(sameClassQueueRequests.size());
    m_sameClassComputeQueueEnabled = false;
    m_secondaryComputeQueueFamily = s_InvalidQueueFamilyIndex;
    m_sameClassTransferQueueEnabled = false;
    m_secondaryTransferQueueFamily = s_InvalidQueueFamilyIndex;
    m_presentNativeQueueIndex = Limit<u32>::s_Max;
    if(
        findNativeQueueIndex(static_cast<u32>(m_graphicsQueueFamily), s_GraphicsQueueIndex) == Limit<u32>::s_Max
        || (
            createAsyncComputeQueue
            && findNativeQueueIndex(static_cast<u32>(m_computeQueueFamily), s_ComputeQueueIndex) == Limit<u32>::s_Max
        )
        || (
            createDedicatedTransferQueue
            && findNativeQueueIndex(static_cast<u32>(m_transferQueueFamily), s_TransferQueueIndex) == Limit<u32>::s_Max
        )
    ){
        NWB_LOGGER_ERROR(NWB_TEXT("Vulkan: Required scheduler queue is absent from the canonical native registry."));
        return false;
    }
    if(!m_deviceParams.headlessDevice){
        m_presentNativeQueueIndex = findNativeQueueIndex(
            static_cast<u32>(m_presentQueueFamily),
            s_PresentQueueIndex
        );
        if(m_presentNativeQueueIndex == Limit<u32>::s_Max){
            NWB_LOGGER_ERROR(NWB_TEXT("Vulkan: Present queue is absent from the canonical native registry."));
            return false;
        }
    }

    const auto capabilitiesForSameClassQueue = [&physicalQueueFamilies](const i32 queueFamily){
        if(
            queueFamily == s_InvalidQueueFamilyIndex
            || static_cast<usize>(queueFamily) >= physicalQueueFamilies.size()
        ){
            NWB_ASSERT(false);
            return GpuQueueCapability::None;
        }
        return VulkanDetail::QueueCapabilitiesForQueueFlags(
            physicalQueueFamilies[static_cast<usize>(queueFamily)].queueFlags
        );
    };
    const auto registerSameClassQueues = [
        this,
        &sameClassQueueRequests,
        &capabilitiesForSameClassQueue,
        &physicalQueueFamilies
    ](
        const CommandQueue::Enum queueClass,
        bool& sameClassQueueEnabled,
        i32& secondaryQueueFamily
    ){
        for(const SameClassQueueRequest& request : sameClassQueueRequests){
            if(request.queueClass != queueClass)
                continue;

            const u32 nativeQueueIndex = findNativeQueueIndex(
                static_cast<u32>(request.family),
                request.queueIndex
            );
            if(nativeQueueIndex == Limit<u32>::s_Max){
                NWB_LOGGER_ERROR(NWB_TEXT("Vulkan: Same-class queue is absent from the canonical native registry."));
                return false;
            }

            m_sameClassQueues.push_back(VulkanPhysicalQueueDesc{
                .nativeQueueIndex = nativeQueueIndex,
                .queueClass = queueClass,
                .capabilities = capabilitiesForSameClassQueue(request.family),
                .timestampValidBits = physicalQueueFamilies[static_cast<usize>(request.family)].timestampValidBits,
                .dedicated = queueClass != CommandQueue::Graphics,
                .primaryForClass = false,
            });
            if(!sameClassQueueEnabled){
                secondaryQueueFamily = request.family;
                sameClassQueueEnabled = true;
            }
        }
        return true;
    };
    if(
        !registerSameClassQueues(
            CommandQueue::Graphics,
            m_sameClassGraphicsQueueEnabled,
            m_secondaryGraphicsQueueFamily
        )
        || !registerSameClassQueues(
            CommandQueue::Compute,
            m_sameClassComputeQueueEnabled,
            m_secondaryComputeQueueFamily
        )
        || !registerSameClassQueues(
            CommandQueue::Transfer,
            m_sameClassTransferQueueEnabled,
            m_secondaryTransferQueueFamily
        )
    )
        return false;

    m_bufferDeviceAddressSupported = vulkan12features.bufferDeviceAddress == VK_TRUE;
    m_textureCompressionBcFeatureEnabled = coreDeviceFeatures.textureCompressionBC == VK_TRUE;
    m_textureCompressionAstcLdrFeatureEnabled = coreDeviceFeatures.textureCompressionASTC_LDR == VK_TRUE;
    m_textureCompressionAstcHdrFeatureEnabled = textureCompressionAstcHdrFeatureEnabled;

    logVulkanDeviceConfiguration(
        scratchArena,
        physicalDeviceProperties,
        maintenance4Enabled,
        maintenance4Features,
        createAsyncComputeQueue,
        createCrossFamilySecondaryComputeQueue,
        secondaryComputeQueueFamily,
        createDedicatedTransferQueue,
        createCrossFamilySecondaryTransferQueue,
        secondaryTransferQueueFamily
    );

    return true;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_VULKAN_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

