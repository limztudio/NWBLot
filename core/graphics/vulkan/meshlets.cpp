// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "backend.h"
#include "arena_names.h"
#include "command_validation.h"

#include <core/common/log.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_VULKAN_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


MeshletPipeline::MeshletPipeline(const VulkanContext& context)
    : RefCounter<GraphicsResource>(context.threadPool)
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
    if(!m_context.extensions.KHR_dynamic_rendering){
        NWB_LOGGER_ERROR(NWB_TEXT("Vulkan: Dynamic rendering extension is required to create meshlet pipelines."));
        return nullptr;
    }
    if(
        !m_context.extensions.EXT_mesh_shader
        || m_context.meshShaderFeatures.meshShader != VK_TRUE
        || !vkCmdDrawMeshTasksEXT
    ){
        NWB_LOGGER_ERROR(NWB_TEXT("Vulkan: Mesh shader feature and entry point are required for meshlet pipelines."));
        return nullptr;
    }
    if(fbinfo.colorFormats.size() > m_context.physicalDeviceProperties.limits.maxColorAttachments){
        NWB_LOGGER_ERROR(NWB_TEXT("Vulkan: Meshlet pipeline color count exceeds the device limit."));
        return nullptr;
    }
    if(desc.renderState.rasterState.depthBiasClamp != 0.0f){
        NWB_LOGGER_ERROR(
            NWB_TEXT("Vulkan: Meshlet pipeline depthBiasClamp requires an unsupported logical-device feature.")
        );
        return nullptr;
    }
    if(desc.renderState.singlePassStereo.enabled){
        NWB_LOGGER_ERROR(NWB_TEXT("Vulkan: Meshlet pipeline single-pass stereo is not implemented."));
        return nullptr;
    }
    if(!desc.MS){
        NWB_LOGGER_ERROR(NWB_TEXT("Vulkan: Mesh shader is required for meshlet pipeline"));
        return nullptr;
    }

    const auto validateShaderOwner = [this](Shader* const shader, const tchar* const stageName){
        if(!shader || &shader->m_context == &m_context)
            return true;
        NWB_LOGGER_ERROR(NWB_TEXT("Vulkan: Meshlet pipeline {} shader belongs to another device."), stageName);
        return false;
    };
    if(
        !validateShaderOwner(desc.AS.get(), NWB_TEXT("task"))
        || !validateShaderOwner(desc.MS.get(), NWB_TEXT("mesh"))
        || !validateShaderOwner(desc.PS.get(), NWB_TEXT("fragment"))
    )
        return nullptr;

    Alloc::ScratchArena scratchArena(VulkanArenaScope::s_MeshletPipelineArena);

    auto* pso = NewArenaObject<MeshletPipeline>(m_context.objectArena, m_context);
    pso->m_desc = desc;
    pso->m_framebufferInfo = fbinfo;

    PipelineShaderStageVector shaderStages{ scratchArena };
    PipelineSpecializationInfoVector specInfos{ scratchArena };
    shaderStages.reserve(s_MeshletPipelineStageReserveCount); // Task (optional), Mesh, Fragment
    specInfos.reserve(s_MeshletPipelineStageReserveCount);

    if(desc.AS && m_context.meshShaderFeatures.taskShader != VK_TRUE){
        NWB_LOGGER_ERROR(NWB_TEXT("Vulkan: Task shader was supplied for meshlet pipeline, but VK_EXT_mesh_shader taskShader was not enabled."));
        DestroyArenaObject(m_context.objectArena, pso);
        return nullptr;
    }
    if(
        desc.AS
        && (
            desc.AS->m_shaderModule == VK_NULL_HANDLE
            || desc.AS->m_desc.shaderType != ShaderType::Amplification
        )
    ){
        NWB_LOGGER_ERROR(NWB_TEXT("Vulkan: Meshlet pipeline task shader has the wrong shader stage."));
        DestroyArenaObject(m_context.objectArena, pso);
        return nullptr;
    }
    if(
        desc.MS
        && (desc.MS->m_shaderModule == VK_NULL_HANDLE || desc.MS->m_desc.shaderType != ShaderType::Mesh)
    ){
        NWB_LOGGER_ERROR(NWB_TEXT("Vulkan: Meshlet pipeline mesh shader has the wrong shader stage."));
        DestroyArenaObject(m_context.objectArena, pso);
        return nullptr;
    }
    if(
        desc.PS
        && (desc.PS->m_shaderModule == VK_NULL_HANDLE || desc.PS->m_desc.shaderType != ShaderType::Pixel)
    ){
        NWB_LOGGER_ERROR(NWB_TEXT("Vulkan: Meshlet pipeline fragment shader has the wrong shader stage."));
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
        pso,
        scratchArena
    ))
        return nullptr;

    auto rasterizer = VulkanDetail::BuildPipelineRasterizationState(
        desc.renderState.rasterState,
        VulkanDetail::ConvertFillMode(desc.renderState.rasterState.fillMode),
        desc.renderState.rasterState.depthClipEnable ? VK_FALSE : VK_TRUE
    );

    const VkDynamicState dynamicStates[] = {
        VK_DYNAMIC_STATE_VIEWPORT,
        VK_DYNAMIC_STATE_SCISSOR,
        VK_DYNAMIC_STATE_BLEND_CONSTANTS,
        VK_DYNAMIC_STATE_STENCIL_REFERENCE,
    };
    VulkanDetail::GraphicsPipelineFixedState fixedState{ scratchArena };
    if(!buildGraphicsPipelineFixedStateOrDestroy(
        fbinfo,
        desc.renderState,
        VulkanDetail::PipelineStencilFaceMode::IncludeStencilFaces,
        dynamicStates,
        static_cast<u32>(LengthOf(dynamicStates)),
        NWB_TEXT("meshlet pipeline"),
        pso,
        fixedState
    ))
        return nullptr;

    auto pipelineInfo = VulkanDetail::MakeVkStruct<VkGraphicsPipelineCreateInfo>(VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO);
    VulkanDetail::AttachPipelineBindingState(pipelineInfo, *pso, &fixedState.renderingInfo);
    pipelineInfo.stageCount = static_cast<u32>(shaderStages.size());
    pipelineInfo.pStages = shaderStages.data();
    pipelineInfo.pVertexInputState = nullptr; // Mesh shaders don't use vertex input
    pipelineInfo.pInputAssemblyState = nullptr; // Mesh shaders don't use input assembly
    VulkanDetail::AttachGraphicsPipelineFixedState(pipelineInfo, rasterizer, fixedState);
    pipelineInfo.renderPass = VK_NULL_HANDLE;

    if(!createPipelineOrDestroy(NWB_TEXT("meshlet pipeline"), pso, pipelineInfo))
        return nullptr;

    return MeshletPipelineHandle(pso, MeshletPipelineHandle::deleter_type(&m_context.objectArena), AdoptRef);
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


