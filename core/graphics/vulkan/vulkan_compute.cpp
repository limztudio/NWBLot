// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "vulkan_backend.h"

#include <logger/client/logger.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_VULKAN_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


ComputePipeline::ComputePipeline(const VulkanContext& context)
    : RefCounter<IComputePipeline>(context.threadPool)
    , m_context(context)
{}
ComputePipeline::~ComputePipeline(){
    VulkanDetail::DestroyPipelineAndOwnedLayout(
        m_context.device,
        m_context.allocationCallbacks,
        m_pipeline,
        m_pipelineLayout,
        m_ownsPipelineLayout
    );
}

Object ComputePipeline::getNativeHandle(ObjectType objectType){
    if(objectType == ObjectTypes::VK_Pipeline)
        return Object(m_pipeline);
    return Object(nullptr);
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


ComputePipelineHandle Device::createComputePipeline(const ComputePipelineDesc& desc){
    VkResult res = VK_SUCCESS;
    Alloc::ScratchArena<> scratchArena;

    auto* pso = NewArenaObject<ComputePipeline>(m_context.objectArena, m_context);
    pso->m_desc = desc;

    if(!desc.CS){
        NWB_LOGGER_ERROR(NWB_TEXT("Vulkan: Failed to create compute pipeline: compute shader is null"));
        DestroyArenaObject(m_context.objectArena, pso);
        return nullptr;
    }

    auto* cs = checked_cast<Shader*>(desc.CS.get());

    auto shaderStage = VulkanDetail::MakeVkStruct<VkPipelineShaderStageCreateInfo>(VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO);
    shaderStage.stage = VK_SHADER_STAGE_COMPUTE_BIT;
    shaderStage.module = cs->m_shaderModule;
    shaderStage.pName = cs->m_entryPointName.c_str();

    VkSpecializationInfo specInfo{};
    if(!cs->m_specializationEntries.empty()){
        specInfo.mapEntryCount = static_cast<u32>(cs->m_specializationEntries.size());
        specInfo.pMapEntries = cs->m_specializationEntries.data();
        specInfo.dataSize = cs->m_specializationData.size();
        specInfo.pData = cs->m_specializationData.data();
        shaderStage.pSpecializationInfo = &specInfo;
    }

    PipelineShaderStageVector shaderStages{ Alloc::ScratchAllocator<VkPipelineShaderStageCreateInfo>(scratchArena) };
    shaderStages.push_back(shaderStage);
    PipelineDescriptorHeapScratch descriptorHeapScratch{ scratchArena };

    if(
        !configurePipelineBindings(
        desc.bindingLayouts,
        NWB_TEXT("compute pipeline"),
        shaderStages,
        descriptorHeapScratch,
        *pso,
        scratchArena
        )
    ){
        DestroyArenaObject(m_context.objectArena, pso);
        return nullptr;
    }

    auto pipelineInfo = VulkanDetail::MakeVkStruct<VkComputePipelineCreateInfo>(VK_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO);
    if(pso->m_usesDescriptorHeap)
        pipelineInfo.pNext = descriptorHeapScratch.pNext();
    pipelineInfo.stage = shaderStages[0];
    pipelineInfo.layout = pso->m_usesDescriptorHeap ? VK_NULL_HANDLE : pso->m_pipelineLayout;

    res = vkCreateComputePipelines(m_context.device, m_context.pipelineCache, 1, &pipelineInfo, m_context.allocationCallbacks, &pso->m_pipeline);
    if(res != VK_SUCCESS){
        NWB_LOGGER_ERROR(NWB_TEXT("Vulkan: Failed to create compute pipeline: {}"), ResultToString(res));
        DestroyArenaObject(m_context.objectArena, pso);
        return nullptr;
    }

    return ComputePipelineHandle(pso, ComputePipelineHandle::deleter_type(&m_context.objectArena), AdoptRef);
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


void CommandList::setComputeState(const ComputeState& state){
    endActiveRenderPass();
    setResourceStatesForBindingSets(state.bindings);
    if(state.indirectParams)
        setBufferState(state.indirectParams, ResourceStates::IndirectArgument);
    commitBarriers();
    m_currentGraphicsState = {};
    m_currentMeshletState = {};
    m_currentRayTracingState = {};
    m_currentComputeState = state;

    auto* pipeline = checked_cast<ComputePipeline*>(state.pipeline);
    if(pipeline)
        vkCmdBindPipeline(m_currentCmdBuf->m_cmdBuf, VK_PIPELINE_BIND_POINT_COMPUTE, pipeline->m_pipeline);

    if(pipeline)
        bindPipelineBindingSets(VK_PIPELINE_BIND_POINT_COMPUTE, pipeline->m_pipelineLayout, pipeline->m_usesDescriptorHeap, pipeline->m_descriptorHeapPushRanges, pipeline->m_descriptorHeapPushDataSize, state.bindings);
}

void CommandList::dispatch(u32 groupsX, u32 groupsY, u32 groupsZ){
    if(groupsX == 0 || groupsY == 0 || groupsZ == 0)
        return;
    if(!m_currentComputeState.pipeline){
        NWB_LOGGER_ERROR(NWB_TEXT("Vulkan: Failed to dispatch compute: no compute pipeline is bound"));
        NWB_ASSERT_MSG(false, NWB_TEXT("Vulkan: Failed to dispatch compute: no compute pipeline is bound"));
        return;
    }
    const auto& limits = m_context.physicalDeviceProperties.limits;
    if(groupsX > limits.maxComputeWorkGroupCount[0] || groupsY > limits.maxComputeWorkGroupCount[1] || groupsZ > limits.maxComputeWorkGroupCount[2]){
        NWB_LOGGER_ERROR(NWB_TEXT("Vulkan: Failed to dispatch compute: group counts ({}, {}, {}) exceed device limits ({}, {}, {})")
            , groupsX
            , groupsY
            , groupsZ
            , limits.maxComputeWorkGroupCount[0]
            , limits.maxComputeWorkGroupCount[1]
            , limits.maxComputeWorkGroupCount[2]
        );
        NWB_ASSERT_MSG(false, NWB_TEXT("Vulkan: Failed to dispatch compute: group counts exceed device limits"));
        return;
    }

    vkCmdDispatch(m_currentCmdBuf->m_cmdBuf, groupsX, groupsY, groupsZ);
}

void CommandList::dispatchIndirect(u32 offsetBytes){
    if(!m_currentComputeState.pipeline){
        NWB_LOGGER_ERROR(NWB_TEXT("Vulkan: Failed to dispatch compute indirect: no compute pipeline is bound"));
        NWB_ASSERT_MSG(false, NWB_TEXT("Vulkan: Failed to dispatch compute indirect: no compute pipeline is bound"));
        return;
    }
    if(!validateIndirectBuffer(m_currentComputeState.indirectParams, offsetBytes, sizeof(DispatchIndirectArguments), 1, NWB_TEXT("dispatchIndirect")))
        return;
    auto* buffer = checked_cast<Buffer*>(m_currentComputeState.indirectParams);
    vkCmdDispatchIndirect(m_currentCmdBuf->m_cmdBuf, buffer->m_buffer, offsetBytes);
    retainResource(m_currentComputeState.indirectParams);
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_VULKAN_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

