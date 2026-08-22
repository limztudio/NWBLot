// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "raytracing_internal.h"
#include "arena_names.h"

#include <core/common/log.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_VULKAN_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace VulkanDetail{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


bool ComputeRayTracingHandleLayout(const VulkanContext& context, u32& outHandleSize, u32& outHandleSizeAligned, u32& outBaseAlignment, const tchar* operation){
    const u32 handleSize = context.rayTracingPipelineProperties.shaderGroupHandleSize;
    const u32 handleAlignment = context.rayTracingPipelineProperties.shaderGroupHandleAlignment;
    const u32 baseAlignment = context.rayTracingPipelineProperties.shaderGroupBaseAlignment;

    if(handleAlignment == 0 || (handleAlignment & (handleAlignment - 1u)) != 0u){
        NWB_LOGGER_ERROR(NWB_TEXT("Vulkan: Failed to {}: shader group handle alignment is invalid"), operation);
        return false;
    }
    if(baseAlignment == 0 || (baseAlignment & (baseAlignment - 1u)) != 0u){
        NWB_LOGGER_ERROR(NWB_TEXT("Vulkan: Failed to {}: shader group base alignment is invalid"), operation);
        return false;
    }
    u32 handleSizeAligned = 0;
    if(!AlignUpU32Checked(handleSize, handleAlignment, handleSizeAligned)){
        NWB_LOGGER_ERROR(NWB_TEXT("Vulkan: Failed to {}: shader group handle size alignment overflows"), operation);
        return false;
    }
    if(handleSizeAligned == 0){
        NWB_LOGGER_ERROR(NWB_TEXT("Vulkan: Failed to {}: shader group handle size is invalid"), operation);
        return false;
    }

    outHandleSize = handleSize;
    outHandleSizeAligned = handleSizeAligned;
    outBaseAlignment = baseAlignment;
    return true;
}

