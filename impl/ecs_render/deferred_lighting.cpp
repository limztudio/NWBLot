// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "renderer_private.h"

#include <impl/assets/graphics/deferred/names.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


bool RendererDeferredSystem::createDeferredLightingResources(){
    auto* device = graphics().getDevice();

    if(!deferredState().m_sceneShadingBuffer){
        Core::BufferDesc sceneShadingBufferDesc;
        sceneShadingBufferDesc
            .setByteSize(sizeof(ECSRenderDetail::SceneShadingGpuData))
            .setIsConstantBuffer(true)
            .setDebugName(ECSRenderDetail::s_SceneShadingBufferName)
            .enableAutomaticStateTracking(Core::ResourceStates::Common)
        ;
        deferredState().m_sceneShadingBuffer = graphics().createBuffer(sceneShadingBufferDesc);
        if(!deferredState().m_sceneShadingBuffer){
            NWB_LOGGER_ERROR(NWB_TEXT("RendererSystem: failed to create scene shading buffer"));
            return false;
        }
    }

    if(!deferredState().m_lightBuffer){
        Core::BufferDesc lightBufferDesc;
        lightBufferDesc
            .setByteSize(static_cast<u64>(sizeof(ECSRenderDetail::SceneLightGpuData) * NWB_SCENE_MAX_LIGHTS))
            .setStructStride(sizeof(ECSRenderDetail::SceneLightGpuData))
            .setDebugName(ECSRenderDetail::s_SceneLightBufferName)
            .enableAutomaticStateTracking(Core::ResourceStates::Common)
        ;
        deferredState().m_lightBuffer = graphics().createBuffer(lightBufferDesc);
        if(!deferredState().m_lightBuffer){
            NWB_LOGGER_ERROR(NWB_TEXT("RendererSystem: failed to create scene light buffer"));
            return false;
        }
    }

    if(!deferredState().m_lightingBindingLayout){
        Core::BindingLayoutDesc bindingLayoutDesc(arena());
        bindingLayoutDesc.setVisibility(Core::ShaderType::Pixel);
        bindingLayoutDesc.addItem(Core::BindingLayoutItem::Texture_SRV(NWB_DEFERRED_LIGHTING_BINDING_GBUFFER_BASE_COLOR, 1));
        bindingLayoutDesc.addItem(Core::BindingLayoutItem::Texture_SRV(NWB_DEFERRED_LIGHTING_BINDING_GBUFFER_NORMAL, 1));
        bindingLayoutDesc.addItem(Core::BindingLayoutItem::Texture_SRV(NWB_DEFERRED_LIGHTING_BINDING_GBUFFER_WORLD_POSITION, 1));
        bindingLayoutDesc.addItem(Core::BindingLayoutItem::Texture_SRV(NWB_DEFERRED_LIGHTING_BINDING_GBUFFER_DEPTH, 1));
        bindingLayoutDesc.addItem(Core::BindingLayoutItem::Sampler(NWB_DEFERRED_LIGHTING_BINDING_SAMPLER, 1));
        bindingLayoutDesc.addItem(Core::BindingLayoutItem::ConstantBuffer(NWB_SCENE_SHADING_DEFERRED_LIGHTING_BINDING, 1));
        bindingLayoutDesc.addItem(Core::BindingLayoutItem::StructuredBuffer_SRV(NWB_SCENE_LIGHT_LIST_DEFERRED_LIGHTING_BINDING, 1));

        deferredState().m_lightingBindingLayout = device->createBindingLayout(bindingLayoutDesc);
        if(!deferredState().m_lightingBindingLayout){
            NWB_LOGGER_ERROR(NWB_TEXT("RendererSystem: failed to create deferred lighting binding layout"));
            return false;
        }
    }

    if(!ECSRenderDetail::CreateClampSampler(*device, deferredState().m_sampler, false)){
        NWB_LOGGER_ERROR(NWB_TEXT("RendererSystem: failed to create deferred lighting sampler"));
        return false;
    }

    if(!m_renderer.shaderSystem().loadDeferredCompositeVertexShader())
        return false;

    if(!m_renderer.shaderSystem().loadShader(
        deferredState().m_lightingPixelShader,
        AssetsGraphicsDeferred::s_LightingPixelShaderName,
        Core::ShaderArchive::s_DefaultVariant,
        Core::ShaderType::Pixel,
        "ECSRender_DeferredLightingPS"
    ))
        return false;

    return true;
}

