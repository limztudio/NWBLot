// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#pragma once


#include "soft_shadow_wavelet.h"


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


class RendererRayTracingState;
struct DeferredFrameTargets;


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


struct OpaqueSoftShadowResolveOptions{
    b8 frontIsA = false;
    b8 temporalActive = false;
    b8 graphOwnsGeometryEntry = false;
    b8 graphOwnsTraceToFirstWavelet = false;
    b8 dispatchResolve = false;
    b8 dispatchResolveTail = false;
    SoftShadowOpaqueResolvePhase::Enum phase = SoftShadowOpaqueResolvePhase::TemporalAndWavelet;
};

struct TransparentSoftShadowResolveOptions{
    b8 frontIsA = false;
    b8 temporalActive = false;
    b8 dispatchResolve = false;
    b8 dispatchResolveTail = false;
    b8 splitResolve = false;
    b8 dispatchTemporalMerge = false;
    b8 graphOwnsTraceToResolve = false;
};


class SoftShadowResolveDispatchBuilder final{
public:
    [[nodiscard]] static SoftShadowResolveDispatch Opaque(const OpaqueSoftShadowResolveOptions& options, const RendererRayTracingState& state, DeferredFrameTargets& targets)noexcept;
    [[nodiscard]] static SoftShadowResolveDispatch Transparent(const TransparentSoftShadowResolveOptions& options, const RendererRayTracingState& state, DeferredFrameTargets& targets)noexcept;
};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

