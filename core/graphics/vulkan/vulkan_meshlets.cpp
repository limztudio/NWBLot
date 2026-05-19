// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "vulkan_backend.h"

#include <core/common/log.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_VULKAN_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


MeshletPipeline::MeshletPipeline(const VulkanContext& context)
    : RefCounter<IMeshletPipeline>(context.threadPool)
    , m_context(context)
{}
MeshletPipeline::~MeshletPipeline(){
    VulkanDetail::DestroyPipelineResource(m_context, *this, m_pipeline);
}
Object MeshletPipeline::getNativeHandle(ObjectType objectType){
    return VulkanDetail::GetPipelineNativeHandle(m_pipeline, objectType);
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


MeshletPipelineHandle Device::createMeshletPipeline(const MeshletPipelineDesc& desc, FramebufferInfo const& fbinfo){
    VkResult res = VK_SUCCESS;

    if(!m_context.extensions.KHR_dynamic_rendering){
        NWB_LOGGER_ERROR(NWB_TEXT("Vulkan: Dynamic rendering extension is required to create meshlet pipelines."));
        return nullptr;
    }
    if(!m_context.extensions.EXT_mesh_shader){
        NWB_LOGGER_ERROR(NWB_TEXT("Vulkan: Mesh shader extension is required to create meshlet pipelines."));
        return nullptr;
    }

    Alloc::ScratchArena<> scratchArena;

    auto* pso = NewArenaObject<MeshletPipeline>(m_context.objectArena, m_context);
    pso->m_desc = desc;

    PipelineShaderStageVector shaderStages{ scratchArena };
    PipelineSpecializationInfoVector specInfos{ scratchArena };
    PipelineDescriptorHeapScratch descriptorHeapScratch{ scratchArena };
    shaderStages.reserve(s_MeshletPipelineStageReserveCount); // Task (optional), Mesh, Fragment
    specInfos.reserve(s_MeshletPipelineStageReserveCount);

    if(desc.AS && m_context.meshShaderFeatures.taskShader != VK_TRUE){
        NWB_LOGGER_ERROR(NWB_TEXT("Vulkan: Task shader was supplied for meshlet pipeline, but VK_EXT_mesh_shader taskShader was not enabled."));
        DestroyArenaObject(m_context.objectArena, pso);
        return nullptr;
    }

    if(desc.AS)
        appendPipelineShaderStage(desc.AS.get(), VK_SHADER_STAGE_TASK_BIT_EXT, specInfos, shaderStages);

    if(desc.MS)
        appendPipelineShaderStage(desc.MS.get(), VK_SHADER_STAGE_MESH_BIT_EXT, specInfos, shaderStages);
    else{
        NWB_LOGGER_ERROR(NWB_TEXT("Vulkan: Mesh shader is required for meshlet pipeline"));
        DestroyArenaObject(m_context.objectArena, pso);
        return nullptr;
    }

    if(desc.PS)
        appendPipelineShaderStage(desc.PS.get(), VK_SHADER_STAGE_FRAGMENT_BIT, specInfos, shaderStages);

    if(!configurePipelineBindingsOrDestroy(
        desc.bindingLayouts,
        NWB_TEXT("meshlet pipeline"),
        shaderStages,
        descriptorHeapScratch,
        pso,
        scratchArena
    ))
        return nullptr;

    auto rasterizer = VulkanDetail::BuildPipelineRasterizationState(
        desc.renderState.rasterState,
        VulkanDetail::ConvertFillMode(desc.renderState.rasterState.fillMode),
        VK_FALSE
    );

    const VkDynamicState dynamicStates[] = {
        VK_DYNAMIC_STATE_VIEWPORT,
        VK_DYNAMIC_STATE_SCISSOR,
    };
    VulkanDetail::GraphicsPipelineFixedState fixedState{ scratchArena };
    if(
        !VulkanDetail::BuildGraphicsPipelineFixedState(
            fbinfo,
            desc.renderState,
            VulkanDetail::PipelineStencilFaceMode::DepthOnly,
            dynamicStates,
            static_cast<u32>(LengthOf(dynamicStates)),
            NWB_TEXT("meshlet pipeline"),
            fixedState
        )
    ){
        DestroyArenaObject(m_context.objectArena, pso);
        return nullptr;
    }

    auto pipelineInfo = VulkanDetail::MakeVkStruct<VkGraphicsPipelineCreateInfo>(VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO);
    if(pso->m_usesDescriptorHeap)
        pipelineInfo.pNext = descriptorHeapScratch.pNext(&fixedState.renderingInfo);
    else
        pipelineInfo.pNext = &fixedState.renderingInfo;
    pipelineInfo.stageCount = static_cast<u32>(shaderStages.size());
    pipelineInfo.pStages = shaderStages.data();
    pipelineInfo.pVertexInputState = nullptr; // Mesh shaders don't use vertex input
    pipelineInfo.pInputAssemblyState = nullptr; // Mesh shaders don't use input assembly
    VulkanDetail::AttachGraphicsPipelineFixedState(pipelineInfo, rasterizer, fixedState);
    pipelineInfo.layout = pso->m_usesDescriptorHeap ? VK_NULL_HANDLE : pso->m_pipelineLayout;
    pipelineInfo.renderPass = VK_NULL_HANDLE;

    res = vkCreateGraphicsPipelines(m_context.device, m_context.pipelineCache, 1, &pipelineInfo, m_context.allocationCallbacks, &pso->m_pipeline);
    if(res != VK_SUCCESS){
        NWB_LOGGER_ERROR(NWB_TEXT("Vulkan: Failed to create meshlet pipeline: {}"), ResultToString(res));
        DestroyArenaObject(m_context.objectArena, pso);
        return nullptr;
    }

    return MeshletPipelineHandle(pso, MeshletPipelineHandle::deleter_type(&m_context.objectArena), AdoptRef);
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


void CommandList::setMeshletState(const MeshletState& state){
    setResourceStatesForBindingSets(state.bindings);
    if(state.indirectParams)
        setBufferState(state.indirectParams, ResourceStates::IndirectArgument);
    commitBarriers();

    if(!ensureGraphicsRenderPass(state.framebuffer))
        return;
    commitBarriers();
    m_currentGraphicsState = {};
    m_currentComputeState = {};
    m_currentRayTracingState = {};
    m_currentMeshletState = state;

    auto* pipeline = checked_cast<MeshletPipeline*>(state.pipeline);
    if(pipeline)
        vkCmdBindPipeline(m_currentCmdBuf->m_cmdBuf, VK_PIPELINE_BIND_POINT_GRAPHICS, pipeline->m_pipeline);

    if(pipeline)
        bindPipelineBindingSets(VK_PIPELINE_BIND_POINT_GRAPHICS, pipeline->m_pipelineLayout, pipeline->m_usesDescriptorHeap, pipeline->m_descriptorHeapPushRanges, pipeline->m_descriptorHeapPushDataSize, state.bindings);

    setViewportState(state.viewport);
}

void CommandList::dispatchMesh(u32 groupsX, u32 groupsY, u32 groupsZ){
    if(groupsX == 0 || groupsY == 0 || groupsZ == 0)
        return;
    if(!m_renderPassActive || !m_currentMeshletState.pipeline){
        NWB_LOGGER_ERROR(NWB_TEXT("Vulkan: Failed to dispatch mesh tasks: no meshlet pipeline and active render pass are bound"));
        NWB_ASSERT_MSG(false, NWB_TEXT("Vulkan: Failed to dispatch mesh tasks: no meshlet pipeline and active render pass are bound"));
        return;
    }
    if(!vkCmdDrawMeshTasksEXT){
        NWB_LOGGER_ERROR(NWB_TEXT("Vulkan: Mesh shader dispatch requested, but vkCmdDrawMeshTasksEXT is unavailable."));
        return;
    }

    vkCmdDrawMeshTasksEXT(m_currentCmdBuf->m_cmdBuf, groupsX, groupsY, groupsZ);
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_VULKAN_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

