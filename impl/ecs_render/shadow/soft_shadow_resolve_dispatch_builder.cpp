// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "soft_shadow_resolve_dispatch_builder.h"

#include <impl/ecs_render/raytrace/rt_private.h>
#include <impl/ecs_render/raytrace/renderer_raytracing_state.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


SoftShadowResolveDispatch SoftShadowResolveDispatchBuilder::Opaque(const OpaqueSoftShadowResolveOptions& options, const RendererRayTracingState& state, DeferredFrameTargets& targets)noexcept{
    Core::Texture* const opaqueResolveMoments = options.frontIsA ? targets.shadowMomentsB.get() : targets.shadowMomentsA.get();
    const u32 opaqueResolveMomentsSlot = options.frontIsA ? targets.bindless.shadowMomentsB.slot() : targets.bindless.shadowMomentsA.slot();
    Core::Texture* const opaqueWaveletInput = options.temporalActive ? (options.frontIsA ? targets.shadowHistB.get() : targets.shadowHistA.get()) : targets.shadowSoftHalfA.get();
    const u32 opaqueWaveletInputSlot = options.temporalActive ? (options.frontIsA ? targets.bindless.shadowHistB.slot() : targets.bindless.shadowHistA.slot()) : targets.bindless.shadowSoftHalfA.slot();
    SoftShadowResolveDispatch opaqueDispatch;
    opaqueDispatch.waveletPipeline = state.m_softShadowResolve.m_scalar.m_wavelet.m_pipeline.get();
    opaqueDispatch.upsamplePipeline = state.m_softShadowResolve.m_scalar.m_upsample.m_pipeline.get();
    opaqueDispatch.firstWaveletResources = {
        opaqueWaveletInput, opaqueResolveMoments, targets.shadowSoftHalfB.get(),
        opaqueWaveletInputSlot, opaqueResolveMomentsSlot, targets.bindless.shadowSoftHalfBStorage.slot()
    };
    opaqueDispatch.outputHalfAResources = {
        targets.shadowSoftHalfB.get(), opaqueResolveMoments, targets.shadowSoftHalfA.get(),
        targets.bindless.shadowSoftHalfB.slot(), opaqueResolveMomentsSlot, targets.bindless.shadowSoftHalfAStorage.slot()
    };
    opaqueDispatch.outputHalfBResources = {
        targets.shadowSoftHalfA.get(), opaqueResolveMoments, targets.shadowSoftHalfB.get(),
        targets.bindless.shadowSoftHalfA.slot(), opaqueResolveMomentsSlot, targets.bindless.shadowSoftHalfBStorage.slot()
    };
    opaqueDispatch.upsampleResources = {
        targets.shadowSoftHalfB.get(), opaqueResolveMoments, targets.shadowSoftHalfB.get(),
        targets.bindless.shadowSoftHalfB.slot(), opaqueResolveMomentsSlot, targets.bindless.shadowSoftHalfBStorage.slot()
    };
    opaqueDispatch.visibilityTexture = targets.shadowVisibility.get();
    opaqueDispatch.visibilityStorage = targets.bindless.shadowVisibilityStorage.slot();
    opaqueDispatch.sceneShading = targets.bindless.sceneShading.slot();
    opaqueDispatch.temporalMomentsValid = options.temporalActive;
    opaqueDispatch.graphOwnsWaveletGeometryEntryState = options.graphOwnsGeometryEntry;
    opaqueDispatch.graphOwnsUpsampleStaticEntryStates = true;
    const b8 graphOwnsDeferredOpaqueWaveletInputs = options.phase == SoftShadowOpaqueResolvePhase::WaveletOnly;
    opaqueDispatch.graphOwnsWaveletMomentsEntryState = graphOwnsDeferredOpaqueWaveletInputs;
    opaqueDispatch.graphOwnsFirstWaveletInputState = graphOwnsDeferredOpaqueWaveletInputs || (options.graphOwnsTraceToFirstWavelet && !options.temporalActive);
    opaqueDispatch.graphOwnsFirstWaveletOutputState = options.dispatchResolve && !options.dispatchResolveTail;
    const b8 graphOwnsOneWaveletOpaqueResolveTailEntryStates = !options.dispatchResolve && options.dispatchResolveTail && NWB_SHADOW_RESOLVE_PASS_COUNT == 1u;
    opaqueDispatch.graphOwnsUpsampleInputColorEntryState = graphOwnsOneWaveletOpaqueResolveTailEntryStates;
    opaqueDispatch.graphOwnsUpsampleVisibilityOutputState = graphOwnsOneWaveletOpaqueResolveTailEntryStates;
    opaqueDispatch.firstWaveletWritesHalfA = false;
    opaqueDispatch.fold = SoftShadowUpsampleFold::Overwrite;
    opaqueDispatch.waveletPassCount = static_cast<u32>(NWB_SHADOW_RESOLVE_PASS_COUNT);
    return opaqueDispatch;
}