void CommandList::setMeshletState(const MeshletState& state){
    if(!recordAndValidateCommandCapability(GpuQueueCapability::Graphics, NWB_TEXT("set meshlet state")))
        return;
    if(!validateMeshletState(state))
        return;
    if(!prepareFramebufferForRendering(state.framebuffer, NWB_TEXT("set meshlet state")))
        return;

    if(state.indirectParams)
        setBufferState(state.indirectParams, ResourceStates::IndirectArgument);
    commitBarriers();
    if(m_commandRecordingFailed)
        return;

    if(!ensureGraphicsRenderPass(state.framebuffer))
        return;
    commitBarriers();
    if(m_commandRecordingFailed)
        return;
    m_currentGraphicsState = {};
    m_currentComputeState = {};
    m_currentRayTracingState = {};
    m_currentMeshletState = state;

    auto* pipeline = state.pipeline;
    if(pipeline){
        vkCmdBindPipeline(m_currentCmdBuf->m_cmdBuf, VK_PIPELINE_BIND_POINT_GRAPHICS, pipeline->m_pipeline);
        retainResource(pipeline);

        const f32 blendConstants[] = {
            state.blendConstantColor.r,
            state.blendConstantColor.g,
            state.blendConstantColor.b,
            state.blendConstantColor.a,
        };
        vkCmdSetBlendConstants(m_currentCmdBuf->m_cmdBuf, blendConstants);

        const DepthStencilState& depthStencilState = pipeline->m_desc.renderState.depthStencilState;
        const u8 stencilReference = depthStencilState.dynamicStencilRef
            ? state.dynamicStencilRefValue
            : depthStencilState.stencilRefValue
        ;
        vkCmdSetStencilReference(
            m_currentCmdBuf->m_cmdBuf,
            VK_STENCIL_FACE_FRONT_AND_BACK,
            stencilReference
        );
    }

    if(pipeline)
        bindDescriptorBufferEmptySet(VK_PIPELINE_BIND_POINT_GRAPHICS, pipeline->m_pipelineLayout);

    setViewportState(state.viewport);
    retainResource(state.indirectParams);
}

void CommandList::dispatchMesh(u32 groupsX, u32 groupsY, u32 groupsZ){
    if(groupsX == 0 || groupsY == 0 || groupsZ == 0)
        return;
    if(!recordAndValidateCommandCapability(GpuQueueCapability::Graphics, NWB_TEXT("dispatch mesh")))
        return;

    MeshletPipeline* const pipeline = m_currentMeshletState.pipeline;
    if(
        !m_renderPassActive
        || !m_renderPassFramebuffer
        || !pipeline
        || pipeline->m_pipeline == VK_NULL_HANDLE
        || pipeline->m_framebufferInfo != m_renderPassFramebuffer->m_framebufferInfo
    ){
        rejectCommandRecording(
            NWB_TEXT("dispatch mesh"),
            NWB_TEXT("no compatible meshlet pipeline and render pass are active")
        );
        return;
    }
    if(
        m_currentMeshletState.viewport.viewports.size() != 1u
        || m_currentMeshletState.viewport.scissorRects.size() > 1u
    ){
        rejectCommandRecording(
            NWB_TEXT("dispatch mesh"),
            NWB_TEXT("mesh dispatch requires one viewport and at most one explicit scissor")
        );
        return;
    }
    if(
        !m_context.extensions.EXT_mesh_shader
        || m_context.meshShaderFeatures.meshShader != VK_TRUE
        || !vkCmdDrawMeshTasksEXT
    ){
        rejectCommandRecording(NWB_TEXT("dispatch mesh"), NWB_TEXT("mesh shader feature or entry point is unavailable"));
        return;
    }

    const VulkanDetail::MeshDispatchLimits dispatchLimits = VulkanDetail::GetMeshDispatchLimits(
        m_context.meshShaderProperties,
        pipeline->m_desc.AS != nullptr
    );
    if(!VulkanDetail::AreMeshDispatchGroupCountsValid(
        groupsX,
        groupsY,
        groupsZ,
        dispatchLimits.maximumGroupCounts,
        dispatchLimits.maximumTotalGroupCount
    )){
        rejectCommandRecording(NWB_TEXT("dispatch mesh"), NWB_TEXT("mesh dispatch group counts exceed device limits"));
        return;
    }

    vkCmdDrawMeshTasksEXT(m_currentCmdBuf->m_cmdBuf, groupsX, groupsY, groupsZ);
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_VULKAN_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

