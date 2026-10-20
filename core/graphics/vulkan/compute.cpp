// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "backend.h"
#include "arena_names.h"

#include <core/common/log.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_VULKAN_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace __hidden_compute{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


static constexpr TStringView s_NoComputePipelineBoundReason = NWB_TEXT("no compute pipeline is bound");


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


ComputePipeline::ComputePipeline(const VulkanContext& context)noexcept
    : RefCounter<GraphicsResource>(context.cpuScheduler)
    , m_context(context)
{}
ComputePipeline::~ComputePipeline()noexcept{
    VulkanDetail::DestroyPipelineResource(m_context, *this, m_pipeline);
}

Object ComputePipeline::getNativeHandle(ObjectType objectType)noexcept{
    return VulkanDetail::GetPipelineNativeHandle(m_pipeline, objectType);
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


Expected<ComputePipelineHandle> Device::createComputePipeline(const ComputePipelineDesc& desc){
    constexpr TStringView s_OperationName = NWB_TEXT("compute pipeline");
    if(!desc.computeShader){
        NWB_LOGGER_ERROR(NWB_TEXT("Vulkan: Failed to create compute pipeline: compute shader is null"));
        return MakeUnexpected(Failure{});
    }

    auto* cs = desc.computeShader.get();
    if(&cs->m_context != &m_context){
        NWB_LOGGER_ERROR(NWB_TEXT("Vulkan: Failed to create compute pipeline: compute shader belongs to another device"));
        return MakeUnexpected(Failure{});
    }
    if(cs->m_shaderModule == VK_NULL_HANDLE || cs->m_desc.shaderType != ShaderType::Compute){
        NWB_LOGGER_ERROR(NWB_TEXT("Vulkan: Failed to create compute pipeline: compute shader has an invalid module or stage"));
        return MakeUnexpected(Failure{});
    }

    Alloc::ScratchArena scratchArena(VulkanArenaScope::s_ComputePipelineArena);

    ComputePipelineHandle pso(NewArenaObject<ComputePipeline>(m_context.objectArena, m_context), ComputePipelineHandle::deleter_type(&m_context.objectArena), s_AdoptRef);
    if(!pso)
        return MakeUnexpected(Failure{});
    pso->m_desc = desc;

    auto shaderStage = VulkanDetail::MakeVkStruct<VkPipelineShaderStageCreateInfo>(VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO);
    shaderStage.stage = VK_SHADER_STAGE_COMPUTE_BIT;
    shaderStage.module = cs->m_shaderModule;
    shaderStage.pName = cs->m_entryPointName.data();

    VkSpecializationInfo specInfo{};
    if(!cs->m_specializationEntries.empty()){
        specInfo = cs->makeSpecializationInfo();
        shaderStage.pSpecializationInfo = &specInfo;
    }

    PipelineShaderStageVector shaderStages{ scratchArena };
    shaderStages.push_back(shaderStage);

    if(!configurePipelineBindings(desc.bindingLayouts, s_OperationName, *pso, scratchArena))
        return MakeUnexpected(Failure{});

    auto pipelineInfo = VulkanDetail::MakeVkStruct<VkComputePipelineCreateInfo>(VK_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO);
    pipelineInfo.stage = shaderStages[0];
    VulkanDetail::AttachPipelineBindingState(pipelineInfo, *pso);

    if(!createPipeline(s_OperationName, *pso, pipelineInfo))
        return MakeUnexpected(Failure{});

    return pso;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


void CommandList::setComputeState(const ComputeState& state){
    if(!recordAndValidateCommandCapability(GpuQueueCapability::Compute, VulkanArenaScope::s_SetComputeStateCommandLabel))
        return;
    if(!state.pipeline){
        rejectCommandRecording(VulkanArenaScope::s_SetComputeStateCommandLabel, NWB_TEXT("compute pipeline is null"));
        return;
    }
    if(&state.pipeline->m_context != &m_context){
        rejectCommandRecording(VulkanArenaScope::s_SetComputeStateCommandLabel, NWB_TEXT("compute pipeline belongs to another device"));
        return;
    }
    if(state.pipeline->m_pipeline == VK_NULL_HANDLE || state.pipeline->m_pipelineLayout == VK_NULL_HANDLE){
        rejectCommandRecording(VulkanArenaScope::s_SetComputeStateCommandLabel, NWB_TEXT("compute pipeline has no valid native pipeline or layout"));
        return;
    }
    if(state.indirectParams && (state.indirectParams->m_buffer == VK_NULL_HANDLE || !state.indirectParams->m_creationDesc.isDrawIndirectArgs)){
        rejectCommandRecording(VulkanArenaScope::s_SetComputeStateCommandLabel, NWB_TEXT("indirect buffer has no valid native buffer or indirect-argument usage"));
        return;
    }
    if(state.indirectParams && !validateBufferForGpuState(state.indirectParams, ResourceStates::IndirectArgument, VulkanArenaScope::s_SetComputeStateCommandLabel, VK_BUFFER_USAGE_INDIRECT_BUFFER_BIT))
        return;

    endActiveRenderPass();
    commitBarriers();
    if(m_commandRecordingFailed)
        return;

    m_currentGraphicsState = {};
    m_currentMeshletState = {};
    m_currentRayTracingState = {};
    m_currentComputeState = state;
    retainResource(state.indirectParams);

    auto* pipeline = state.pipeline;
    m_context.deviceDispatch.vkCmdBindPipeline(m_currentCmdBuf->m_cmdBuf, VK_PIPELINE_BIND_POINT_COMPUTE, pipeline->m_pipeline);
    retainResource(pipeline);
}

void CommandList::dispatch(u32 groupsX, u32 groupsY, u32 groupsZ){
    constexpr TStringView s_OperationName = NWB_TEXT("dispatch");
    if(groupsX == 0 || groupsY == 0 || groupsZ == 0)
        return;
    if(!recordAndValidateCommandCapability(GpuQueueCapability::Compute, s_OperationName))
        return;
    if(!m_currentComputeState.pipeline){
        rejectCommandRecording(s_OperationName, __hidden_compute::s_NoComputePipelineBoundReason);
        return;
    }

    const auto& limits = m_context.physicalDeviceProperties.limits;
    if(!VulkanDetail::AreDispatchGroupCountsValid(groupsX, groupsY, groupsZ, limits.maxComputeWorkGroupCount)){
        rejectCommandRecording(s_OperationName, NWB_TEXT("group counts exceed device limits"));
        return;
    }

    m_context.deviceDispatch.vkCmdDispatch(m_currentCmdBuf->m_cmdBuf, groupsX, groupsY, groupsZ);
}

void CommandList::dispatchIndirect(u32 offsetBytes){
    if(!recordAndValidateCommandCapability(GpuQueueCapability::Compute, VulkanArenaScope::s_DispatchIndirectCommandLabel))
        return;
    if(!m_currentComputeState.pipeline){
        rejectCommandRecording(VulkanArenaScope::s_DispatchIndirectCommandLabel, __hidden_compute::s_NoComputePipelineBoundReason);
        return;
    }
    if(!m_currentComputeState.indirectParams){
        rejectCommandRecording(VulkanArenaScope::s_DispatchIndirectCommandLabel, NWB_TEXT("no indirect buffer is bound"));
        return;
    }

    auto* buffer = m_currentComputeState.indirectParams;
    if((offsetBytes & s_BufferAlignmentMask) != 0u){
        rejectCommandRecording(VulkanArenaScope::s_DispatchIndirectCommandLabel, NWB_TEXT("indirect argument offset is not 4-byte aligned"));
        return;
    }
    if(!VulkanDetail::IsBufferRangeInBounds(buffer->m_creationDesc, offsetBytes, sizeof(DispatchIndirectArguments))){
        rejectCommandRecording(VulkanArenaScope::s_DispatchIndirectCommandLabel, NWB_TEXT("indirect argument range is outside the buffer"));
        return;
    }

    // The bound buffer can pack independent commands or shader data. Only this dispatch's arguments are consumed.
    setBufferState(buffer, ResourceStates::IndirectArgument, false, BufferRange(offsetBytes, sizeof(DispatchIndirectArguments)));
    if(m_commandRecordingFailed)
        return;
    commitBarriers();
    if(m_commandRecordingFailed)
        return;

    m_context.deviceDispatch.vkCmdDispatchIndirect(m_currentCmdBuf->m_cmdBuf, buffer->m_buffer, offsetBytes);
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_VULKAN_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

