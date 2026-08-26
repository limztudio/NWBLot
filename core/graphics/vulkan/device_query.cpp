// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "backend.h"
#include "arena_names.h"
#include "raytracing_internal.h"

#include <core/common/log.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_VULKAN_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace __hidden_device_query{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


[[nodiscard]] VulkanDetail::RayTracingCapabilityInputs CollectRayTracingCapabilityInputs(const VulkanContext& context)noexcept{
    VulkanDetail::RayTracingCapabilityInputs inputs;
    inputs.accelerationStructureExtensionEnabled = context.extensions.KHR_acceleration_structure;
    inputs.accelerationStructureFeatureEnabled = context.accelerationStructureFeatureEnabled;
    inputs.createAccelerationStructureEntryPointAvailable = vkCreateAccelerationStructureKHR != nullptr;
    inputs.destroyAccelerationStructureEntryPointAvailable = vkDestroyAccelerationStructureKHR != nullptr;
    inputs.getAccelerationStructureBuildSizesEntryPointAvailable = vkGetAccelerationStructureBuildSizesKHR != nullptr;
    inputs.getAccelerationStructureDeviceAddressEntryPointAvailable = vkGetAccelerationStructureDeviceAddressKHR != nullptr;
    inputs.cmdBuildAccelerationStructuresEntryPointAvailable = vkCmdBuildAccelerationStructuresKHR != nullptr;

    inputs.rayTracingPipelineExtensionEnabled = context.extensions.KHR_ray_tracing_pipeline;
    inputs.rayTracingPipelineFeatureEnabled = context.rayTracingPipelineFeatureEnabled;
    inputs.createRayTracingPipelinesEntryPointAvailable = vkCreateRayTracingPipelinesKHR != nullptr;
    inputs.getRayTracingShaderGroupHandlesEntryPointAvailable = vkGetRayTracingShaderGroupHandlesKHR != nullptr;
    inputs.cmdTraceRaysEntryPointAvailable = vkCmdTraceRaysKHR != nullptr;

    inputs.opacityMicromapExtensionEnabled = context.extensions.EXT_opacity_micromap;
    inputs.opacityMicromapFeatureEnabled = context.opacityMicromapFeatureEnabled;
    inputs.synchronization2ExtensionEnabled = context.extensions.KHR_synchronization2;
    inputs.createMicromapEntryPointAvailable = vkCreateMicromapEXT != nullptr;
    inputs.destroyMicromapEntryPointAvailable = vkDestroyMicromapEXT != nullptr;
    inputs.getMicromapBuildSizesEntryPointAvailable = vkGetMicromapBuildSizesEXT != nullptr;
    inputs.cmdBuildMicromapsEntryPointAvailable = vkCmdBuildMicromapsEXT != nullptr;
    return inputs;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


bool Device::queryFeatureSupport(Feature::Enum feature, void* featureInfo, usize featureInfoSize){
    const VulkanDetail::RayTracingCapabilityInputs rayTracingCapabilities =
        __hidden_device_query::CollectRayTracingCapabilityInputs(m_context);

    switch(feature){
    case Feature::DeferredCommandLists:
        return true;
    case Feature::RayTracingAccelStruct:
        return VulkanDetail::SupportsRayTracingAccelStruct(rayTracingCapabilities);
    case Feature::RayTracingPipeline:
        return VulkanDetail::SupportsRayTracingPipeline(rayTracingCapabilities);
    case Feature::RayQuery:
        return
            m_context.extensions.KHR_ray_query
            && m_context.rayQueryFeatureEnabled
            && VulkanDetail::SupportsRayTracingAccelStruct(rayTracingCapabilities)
        ;
    case Feature::ShaderExecutionReordering:
        return
            (m_context.extensions.EXT_ray_tracing_invocation_reorder && m_context.rayTracingInvocationReorderExtFeatureEnabled)
            || (m_context.extensions.NV_ray_tracing_invocation_reorder && m_context.rayTracingInvocationReorderFeatureEnabled)
        ;
    case Feature::Spheres:
        return
            m_context.extensions.NV_ray_tracing_linear_swept_spheres
            && m_context.rayTracingLinearSweptSpheresFeatures.spheres == VK_TRUE
            && queryFeatureSupport(Feature::RayTracingPipeline)
        ;
    case Feature::LinearSweptSpheres:
        return
            m_context.extensions.NV_ray_tracing_linear_swept_spheres
            && m_context.rayTracingLinearSweptSpheresFeatures.linearSweptSpheres == VK_TRUE
            && queryFeatureSupport(Feature::RayTracingPipeline)
        ;
    case Feature::RayTracingOpacityMicromap:
        return VulkanDetail::SupportsRayTracingOpacityMicromap(rayTracingCapabilities);
    case Feature::RayTracingClusters:
        return
            m_context.extensions.NV_cluster_acceleration_structure
            && m_context.clusterAccelerationStructureFeatureEnabled
            && VulkanDetail::SupportsRayTracingPipeline(rayTracingCapabilities)
            && vkGetClusterAccelerationStructureBuildSizesNV
            && vkCmdBuildClusterAccelerationStructureIndirectNV
        ;
    case Feature::SamplerFeedback:
    case Feature::VirtualResources:
        // Retained unsupported feature ordinal for ABI compatibility.
        return false;
    case Feature::CooperativeVectorInferencing:
        return
            m_context.extensions.NV_cooperative_vector
            && m_context.coopVecFeatures.cooperativeVector == VK_TRUE
            && vkGetPhysicalDeviceCooperativeVectorPropertiesNV
            && vkConvertCooperativeVectorMatrixNV
            && vkCmdConvertCooperativeVectorMatrixNV
        ;
    case Feature::CooperativeVectorTraining:
        return
            m_context.extensions.NV_cooperative_vector
            && m_context.coopVecFeatures.cooperativeVector == VK_TRUE
            && m_context.coopVecFeatures.cooperativeVectorTraining == VK_TRUE
            && vkGetPhysicalDeviceCooperativeVectorPropertiesNV
            && vkConvertCooperativeVectorMatrixNV
            && vkCmdConvertCooperativeVectorMatrixNV
        ;
    case Feature::Meshlets:
        return m_context.extensions.EXT_mesh_shader && m_context.meshShaderFeatures.meshShader == VK_TRUE && vkCmdDrawMeshTasksEXT;
    case Feature::VariableRateShading:
        return m_context.extensions.KHR_fragment_shading_rate;
    case Feature::WaveLaneCountMinMax:{
        auto* out = static_cast<WaveLaneCountMinMaxFeatureInfo*>(featureInfo);
        if(out && featureInfoSize >= sizeof(WaveLaneCountMinMaxFeatureInfo)){
            out->minWaveLaneCount = m_context.subgroupProperties.subgroupSize;
            out->maxWaveLaneCount = m_context.subgroupProperties.subgroupSize;
        }
        return true;
    }
    case Feature::ConstantBufferRanges:
        return true;
    default:
        return false;
    }
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
    const VkResult res = vkGetPhysicalDeviceImageFormatProperties2(
        m_context.physicalDevice,
        &imageFormatInfo,
        &imageFormatProperties
    );
    if(res == VK_SUCCESS)
        return true;

    if(res != VK_ERROR_FORMAT_NOT_SUPPORTED){
        NWB_LOGGER_WARNING(
            NWB_TEXT("Vulkan: Failed to probe sampled texture format {}: {}"),
            StringConvert(GetFormatInfo(format).name),
            ResultToString(res)
        );
    }
    return false;
}

FormatSupport::Mask Device::queryFormatSupportUncached(const Format::Enum format)const{
    if(Format::IsASTCHdrFormat(format) && !m_context.extensions.EXT_texture_compression_astc_hdr)
        return FormatSupport::None;

    const VkFormat vkFormat = ConvertFormat(format);
    if(vkFormat == VK_FORMAT_UNDEFINED)
        return FormatSupport::None;

    VkFormatProperties props;
    vkGetPhysicalDeviceFormatProperties(m_context.physicalDevice, vkFormat, &props);

    FormatSupport::Mask support = FormatSupport::None;

    VkFormatFeatureFlags features = props.optimalTilingFeatures;

    if(features & VK_FORMAT_FEATURE_SAMPLED_IMAGE_BIT)
        support |= FormatSupport::Texture;
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
        if(
            (support & FormatSupport::Texture) == FormatSupport::Texture
            && !canCreateSampledTextureFormat(format)
        ){
            support &= ~requiredReadableSupport;
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
    if(objectType == ObjectTypes::VK_Queue){
        Queue* q = getQueue(queue);
        return q ? Object(q->m_queue) : Object(nullptr);
    }
    return Object(nullptr);
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


Heap::Heap(const VulkanContext& context, VulkanAllocator& allocator)
    : RefCounter<GraphicsResource>(context.threadPool)
    , m_bindingReservations(context.objectArena)
    , m_context(context)
    , m_allocator(allocator)
{}
Heap::~Heap(){
    {
        ScopedLock lock(m_bindingMutex);
        if(!m_bindingReservations.empty()){
            NWB_LOGGER_ERROR(NWB_TEXT("Vulkan: Refusing to free a heap with live placed-resource bindings"));
            NWB_ASSERT_MSG(false, NWB_TEXT("Vulkan: Heap destroyed with live placed-resource bindings"));
            return;
        }
    }
    m_allocator.freeHeap(*this);
}

Object Heap::getNativeHandle(ObjectType objectType){
    if(objectType == ObjectTypes::VK_DeviceMemory)
        return Object(m_memory);
    return Object(nullptr);
}

void Heap::eraseBindingReservationLocked(const void* owner){
    for(auto reservation = m_bindingReservations.begin(); reservation != m_bindingReservations.end(); ++reservation){
        if(reservation->owner != owner)
            continue;

        m_bindingReservations.erase(reservation);
        return;
    }

    NWB_LOGGER_ERROR(NWB_TEXT("Vulkan: Failed to unregister a placed-resource heap binding"));
    NWB_ASSERT_MSG(false, NWB_TEXT("Vulkan: Missing placed-resource heap binding"));
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


HeapHandle Device::createHeap(const HeapDesc& d){
    VkResult res = VK_SUCCESS;

    if(d.capacity == 0){
        NWB_LOGGER_ERROR(NWB_TEXT("Vulkan: Failed to create heap: capacity is zero"));
        NWB_ASSERT_MSG(false, NWB_TEXT("Vulkan: Failed to create heap: capacity is zero"));
        return nullptr;
    }

    switch(d.type){
    case HeapType::DeviceLocal:
    case HeapType::Upload:
    case HeapType::Readback:
        break;
    default:
        NWB_LOGGER_ERROR(NWB_TEXT("Vulkan: Failed to create heap: invalid heap type"));
        NWB_ASSERT_MSG(false, NWB_TEXT("Vulkan: Failed to create heap: invalid heap type"));
        return nullptr;
    }

    auto* heap = NewArenaObject<Heap>(m_context.objectArena, m_context, m_allocator);
    heap->m_desc = d;

    res = m_allocator.allocateHeap(*heap);
    if(res != VK_SUCCESS){
        NWB_LOGGER_ERROR(NWB_TEXT("Vulkan: Failed to allocate heap memory ({} bytes): {}"), d.capacity, ResultToString(res));
        DestroyArenaObject(m_context.objectArena, heap);
        return nullptr;
    }

    return HeapHandle(heap, HeapHandle::deleter_type(&m_context.objectArena), AdoptRef);
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


CooperativeVectorDeviceFeatures Device::queryCoopVecFeatures(){
    VkResult res = VK_SUCCESS;

    CooperativeVectorDeviceFeatures output(m_context.objectArena);

    if(!m_context.extensions.NV_cooperative_vector || !m_context.coopVecFeatures.cooperativeVector)
        return output;

    uint32_t propertyCount = 0;
    res = vkGetPhysicalDeviceCooperativeVectorPropertiesNV(m_context.physicalDevice, &propertyCount, nullptr);
    if(res != VK_SUCCESS || propertyCount == 0)
        return output;

    Alloc::ScratchArena scratchArena(VulkanArenaScope::s_CooperativeVectorQueryArena);
    Vector<VkCooperativeVectorPropertiesNV, Alloc::ScratchArena> properties(propertyCount, scratchArena);
    for(u32 i = 0; i < propertyCount; ++i){
        properties[i].sType = VK_STRUCTURE_TYPE_COOPERATIVE_VECTOR_PROPERTIES_NV;
        properties[i].pNext = nullptr;
    }

    res = vkGetPhysicalDeviceCooperativeVectorPropertiesNV(m_context.physicalDevice, &propertyCount, properties.data());
    if(res != VK_SUCCESS)
        return output;

    output.matMulFormats.resize(propertyCount);
    auto fillMatMulFormat = [&](usize i){
        const auto& prop = properties[i];
        CooperativeVectorMatMulFormatCombo& combo = output.matMulFormats[i];
        combo.inputType = VulkanDetail::ConvertCoopVecDataType(static_cast<VkComponentTypeKHR>(prop.inputType));
        combo.inputInterpretation = VulkanDetail::ConvertCoopVecDataType(static_cast<VkComponentTypeKHR>(prop.inputInterpretation));
        combo.matrixInterpretation = VulkanDetail::ConvertCoopVecDataType(static_cast<VkComponentTypeKHR>(prop.matrixInterpretation));
        combo.biasInterpretation = VulkanDetail::ConvertCoopVecDataType(static_cast<VkComponentTypeKHR>(prop.biasInterpretation));
        combo.outputType = VulkanDetail::ConvertCoopVecDataType(static_cast<VkComponentTypeKHR>(prop.resultType));
        combo.transposeSupported = prop.transpose != VK_FALSE;
    };

    if(taskPool().isParallelEnabled() && propertyCount >= s_ParallelCoopVecThreshold)
        scheduleParallelFor(static_cast<usize>(0), propertyCount, fillMatMulFormat);
    else{
        for(usize i = 0; i < propertyCount; ++i)
            fillMatMulFormat(i);
    }

    output.trainingFloat16 =
        m_context.coopVecFeatures.cooperativeVectorTraining != VK_FALSE
        && m_context.coopVecProperties.cooperativeVectorTrainingFloat16Accumulation != VK_FALSE
    ;
    output.trainingFloat32 =
        m_context.coopVecFeatures.cooperativeVectorTraining != VK_FALSE
        && m_context.coopVecProperties.cooperativeVectorTrainingFloat32Accumulation != VK_FALSE
    ;

    return output;
}

usize Device::getCoopVecMatrixSize(CooperativeVectorDataType::Enum type, CooperativeVectorMatrixLayout::Enum layout, i32 rows, i32 columns){
    VkResult res = VK_SUCCESS;

    if(
        type > CooperativeVectorDataType::Float64
        || layout > CooperativeVectorMatrixLayout::TrainingOptimal
        || type == CooperativeVectorDataType::UInt8Packed
        || type == CooperativeVectorDataType::SInt8Packed
        || rows <= 0
        || columns <= 0
    )
        return 0;
    if(
        (type == CooperativeVectorDataType::FloatE4M3 || type == CooperativeVectorDataType::FloatE5M2)
        && (layout == CooperativeVectorMatrixLayout::RowMajor || layout == CooperativeVectorMatrixLayout::ColumnMajor)
    )
        return 0;
    if(
        !m_context.extensions.NV_cooperative_vector
        || m_context.coopVecFeatures.cooperativeVector != VK_TRUE
        || !vkGetPhysicalDeviceCooperativeVectorPropertiesNV
        || !vkConvertCooperativeVectorMatrixNV
    )
        return 0;

    const VkComponentTypeKHR componentType = VulkanDetail::ConvertCoopVecDataType(type);
    if(componentType != VK_COMPONENT_TYPE_FLOAT32_KHR){
        uint32_t propertyCount = 0u;
        res = vkGetPhysicalDeviceCooperativeVectorPropertiesNV(m_context.physicalDevice, &propertyCount, nullptr);
        if(res != VK_SUCCESS || propertyCount == 0u)
            return 0;

        Alloc::ScratchArena scratchArena(VulkanArenaScope::s_CooperativeVectorQueryArena);
        Vector<VkCooperativeVectorPropertiesNV, Alloc::ScratchArena> properties(propertyCount, scratchArena);
        for(VkCooperativeVectorPropertiesNV& property : properties){
            property.sType = VK_STRUCTURE_TYPE_COOPERATIVE_VECTOR_PROPERTIES_NV;
            property.pNext = nullptr;
        }

        res = vkGetPhysicalDeviceCooperativeVectorPropertiesNV(m_context.physicalDevice, &propertyCount, properties.data());
        if(res != VK_SUCCESS)
            return 0;

        bool matrixInterpretationSupported = false;
        for(u32 i = 0u; i < propertyCount; ++i){
            if(properties[i].matrixInterpretation == componentType){
                matrixInterpretationSupported = true;
                break;
            }
        }
        if(!matrixInterpretationSupported)
            return 0;
    }

    usize dstSize = 0;
    const usize dataTypeSize = GetCooperativeVectorDataTypeSize(type);
    const usize rowCount = static_cast<usize>(rows);
    const usize columnCount = static_cast<usize>(columns);
    usize rowByteSize = 0u;
    if(!TryMultiply<usize>(columnCount, dataTypeSize, rowByteSize))
        return 0;

    const usize srcStride = GetCooperativeVectorOptimalMatrixStride(
        type,
        CooperativeVectorMatrixLayout::RowMajor,
        static_cast<u32>(rows),
        static_cast<u32>(columns)
    );
    if(srcStride == 0u)
        return 0;

    usize precedingRowsByteSize = 0u;
    if(!TryMultiply<usize>(rowCount - 1u, srcStride, precedingRowsByteSize))
        return 0;
    if(AddOverflows<usize>(precedingRowsByteSize, rowByteSize))
        return 0;
    const usize srcSize = precedingRowsByteSize + rowByteSize;

    const usize dstStride = GetCooperativeVectorOptimalMatrixStride(
        type,
        layout,
        static_cast<u32>(rows),
        static_cast<u32>(columns)
    );
    if(
        (layout == CooperativeVectorMatrixLayout::RowMajor || layout == CooperativeVectorMatrixLayout::ColumnMajor)
        && dstStride == 0u
    )
        return 0;

    auto convertInfo = VulkanDetail::MakeVkStruct<VkConvertCooperativeVectorMatrixInfoNV>(VK_STRUCTURE_TYPE_CONVERT_COOPERATIVE_VECTOR_MATRIX_INFO_NV);
    convertInfo.srcSize = srcSize;
    convertInfo.srcData.hostAddress = nullptr;
    convertInfo.pDstSize = &dstSize;
    convertInfo.dstData.hostAddress = nullptr;
    convertInfo.srcComponentType = componentType;
    convertInfo.dstComponentType = convertInfo.srcComponentType;
    convertInfo.numRows = static_cast<u32>(rows);
    convertInfo.numColumns = static_cast<u32>(columns);
    convertInfo.srcLayout = VK_COOPERATIVE_VECTOR_MATRIX_LAYOUT_ROW_MAJOR_NV;
    convertInfo.srcStride = srcStride;
    convertInfo.dstLayout = VulkanDetail::ConvertCoopVecMatrixLayout(layout);
    convertInfo.dstStride = dstStride;

    res = vkConvertCooperativeVectorMatrixNV(m_context.device, &convertInfo);
    if(res == VK_SUCCESS)
        return dstSize;

    return 0;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_VULKAN_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

