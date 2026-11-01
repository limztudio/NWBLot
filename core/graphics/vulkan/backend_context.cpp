// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "backend_context.h"
#include "backend_context_capabilities.h"
#include "backend_context_detail.h"


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_VULKAN_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace VulkanDetail{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


RequiredQueueFamilySelection SelectRequiredQueueFamilies(const Span<const VkQueueFamilyProperties> queueFamilies, const b8 enableAsyncComputeLane, const b8 enableDedicatedTransferQueue)noexcept{
    RequiredQueueFamilySelection result;
    i32 firstGraphicsFamily = s_InvalidQueueFamilyIndex;
    i32 firstComputeFamily = s_InvalidQueueFamilyIndex;
    i32 firstGraphicsComputeFamily = s_InvalidQueueFamilyIndex;
    i32 firstDedicatedComputeFamily = s_InvalidQueueFamilyIndex;

    for(usize familyIndex = 0u; familyIndex < queueFamilies.size(); ++familyIndex){
        const VkQueueFamilyProperties& family = queueFamilies[familyIndex];
        if(family.queueCount == 0u)
            continue;

        const b8 supportsGraphics = (family.queueFlags & VK_QUEUE_GRAPHICS_BIT) != 0u;
        const b8 supportsCompute = (family.queueFlags & VK_QUEUE_COMPUTE_BIT) != 0u;
        if(firstGraphicsFamily == s_InvalidQueueFamilyIndex && supportsGraphics)
            firstGraphicsFamily = static_cast<i32>(familyIndex);
        if(firstComputeFamily == s_InvalidQueueFamilyIndex && supportsCompute)
            firstComputeFamily = static_cast<i32>(familyIndex);
        if(firstGraphicsComputeFamily == s_InvalidQueueFamilyIndex && supportsGraphics && supportsCompute)
            firstGraphicsComputeFamily = static_cast<i32>(familyIndex);
        if(firstDedicatedComputeFamily == s_InvalidQueueFamilyIndex && supportsCompute && !supportsGraphics)
            firstDedicatedComputeFamily = static_cast<i32>(familyIndex);
        if(enableDedicatedTransferQueue && result.dedicatedTransferFamily == s_InvalidQueueFamilyIndex && (family.queueFlags & VK_QUEUE_TRANSFER_BIT) != 0u && !supportsGraphics && !supportsCompute)
            result.dedicatedTransferFamily = static_cast<i32>(familyIndex);
    }

    if(firstGraphicsComputeFamily != s_InvalidQueueFamilyIndex){
        result.graphicsFamily = firstGraphicsComputeFamily;
        result.computeFamily = firstGraphicsComputeFamily;
        if(enableAsyncComputeLane)
            result.asyncComputeFamily = firstDedicatedComputeFamily;
    }
    else{
        result.graphicsFamily = firstGraphicsFamily;
        result.computeFamily = firstComputeFamily;
    }

    return result;
}

b8 HasDeviceExtension(const Span<const VkExtensionProperties> extensions, const AStringView extensionName)noexcept{
    if(extensionName.size() >= VK_MAX_EXTENSION_NAME_SIZE || extensionName.find('\0') != AStringView::npos)
        return false;

    for(const VkExtensionProperties& extension : extensions){
        const auto& nativeName = extension.extensionName;
        if(nativeName[extensionName.size()] == '\0' && AStringView(nativeName, extensionName.size()) == extensionName)
            return true;
    }
    return false;
}

void QueryPhysicalDeviceFeatureSupport(const VolkInstanceTable& instanceDispatch, const VkPhysicalDevice physicalDevice, const PhysicalDeviceFeatureQueryOptions& options, PhysicalDeviceFeatureSupport& outSupport)noexcept{
    outSupport = {};
    outSupport.features.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FEATURES_2;
    outSupport.vulkan11.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_1_FEATURES;
    outSupport.vulkan12.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_2_FEATURES;
    outSupport.vulkan13.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_3_FEATURES;
    outSupport.descriptorBuffer.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_DESCRIPTOR_BUFFER_FEATURES_EXT;
    outSupport.dynamicRendering.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_DYNAMIC_RENDERING_FEATURES;
    outSupport.synchronization2.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_SYNCHRONIZATION_2_FEATURES;
    outSupport.maintenance4.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_MAINTENANCE_4_FEATURES;

    void* featureChain = nullptr;
    const auto appendFeature = [&featureChain](const NotNull<void*> feature)noexcept{
        auto& featureBase = *static_cast<VkBaseOutStructure*>(feature.get());
        featureBase.pNext = static_cast<VkBaseOutStructure*>(featureChain);
        featureChain = feature.get();
    };
    appendFeature(MakeNotNull(&outSupport.vulkan11));
    appendFeature(MakeNotNull(&outSupport.vulkan12));
    if(options.apiSupportsVulkan13)
        appendFeature(MakeNotNull(&outSupport.vulkan13));
    else{
        if(options.dynamicRenderingExtensionAvailable)
            appendFeature(MakeNotNull(&outSupport.dynamicRendering));
        if(options.synchronization2ExtensionAvailable)
            appendFeature(MakeNotNull(&outSupport.synchronization2));
        if(options.maintenance4ExtensionAvailable)
            appendFeature(MakeNotNull(&outSupport.maintenance4));
    }
    if(options.descriptorBufferExtensionAvailable)
        appendFeature(MakeNotNull(&outSupport.descriptorBuffer));

    outSupport.features.pNext = featureChain;
    instanceDispatch.vkGetPhysicalDeviceFeatures2(physicalDevice, &outSupport.features);

    if(options.apiSupportsVulkan13){
        outSupport.dynamicRendering.dynamicRendering = outSupport.vulkan13.dynamicRendering;
        outSupport.synchronization2.synchronization2 = outSupport.vulkan13.synchronization2;
        outSupport.maintenance4.maintenance4 = outSupport.vulkan13.maintenance4;
    }

    // Publish a pointer-free snapshot; query chains must never survive a copy.
    outSupport.features.pNext = nullptr;
    outSupport.vulkan11.pNext = nullptr;
    outSupport.vulkan12.pNext = nullptr;
    outSupport.vulkan13.pNext = nullptr;
    outSupport.descriptorBuffer.pNext = nullptr;
    outSupport.dynamicRendering.pNext = nullptr;
    outSupport.synchronization2.pNext = nullptr;
    outSupport.maintenance4.pNext = nullptr;
}

AStringView FindMissingMandatoryPhysicalDeviceFeature(const PhysicalDeviceFeatureSupport& support)noexcept{
    const VkPhysicalDeviceFeatures& core = support.features.features;
    if(core.shaderImageGatherExtended != VK_TRUE)
        return "shaderImageGatherExtended";
    if(core.samplerAnisotropy != VK_TRUE)
        return "samplerAnisotropy";
    if(core.tessellationShader != VK_TRUE)
        return "tessellationShader";
    if(core.geometryShader != VK_TRUE)
        return "geometryShader";
    if(core.imageCubeArray != VK_TRUE)
        return "imageCubeArray";
    if(core.shaderInt16 != VK_TRUE)
        return "shaderInt16";
    if(core.depthClamp != VK_TRUE)
        return "depthClamp";
    if(core.fillModeNonSolid != VK_TRUE)
        return "fillModeNonSolid";
    if(core.fragmentStoresAndAtomics != VK_TRUE)
        return "fragmentStoresAndAtomics";
    if(core.dualSrcBlend != VK_TRUE)
        return "dualSrcBlend";
    if(core.vertexPipelineStoresAndAtomics != VK_TRUE)
        return "vertexPipelineStoresAndAtomics";
    if(core.shaderInt64 != VK_TRUE)
        return "shaderInt64";
    if(core.shaderStorageImageWriteWithoutFormat != VK_TRUE)
        return "shaderStorageImageWriteWithoutFormat";
    if(core.shaderStorageImageReadWithoutFormat != VK_TRUE)
        return "shaderStorageImageReadWithoutFormat";
    if(core.independentBlend != VK_TRUE)
        return "independentBlend";
    if(core.fullDrawIndexUint32 != VK_TRUE)
        return "fullDrawIndexUint32";
    if(core.multiDrawIndirect != VK_TRUE)
        return "multiDrawIndirect";
    if(core.drawIndirectFirstInstance != VK_TRUE)
        return "drawIndirectFirstInstance";
    if(support.vulkan11.storageBuffer16BitAccess != VK_TRUE)
        return "storageBuffer16BitAccess";
    if(support.vulkan11.shaderDrawParameters != VK_TRUE)
        return "shaderDrawParameters";
    if(support.vulkan12.bufferDeviceAddress != VK_TRUE)
        return "bufferDeviceAddress";
    if(support.vulkan12.descriptorIndexing != VK_TRUE)
        return "descriptorIndexing";
    if(support.vulkan12.runtimeDescriptorArray != VK_TRUE)
        return "runtimeDescriptorArray";
    if(support.vulkan12.timelineSemaphore != VK_TRUE)
        return "timelineSemaphore";
    if(support.vulkan12.shaderFloat16 != VK_TRUE)
        return "shaderFloat16";
    if(support.vulkan12.shaderSampledImageArrayNonUniformIndexing != VK_TRUE)
        return "shaderSampledImageArrayNonUniformIndexing";
    if(support.vulkan12.shaderStorageBufferArrayNonUniformIndexing != VK_TRUE)
        return "shaderStorageBufferArrayNonUniformIndexing";
    if(support.vulkan12.shaderSubgroupExtendedTypes != VK_TRUE)
        return "shaderSubgroupExtendedTypes";
    if(support.vulkan12.scalarBlockLayout != VK_TRUE)
        return "scalarBlockLayout";
    if(support.dynamicRendering.dynamicRendering != VK_TRUE)
        return "dynamicRendering";
    if(support.synchronization2.synchronization2 != VK_TRUE)
        return "synchronization2";
    if(support.descriptorBuffer.descriptorBuffer != VK_TRUE)
        return "descriptorBuffer";
    return {};
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


OptionalDeviceFeatureSet MakeRequestedOptionalDeviceFeatures()noexcept{
    OptionalDeviceFeatureSet features;

    features.accelerationStructure.accelerationStructure = VK_TRUE;

    features.rayTracingPipeline.rayTracingPipeline = VK_TRUE;
    features.rayTracingPipeline.rayTraversalPrimitiveCulling = VK_TRUE;

    features.rayQuery.rayQuery = VK_TRUE;

    features.opacityMicromap.micromap = VK_TRUE;

    features.clusterAccelerationStructure.clusterAccelerationStructure = VK_TRUE;

    features.rayTracingInvocationReorder.rayTracingInvocationReorder = VK_TRUE;
    features.rayTracingInvocationReorderExt.rayTracingInvocationReorder = VK_TRUE;

    features.rayTracingLinearSweptSpheres.spheres = VK_FALSE;
    features.rayTracingLinearSweptSpheres.linearSweptSpheres = VK_FALSE;

    features.meshShader.meshShader = VK_TRUE;

    features.fragmentShadingRate.pipelineFragmentShadingRate = VK_TRUE;
    features.fragmentShadingRate.primitiveFragmentShadingRate = VK_TRUE;
    features.fragmentShadingRate.attachmentFragmentShadingRate = VK_TRUE;

    // Descriptor-buffer feature support is mandatory.
    features.descriptorBuffer.descriptorBuffer = VK_TRUE;

    features.deviceFault.deviceFault = VK_TRUE;
    features.deviceFault.deviceFaultVendorBinary = VK_TRUE;

    features.textureCompressionAstcHdr.textureCompressionASTC_HDR = VK_TRUE;

    return features;
}

void* GetOptionalDeviceFeatureStruct(OptionalDeviceFeatureSet& features, DeviceExtensionFeature::Enum feature)noexcept{
    switch(feature){
    case DeviceExtensionFeature::AccelerationStructure: return &features.accelerationStructure;
    case DeviceExtensionFeature::RayTracingPipeline: return &features.rayTracingPipeline;
    case DeviceExtensionFeature::RayQuery: return &features.rayQuery;
    case DeviceExtensionFeature::OpacityMicromap: return &features.opacityMicromap;
    case DeviceExtensionFeature::ClusterAccelerationStructure: return &features.clusterAccelerationStructure;
    case DeviceExtensionFeature::RayTracingInvocationReorder: return &features.rayTracingInvocationReorder;
    case DeviceExtensionFeature::RayTracingInvocationReorderExt: return &features.rayTracingInvocationReorderExt;
    case DeviceExtensionFeature::RayTracingLinearSweptSpheres: return &features.rayTracingLinearSweptSpheres;
    case DeviceExtensionFeature::MeshShader: return &features.meshShader;
    case DeviceExtensionFeature::FragmentShadingRate: return &features.fragmentShadingRate;
    case DeviceExtensionFeature::DescriptorBuffer: return &features.descriptorBuffer;
    case DeviceExtensionFeature::DeviceFault: return &features.deviceFault;
    case DeviceExtensionFeature::TextureCompressionAstcHdr: return &features.textureCompressionAstcHdr;
    case DeviceExtensionFeature::None:
    case DeviceExtensionFeature::Count:
    default:
        return nullptr;
    }
}

Format::Enum GetBackBufferFormat(const DeviceCreationParameters& params)noexcept{
    if(params.headlessDevice)
        return params.swapChainFormat;

    if(params.swapChainFormat == Format::RGBA8_UNORM_SRGB)
        return Format::BGRA8_UNORM_SRGB;
    if(params.swapChainFormat == Format::RGBA8_UNORM)
        return Format::BGRA8_UNORM;
    return params.swapChainFormat;
}

ScratchString VulkanVersionToString(Alloc::ScratchArena& arena, u32 version){
    auto ss = ScratchStringStream(s_FileOpenWrite, arena);
    ss << VK_API_VERSION_MAJOR(version) << "." << VK_API_VERSION_MINOR(version) << "." << VK_API_VERSION_PATCH(version);
    if(VK_API_VERSION_VARIANT(version) != 0)
        ss << " variant " << VK_API_VERSION_VARIANT(version);
    return ss.str();
}

AStringView PhysicalDeviceTypeToString(VkPhysicalDeviceType type)noexcept{
    switch(type){
    case VK_PHYSICAL_DEVICE_TYPE_OTHER: return "other";
    case VK_PHYSICAL_DEVICE_TYPE_INTEGRATED_GPU: return "integrated GPU";
    case VK_PHYSICAL_DEVICE_TYPE_DISCRETE_GPU: return "discrete GPU";
    case VK_PHYSICAL_DEVICE_TYPE_VIRTUAL_GPU: return "virtual GPU";
    case VK_PHYSICAL_DEVICE_TYPE_CPU: return "CPU";
    default: return s_UnknownDebugLabel;
    }
}

AStringView SwapChainFormatToString(VkFormat format)noexcept{
    switch(format){
    case VK_FORMAT_A2B10G10R10_UNORM_PACK32: return "VK_FORMAT_A2B10G10R10_UNORM_PACK32";
    case VK_FORMAT_R8G8B8A8_UNORM: return "VK_FORMAT_R8G8B8A8_UNORM";
    case VK_FORMAT_R8G8B8A8_SRGB: return "VK_FORMAT_R8G8B8A8_SRGB";
    case VK_FORMAT_B8G8R8A8_UNORM: return "VK_FORMAT_B8G8R8A8_UNORM";
    case VK_FORMAT_B8G8R8A8_SRGB: return "VK_FORMAT_B8G8R8A8_SRGB";
    default: return s_UnknownDebugLabel;
    }
}

AStringView ColorSpaceToString(VkColorSpaceKHR colorSpace)noexcept{
    switch(colorSpace){
    case VK_COLOR_SPACE_HDR10_ST2084_EXT: return "VK_COLOR_SPACE_HDR10_ST2084_EXT";
    case VK_COLOR_SPACE_SRGB_NONLINEAR_KHR: return "VK_COLOR_SPACE_SRGB_NONLINEAR_KHR";
    default: return s_UnknownDebugLabel;
    }
}

AStringView PresentModeToString(VkPresentModeKHR mode)noexcept{
    switch(mode){
    case VK_PRESENT_MODE_IMMEDIATE_KHR: return "VK_PRESENT_MODE_IMMEDIATE_KHR";
    case VK_PRESENT_MODE_MAILBOX_KHR: return "VK_PRESENT_MODE_MAILBOX_KHR";
    case VK_PRESENT_MODE_FIFO_KHR: return "VK_PRESENT_MODE_FIFO_KHR";
    case VK_PRESENT_MODE_FIFO_RELAXED_KHR: return "VK_PRESENT_MODE_FIFO_RELAXED_KHR";
    default: return s_UnknownDebugLabel;
    }
}

void SetHdr10Metadata(const VolkDeviceTable& deviceDispatch, const VkDevice device, const VkSwapchainKHR swapChain){
    if(!deviceDispatch.vkSetHdrMetadataEXT || !device || !swapChain)
        return;

    VkHdrMetadataEXT metadata = {};
    metadata.sType = VK_STRUCTURE_TYPE_HDR_METADATA_EXT;
    // Rec.2020 primaries and D65 white point.
    // These values describe the HDR10 presentation contract: PQ-encoded content mastered for a 1,000-nit peak.
    metadata.displayPrimaryRed = s_Hdr10DisplayPrimaryRed;
    metadata.displayPrimaryGreen = s_Hdr10DisplayPrimaryGreen;
    metadata.displayPrimaryBlue = s_Hdr10DisplayPrimaryBlue;
    metadata.whitePoint = s_Hdr10WhitePointD65;
    metadata.maxLuminance = s_Hdr10MasteringPeakLuminance;
    metadata.minLuminance = s_Hdr10MinimumLuminance;
    metadata.maxContentLightLevel = s_Hdr10MasteringPeakLuminance;
    metadata.maxFrameAverageLightLevel = s_Hdr10MaximumFrameAverageLightLevel;
    deviceDispatch.vkSetHdrMetadataEXT(device, s_Hdr10MetadataSwapChainCount, &swapChain, &metadata);
}

u64 GetDeviceLocalMemoryBytes(const VolkInstanceTable& instanceDispatch, VkPhysicalDevice physicalDevice){
    VkPhysicalDeviceMemoryProperties memoryProperties;
    instanceDispatch.vkGetPhysicalDeviceMemoryProperties(physicalDevice, &memoryProperties);

    u64 bytes = 0;
    for(uint32_t heapIndex = 0; heapIndex < memoryProperties.memoryHeapCount; ++heapIndex){
        const VkMemoryHeap& heap = memoryProperties.memoryHeaps[heapIndex];
        if(heap.flags & VK_MEMORY_HEAP_DEVICE_LOCAL_BIT)
            bytes += static_cast<u64>(heap.size);
    }

    return bytes;
}

void PopulateAdapterInfo(const VolkInstanceTable& instanceDispatch, VkPhysicalDevice physicalDevice, AdapterInfo& outAdapter){
    VkPhysicalDeviceProperties2 properties2 = {};
    properties2.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_PROPERTIES_2;
    VkPhysicalDeviceIDProperties idProperties = {};
    idProperties.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_ID_PROPERTIES;
    properties2.pNext = &idProperties;
    instanceDispatch.vkGetPhysicalDeviceProperties2(physicalDevice, &properties2);

    const auto& properties = properties2.properties;
    outAdapter.name = properties.deviceName;
    outAdapter.vendorID = properties.vendorID;
    outAdapter.deviceID = properties.deviceID;
    outAdapter.dedicatedVideoMemory = GetDeviceLocalMemoryBytes(instanceDispatch, physicalDevice);

    NWB_MEMCPY(outAdapter.uuid.data(), outAdapter.uuid.size(), idProperties.deviceUUID, outAdapter.uuid.size());
    outAdapter.hasUUID = true;
    outAdapter.luid = {};
    outAdapter.hasLUID = false;
    if(idProperties.deviceLUIDValid){
        NWB_MEMCPY(outAdapter.luid.data(), outAdapter.luid.size(), idProperties.deviceLUID, outAdapter.luid.size());
        outAdapter.hasLUID = true;
    }
}

b8 SupportsRequestedOptionalDeviceFeature(const OptionalDeviceFeatureSet& requested, const OptionalDeviceFeatureSet& supported, DeviceExtensionFeature::Enum feature)noexcept{
    switch(feature){
    case DeviceExtensionFeature::AccelerationStructure:
        return SupportsRequestedValue(requested.accelerationStructure.accelerationStructure, supported.accelerationStructure.accelerationStructure);
    case DeviceExtensionFeature::RayTracingPipeline:
        return
            SupportsRequestedValue(requested.rayTracingPipeline.rayTracingPipeline, supported.rayTracingPipeline.rayTracingPipeline)
            && SupportsRequestedValue(requested.rayTracingPipeline.rayTracingPipelineShaderGroupHandleCaptureReplay, supported.rayTracingPipeline.rayTracingPipelineShaderGroupHandleCaptureReplay)
            && SupportsRequestedValue(requested.rayTracingPipeline.rayTracingPipelineShaderGroupHandleCaptureReplayMixed, supported.rayTracingPipeline.rayTracingPipelineShaderGroupHandleCaptureReplayMixed)
            && SupportsRequestedValue(requested.rayTracingPipeline.rayTracingPipelineTraceRaysIndirect, supported.rayTracingPipeline.rayTracingPipelineTraceRaysIndirect)
            && SupportsRequestedValue(requested.rayTracingPipeline.rayTraversalPrimitiveCulling, supported.rayTracingPipeline.rayTraversalPrimitiveCulling)
        ;
    case DeviceExtensionFeature::RayQuery:
        return SupportsRequestedValue(requested.rayQuery.rayQuery, supported.rayQuery.rayQuery);
    case DeviceExtensionFeature::OpacityMicromap:
        return SupportsRequestedValue(requested.opacityMicromap.micromap, supported.opacityMicromap.micromap);
    case DeviceExtensionFeature::ClusterAccelerationStructure:
        return SupportsRequestedValue(requested.clusterAccelerationStructure.clusterAccelerationStructure, supported.clusterAccelerationStructure.clusterAccelerationStructure);
    case DeviceExtensionFeature::RayTracingInvocationReorder:
        return SupportsRequestedValue(requested.rayTracingInvocationReorder.rayTracingInvocationReorder, supported.rayTracingInvocationReorder.rayTracingInvocationReorder);
    case DeviceExtensionFeature::RayTracingInvocationReorderExt:
        return SupportsRequestedValue(requested.rayTracingInvocationReorderExt.rayTracingInvocationReorder, supported.rayTracingInvocationReorderExt.rayTracingInvocationReorder);
    case DeviceExtensionFeature::RayTracingLinearSweptSpheres:
        return supported.rayTracingLinearSweptSpheres.spheres == VK_TRUE || supported.rayTracingLinearSweptSpheres.linearSweptSpheres == VK_TRUE;
    case DeviceExtensionFeature::MeshShader:
        return SupportsRequestedValue(requested.meshShader.taskShader, supported.meshShader.taskShader) && SupportsRequestedValue(requested.meshShader.meshShader, supported.meshShader.meshShader);
    case DeviceExtensionFeature::FragmentShadingRate:
        return
            SupportsRequestedValue(requested.fragmentShadingRate.pipelineFragmentShadingRate, supported.fragmentShadingRate.pipelineFragmentShadingRate)
            && SupportsRequestedValue(requested.fragmentShadingRate.primitiveFragmentShadingRate, supported.fragmentShadingRate.primitiveFragmentShadingRate)
            && SupportsRequestedValue(requested.fragmentShadingRate.attachmentFragmentShadingRate, supported.fragmentShadingRate.attachmentFragmentShadingRate)
        ;
    case DeviceExtensionFeature::DescriptorBuffer:
        return SupportsRequestedValue(requested.descriptorBuffer.descriptorBuffer, supported.descriptorBuffer.descriptorBuffer);
    case DeviceExtensionFeature::DeviceFault:
        return SupportsRequestedValue(requested.deviceFault.deviceFault, supported.deviceFault.deviceFault);
    case DeviceExtensionFeature::TextureCompressionAstcHdr:
        return SupportsRequestedValue(requested.textureCompressionAstcHdr.textureCompressionASTC_HDR, supported.textureCompressionAstcHdr.textureCompressionASTC_HDR);
    case DeviceExtensionFeature::None:
    case DeviceExtensionFeature::Count:
    default:
        return true;
    }
}

void FinalizeOptionalDeviceFeatureEnablement(OptionalDeviceFeatureSet& enabled, const OptionalDeviceFeatureSet& supported)noexcept{
    enabled.meshShader.taskShader = supported.meshShader.taskShader;
    enabled.rayTracingLinearSweptSpheres.spheres = supported.rayTracingLinearSweptSpheres.spheres;
    enabled.rayTracingLinearSweptSpheres.linearSweptSpheres = supported.rayTracingLinearSweptSpheres.linearSweptSpheres;
    enabled.deviceFault.deviceFaultVendorBinary = supported.deviceFault.deviceFaultVendorBinary;
}

void AppendOptionalDeviceFeature(DeviceExtensionFeature::Enum feature, void*& pNext, OptionalDeviceFeatureSet& features, Array<b8, static_cast<usize>(DeviceExtensionFeature::Count)>& appended)noexcept{
    if(feature == DeviceExtensionFeature::None || feature == DeviceExtensionFeature::Count)
        return;

    const usize featureIndex = static_cast<usize>(feature);
    if(appended[featureIndex])
        return;

    if(void* featureStruct = GetOptionalDeviceFeatureStruct(features, feature)){
        AppendFeatureStruct(pNext, *MakeNotNull(static_cast<VkBaseOutStructure*>(featureStruct)));
        appended[featureIndex] = true;
    }
}

AStringView DebugUtilsSeverityToString(const VkDebugUtilsMessageSeverityFlagBitsEXT severity)noexcept{
    switch(severity){
    case VK_DEBUG_UTILS_MESSAGE_SEVERITY_VERBOSE_BIT_EXT: return "verbose";
    case VK_DEBUG_UTILS_MESSAGE_SEVERITY_INFO_BIT_EXT: return "info";
    case VK_DEBUG_UTILS_MESSAGE_SEVERITY_WARNING_BIT_EXT: return "warning";
    case VK_DEBUG_UTILS_MESSAGE_SEVERITY_ERROR_BIT_EXT: return "error";
    default: return s_UnknownDebugLabel;
    }
}

VKAPI_ATTR VkBool32 VKAPI_CALL VulkanDebugCallback(VkDebugUtilsMessageSeverityFlagBitsEXT severity, VkDebugUtilsMessageTypeFlagsEXT types, const VkDebugUtilsMessengerCallbackDataEXT* callbackData, void* userData){
    const i32 messageId = callbackData ? callbackData->messageIdNumber : 0;
    const auto* backend = static_cast<const BackendContext*>(userData);
    if(backend && backend->isValidationMessageIdIgnored(messageId))
        return VK_FALSE;

    const AStringView messageIdName = callbackData && callbackData->pMessageIdName ? callbackData->pMessageIdName : "";
    const AStringView message = callbackData && callbackData->pMessage ? callbackData->pMessage : "";
    NWB_LOGGER_WARNING(NWB_TEXT("Vulkan debug: [severity={} types=0x{:x} id={} name='{}'] {}"), ConvertString(DebugUtilsSeverityToString(severity)), static_cast<u32>(types), messageId, ConvertString(messageIdName), ConvertString(message));

    return VK_FALSE;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


BackendContext::BackendContext(const DeviceCreationParameters& params, SwapChainRuntimeState& swapChainState, GraphicsAllocator& allocator, CpuTaskScheduler& cpuScheduler)
    : m_deviceParams(params)
    , m_swapChainState(swapChainState)
    , m_allocator(allocator)
    , m_cpuScheduler(cpuScheduler)
    , m_arena(m_allocator.getObjectArena())
    , m_enabledExtensions(m_arena)
    , m_optionalExtensions(m_arena)
    , m_rayTracingExtensions(0, ExtensionNameHasher(), ExtensionNameEqualTo(), m_arena)
    , m_rendererString(m_arena)
    , m_swapChainImages(m_arena)
    , m_nativeQueues(m_arena)
    , m_sameClassQueues(m_arena)
    , m_acquireSyncSlots(m_arena)
    , m_presentSemaphores(m_arena)
    , m_framesInFlight(Deque<EventQueryHandle, Alloc::GlobalArena>(m_arena))
    , m_queryPool(m_arena)
{
    initDefaultExtensions();
}

b8 BackendContext::isValidationMessageIdIgnored(i32 messageId)const noexcept{
    for(const auto& ignored : m_deviceParams.ignoredValidationMessageIds){
        if(ignored == messageId)
            return true;
    }
    return false;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_VULKAN_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