SoftShadowResolveDispatch SoftShadowResolveDispatchBuilder::Transparent(const TransparentSoftShadowResolveOptions& options, const RendererRayTracingState& state, DeferredFrameTargets& targets)noexcept{
    Core::Texture* const transparentResolveMoments = options.frontIsA ? targets.transparentMomentsB.get() : targets.transparentMomentsA.get();
    const u32 transparentResolveMomentsSlot = options.frontIsA ? targets.bindless.transparentMomentsB.slot() : targets.bindless.transparentMomentsA.slot();
    Core::Texture* const transparentWaveletInput = options.temporalActive ? (options.frontIsA ? targets.transparentHistB.get() : targets.transparentHistA.get()) : targets.transparentSoftHalf.get();
    const u32 transparentWaveletInputSlot = options.temporalActive ? (options.frontIsA ? targets.bindless.transparentHistB.slot() : targets.bindless.transparentHistA.slot()) : targets.bindless.transparentSoftHalf.slot();
    SoftShadowResolveDispatch transparentDispatch;
    transparentDispatch.waveletPipeline = state.m_softShadowResolve.m_rgb.m_wavelet.m_pipeline.get();
    transparentDispatch.upsamplePipeline = state.m_softShadowResolve.m_rgb.m_upsample.m_pipeline.get();
    transparentDispatch.firstWaveletResources = {
        transparentWaveletInput, transparentResolveMoments, targets.shadowSoftHalfA.get(),
        transparentWaveletInputSlot, transparentResolveMomentsSlot, targets.bindless.shadowSoftHalfAStorage.slot()
    };
    transparentDispatch.outputHalfAResources = {
        targets.shadowSoftHalfB.get(), transparentResolveMoments, targets.shadowSoftHalfA.get(),
        targets.bindless.shadowSoftHalfB.slot(), transparentResolveMomentsSlot, targets.bindless.shadowSoftHalfAStorage.slot()
    };
    transparentDispatch.outputHalfBResources = {
        targets.shadowSoftHalfA.get(), transparentResolveMoments, targets.shadowSoftHalfB.get(),
        targets.bindless.shadowSoftHalfA.slot(), transparentResolveMomentsSlot, targets.bindless.shadowSoftHalfBStorage.slot()
    };
    transparentDispatch.upsampleResources = {
        targets.shadowSoftHalfA.get(), transparentResolveMoments, targets.shadowSoftHalfA.get(),
        targets.bindless.shadowSoftHalfA.slot(), transparentResolveMomentsSlot, targets.bindless.shadowSoftHalfAStorage.slot()
    };
    transparentDispatch.visibilityTexture = targets.shadowVisibility.get();
    transparentDispatch.visibilityStorage = targets.bindless.shadowVisibilityStorage.slot();
    transparentDispatch.sceneShading = targets.bindless.sceneShading.slot();
    transparentDispatch.temporalMomentsValid = options.temporalActive;
    const b8 graphOwnsTransparentTemporalMergeToWaveletBoundary = options.dispatchResolve && options.splitResolve && !options.dispatchTemporalMerge && options.temporalActive;
    transparentDispatch.graphOwnsFirstWaveletInputState = (options.graphOwnsTraceToResolve && !options.temporalActive) || graphOwnsTransparentTemporalMergeToWaveletBoundary;
    transparentDispatch.graphOwnsWaveletMomentsEntryState = graphOwnsTransparentTemporalMergeToWaveletBoundary;
    transparentDispatch.graphOwnsWaveletGeometryEntryState = options.graphOwnsTraceToResolve;
    transparentDispatch.graphOwnsUpsampleStaticEntryStates = true;
    transparentDispatch.graphOwnsFirstWaveletOutputState = options.dispatchResolve && options.splitResolve;
    const b8 graphOwnsOneWaveletTransparentResolveTailEntryStates = !options.dispatchResolve && options.dispatchResolveTail && NWB_SHADOW_RESOLVE_TRANSPARENT_PASS_COUNT == 1u;
    transparentDispatch.graphOwnsUpsampleInputColorEntryState = graphOwnsOneWaveletTransparentResolveTailEntryStates;
    transparentDispatch.graphOwnsUpsampleVisibilityOutputState = graphOwnsOneWaveletTransparentResolveTailEntryStates;
    transparentDispatch.firstWaveletWritesHalfA = true;
    transparentDispatch.fold = SoftShadowUpsampleFold::Multiply;
    transparentDispatch.waveletPassCount = static_cast<u32>(NWB_SHADOW_RESOLVE_TRANSPARENT_PASS_COUNT);
    return transparentDispatch;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

