// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "backend.h"
#include "arena_names.h"
#include "device_detail.h"
#include "raytracing_internal.h"

#include <core/common/log.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_VULKAN_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace __hidden_device_query{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


[[nodiscard]] VulkanDetail::RayTracingCapabilityInputs CollectRayTracingCapabilityInputs(const VulkanContext& context)noexcept{
    VulkanDetail::RayTracingCapabilityInputs inputs;
    inputs.accelerationStructureExtensionEnabled = context.extensions.khrAccelerationStructure;
    inputs.accelerationStructureFeatureEnabled = context.accelerationStructureFeatureEnabled;
    inputs.createAccelerationStructureEntryPointAvailable = context.deviceDispatch.vkCreateAccelerationStructureKHR != nullptr;
    inputs.destroyAccelerationStructureEntryPointAvailable = context.deviceDispatch.vkDestroyAccelerationStructureKHR != nullptr;
    inputs.getAccelerationStructureBuildSizesEntryPointAvailable = context.deviceDispatch.vkGetAccelerationStructureBuildSizesKHR != nullptr;
    inputs.getAccelerationStructureDeviceAddressEntryPointAvailable = context.deviceDispatch.vkGetAccelerationStructureDeviceAddressKHR != nullptr;
    inputs.cmdBuildAccelerationStructuresEntryPointAvailable = context.deviceDispatch.vkCmdBuildAccelerationStructuresKHR != nullptr;

    inputs.rayTracingPipelineExtensionEnabled = context.extensions.khrRayTracingPipeline;
    inputs.rayTracingPipelineFeatureEnabled = context.rayTracingPipelineFeatureEnabled;
    inputs.createRayTracingPipelinesEntryPointAvailable = context.deviceDispatch.vkCreateRayTracingPipelinesKHR != nullptr;
    inputs.getRayTracingShaderGroupHandlesEntryPointAvailable = context.deviceDispatch.vkGetRayTracingShaderGroupHandlesKHR != nullptr;
    inputs.cmdTraceRaysEntryPointAvailable = context.deviceDispatch.vkCmdTraceRaysKHR != nullptr;

    inputs.opacityMicromapExtensionEnabled = context.extensions.extOpacityMicromap;
    inputs.opacityMicromapFeatureEnabled = context.opacityMicromapFeatureEnabled;
    inputs.synchronization2ExtensionEnabled = context.extensions.khrSynchronization2;
    inputs.createMicromapEntryPointAvailable = context.deviceDispatch.vkCreateMicromapEXT != nullptr;
    inputs.destroyMicromapEntryPointAvailable = context.deviceDispatch.vkDestroyMicromapEXT != nullptr;
    inputs.getMicromapBuildSizesEntryPointAvailable = context.deviceDispatch.vkGetMicromapBuildSizesEXT != nullptr;
    inputs.cmdBuildMicromapsEntryPointAvailable = context.deviceDispatch.vkCmdBuildMicromapsEXT != nullptr;
    return inputs;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


bool Device::queryFeatureSupport(Feature::Enum feature)const noexcept{
    const VulkanDetail::RayTracingCapabilityInputs rayTracingCapabilities = __hidden_device_query::CollectRayTracingCapabilityInputs(m_context);

    switch(feature){
    case Feature::DeferredCommandLists:
        return true;
    case Feature::RayTracingAccelStruct:
        return VulkanDetail::SupportsRayTracingAccelStruct(rayTracingCapabilities);
    case Feature::RayTracingPipeline:
        return VulkanDetail::SupportsRayTracingPipeline(rayTracingCapabilities);
    case Feature::RayQuery:
        return m_context.extensions.khrRayQuery && m_context.rayQueryFeatureEnabled && VulkanDetail::SupportsRayTracingAccelStruct(rayTracingCapabilities);
    case Feature::ShaderExecutionReordering:
        return (m_context.extensions.extRayTracingInvocationReorder && m_context.rayTracingInvocationReorderExtFeatureEnabled) || (m_context.extensions.nvRayTracingInvocationReorder && m_context.rayTracingInvocationReorderFeatureEnabled);
    case Feature::Spheres:
        return m_context.extensions.nvRayTracingLinearSweptSpheres && m_context.rayTracingLinearSweptSpheresFeatures.spheres == VK_TRUE && queryFeatureSupport(Feature::RayTracingPipeline);
    case Feature::LinearSweptSpheres:
        return m_context.extensions.nvRayTracingLinearSweptSpheres && m_context.rayTracingLinearSweptSpheresFeatures.linearSweptSpheres == VK_TRUE && queryFeatureSupport(Feature::RayTracingPipeline);
    case Feature::RayTracingOpacityMicromap:
        return VulkanDetail::SupportsRayTracingOpacityMicromap(rayTracingCapabilities);
    case Feature::RayTracingClusters:
        return
            m_context.extensions.nvClusterAccelerationStructure
            && m_context.clusterAccelerationStructureFeatureEnabled
            && VulkanDetail::SupportsRayTracingPipeline(rayTracingCapabilities)
            && m_context.deviceDispatch.vkGetClusterAccelerationStructureBuildSizesNV
            && m_context.deviceDispatch.vkCmdBuildClusterAccelerationStructureIndirectNV
        ;
    case Feature::CooperativeVectorInferencing:
        return
            m_context.extensions.nvCooperativeVector
            && m_context.coopVecFeatures.cooperativeVector == VK_TRUE
            && m_context.instanceDispatch.vkGetPhysicalDeviceCooperativeVectorPropertiesNV
            && m_context.deviceDispatch.vkConvertCooperativeVectorMatrixNV
            && m_context.deviceDispatch.vkCmdConvertCooperativeVectorMatrixNV
        ;
    case Feature::CooperativeVectorTraining:
        return
            m_context.extensions.nvCooperativeVector
            && m_context.coopVecFeatures.cooperativeVector == VK_TRUE
            && m_context.coopVecFeatures.cooperativeVectorTraining == VK_TRUE
            && m_context.instanceDispatch.vkGetPhysicalDeviceCooperativeVectorPropertiesNV
            && m_context.deviceDispatch.vkConvertCooperativeVectorMatrixNV
            && m_context.deviceDispatch.vkCmdConvertCooperativeVectorMatrixNV
        ;
    case Feature::Meshlets:
        return m_context.extensions.extMeshShader && m_context.meshShaderFeatures.meshShader == VK_TRUE && m_context.deviceDispatch.vkCmdDrawMeshTasksEXT;
    case Feature::VariableRateShading:
        return false;
    case Feature::ConstantBufferRanges:
        return true;
    default:
        return false;
    }
}

WaveLaneCountRange Device::getWaveLaneCounts()const noexcept{
    const u32 count = m_context.subgroupProperties.subgroupSize;
    return { count, count };
}

u64 Device::getMaxStorageBufferRange()const noexcept{
    return static_cast<u64>(m_context.physicalDeviceProperties.limits.maxStorageBufferRange);
}

bool Device::canCreateSampledTextureFormat(const Format::Enum format)const{
    const VkFormat vkFormat = ConvertFormat(format);
    if(vkFormat == VK_FORMAT_UNDEFINED)
        return false;

    auto imageFormatInfo = VulkanDetail::MakeVkStruct<VkPhysicalDeviceImageFormatInfo2>(VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_IMAGE_FORMAT_INFO_2);
    imageFormatInfo.format = vkFormat;
    imageFormatInfo.type = VK_IMAGE_TYPE_2D;
    imageFormatInfo.tiling = VK_IMAGE_TILING_OPTIMAL;
    // Static textures must be uploadable as well as sampled, so verify both uses together.
    imageFormatInfo.usage = VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT;

    auto imageFormatProperties = VulkanDetail::MakeVkStruct<VkImageFormatProperties2>(VK_STRUCTURE_TYPE_IMAGE_FORMAT_PROPERTIES_2);
    const VkResult res = m_context.instanceDispatch.vkGetPhysicalDeviceImageFormatProperties2(m_context.physicalDevice, &imageFormatInfo, &imageFormatProperties);
    if(res == VK_SUCCESS)
        return true;

    if(res != VK_ERROR_FORMAT_NOT_SUPPORTED){
        NWB_LOGGER_WARNING(NWB_TEXT("Vulkan: Failed to probe sampled texture format {}: {}"), StringConvert(GetFormatInfo(format).getName()), ResultToString(res));
    }
    return false;
}

FormatSupport::Mask Device::queryFormatSupportUncached(const Format::Enum format)const{
    const VulkanDetail::CompressedTextureFeatureState compressedTextureFeatures{
        .bcEnabled = m_context.textureCompressionBcFeatureEnabled,
        .astcLdrEnabled = m_context.textureCompressionAstcLdrFeatureEnabled,
        .astcHdrEnabled = m_context.textureCompressionAstcHdrFeatureEnabled,
    };
    if(!VulkanDetail::IsCompressedTextureFormatFeatureEnabled(format, compressedTextureFeatures))
        return FormatSupport::None;

    const VkFormat vkFormat = ConvertFormat(format);
    if(vkFormat == VK_FORMAT_UNDEFINED)
        return FormatSupport::None;

    VkFormatProperties props;
    m_context.instanceDispatch.vkGetPhysicalDeviceFormatProperties(m_context.physicalDevice, vkFormat, &props);

    FormatSupport::Mask support = FormatSupport::None;

    VkFormatFeatureFlags features = props.optimalTilingFeatures;

    if(features & VK_FORMAT_FEATURE_SAMPLED_IMAGE_BIT)
        support |= FormatSupport::Texture | FormatSupport::ShaderLoad;
    if(features & VK_FORMAT_FEATURE_DEPTH_STENCIL_ATTACHMENT_BIT)
        support |= FormatSupport::DepthStencil;
    if(features & VK_FORMAT_FEATURE_COLOR_ATTACHMENT_BIT)
        support |= FormatSupport::RenderTarget;
    if(features & VK_FORMAT_FEATURE_COLOR_ATTACHMENT_BLEND_BIT)
        support |= FormatSupport::Blendable;
    if(features & VK_FORMAT_FEATURE_STORAGE_IMAGE_BIT)
        support |= FormatSupport::ShaderUavStore;
    if(features & VK_FORMAT_FEATURE_SAMPLED_IMAGE_FILTER_LINEAR_BIT)
        support |= FormatSupport::ShaderSample;

    VkFormatFeatureFlags bufferFeatures = props.bufferFeatures;
    if(bufferFeatures & VK_FORMAT_FEATURE_UNIFORM_TEXEL_BUFFER_BIT)
        support |= FormatSupport::Buffer;
    if(bufferFeatures & VK_FORMAT_FEATURE_VERTEX_BUFFER_BIT)
        support |= FormatSupport::Buffer;

    return support;
}

void Device::probeCompressedTextureFormats(){
    constexpr FormatSupport::Mask requiredReadableSupport = FormatSupport::Texture | FormatSupport::ShaderSample;

    u32 readableAstcLdrFormatCount = 0u;
    u32 readableAstcHdrFormatCount = 0u;
    u32 readableBcFormatCount = 0u;
    u32 astcLdrFormatCount = 0u;
    u32 astcHdrFormatCount = 0u;
    u32 bcFormatCount = 0u;
    for(u32 formatValue = static_cast<u32>(Format::BC1_UNORM); formatValue <= static_cast<u32>(Format::ASTC_12x12_FLOAT); ++formatValue){
        const Format::Enum format = static_cast<Format::Enum>(formatValue);
        FormatSupport::Mask support = queryFormatSupportUncached(format);
        if((support & FormatSupport::Texture) == FormatSupport::Texture && !canCreateSampledTextureFormat(format)){
            support &= ~(requiredReadableSupport | FormatSupport::ShaderLoad);
        }
        m_compressedFormatSupport[formatValue] = support;

        const bool readable = (support & requiredReadableSupport) == requiredReadableSupport;
        if(Format::IsASTCCompressedFormat(format)){
            if(Format::IsASTCHdrFormat(format)){
                ++astcHdrFormatCount;
                if(readable)
                    ++readableAstcHdrFormatCount;
            }
            else{
                ++astcLdrFormatCount;
                if(readable)
                    ++readableAstcLdrFormatCount;
            }
        }
        else{
            NWB_ASSERT(Format::IsBCCompressedFormat(format));
            ++bcFormatCount;
            if(readable)
                ++readableBcFormatCount;
        }
    }

    NWB_LOGGER_INFO(
        NWB_TEXT("Vulkan: compressed texture probe found {}/{} readable ASTC LDR formats, {}/{} readable ASTC HDR formats, and {}/{} readable BC formats."),
        readableAstcLdrFormatCount,
        astcLdrFormatCount,
        readableAstcHdrFormatCount,
        astcHdrFormatCount,
        readableBcFormatCount,
        bcFormatCount
    );
}

FormatSupport::Mask Device::queryFormatSupport(const Format::Enum format){
    if(Format::IsBlockCompressedFormat(format))
        return m_compressedFormatSupport[static_cast<usize>(format)];

    return queryFormatSupportUncached(format);
}

Object Device::getNativeQueue(ObjectType objectType, CommandQueue::Enum queue){
    return getNativeQueue(objectType, getPrimaryPhysicalQueue(queue));
}

Object Device::getNativeQueue(ObjectType objectType, const GpuPhysicalQueueId& queue){
    if(objectType == ObjectTypes::s_Queue){
        Queue* q = getQueue(queue);
        return q ? Object(q->m_nativeQueue.queue) : Object(nullptr);
    }
    return Object(nullptr);
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_VULKAN_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