bool RendererDeferredSystem::createDeferredLightingPipeline(DeferredFrameTargets& targets){
    if(!targets.opaqueLightingFramebuffer)
        return false;
    if(!createDeferredLightingResources())
        return false;

    const Core::FramebufferInfo& framebufferInfo = targets.opaqueLightingFramebuffer->getFramebufferInfo();
    if(deferredState().m_lightingPipeline && deferredState().m_lightingPipeline->getFramebufferInfo() == framebufferInfo)
        return true;

    Core::GraphicsPipelineDesc pipelineDesc;
    pipelineDesc
        .setVertexShader(deferredState().m_compositeVertexShader)
        .setPixelShader(deferredState().m_lightingPixelShader)
        .setRenderState(ECSRenderDetail::BuildCompositeRenderState())
        .addBindingLayout(deferredState().m_lightingBindingLayout)
    ;

    auto* device = graphics().getDevice();
    deferredState().m_lightingPipeline = device->createGraphicsPipeline(pipelineDesc, framebufferInfo);
    if(!deferredState().m_lightingPipeline){
        NWB_LOGGER_ERROR(NWB_TEXT("RendererSystem: failed to create deferred lighting pipeline"));
        return false;
    }

    return true;
}

bool RendererDeferredSystem::updateSceneShadingBuffer(Core::CommandList& commandList, const f32 fallbackAspectRatio){
    NWB_ASSERT(deferredState().m_sceneShadingBuffer);
    NWB_ASSERT(deferredState().m_lightBuffer);

    ECSRenderDetail::SceneLightGpuData lightData[NWB_SCENE_MAX_LIGHTS];
    const u32 lightCount = ECSRenderDetail::ResolveSceneLights(world(), lightData, NWB_SCENE_MAX_LIGHTS);

    commandList.setBufferState(deferredState().m_lightBuffer.get(), Core::ResourceStates::CopyDest);
    commandList.commitBarriers();
    commandList.writeBuffer(deferredState().m_lightBuffer.get(), lightData, static_cast<usize>(lightCount) * sizeof(ECSRenderDetail::SceneLightGpuData));
    commandList.setBufferState(deferredState().m_lightBuffer.get(), Core::ResourceStates::ShaderResource);
    commandList.commitBarriers();

    const ECSRenderDetail::SceneShadingGpuData sceneShadingState = ECSRenderDetail::ResolveSceneShadingState(world(), fallbackAspectRatio, lightCount);
    if(
        deferredState().m_sceneShadingGpuDataValid
        && NWB_MEMCMP(deferredState().m_sceneShadingGpuData, &sceneShadingState, sizeof(sceneShadingState)) == 0
    )
        return true;

    commandList.setBufferState(deferredState().m_sceneShadingBuffer.get(), Core::ResourceStates::CopyDest);
    commandList.commitBarriers();
    commandList.writeBuffer(deferredState().m_sceneShadingBuffer.get(), &sceneShadingState, sizeof(sceneShadingState));
    commandList.setBufferState(deferredState().m_sceneShadingBuffer.get(), Core::ResourceStates::ConstantBuffer);
    commandList.commitBarriers();
    NWB_MEMCPY(
        deferredState().m_sceneShadingGpuData,
        sizeof(deferredState().m_sceneShadingGpuData),
        &sceneShadingState,
        sizeof(sceneShadingState)
    );
    deferredState().m_sceneShadingGpuDataValid = true;
    return true;
}

bool RendererDeferredSystem::renderDeferredLighting(Core::CommandList& commandList, DeferredFrameTargets& targets){
    NWB_ASSERT(targets.lightingBindingSet);
    NWB_ASSERT(targets.opaqueLightingFramebuffer);
    NWB_ASSERT(deferredState().m_lightingPipeline);

    Core::GpuTimingMeasure timing(graphics().gpuTiming(), RendererGpuTimingScope::s_DeferredLighting, graphics().getDevice(), commandList);

    commandList.setResourceStatesForBindingSet(targets.lightingBindingSet.get());
    commandList.commitBarriers();

    Core::ViewportState viewportState;
    viewportState.addViewportAndScissorRect(targets.opaqueLightingFramebuffer->getFramebufferInfo().getViewport());

    Core::GraphicsState graphicsState;
    graphicsState.setPipeline(deferredState().m_lightingPipeline.get());
    graphicsState.setFramebuffer(targets.opaqueLightingFramebuffer.get());
    graphicsState.setViewport(viewportState);
    graphicsState.addBindingSet(targets.lightingBindingSet.get());

    commandList.setGraphicsState(graphicsState);

    Core::DrawArguments drawArgs;
    drawArgs.setVertexCount(3);
    commandList.draw(drawArgs);
    commandList.endRenderPass();
    return true;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

