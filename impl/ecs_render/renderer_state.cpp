// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "renderer_state.h"


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


usize MaterialPipelineKeyHasher::operator()(const MaterialPipelineKey& key)const{
    usize seed = Hasher<Name>{}(key.material);
    Core::CoreDetail::HashCombine(seed, static_cast<u32>(key.pass));
    Core::CoreDetail::HashCombine(seed, key.twoSided ? 1u : 0u);
    Core::CoreDetail::HashCombine(seed, static_cast<u32>(key.csgMode));
    Core::CoreDetail::HashCombine(seed, Hasher<Name>{}(key.csgEvaluatorVariant));
    Core::CoreDetail::HashCombine(seed, key.framebufferInfo.depthFormat);
    Core::CoreDetail::HashCombine(seed, key.framebufferInfo.sampleCount);
    Core::CoreDetail::HashCombine(seed, key.framebufferInfo.sampleQuality);
    for(const Core::Format::Enum format : key.framebufferInfo.colorFormats)
        Core::CoreDetail::HashCombine(seed, format);

    return seed;
}

bool MaterialPipelineKeyEqualTo::operator()(const MaterialPipelineKey& lhs, const MaterialPipelineKey& rhs)const{
    return
        lhs.material == rhs.material
        && lhs.pass == rhs.pass
        && lhs.twoSided == rhs.twoSided
        && lhs.csgMode == rhs.csgMode
        && lhs.csgEvaluatorVariant == rhs.csgEvaluatorVariant
        && lhs.framebufferInfo == rhs.framebufferInfo
    ;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


RendererMeshState::RendererMeshState(Core::Alloc::GlobalArena& arena)
    : m_meshes(0, Hasher<Name>(), EqualTo<Name>(), arena)
{}


void RendererMeshState::invalidateResources(){
    m_meshes.clear();
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


RendererMaterialState::RendererMaterialState(Core::Alloc::GlobalArena& arena)
    : m_surfaceInfos(0, Hasher<Name>(), EqualTo<Name>(), arena)
    , m_pipelines(0, MaterialPipelineKeyHasher(), MaterialPipelineKeyEqualTo(), arena)
    , m_instanceMutableCache(0, Hasher<Core::ECS::EntityID>(), EqualTo<Core::ECS::EntityID>(), arena)
    , m_loggedMaterialPaths(0, Hasher<Name>(), EqualTo<Name>(), arena)
{}


void RendererMaterialState::invalidateResources(){
    m_pipelines.clear();
    m_instanceMutableCache.clear();
    m_loggedMaterialPaths.clear();
    m_instanceMutableCacheComponentMutationVersion = 0u;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


void RendererDrawState::invalidateResources(){
    m_meshBindingLayout.reset();
    m_computeBindingLayout.reset();
    m_emulationViewBindingLayout.reset();
    m_instanceBuffer.reset();
    m_materialTypedBuffer.reset();
    m_meshViewBuffer.reset();
    m_emulationViewBindingSet.reset();
    m_emulationVertexShader.reset();
    m_emulationInputLayout.reset();
    m_instanceBufferCapacity = 0u;
    m_materialTypedBufferCapacity = 0u;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


void RendererCsgState::invalidateResources(){
    m_clipBindingLayout.reset();
    m_clipBindingSet.reset();
    m_receiverRangeBuffer.reset();
    m_cutterBuffer.reset();
    m_parameterByteBuffer.reset();
    m_capVertexBuffer.reset();
    m_capProxyBuffer.reset();
    m_capVertexShader.reset();
    m_capPixelShader.reset();
    m_capProxyPlaneMeshShader.reset();
    m_capProxyBoxMeshShader.reset();
    m_capProxySphereMeshShader.reset();
    m_capProxyCapsuleMeshShader.reset();
    m_capAvboitOccupancyPixelShader.reset();
    m_capAvboitExtinctionPixelShader.reset();
    m_capAvboitAccumulatePixelShader.reset();
    m_capInputLayout.reset();
    m_capProxyBindingLayout.reset();
    m_capProxyBindingSet.reset();
    m_capPipeline.reset();
    m_capAvboitOccupancyPipeline.reset();
    m_capAvboitExtinctionPipeline.reset();
    m_capAvboitAccumulatePipeline.reset();
    m_capProxyPlanePipeline.reset();
    m_capProxyBoxPipeline.reset();
    m_capProxySpherePipeline.reset();
    m_capProxyCapsulePipeline.reset();
    m_receiverRangeBufferCapacity = 0u;
    m_cutterBufferCapacity = 0u;
    m_parameterByteBufferCapacity = 0u;
    m_capVertexBufferCapacity = 0u;
    m_capProxyBufferCapacity = 0u;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


void RendererDeferredState::invalidateResources(){
    m_lightingBindingLayout.reset();
    m_sceneShadingBuffer.reset();
    m_compositeVertexShader.reset();
    m_lightingPixelShader.reset();
    m_lightingPipeline.reset();
    m_compositeBindingLayout.reset();
    m_sampler.reset();
    m_compositePixelShader.reset();
    m_compositePipeline.reset();
    m_targets = DeferredFrameTargets{};
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


void RendererAvboitState::invalidateResources(){
    m_emptyBindingLayout.reset();
    m_occupancyBindingLayout.reset();
    m_depthWarpBindingLayout.reset();
    m_extinctionBindingLayout.reset();
    m_integrateBindingLayout.reset();
    m_accumulateBindingLayout.reset();
    m_linearSampler.reset();
    m_occupancyPixelShader.reset();
    m_depthWarpComputeShader.reset();
    m_extinctionPixelShader.reset();
    m_integrateComputeShader.reset();
    m_accumulatePixelShader.reset();
    m_depthWarpPipeline.reset();
    m_integratePipeline.reset();
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

