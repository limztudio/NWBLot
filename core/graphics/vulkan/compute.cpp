// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "backend.h"
#include "arena_names.h"

#include <core/common/log.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_VULKAN_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


ComputePipeline::ComputePipeline(const VulkanContext& context)
    : RefCounter<GraphicsResource>(context.threadPool)
    , m_context(context)
{}
ComputePipeline::~ComputePipeline(){
    VulkanDetail::DestroyPipelineResource(m_context, *this, m_pipeline);
}

Object ComputePipeline::getNativeHandle(ObjectType objectType){
    return VulkanDetail::GetPipelineNativeHandle(m_pipeline, objectType);
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


ComputePipelineHandle Device::createComputePipeline(const ComputePipelineDesc& desc){
    if(!desc.CS){
        NWB_LOGGER_ERROR(NWB_TEXT("Vulkan: Failed to create compute pipeline: compute shader is null"));
        return nullptr;
    }

    auto* cs = desc.CS.get();
    if(&cs->m_context != &m_context){
        NWB_LOGGER_ERROR(NWB_TEXT("Vulkan: Failed to create compute pipeline: compute shader belongs to another device"));
        return nullptr;
    }
    if(cs->m_shaderModule == VK_NULL_HANDLE || cs->m_desc.shaderType != ShaderType::Compute){
        NWB_LOGGER_ERROR(NWB_TEXT("Vulkan: Failed to create compute pipeline: compute shader has an invalid module or stage"));
        return nullptr;
    }

    Alloc::ScratchArena scratchArena(VulkanArenaScope::s_ComputePipelineArena);

    auto* pso = NewArenaObject<ComputePipeline>(m_context.objectArena, m_context);
    pso->m_desc = desc;

    auto shaderStage = VulkanDetail::MakeVkStruct<VkPipelineShaderStageCreateInfo>(VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO);
    shaderStage.stage = VK_SHADER_STAGE_COMPUTE_BIT;
    shaderStage.module = cs->m_shaderModule;
    shaderStage.pName = cs->m_entryPointName.c_str();

    VkSpecializationInfo specInfo{};
    if(!cs->m_specializationEntries.empty()){
        specInfo = cs->makeSpecializationInfo();
        shaderStage.pSpecializationInfo = &specInfo;
    }

    PipelineShaderStageVector shaderStages{ scratchArena };
    shaderStages.push_back(shaderStage);

    if(!configurePipelineBindingsOrDestroy(
        desc.bindingLayouts,
        NWB_TEXT("compute pipeline"),
        pso,
        scratchArena
    ))
        return nullptr;

    auto pipelineInfo = VulkanDetail::MakeVkStruct<VkComputePipelineCreateInfo>(VK_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO);
    pipelineInfo.stage = shaderStages[0];
    VulkanDetail::AttachPipelineBindingState(pipelineInfo, *pso);

    if(!createPipelineOrDestroy(NWB_TEXT("compute pipeline"), pso, pipelineInfo))
        return nullptr;

    return ComputePipelineHandle(pso, ComputePipelineHandle::deleter_type(&m_context.objectArena), AdoptRef);
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


void CommandList::setComputeState(const ComputeState& state){
    if(!recordAndValidateCommandCapability(GpuQueueCapability::Compute, NWB_TEXT("set compute state")))
        return;
    if(!state.pipeline){
        rejectCommandRecording(NWB_TEXT("set compute state"), NWB_TEXT("compute pipeline is null"));
        return;
    }
    if(&state.pipeline->m_context != &m_context){
        rejectCommandRecording(NWB_TEXT("set compute state"), NWB_TEXT("compute pipeline belongs to another device"));
        return;
    }
    if(state.pipeline->m_pipeline == VK_NULL_HANDLE || state.pipeline->m_pipelineLayout == VK_NULL_HANDLE){
        rejectCommandRecording(
            NWB_TEXT("set compute state"),
            NWB_TEXT("compute pipeline has no valid native pipeline or layout")
        );
        return;
    }
    if(
        state.indirectParams
        && (
            state.indirectParams->m_buffer == VK_NULL_HANDLE
            || !state.indirectParams->m_creationDesc.isDrawIndirectArgs
        )
    ){
        rejectCommandRecording(
            NWB_TEXT("set compute state"),
            NWB_TEXT("indirect buffer has no valid native buffer or indirect-argument usage")
        );
        return;
    }
    if(
        state.indirectParams
        && !validateBufferForGpuState(
            state.indirectParams,
            ResourceStates::IndirectArgument,
            NWB_TEXT("set compute state"),
            VK_BUFFER_USAGE_INDIRECT_BUFFER_BIT
        )
    )
        return;

    endActiveRenderPass();
    if(state.indirectParams)
        setBufferState(state.indirectParams, ResourceStates::IndirectArgument);
    if(m_commandRecordingFailed)
        return;
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
    if(groupsX == 0 || groupsY == 0 || groupsZ == 0)
        return;
    if(!recordAndValidateCommandCapability(GpuQueueCapability::Compute, NWB_TEXT("dispatch")))
        return;
    if(!m_currentComputeState.pipeline){
        rejectCommandRecording(NWB_TEXT("dispatch"), NWB_TEXT("no compute pipeline is bound"));
        return;
    }

    const auto& limits = m_context.physicalDeviceProperties.limits;
    if(!VulkanDetail::AreDispatchGroupCountsValid(groupsX, groupsY, groupsZ, limits.maxComputeWorkGroupCount)){
        rejectCommandRecording(NWB_TEXT("dispatch"), NWB_TEXT("group counts exceed device limits"));
        return;
    }

    m_context.deviceDispatch.vkCmdDispatch(m_currentCmdBuf->m_cmdBuf, groupsX, groupsY, groupsZ);
}

void CommandList::dispatchIndirect(u32 offsetBytes){
    if(!recordAndValidateCommandCapability(GpuQueueCapability::Compute, NWB_TEXT("dispatch indirect")))
        return;
    if(!m_currentComputeState.pipeline){
        rejectCommandRecording(NWB_TEXT("dispatch indirect"), NWB_TEXT("no compute pipeline is bound"));
        return;
    }
    if(!m_currentComputeState.indirectParams){
        rejectCommandRecording(NWB_TEXT("dispatch indirect"), NWB_TEXT("no indirect buffer is bound"));
        return;
    }

    auto* buffer = m_currentComputeState.indirectParams;
    if(!buffer->m_creationDesc.isDrawIndirectArgs){
        rejectCommandRecording(NWB_TEXT("dispatch indirect"), NWB_TEXT("buffer was not created with indirect-argument usage"));
        return;
    }
    if((offsetBytes & s_BufferAlignmentMask) != 0u){
        rejectCommandRecording(NWB_TEXT("dispatch indirect"), NWB_TEXT("indirect argument offset is not 4-byte aligned"));
        return;
    }
    if(!VulkanDetail::IsBufferRangeInBounds(buffer->m_creationDesc, offsetBytes, sizeof(DispatchIndirectArguments))){
        rejectCommandRecording(NWB_TEXT("dispatch indirect"), NWB_TEXT("indirect argument range is outside the buffer"));
        return;
    }

    m_context.deviceDispatch.vkCmdDispatchIndirect(m_currentCmdBuf->m_cmdBuf, buffer->m_buffer, offsetBytes);
    retainResource(buffer);
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_VULKAN_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

