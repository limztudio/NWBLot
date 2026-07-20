// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include <impl/ecs_render/kernel/renderer_private.h>

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
        bindingLayoutDesc.addItem(Core::BindingLayoutItem::ConstantBuffer(NWB_DEFERRED_LIGHTING_BINDING_SCENE_SHADING, 1));
        bindingLayoutDesc.addItem(Core::BindingLayoutItem::StructuredBuffer_SRV(NWB_DEFERRED_LIGHTING_BINDING_LIGHT_LIST, 1));
        bindingLayoutDesc.addItem(Core::BindingLayoutItem::Texture_SRV(NWB_DEFERRED_LIGHTING_BINDING_SHADOW_VISIBILITY, 1));
        bindingLayoutDesc.addItem(Core::BindingLayoutItem::Texture_SRV(NWB_DEFERRED_LIGHTING_BINDING_CAUSTIC_IRRADIANCE, 1));

        // Surfel GI: a single RESOLVED screen-space irradiance texture (RGBA16F) the surfel_resolve_cs COMPUTE pass
        // writes and this pixel shader samples. Reading the resolved texture (not the read-write surfel pool) keeps the
        // pool off the pixel shader (compute-only), eliminating the frames-in-flight pool race. Created with the
        // deferred targets, so it is bound at set creation (no lazy rebuild). Cleared to 0 (a=0) -> hemiAmbient no-op.
        bindingLayoutDesc.addItem(Core::BindingLayoutItem::Texture_SRV(NWB_DEFERRED_LIGHTING_BINDING_GI_SURFEL_IRRADIANCE, 1));

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

    // The deferred lighting shader is the engine harness; it includes the cook-generated BXDF dispatch module
    // assembled from every material's `bxdf`. The engine ships no default BXDF and projects do not select a
    // lighting shader -- shading is entirely material-driven (see EmitDeferredBxdfDispatchModule).
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

    // Caustic-light classification (P1): rank the opted-in directional/spot lights and assign a caustic slot into
    // each chosen light's params.w, gated on the scene holding at least one refractive instance (gathered earlier this
    // frame by prepareCausticEmissionTargets into the ray-tracing state).
    const u32 refractiveInstanceCount = rayTracingState().m_causticRefractiveInstanceCount;
    const u32 causticLightCount = ECSRenderDetail::ResolveCausticLights(lightData, lightCount, refractiveInstanceCount);
    rayTracingState().m_causticLightCount = causticLightCount;
    // Active shadow slots = the importance-ranked pool ResolveSceneLights filled (slots 0..min(lightCount,N)-1); the
    // half-res shadow upsample reads this so it only reconstructs the slots that hold a light.
    rayTracingState().m_shadowSlotCount = (lightCount < NWB_SCENE_SHADOW_SLOT_COUNT) ? lightCount : NWB_SCENE_SHADOW_SLOT_COUNT;
    // Soft opaque shadow (all light types): record which shadow slots hold a light (params.z >= 0), regardless of type.
    // The soft path traces + denoises + upsamples exactly these slots (once per set bit): a directional light softens by
    // its constant angular radius, a point/spot light by the distance-dependent cone its source sphere subtends -- both
    // handled inside the trace, so every slot light is soft. A light can land on any slot index (the slot allocator
    // ranks by importance, not type), so this is a scattered bitmask, not a contiguous range.
    u32 softShadowSlotMask = 0u;
    for(u32 i = 0u; i < lightCount; ++i){
        const f32 slot = lightData[i].params.z;
        if(slot >= 0.f){
            const u32 slotIndex = static_cast<u32>(slot);
            if(slotIndex < NWB_SCENE_SHADOW_SLOT_COUNT)
                softShadowSlotMask |= (1u << slotIndex);
        }
    }
    rayTracingState().m_softShadowSlotMask = softShadowSlotMask;
    logCausticClassificationOnce(lightData, lightCount, causticLightCount, refractiveInstanceCount);

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

    // The resolved surfel-irradiance texture is created with the deferred targets and bound at creation, so the lighting
    // binding set needs no per-frame rebuild for GI.

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

void RendererDeferredSystem::logCausticClassificationOnce(
    const ECSRenderDetail::SceneLightGpuData* lights,
    const u32 lightCount,
    const u32 causticLightCount,
    const u32 refractiveInstanceCount
){
    // P1 gate observable: emit ONCE (rate-limited by the ray-tracing-state flag, not per-frame spam) the chosen
    // opted-in caustic lights + the refractive emission targets, so a smoke run can confirm the classification +
    // gather without any rendering change. Reports the caustic-light count, the refractive emission-target AABB count
    // + their combined world extent, then one line per chosen caustic light (slot, light index, type).
    if(rayTracingState().m_causticEmissionGateLogged)
        return;
    rayTracingState().m_causticEmissionGateLogged = true;

    const Float4& boundsMin = rayTracingState().m_causticTargetBoundsMin;
    const Float4& boundsMax = rayTracingState().m_causticTargetBoundsMax;
    NWB_LOGGER_ESSENTIAL_INFO(NWB_TEXT("RendererSystem: caustic P1 -- {} caustic light(s); {} refractive emission target(s), combined extent min ({}, {}, {}) max ({}, {}, {})")
        , causticLightCount
        , refractiveInstanceCount
        , boundsMin.x
        , boundsMin.y
        , boundsMin.z
        , boundsMax.x
        , boundsMax.y
        , boundsMax.z
    );

    for(u32 i = 0u; i < lightCount; ++i){
        if(lights[i].params.w < 0.f)
            continue;
        // params.y carries the light type (Directional=0, Point=1, Spot=2); point lights are excluded so only
        // directional/spot reach here.
        const bool directional = lights[i].params.y < 0.5f;
        NWB_LOGGER_ESSENTIAL_INFO(NWB_TEXT("RendererSystem: caustic P1 -- caustic slot {} -> light index {} ({})")
            , static_cast<u32>(lights[i].params.w)
            , i
            , directional ? NWB_TEXT("directional") : NWB_TEXT("spot")
        );
    }
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

