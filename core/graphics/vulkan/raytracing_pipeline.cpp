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


VkPipelineCreateFlags2 ComputeRayTracingPipelineCreateFlags(const RayTracingPipelineDesc& desc)noexcept{
    VkPipelineCreateFlags2 flags = VK_PIPELINE_CREATE_2_DESCRIPTOR_BUFFER_BIT_EXT;
    if(desc.allowOpacityMicromaps)
        flags |= VK_PIPELINE_CREATE_2_RAY_TRACING_OPACITY_MICROMAP_BIT_EXT;
    if(desc.allowSpheres || desc.allowLinearSweptSpheres)
        flags |= VK_PIPELINE_CREATE_2_RAY_TRACING_ALLOW_SPHERES_AND_LINEAR_SWEPT_SPHERES_BIT_NV;
    return flags;
}

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


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


RayTracingPipeline::RayTracingPipeline(
    const VulkanContext& context,
    Device& device,
    const bool allowClusterAccelerationStructures
)
    : RefCounter<GraphicsResource>(context.threadPool)
    , m_desc(context.objectArena)
    , m_allowClusterAccelerationStructuresAtCreation(allowClusterAccelerationStructures)
    , m_shaderGroupHandles(context.objectArena)
    , m_shaderGroups(context.objectArena)
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

    if(!queryFeatureSupport(Feature::RayTracingPipeline)){
        NWB_LOGGER_ERROR(NWB_TEXT("Vulkan: Failed to create ray tracing pipeline: ray tracing pipeline support is unavailable"));
        return nullptr;
    }
    if(desc.allowOpacityMicromaps && !queryFeatureSupport(Feature::RayTracingOpacityMicromap)){
        NWB_LOGGER_ERROR(NWB_TEXT("Vulkan: Failed to create ray tracing pipeline: opacity micromap support is unavailable"));
        return nullptr;
    }
    if(desc.allowClusterAccelerationStructures && !queryFeatureSupport(Feature::RayTracingClusters)){
        NWB_LOGGER_ERROR(NWB_TEXT("Vulkan: Failed to create ray tracing pipeline: cluster acceleration structures are unavailable"));
        return nullptr;
    }
    if(desc.maxRecursionDepth > m_context.rayTracingPipelineProperties.maxRayRecursionDepth){
        NWB_LOGGER_ERROR(NWB_TEXT("Vulkan: Failed to create ray tracing pipeline: max recursion depth {} exceeds device limit {}")
            , desc.maxRecursionDepth
            , m_context.rayTracingPipelineProperties.maxRayRecursionDepth
        );
        return nullptr;
    }
    if(desc.allowSpheres && !queryFeatureSupport(Feature::Spheres)){
        NWB_LOGGER_ERROR(NWB_TEXT("Vulkan: Failed to create ray tracing pipeline: sphere geometry pipeline support is unavailable"));
        return nullptr;
    }
    if(desc.allowLinearSweptSpheres && !queryFeatureSupport(Feature::LinearSweptSpheres)){
        NWB_LOGGER_ERROR(NWB_TEXT("Vulkan: Failed to create ray tracing pipeline: linear swept sphere geometry pipeline support is unavailable"));
        return nullptr;
    }

    constexpr ShaderType::Mask s_GeneralShaderTypes = static_cast<ShaderType::Mask>(
        ShaderType::RayGeneration | ShaderType::Miss | ShaderType::Callable
    );
    const auto validateShader = [this](
        Shader* const shader,
        const ShaderType::Mask allowedShaderTypes,
        const tchar* const stageName
    ){
        if(!shader){
            NWB_LOGGER_ERROR(NWB_TEXT("Vulkan: Failed to create ray tracing pipeline: {} shader is null"), stageName);
            return false;
        }
        if(&shader->m_context != &m_context){
            NWB_LOGGER_ERROR(NWB_TEXT("Vulkan: Failed to create ray tracing pipeline: {} shader belongs to another device"), stageName);
            return false;
        }
        if(
            shader->m_shaderModule == VK_NULL_HANDLE
            || !VulkanDetail::IsRayTracingShaderTypeAllowed(shader->m_desc.shaderType, allowedShaderTypes)
        ){
            NWB_LOGGER_ERROR(NWB_TEXT("Vulkan: Failed to create ray tracing pipeline: {} shader has an invalid module or stage"), stageName);
            return false;
        }
        return true;
    };
    for(const auto& shaderDesc : desc.shaders){
        if(!validateShader(shaderDesc.shader.get(), s_GeneralShaderTypes, NWB_TEXT("general")))
            return nullptr;
    }
    for(const auto& hitGroup : desc.hitGroups){
        if(hitGroup.isProceduralPrimitive != static_cast<bool>(hitGroup.intersectionShader)){
            NWB_LOGGER_ERROR(NWB_TEXT("Vulkan: Failed to create ray tracing pipeline: hit-group topology does not match its intersection shader"));
            return nullptr;
        }
        if(
            (hitGroup.closestHitShader && !validateShader(hitGroup.closestHitShader.get(), ShaderType::ClosestHit, NWB_TEXT("closest-hit")))
            || (hitGroup.anyHitShader && !validateShader(hitGroup.anyHitShader.get(), ShaderType::AnyHit, NWB_TEXT("any-hit")))
            || (hitGroup.intersectionShader && !validateShader(hitGroup.intersectionShader.get(), ShaderType::Intersection, NWB_TEXT("intersection")))
        )
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

    auto* pso = NewArenaObject<RayTracingPipeline>(
        m_context.objectArena,
        m_context,
        *this,
        desc.allowClusterAccelerationStructures
    );
    if(!pso){
        NWB_LOGGER_ERROR(NWB_TEXT("Vulkan: Failed to create ray tracing pipeline: object allocation failed"));
        return nullptr;
    }
    pso->m_desc = desc;

    Alloc::ScratchArena scratchArena(VulkanArenaScope::s_RayTracingArena, s_RayTracingScratchArenaBytes);

    PipelineShaderStageVector stages{ scratchArena };
    Vector<VkRayTracingShaderGroupCreateInfoKHR, Alloc::ScratchArena> groups{ scratchArena };
    PipelineSpecializationInfoVector specInfos{ scratchArena };

    stages.reserve(maxShaderStages);
    groups.reserve(desc.shaders.size() + desc.hitGroups.size());
    specInfos.reserve(maxShaderStages);
    pso->m_shaderGroups.reserve(desc.shaders.size() + desc.hitGroups.size());

    auto addShaderSpecialization = [&](Shader* s, VkPipelineShaderStageCreateInfo& stageInfo){
        if(s->m_specializationEntries.empty())
            return;

        specInfos.push_back(s->makeSpecializationInfo());
        stageInfo.pSpecializationInfo = &specInfos.back();
    };

    for(const auto& shaderDesc : desc.shaders){
        auto* s = shaderDesc.shader.get();

        auto stageInfo = VulkanDetail::MakeVkStruct<VkPipelineShaderStageCreateInfo>(VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO);
        stageInfo.module = s->m_shaderModule;
        stageInfo.pName = s->m_entryPointName.c_str();

        ShaderTableRecordKind::Enum recordKind = ShaderTableRecordKind::Invalid;
        switch(s->m_desc.shaderType){
        case ShaderType::RayGeneration:
            stageInfo.stage = VK_SHADER_STAGE_RAYGEN_BIT_KHR;
            recordKind = ShaderTableRecordKind::RayGeneration;
            break;
        case ShaderType::Miss:
            stageInfo.stage = VK_SHADER_STAGE_MISS_BIT_KHR;
            recordKind = ShaderTableRecordKind::Miss;
            break;
        case ShaderType::Callable:
            stageInfo.stage = VK_SHADER_STAGE_CALLABLE_BIT_KHR;
            recordKind = ShaderTableRecordKind::Callable;
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

        const AStringView exportName = !shaderDesc.exportName.empty()
            ? AStringView(shaderDesc.exportName)
            : AStringView(s->m_entryPointName)
        ;
        pso->m_shaderGroups.emplace_back(m_context.objectArena);
        ShaderTableGroupMetadata& metadata = pso->m_shaderGroups.back();
        metadata.exportName.assign(exportName);
        metadata.kind = recordKind;
        metadata.groupIndex = static_cast<u32>(groups.size() - 1u);
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

        pso->m_shaderGroups.emplace_back(m_context.objectArena);
        ShaderTableGroupMetadata& metadata = pso->m_shaderGroups.back();
        metadata.exportName.assign(AStringView(hitGroup.exportName));
        metadata.kind = ShaderTableRecordKind::HitGroup;
        metadata.groupIndex = static_cast<u32>(groups.size() - 1u);
    }

    if(stages.empty() || groups.empty()){
        NWB_LOGGER_ERROR(NWB_TEXT("Vulkan: Failed to create ray tracing pipeline: no shader stages or groups were provided"));
        DestroyArenaObject(m_context.objectArena, pso);
        return nullptr;
    }
    if(pso->m_shaderGroups.size() != groups.size()){
        NWB_LOGGER_ERROR(NWB_TEXT("Vulkan: Failed to create ray tracing pipeline: shader group metadata count mismatch"));
        DestroyArenaObject(m_context.objectArena, pso);
        return nullptr;
    }
    for(usize groupIndex = 0u; groupIndex < pso->m_shaderGroups.size(); ++groupIndex){
        if(pso->m_shaderGroups[groupIndex].groupIndex == groupIndex)
            continue;

        NWB_LOGGER_ERROR(NWB_TEXT("Vulkan: Failed to create ray tracing pipeline: shader group metadata order mismatch"));
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

    const bool enablePipelineFlags2 = desc.allowSpheres || desc.allowLinearSweptSpheres;
    auto pipelineFlags2 = VulkanDetail::MakeVkStruct<VkPipelineCreateFlags2CreateInfo>(VK_STRUCTURE_TYPE_PIPELINE_CREATE_FLAGS_2_CREATE_INFO);
    pipelineFlags2.flags = VulkanDetail::ComputeRayTracingPipelineCreateFlags(desc);

    auto clusterCreateInfo = VulkanDetail::MakeVkStruct<VkRayTracingPipelineClusterAccelerationStructureCreateInfoNV>(
        VK_STRUCTURE_TYPE_RAY_TRACING_PIPELINE_CLUSTER_ACCELERATION_STRUCTURE_CREATE_INFO_NV
    );
    clusterCreateInfo.allowClusterAccelerationStructure = pso->allowsClusterAccelerationStructures() ? VK_TRUE : VK_FALSE;

    void* createInfoPNext = nullptr;
    if(pso->allowsClusterAccelerationStructures()){
        clusterCreateInfo.pNext = createInfoPNext;
        createInfoPNext = &clusterCreateInfo;
    }
    if(enablePipelineFlags2){
        pipelineFlags2.pNext = createInfoPNext;
        createInfoPNext = &pipelineFlags2;
    }

    auto createInfo = VulkanDetail::MakeVkStruct<VkRayTracingPipelineCreateInfoKHR>(VK_STRUCTURE_TYPE_RAY_TRACING_PIPELINE_CREATE_INFO_KHR);
    createInfo.pNext = createInfoPNext;
    if(!enablePipelineFlags2)
        createInfo.flags = static_cast<VkPipelineCreateFlags>(pipelineFlags2.flags);
    createInfo.stageCount = static_cast<u32>(stages.size());
    createInfo.pStages = stages.data();
    createInfo.groupCount = static_cast<u32>(groups.size());
    createInfo.pGroups = groups.data();
    createInfo.maxPipelineRayRecursionDepth = desc.maxRecursionDepth;
    createInfo.layout = pso->m_pipelineLayout;

    res = m_context.deviceDispatch.vkCreateRayTracingPipelinesKHR(m_context.device, VK_NULL_HANDLE, m_context.pipelineCache, 1, &createInfo, m_context.allocationCallbacks, &pso->m_pipeline);
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
    if(handleSize == 0 || static_cast<usize>(groupCount) > Limit<usize>::s_Max / static_cast<usize>(handleSize)){
        NWB_LOGGER_ERROR(NWB_TEXT("Vulkan: Failed to create ray tracing pipeline: shader group handle table size overflows"));
        DestroyArenaObject(m_context.objectArena, pso);
        return nullptr;
    }

    const usize shaderGroupHandleBytes = static_cast<usize>(groupCount) * static_cast<usize>(handleSize);
    pso->m_shaderGroupHandles.resize(shaderGroupHandleBytes);
    res = m_context.deviceDispatch.vkGetRayTracingShaderGroupHandlesKHR(
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


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_VULKAN_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