bool ComputeShaderTableByteSize(u32 recordCount, u32 handleSizeAligned, u32 baseAlignment, u64& outByteSize, const tchar* operation){
    if(recordCount == 0 || handleSizeAligned == 0){
        NWB_LOGGER_ERROR(NWB_TEXT("Vulkan: Failed to {}: shader table record count or stride is invalid"), operation);
        return false;
    }
    if(static_cast<u64>(recordCount) > Limit<u64>::s_Max / static_cast<u64>(handleSizeAligned)){
        NWB_LOGGER_ERROR(NWB_TEXT("Vulkan: Failed to {}: shader table size overflows"), operation);
        return false;
    }

    const u64 rawSize = static_cast<u64>(recordCount) * static_cast<u64>(handleSizeAligned);
    if(!AlignUpU64Checked(rawSize, static_cast<u64>(baseAlignment), outByteSize)){
        NWB_LOGGER_ERROR(NWB_TEXT("Vulkan: Failed to {}: shader table alignment overflows"), operation);
        return false;
    }

    return true;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


RayTracingPipeline::RayTracingPipeline(const VulkanContext& context, Device& device)
    : RefCounter<GraphicsResource>(context.threadPool)
    , m_desc(context.objectArena)
    , m_shaderGroupHandles(context.objectArena)
    , m_context(context)
    , m_device(device)
{}
RayTracingPipeline::~RayTracingPipeline(){
    VulkanDetail::DestroyPipelineResource(m_context, *this, m_pipeline);
}

Object RayTracingPipeline::getNativeHandle(ObjectType objectType){
    return VulkanDetail::GetPipelineNativeHandle(m_pipeline, objectType);
}

RayTracingPipelineHandle Device::createRayTracingPipeline(const RayTracingPipelineDesc& desc){
    VkResult res = VK_SUCCESS;

    if(!m_context.extensions.KHR_ray_tracing_pipeline){
        NWB_LOGGER_ERROR(NWB_TEXT("Vulkan: Ray tracing pipeline extension is required to create ray tracing pipelines."));
        return nullptr;
    }
    if(desc.maxRecursionDepth > m_context.rayTracingPipelineProperties.maxRayRecursionDepth){
        NWB_LOGGER_ERROR(NWB_TEXT("Vulkan: Failed to create ray tracing pipeline: max recursion depth {} exceeds device limit {}")
            , desc.maxRecursionDepth
            , m_context.rayTracingPipelineProperties.maxRayRecursionDepth
        );
        return nullptr;
    }
    if(
        desc.allowSpheres
        && (
            !m_context.extensions.NV_ray_tracing_linear_swept_spheres
            || m_context.rayTracingLinearSweptSpheresFeatures.spheres != VK_TRUE
        )
    ){
        NWB_LOGGER_ERROR(NWB_TEXT("Vulkan: Failed to create ray tracing pipeline: sphere geometry pipeline support is unavailable"));
        return nullptr;
    }
    if(
        desc.allowLinearSweptSpheres
        && (
            !m_context.extensions.NV_ray_tracing_linear_swept_spheres
            || m_context.rayTracingLinearSweptSpheresFeatures.linearSweptSpheres != VK_TRUE
        )
    ){
        NWB_LOGGER_ERROR(NWB_TEXT("Vulkan: Failed to create ray tracing pipeline: linear swept sphere geometry pipeline support is unavailable"));
        return nullptr;
    }
    if(desc.hitGroups.size() > (static_cast<usize>(-1) - desc.shaders.size()) / s_RayTracingHitGroupShaderStageCount){
        NWB_LOGGER_ERROR(NWB_TEXT("Vulkan: Failed to create ray tracing pipeline: shader stage count overflows"));
        return nullptr;
    }
    const usize maxShaderStages = desc.shaders.size() + desc.hitGroups.size() * s_RayTracingHitGroupShaderStageCount;
    if(maxShaderStages > static_cast<usize>(UINT32_MAX)){
        NWB_LOGGER_ERROR(NWB_TEXT("Vulkan: Failed to create ray tracing pipeline: shader stage count exceeds Vulkan limit"));
        return nullptr;
    }

    auto* pso = NewArenaObject<RayTracingPipeline>(m_context.objectArena, m_context, *this);
    pso->m_desc = desc;

    Alloc::ScratchArena scratchArena(VulkanArenaScope::s_RayTracingArena, s_RayTracingScratchArenaBytes);

    PipelineShaderStageVector stages{ scratchArena };
    Vector<VkRayTracingShaderGroupCreateInfoKHR, Alloc::ScratchArena> groups{ scratchArena };
    PipelineSpecializationInfoVector specInfos{ scratchArena };

    stages.reserve(maxShaderStages);
    groups.reserve(desc.shaders.size() + desc.hitGroups.size());
    specInfos.reserve(maxShaderStages);

    auto addShaderSpecialization = [&](Shader* s, VkPipelineShaderStageCreateInfo& stageInfo){
        if(s->m_specializationEntries.empty())
            return;

        specInfos.push_back(s->makeSpecializationInfo());
        stageInfo.pSpecializationInfo = &specInfos.back();
    };

    for(const auto& shaderDesc : desc.shaders){
        if(!shaderDesc.shader)
            continue;

        auto* s = shaderDesc.shader.get();

        auto stageInfo = VulkanDetail::MakeVkStruct<VkPipelineShaderStageCreateInfo>(VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO);
        stageInfo.module = s->m_shaderModule;
        stageInfo.pName = s->m_entryPointName.c_str();

        switch(s->m_desc.shaderType){
        case ShaderType::RayGeneration:
            stageInfo.stage = VK_SHADER_STAGE_RAYGEN_BIT_KHR;
            break;
        case ShaderType::Miss:
            stageInfo.stage = VK_SHADER_STAGE_MISS_BIT_KHR;
            break;
        case ShaderType::Callable:
            stageInfo.stage = VK_SHADER_STAGE_CALLABLE_BIT_KHR;
            break;
        default:
            continue;
        }
        addShaderSpecialization(s, stageInfo);

        auto group = VulkanDetail::MakeVkStruct<VkRayTracingShaderGroupCreateInfoKHR>(VK_STRUCTURE_TYPE_RAY_TRACING_SHADER_GROUP_CREATE_INFO_KHR);
        group.type = VK_RAY_TRACING_SHADER_GROUP_TYPE_GENERAL_KHR;
        group.generalShader = static_cast<u32>(stages.size());
        group.closestHitShader = VK_SHADER_UNUSED_KHR;
        group.anyHitShader = VK_SHADER_UNUSED_KHR;
        group.intersectionShader = VK_SHADER_UNUSED_KHR;

        stages.push_back(stageInfo);
        groups.push_back(group);
    }

    for(const auto& hitGroup : desc.hitGroups){
        auto group = VulkanDetail::MakeVkStruct<VkRayTracingShaderGroupCreateInfoKHR>(VK_STRUCTURE_TYPE_RAY_TRACING_SHADER_GROUP_CREATE_INFO_KHR);
        group.type = hitGroup.isProceduralPrimitive ? VK_RAY_TRACING_SHADER_GROUP_TYPE_PROCEDURAL_HIT_GROUP_KHR : VK_RAY_TRACING_SHADER_GROUP_TYPE_TRIANGLES_HIT_GROUP_KHR;
        group.generalShader = VK_SHADER_UNUSED_KHR;
        group.closestHitShader = VK_SHADER_UNUSED_KHR;
        group.anyHitShader = VK_SHADER_UNUSED_KHR;
        group.intersectionShader = VK_SHADER_UNUSED_KHR;

        if(hitGroup.closestHitShader){
            auto* s = hitGroup.closestHitShader.get();
            auto stageInfo = VulkanDetail::MakeVkStruct<VkPipelineShaderStageCreateInfo>(VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO);
            stageInfo.stage = VK_SHADER_STAGE_CLOSEST_HIT_BIT_KHR;
            stageInfo.module = s->m_shaderModule;
            stageInfo.pName = s->m_entryPointName.c_str();
            addShaderSpecialization(s, stageInfo);
            group.closestHitShader = static_cast<u32>(stages.size());
            stages.push_back(stageInfo);
        }
        if(hitGroup.anyHitShader){
            auto* s = hitGroup.anyHitShader.get();
            auto stageInfo = VulkanDetail::MakeVkStruct<VkPipelineShaderStageCreateInfo>(VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO);
            stageInfo.stage = VK_SHADER_STAGE_ANY_HIT_BIT_KHR;
            stageInfo.module = s->m_shaderModule;
            stageInfo.pName = s->m_entryPointName.c_str();
            addShaderSpecialization(s, stageInfo);
            group.anyHitShader = static_cast<u32>(stages.size());
            stages.push_back(stageInfo);
        }
        if(hitGroup.intersectionShader){
            auto* s = hitGroup.intersectionShader.get();
            auto stageInfo = VulkanDetail::MakeVkStruct<VkPipelineShaderStageCreateInfo>(VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO);
            stageInfo.stage = VK_SHADER_STAGE_INTERSECTION_BIT_KHR;
            stageInfo.module = s->m_shaderModule;
            stageInfo.pName = s->m_entryPointName.c_str();
            addShaderSpecialization(s, stageInfo);
            group.intersectionShader = static_cast<u32>(stages.size());
            stages.push_back(stageInfo);
        }
        groups.push_back(group);
    }

    if(stages.empty() || groups.empty()){
        NWB_LOGGER_ERROR(NWB_TEXT("Vulkan: Failed to create ray tracing pipeline: no shader stages or groups were provided"));
        DestroyArenaObject(m_context.objectArena, pso);
        return nullptr;
    }

    if(!configurePipelineBindingsOrDestroy(
        desc.globalBindingLayouts,
        NWB_TEXT("ray tracing pipeline"),
        pso,
        scratchArena
    ))
        return nullptr;

    const bool enableSpherePipelineFlags = desc.allowSpheres || desc.allowLinearSweptSpheres;
    auto pipelineFlags2 = VulkanDetail::MakeVkStruct<VkPipelineCreateFlags2CreateInfo>(VK_STRUCTURE_TYPE_PIPELINE_CREATE_FLAGS_2_CREATE_INFO);
    if(enableSpherePipelineFlags)
        pipelineFlags2.flags |= VK_PIPELINE_CREATE_2_RAY_TRACING_ALLOW_SPHERES_AND_LINEAR_SWEPT_SPHERES_BIT_NV;

    auto createInfo = VulkanDetail::MakeVkStruct<VkRayTracingPipelineCreateInfoKHR>(VK_STRUCTURE_TYPE_RAY_TRACING_PIPELINE_CREATE_INFO_KHR);
    if(pipelineFlags2.flags != 0)
        createInfo.pNext = &pipelineFlags2;
    createInfo.flags |= VK_PIPELINE_CREATE_DESCRIPTOR_BUFFER_BIT_EXT;
    createInfo.stageCount = static_cast<u32>(stages.size());
    createInfo.pStages = stages.data();
    createInfo.groupCount = static_cast<u32>(groups.size());
    createInfo.pGroups = groups.data();
    createInfo.maxPipelineRayRecursionDepth = desc.maxRecursionDepth;
    createInfo.layout = pso->m_pipelineLayout;

    res = vkCreateRayTracingPipelinesKHR(m_context.device, VK_NULL_HANDLE, m_context.pipelineCache, 1, &createInfo, m_context.allocationCallbacks, &pso->m_pipeline);
    if(res != VK_SUCCESS){
        NWB_LOGGER_ERROR(NWB_TEXT("Vulkan: Failed to create ray tracing pipeline: {}"), ResultToString(res));
        DestroyArenaObject(m_context.objectArena, pso);
        return nullptr;
    }

    u32 handleSize = 0;
    u32 handleSizeAligned = 0;
    u32 baseAlignment = 0;
    if(!VulkanDetail::ComputeRayTracingHandleLayout(m_context, handleSize, handleSizeAligned, baseAlignment, NWB_TEXT("create ray tracing pipeline"))){
        DestroyArenaObject(m_context.objectArena, pso);
        return nullptr;
    }

    u32 groupCount = static_cast<u32>(groups.size());
    if(handleSizeAligned == 0 || static_cast<usize>(groupCount) > Limit<usize>::s_Max / static_cast<usize>(handleSizeAligned)){
        NWB_LOGGER_ERROR(NWB_TEXT("Vulkan: Failed to create ray tracing pipeline: shader group handle table size overflows"));
        DestroyArenaObject(m_context.objectArena, pso);
        return nullptr;
    }

    const usize shaderGroupHandleBytes = static_cast<usize>(groupCount) * static_cast<usize>(handleSize);
    pso->m_shaderGroupHandles.resize(shaderGroupHandleBytes);
    res = vkGetRayTracingShaderGroupHandlesKHR(
        m_context.device,
        pso->m_pipeline,
        0,
        groupCount,
        pso->m_shaderGroupHandles.size(),
        pso->m_shaderGroupHandles.data()
    );
    if(res != VK_SUCCESS){
        NWB_LOGGER_ERROR(NWB_TEXT("Vulkan: Failed to retrieve ray tracing shader group handles: {}"), ResultToString(res));
        DestroyArenaObject(m_context.objectArena, pso);
        return nullptr;
    }

    return RayTracingPipelineHandle(pso, RayTracingPipelineHandle::deleter_type(&m_context.objectArena), AdoptRef);
}


ShaderTable::ShaderTable(const VulkanContext& context, Device& device)
    : RefCounter<GraphicsResource>(context.threadPool)
    , m_context(context)
    , m_device(device)
{}
ShaderTable::~ShaderTable(){}

RayTracingShaderTableHandle RayTracingPipeline::createShaderTable(){
    auto* sbt = NewArenaObject<ShaderTable>(m_context.objectArena, m_context, m_device);
    sbt->m_pipeline = Handle<RayTracingPipeline>(this, Handle<RayTracingPipeline>::deleter_type(&m_context.objectArena));
    return RayTracingShaderTableHandle(sbt, RayTracingShaderTableHandle::deleter_type(&m_context.objectArena), AdoptRef);
}

u32 ShaderTable::findGroupIndex(const AStringView exportName)const{
    if(!m_pipeline)
        return UINT32_MAX;

    u32 groupIndex = 0;
    for(const auto& shaderDesc : m_pipeline->m_desc.shaders){
        if(!shaderDesc.shader)
            continue;

        const ShaderDesc& desc = shaderDesc.shader->getDescription();
        switch(desc.shaderType){
        case ShaderType::RayGeneration:
        case ShaderType::Miss:
        case ShaderType::Callable:
            break;
        default:
            continue;
        }

        const AStringView shaderExportName = !shaderDesc.exportName.empty()
            ? AStringView(shaderDesc.exportName)
            : AStringView(desc.entryName)
        ;
        if(shaderExportName == exportName)
            return groupIndex;
        ++groupIndex;
    }
    for(const auto& hitGroup : m_pipeline->m_desc.hitGroups){
        if(AStringView(hitGroup.exportName) == exportName)
            return groupIndex;
        ++groupIndex;
    }
    return UINT32_MAX;
}

u32 ShaderTable::appendShaderRecord(
    const AStringView exportName,
    BufferHandle& buffer,
    u64& offset,
    u32& count,
    const tchar* operationName,
    const tchar* recordName,
    const tchar* exportKind
){
    if(count == Limit<u32>::s_Max){
        NWB_LOGGER_ERROR(NWB_TEXT("Vulkan: Failed to {}: shader table record count exceeds u32 range"), operationName);
        NWB_ASSERT_MSG(false, NWB_TEXT("Vulkan: Failed to add shader table record: record count exceeds u32 range"));
        return count;
    }
    if(!m_pipeline){
        const u32 recordIndex = count;
        ++count;
        return recordIndex;
    }

    u32 handleSize = 0;
    u32 handleSizeAligned = 0;
    u32 baseAlignment = 0;
    if(!VulkanDetail::ComputeRayTracingHandleLayout(m_context, handleSize, handleSizeAligned, baseAlignment, operationName))
        return count;

    const u32 recordIndex = count;
    const u32 newCount = recordIndex + 1;
    u64 sbtSize = 0;
    if(!VulkanDetail::ComputeShaderTableByteSize(newCount, handleSizeAligned, baseAlignment, sbtSize, operationName))
        return count;

    BufferHandle newBuffer;
    allocateSBTBuffer(newBuffer, sbtSize);
    if(!newBuffer){
        NWB_LOGGER_ERROR(NWB_TEXT("Vulkan: Failed to allocate {} SBT buffer"), recordName);
        NWB_ASSERT_MSG(false, NWB_TEXT("Vulkan: Failed to allocate shader table buffer"));
        return count;
    }

    void* mapped = m_device.mapBuffer(newBuffer.get(), CpuAccessMode::Write);
    if(!mapped){
        NWB_LOGGER_ERROR(NWB_TEXT("Vulkan: Failed to map {} SBT buffer"), recordName);
        NWB_ASSERT_MSG(false, NWB_TEXT("Vulkan: Failed to map shader table buffer"));
        return count;
    }

    NWB_MEMSET(mapped, 0, static_cast<usize>(sbtSize));

    if(buffer && count > 0){
        void* oldMapped = m_device.mapBuffer(buffer.get(), CpuAccessMode::Read);
        if(!oldMapped){
            NWB_LOGGER_ERROR(NWB_TEXT("Vulkan: Failed to map previous {} SBT buffer"), recordName);
            NWB_ASSERT_MSG(false, NWB_TEXT("Vulkan: Failed to map previous shader table buffer"));
            m_device.unmapBuffer(newBuffer.get());
            return count;
        }
        const usize copySize = static_cast<usize>(count) * handleSizeAligned;
        VulkanDetail::CopyHostMemory(taskPool(), mapped, oldMapped, copySize);
        m_device.unmapBuffer(buffer.get());
    }

    const u32 groupIndex = findGroupIndex(exportName);
    if(groupIndex == UINT32_MAX){
        m_device.unmapBuffer(newBuffer.get());
        NWB_LOGGER_ERROR(NWB_TEXT("Vulkan: {} export not found in pipeline"), exportKind);
        NWB_ASSERT_MSG(false, NWB_TEXT("Vulkan: Shader table export not found in pipeline"));
        return count;
    }

    auto* dst = static_cast<u8*>(mapped) + recordIndex * handleSizeAligned;
    const u8* handle = m_pipeline->m_shaderGroupHandles.data() + static_cast<usize>(groupIndex) * handleSize;
    NWB_MEMCPY(dst, handleSizeAligned, handle, handleSize);
    m_device.unmapBuffer(newBuffer.get());

    buffer = newBuffer;
    offset = 0;
    ++count;
    return recordIndex;
}

void ShaderTable::allocateSBTBuffer(BufferHandle& outBuffer, u64 sbtSize){
    BufferDesc bufferDesc;
    bufferDesc.byteSize = sbtSize;
    bufferDesc.debugName = "SBT_Buffer";
    bufferDesc.isShaderBindingTable = true;
    bufferDesc.cpuAccess = CpuAccessMode::Write;

    outBuffer = m_device.createBuffer(bufferDesc);
}

void ShaderTable::setRayGenerationShader(const AStringView exportName){
    if(!m_pipeline){
        NWB_LOGGER_ERROR(NWB_TEXT("Vulkan: Failed to set ray generation shader: shader table has no pipeline"));
        return;
    }

    u32 handleSize = 0;
    u32 handleSizeAligned = 0;
    u32 baseAlignment = 0;
    if(!VulkanDetail::ComputeRayTracingHandleLayout(m_context, handleSize, handleSizeAligned, baseAlignment, NWB_TEXT("set ray generation shader")))
        return;
    u64 sbtSize = 0;
    if(!VulkanDetail::ComputeShaderTableByteSize(1, handleSizeAligned, baseAlignment, sbtSize, NWB_TEXT("set ray generation shader")))
        return;

    u32 groupIndex = findGroupIndex(exportName);
    if(groupIndex == UINT32_MAX){
        NWB_LOGGER_ERROR(NWB_TEXT("Vulkan: Ray generation export not found in pipeline"));
        NWB_ASSERT_MSG(false, NWB_TEXT("Vulkan: Ray generation export not found in pipeline"));
        return;
    }

    allocateSBTBuffer(m_raygenBuffer, sbtSize);
    if(!m_raygenBuffer){
        NWB_LOGGER_ERROR(NWB_TEXT("Vulkan: Failed to allocate ray generation SBT buffer"));
        return;
    }

    m_raygenOffset = 0;

    void* mapped = m_device.mapBuffer(m_raygenBuffer.get(), CpuAccessMode::Write);
    if(!mapped){
        NWB_LOGGER_ERROR(NWB_TEXT("Vulkan: Failed to map ray generation SBT buffer"));
        NWB_ASSERT_MSG(false, NWB_TEXT("Vulkan: Failed to map ray generation SBT buffer"));
        return;
    }

    NWB_MEMSET(mapped, 0, static_cast<usize>(sbtSize));

    const u8* handle = m_pipeline->m_shaderGroupHandles.data() + static_cast<usize>(groupIndex) * handleSize;
    NWB_MEMCPY(mapped, handleSizeAligned, handle, handleSize);
    m_device.unmapBuffer(m_raygenBuffer.get());
}

u32 ShaderTable::addMissShader(const AStringView exportName){
    return appendShaderRecord(exportName, m_missBuffer, m_missOffset, m_missCount, NWB_TEXT("add miss shader"), NWB_TEXT("miss"), NWB_TEXT("Miss shader"));
}

u32 ShaderTable::addHitGroup(const AStringView exportName){
    return appendShaderRecord(exportName, m_hitBuffer, m_hitOffset, m_hitCount, NWB_TEXT("add hit group"), NWB_TEXT("hit"), NWB_TEXT("Hit group"));
}

u32 ShaderTable::addCallableShader(const AStringView exportName){
    return appendShaderRecord(exportName, m_callableBuffer, m_callableOffset, m_callableCount, NWB_TEXT("add callable shader"), NWB_TEXT("callable"), NWB_TEXT("Callable shader"));
}

void ShaderTable::clearMissShaders(){ m_missCount = 0; m_missBuffer = nullptr; }
void ShaderTable::clearHitShaders(){ m_hitCount = 0; m_hitBuffer = nullptr; }
void ShaderTable::clearCallableShaders(){ m_callableCount = 0; m_callableBuffer = nullptr; }

Object ShaderTable::getNativeHandle(ObjectType objectType){
    if(objectType == ObjectTypes::VK_Buffer && m_raygenBuffer){
        auto* buf = m_raygenBuffer.get();
        return Object(buf->m_buffer);
    }
    return Object{nullptr};
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_VULKAN_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

