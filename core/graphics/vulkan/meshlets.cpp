// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "backend.h"
#include "graphics_pipeline_state_builder.h"
#include "framebuffer_rendering_preparation.h"
#include "arena_names.h"
#include "command_validation.h"

#include <core/common/log.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_VULKAN_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


MeshletPipeline::MeshletPipeline(const VulkanContext& context)noexcept
    : RefCounter<GraphicsResource>(context.cpuScheduler)
    , m_context(context)
{}

MeshletPipeline::~MeshletPipeline()noexcept{
    VulkanDetail::DestroyPipelineResource(m_context, *this, m_pipeline);
}

Object MeshletPipeline::getNativeHandle(ObjectType objectType)noexcept{
    return VulkanDetail::GetPipelineNativeHandle(m_pipeline, objectType);
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


Expected<MeshletPipelineHandle> Device::createMeshletPipeline(const MeshletPipelineDesc& desc, FramebufferInfo const& fbinfo){
    constexpr TStringView s_OperationName = FOUNDATION_TEXT("meshlet pipeline");
    if(!m_context.extensions.khrDynamicRendering){
        NWB_LOGGER_ERROR(FOUNDATION_TEXT("Vulkan: Dynamic rendering extension is required to create meshlet pipelines."));
        return MakeUnexpected(Failure{});
    }
    if(!m_context.extensions.extMeshShader || m_context.meshShaderFeatures.meshShader != VK_TRUE || !m_context.deviceDispatch.vkCmdDrawMeshTasksEXT){
        NWB_LOGGER_ERROR(FOUNDATION_TEXT("Vulkan: Mesh shader feature and entry point are required for meshlet pipelines."));
        return MakeUnexpected(Failure{});
    }
    if(fbinfo.colorFormats.size() > m_context.physicalDeviceProperties.limits.maxColorAttachments){
        NWB_LOGGER_ERROR(FOUNDATION_TEXT("Vulkan: Meshlet pipeline color count exceeds the device limit."));
        return MakeUnexpected(Failure{});
    }
    if(desc.renderState.rasterState.depthBiasClamp != 0.0f){
        NWB_LOGGER_ERROR(FOUNDATION_TEXT("Vulkan: Meshlet pipeline depthBiasClamp requires an unsupported logical-device feature."));
        return MakeUnexpected(Failure{});
    }
    if(desc.renderState.singlePassStereo.enabled){
        NWB_LOGGER_ERROR(FOUNDATION_TEXT("Vulkan: Meshlet pipeline single-pass stereo is not implemented."));
        return MakeUnexpected(Failure{});
    }
    if(!desc.meshShader){
        NWB_LOGGER_ERROR(FOUNDATION_TEXT("Vulkan: Mesh shader is required for meshlet pipeline"));
        return MakeUnexpected(Failure{});
    }

    const auto validateShaderOwner = [this](Shader* const shader, const TStringView stageName){
        if(!shader || &shader->m_context == &m_context)
            return true;
        NWB_LOGGER_ERROR(FOUNDATION_TEXT("Vulkan: Meshlet pipeline {} shader belongs to another device."), stageName);
        return false;
    };
    if(
        !validateShaderOwner(desc.amplificationShader.get(), VulkanArenaScope::s_TaskStageLabel)
        || !validateShaderOwner(desc.meshShader.get(), VulkanArenaScope::s_MeshStageLabel)
        || !validateShaderOwner(desc.pixelShader.get(), VulkanArenaScope::s_FragmentStageLabel)
    )
        return MakeUnexpected(Failure{});

    Alloc::ScratchArena scratchArena(VulkanArenaScope::s_MeshletPipelineArena);

    MeshletPipelineHandle pso(NewArenaObject<MeshletPipeline>(m_context.objectArena, m_context), MeshletPipelineHandle::deleter_type(&m_context.objectArena), s_AdoptRef);
    if(!pso)
        return MakeUnexpected(Failure{});
    pso->m_desc = desc;
    pso->m_framebufferInfo = fbinfo;

    PipelineShaderStageVector shaderStages{ scratchArena };
    PipelineSpecializationInfoVector specInfos{ scratchArena };
    shaderStages.reserve(s_MeshletPipelineStageReserveCount);
    specInfos.reserve(s_MeshletPipelineStageReserveCount);

    if(desc.amplificationShader && m_context.meshShaderFeatures.taskShader != VK_TRUE){
        NWB_LOGGER_ERROR(FOUNDATION_TEXT("Vulkan: Task shader was supplied for meshlet pipeline, but VK_EXT_mesh_shader taskShader was not enabled."));
        return MakeUnexpected(Failure{});
    }
    if(desc.amplificationShader && (desc.amplificationShader->m_shaderModule == VK_NULL_HANDLE || desc.amplificationShader->m_desc.shaderType != ShaderType::Amplification)){
        NWB_LOGGER_ERROR(FOUNDATION_TEXT("Vulkan: Meshlet pipeline task shader has the wrong shader stage."));
        return MakeUnexpected(Failure{});
    }
    if(desc.meshShader && (desc.meshShader->m_shaderModule == VK_NULL_HANDLE || desc.meshShader->m_desc.shaderType != ShaderType::Mesh)){
        NWB_LOGGER_ERROR(FOUNDATION_TEXT("Vulkan: Meshlet pipeline mesh shader has the wrong shader stage."));
        return MakeUnexpected(Failure{});
    }
    if(desc.pixelShader && (desc.pixelShader->m_shaderModule == VK_NULL_HANDLE || desc.pixelShader->m_desc.shaderType != ShaderType::Pixel)){
        NWB_LOGGER_ERROR(FOUNDATION_TEXT("Vulkan: Meshlet pipeline fragment shader has the wrong shader stage."));
        return MakeUnexpected(Failure{});
    }

    if(desc.amplificationShader)
        appendPipelineShaderStage(*desc.amplificationShader, VK_SHADER_STAGE_TASK_BIT_EXT, specInfos, shaderStages);

    if(desc.meshShader)
        appendPipelineShaderStage(*desc.meshShader, VK_SHADER_STAGE_MESH_BIT_EXT, specInfos, shaderStages);
    else{
        NWB_LOGGER_ERROR(FOUNDATION_TEXT("Vulkan: Mesh shader is required for meshlet pipeline"));
        return MakeUnexpected(Failure{});
    }

    if(desc.pixelShader)
        appendPipelineShaderStage(*desc.pixelShader, VK_SHADER_STAGE_FRAGMENT_BIT, specInfos, shaderStages);

    if(!configurePipelineBindings(desc.bindingLayouts, s_OperationName, scratchArena, *pso))
        return MakeUnexpected(Failure{});

    auto rasterizer = VulkanDetail::GraphicsPipelineStateBuilder::BuildPipelineRasterizationState(desc.renderState.rasterState, VulkanDetail::ConvertFillMode(desc.renderState.rasterState.fillMode), desc.renderState.rasterState.depthClipEnable ? VK_FALSE : VK_TRUE);

    const Array<VkDynamicState, 4> dynamicStates = {{ VK_DYNAMIC_STATE_VIEWPORT, VK_DYNAMIC_STATE_SCISSOR, VK_DYNAMIC_STATE_BLEND_CONSTANTS, VK_DYNAMIC_STATE_STENCIL_REFERENCE }};
    const auto fixedState = VulkanDetail::GraphicsPipelineStateBuilder::BuildGraphicsPipelineFixedState(fbinfo, desc.renderState, VulkanDetail::PipelineStencilFaceMode::IncludeStencilFaces, dynamicStates, s_OperationName, scratchArena);
    if(!fixedState)
        return MakeUnexpected(Failure{});

    auto pipelineInfo = VulkanDetail::MakeVkStruct<VkGraphicsPipelineCreateInfo>(VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO);
    VulkanDetail::AttachPipelineBindingState(pipelineInfo, *pso, &fixedState->renderingInfo);
    pipelineInfo.stageCount = static_cast<u32>(shaderStages.size());
    pipelineInfo.pStages = shaderStages.data();
    pipelineInfo.pVertexInputState = nullptr;
    pipelineInfo.pInputAssemblyState = nullptr;
    VulkanDetail::GraphicsPipelineStateBuilder::AttachGraphicsPipelineFixedState(pipelineInfo, rasterizer, *fixedState);
    pipelineInfo.renderPass = VK_NULL_HANDLE;

    if(!createPipeline(s_OperationName, pipelineInfo, *pso))
        return MakeUnexpected(Failure{});

    return pso;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


void CommandList::setMeshletState(const MeshletState& state){
    if(!recordAndValidateCommandCapability(GpuQueueCapability::Graphics, s_SetMeshletStateOperation))
        return;

    const b8 hasPreviousMeshletState = m_hasBoundMeshletState;
    const b8 sameActiveRenderPass = m_renderPassActive && state.framebuffer && m_renderPassFramebuffer == state.framebuffer;
    const b8 sameFramebuffer = hasPreviousMeshletState && m_currentMeshletState.framebuffer == state.framebuffer;
    const b8 pipelineChanged = !hasPreviousMeshletState || m_currentMeshletState.pipeline != state.pipeline;
    const b8 indirectParamsChanged = !hasPreviousMeshletState || m_currentMeshletState.indirectParams != state.indirectParams;

    if(
        hasPreviousMeshletState
        && sameActiveRenderPass
        && sameFramebuffer
        && !indirectParamsChanged
        && (
            !pipelineChanged
            || (
                state.framebuffer->m_staticStructureValid
                && state.framebuffer->m_allAttachmentsManagedNonVirtual
                && state.framebuffer->m_allAttachmentsExclusiveSharing
                && !m_stateTracker.hasPermanentTextureStates()
            )
        )
    ){
        if(pipelineChanged && state.pipeline){
            if(&state.pipeline->m_context != &m_context){
                rejectCommandRecording(s_SetMeshletStateOperation, FOUNDATION_TEXT("meshlet pipeline belongs to another device"));
                return;
            }
            if(!validateMeshletPipelineForFramebuffer(*state.pipeline, *state.framebuffer, s_SetMeshletStateOperation))
                return;
        }

        const b8 viewportsEqual = VulkanDetail::ViewportsEqual(m_currentMeshletState.viewport.viewports, state.viewport.viewports);
        const b8 explicitScissorsEqual = VulkanDetail::ScissorsEqual(m_currentMeshletState.viewport.scissorRects, state.viewport.scissorRects);
        const b8 updateViewport = !viewportsEqual;
        const b8 updateScissor = !explicitScissorsEqual || (state.viewport.scissorRects.empty() && !viewportsEqual);
        if(updateViewport || updateScissor){
            if(!validateViewportState(state.viewport, s_SetMeshletStateOperation))
                return;
            VulkanDetail::CopyViewportStateCompact(m_currentMeshletState.viewport, state.viewport);
        }

        const b8 blendConstantsChanged = !(m_currentMeshletState.blendConstantColor == state.blendConstantColor);
        const b8 dynamicStencilRefChanged = m_currentMeshletState.dynamicStencilRefValue != state.dynamicStencilRefValue;
        m_currentMeshletState.blendConstantColor = state.blendConstantColor;
        m_currentMeshletState.dynamicStencilRefValue = state.dynamicStencilRefValue;

        if(pipelineChanged){
            invalidateGraphicsDrawValidationCache();
            m_currentMeshletState.pipeline = state.pipeline;
            if(state.pipeline){
                auto& pipeline = *MakeNotNull(state.pipeline);
                updateActivePushConstantPipeline(s_PipelineKindMeshlet, pipeline.pipelineLayout, pipeline.pushConstantByteSize, GpuQueueCapability::Graphics);
                m_context.deviceDispatch.vkCmdBindPipeline(m_currentCmdBuf->m_cmdBuf, VK_PIPELINE_BIND_POINT_GRAPHICS, pipeline.m_pipeline);
                retainResource(&pipeline);

                const Array<f32, 4> blendConstants = {{ state.blendConstantColor.r, state.blendConstantColor.g, state.blendConstantColor.b, state.blendConstantColor.a }};
                m_context.deviceDispatch.vkCmdSetBlendConstants(m_currentCmdBuf->m_cmdBuf, blendConstants.data());

                const DepthStencilState& depthStencilState = pipeline.m_desc.renderState.depthStencilState;
                const u8 stencilReference = depthStencilState.dynamicStencilRef ? state.dynamicStencilRefValue : depthStencilState.stencilRefValue;
                m_context.deviceDispatch.vkCmdSetStencilReference(m_currentCmdBuf->m_cmdBuf, VK_STENCIL_FACE_FRONT_AND_BACK, stencilReference);
            }
            else
                updateActivePushConstantPipeline(s_PipelineKindNone, VK_NULL_HANDLE, 0u, GpuQueueCapability::None);

            if(updateViewport || updateScissor)
                setViewportState(state.viewport, updateViewport, updateScissor);
            return;
        }

        if(updateViewport || updateScissor)
            setViewportState(state.viewport, updateViewport, updateScissor);

        if(state.pipeline){
            if(blendConstantsChanged){
                const Array<f32, 4> blendConstants = {{ state.blendConstantColor.r, state.blendConstantColor.g, state.blendConstantColor.b, state.blendConstantColor.a }};
                m_context.deviceDispatch.vkCmdSetBlendConstants(m_currentCmdBuf->m_cmdBuf, blendConstants.data());
            }
            if(dynamicStencilRefChanged){
                const DepthStencilState& depthStencilState = state.pipeline->m_desc.renderState.depthStencilState;
                if(depthStencilState.dynamicStencilRef)
                    m_context.deviceDispatch.vkCmdSetStencilReference(m_currentCmdBuf->m_cmdBuf, VK_STENCIL_FACE_FRONT_AND_BACK, state.dynamicStencilRefValue);
            }
        }
        return;
    }

    if(!validateMeshletState(state))
        return;
    if((!sameActiveRenderPass || !state.framebuffer->m_allAttachmentsManagedNonVirtual) && !FramebufferRenderingPreparation(*this).prepareFramebuffer(state.framebuffer, s_SetMeshletStateOperation))
        return;

    if(state.indirectParams){
        auto& indirect = *MakeNotNull(state.indirectParams);
        setBufferStateValidated(indirect, ResourceStates::IndirectArgument, false, BufferRange(0u, indirect.m_creationDesc.byteSize));
    }
    if(!m_pendingImageBarriers.empty() || !m_pendingBufferBarriers.empty()){
        commitBarriers();
        if(m_commandRecordingFailed)
            return;
    }

    if(!ensureGraphicsRenderPass(state.framebuffer))
        return;
    if(!sameActiveRenderPass && (!m_pendingImageBarriers.empty() || !m_pendingBufferBarriers.empty())){
        commitBarriers();
        if(m_commandRecordingFailed)
            return;
    }

    const b8 blendConstantsChanged = !(m_currentMeshletState.blendConstantColor == state.blendConstantColor);
    const b8 dynamicStencilRefChanged = m_currentMeshletState.dynamicStencilRefValue != state.dynamicStencilRefValue;
    const b8 viewportsEqual = hasPreviousMeshletState && VulkanDetail::ViewportsEqual(m_currentMeshletState.viewport.viewports, state.viewport.viewports);
    const b8 explicitScissorsEqual = hasPreviousMeshletState && VulkanDetail::ScissorsEqual(m_currentMeshletState.viewport.scissorRects, state.viewport.scissorRects);
    const b8 updateViewport = !viewportsEqual;
    const b8 updateScissor = !explicitScissorsEqual || (state.viewport.scissorRects.empty() && !viewportsEqual);

    clearDirtyPipelineStates(s_DirtyMeshletState);
    invalidateGraphicsDrawValidationCache();
    m_currentMeshletState.pipeline = state.pipeline;
    m_currentMeshletState.framebuffer = state.framebuffer;
    if(updateViewport || !explicitScissorsEqual || !hasPreviousMeshletState)
        VulkanDetail::CopyViewportStateCompact(m_currentMeshletState.viewport, state.viewport);
    m_currentMeshletState.blendConstantColor = state.blendConstantColor;
    m_currentMeshletState.indirectParams = state.indirectParams;
    m_currentMeshletState.dynamicStencilRefValue = state.dynamicStencilRefValue;
    m_dirtyPipelineStateMask |= s_DirtyMeshletState;
    m_hasBoundMeshletState = true;

    if(state.pipeline){
        auto& pipeline = *MakeNotNull(state.pipeline);
        updateActivePushConstantPipeline(s_PipelineKindMeshlet, pipeline.pipelineLayout, pipeline.pushConstantByteSize, GpuQueueCapability::Graphics);
        if(pipelineChanged){
            m_context.deviceDispatch.vkCmdBindPipeline(m_currentCmdBuf->m_cmdBuf, VK_PIPELINE_BIND_POINT_GRAPHICS, pipeline.m_pipeline);
            retainResource(&pipeline);

            const Array<f32, 4> blendConstants = {{ state.blendConstantColor.r, state.blendConstantColor.g, state.blendConstantColor.b, state.blendConstantColor.a }};
            m_context.deviceDispatch.vkCmdSetBlendConstants(m_currentCmdBuf->m_cmdBuf, blendConstants.data());

            const DepthStencilState& depthStencilState = pipeline.m_desc.renderState.depthStencilState;
            const u8 stencilReference = depthStencilState.dynamicStencilRef ? state.dynamicStencilRefValue : depthStencilState.stencilRefValue;
            m_context.deviceDispatch.vkCmdSetStencilReference(m_currentCmdBuf->m_cmdBuf, VK_STENCIL_FACE_FRONT_AND_BACK, stencilReference);
        }
        else{
            if(blendConstantsChanged){
                const Array<f32, 4> blendConstants = {{ state.blendConstantColor.r, state.blendConstantColor.g, state.blendConstantColor.b, state.blendConstantColor.a }};
                m_context.deviceDispatch.vkCmdSetBlendConstants(m_currentCmdBuf->m_cmdBuf, blendConstants.data());
            }
            const DepthStencilState& depthStencilState = pipeline.m_desc.renderState.depthStencilState;
            if(depthStencilState.dynamicStencilRef && dynamicStencilRefChanged)
                m_context.deviceDispatch.vkCmdSetStencilReference(m_currentCmdBuf->m_cmdBuf, VK_STENCIL_FACE_FRONT_AND_BACK, state.dynamicStencilRefValue);
        }
    }
    else
        updateActivePushConstantPipeline(s_PipelineKindNone, VK_NULL_HANDLE, 0u, GpuQueueCapability::None);

    if(updateViewport || updateScissor)
        setViewportState(state.viewport, updateViewport, updateScissor);
    if(indirectParamsChanged)
        retainResource(state.indirectParams);
}

void CommandList::dispatchMesh(u32 groupsX, u32 groupsY, u32 groupsZ){
    if(groupsX == 0 || groupsY == 0 || groupsZ == 0)
        return;
    if(!recordAndValidateCommandCapability(GpuQueueCapability::Graphics, VulkanArenaScope::s_DispatchMeshCommandLabel))
        return;

    MeshletPipeline* const pipeline = m_currentMeshletState.pipeline;
    if(!m_renderPassActive || !m_renderPassFramebuffer || !pipeline || pipeline->m_pipeline == VK_NULL_HANDLE || pipeline->m_framebufferInfo != m_renderPassFramebuffer->m_framebufferInfo){
        rejectCommandRecording(VulkanArenaScope::s_DispatchMeshCommandLabel, FOUNDATION_TEXT("no compatible meshlet pipeline and render pass are active"));
        return;
    }
    if(m_currentMeshletState.viewport.viewports.size() != 1u || m_currentMeshletState.viewport.scissorRects.size() > 1u){
        rejectCommandRecording(VulkanArenaScope::s_DispatchMeshCommandLabel, FOUNDATION_TEXT("mesh dispatch requires one viewport and at most one explicit scissor"));
        return;
    }
    if(!m_context.extensions.extMeshShader || m_context.meshShaderFeatures.meshShader != VK_TRUE || !m_context.deviceDispatch.vkCmdDrawMeshTasksEXT){
        rejectCommandRecording(VulkanArenaScope::s_DispatchMeshCommandLabel, s_MeshShaderUnavailableReason);
        return;
    }

    const VulkanDetail::MeshDispatchLimits dispatchLimits = VulkanDetail::GetMeshDispatchLimits(m_context.meshShaderProperties, pipeline->m_desc.amplificationShader != nullptr);
    if(!VulkanDetail::AreMeshDispatchGroupCountsValid(groupsX, groupsY, groupsZ, dispatchLimits.maximumGroupCounts, dispatchLimits.maximumTotalGroupCount)){
        rejectCommandRecording(VulkanArenaScope::s_DispatchMeshCommandLabel, FOUNDATION_TEXT("mesh dispatch group counts exceed device limits"));
        return;
    }

    m_context.deviceDispatch.vkCmdDrawMeshTasksEXT(m_currentCmdBuf->m_cmdBuf, groupsX, groupsY, groupsZ);
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_VULKAN_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

