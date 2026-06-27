// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#pragma once


#include "renderer_pipeline_types.h"

#include <core/graphics/api.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


struct AvboitFrameTargets{
    u32 fullWidth = 0;
    u32 fullHeight = 0;
    u32 lowWidth = 0;
    u32 lowHeight = 0;
    u32 virtualSliceCount = 0;
    u32 physicalSliceCount = 0;
    Core::Format::Enum lowRasterFormat = Core::Format::UNKNOWN;
    Core::Format::Enum accumColorFormat = Core::Format::UNKNOWN;
    Core::Format::Enum accumExtinctionFormat = Core::Format::UNKNOWN;
    Core::Format::Enum transmittanceFormat = Core::Format::UNKNOWN;
    Core::TextureHandle lowRasterTarget;
    Core::TextureHandle accumColor;
    Core::TextureHandle accumExtinction;
    Core::TextureHandle transmittanceTexture;
    Core::FramebufferHandle lowFramebuffer;
    Core::FramebufferHandle accumulationFramebuffer;
    Core::BufferHandle coverageBuffer;
    Core::BufferHandle depthWarpBuffer;
    Core::BufferHandle controlBuffer;
    Core::BufferHandle extinctionBuffer;
    Core::BufferHandle extinctionOverflowBuffer;
    Core::BindingSetHandle occupancyBindingSet;
    Core::BindingSetHandle depthWarpBindingSet;
    Core::BindingSetHandle extinctionBindingSet;
    Core::BindingSetHandle integrateBindingSet;
    Core::BindingSetHandle accumulateBindingSet;

    [[nodiscard]] bool valid()const noexcept{
#if defined(NWB_DEBUG)
        return
            fullWidth > 0
            && fullHeight > 0
            && lowWidth > 0
            && lowHeight > 0
            && virtualSliceCount > 0
            && physicalSliceCount > 0
            && lowRasterFormat != Core::Format::UNKNOWN
            && accumColorFormat != Core::Format::UNKNOWN
            && accumExtinctionFormat != Core::Format::UNKNOWN
            && transmittanceFormat != Core::Format::UNKNOWN
            && lowRasterTarget != nullptr
            && accumColor != nullptr
            && accumExtinction != nullptr
            && transmittanceTexture != nullptr
            && lowFramebuffer != nullptr
            && accumulationFramebuffer != nullptr
            && coverageBuffer != nullptr
            && depthWarpBuffer != nullptr
            && controlBuffer != nullptr
            && extinctionBuffer != nullptr
            && extinctionOverflowBuffer != nullptr
            && occupancyBindingSet != nullptr
            && depthWarpBindingSet != nullptr
            && extinctionBindingSet != nullptr
            && integrateBindingSet != nullptr
            && accumulateBindingSet != nullptr
        ;
#else
        return accumulationFramebuffer != nullptr;
#endif
    }
};

struct MaterialPassDrawContext{
    Core::CommandList& commandList;
    Core::Framebuffer* framebuffer = nullptr;
    MaterialPipelinePass::Enum pass = MaterialPipelinePass::Opaque;
    Core::BindingSet* passBindingSet = nullptr;
    const AvboitFrameTargets* avboitTargets = nullptr;
    const Core::ViewportState& viewportState;
};

struct DeferredFrameTargets{
    u32 width = 0;
    u32 height = 0;
    Core::Format::Enum albedoFormat = Core::Format::UNKNOWN;
    Core::Format::Enum normalFormat = Core::Format::UNKNOWN;
    Core::Format::Enum worldPositionFormat = Core::Format::UNKNOWN;
    Core::Format::Enum opaqueColorFormat = Core::Format::UNKNOWN;
    Core::Format::Enum depthFormat = Core::Format::UNKNOWN;
    Core::Format::Enum shadowVisibilityFormat = Core::Format::UNKNOWN;
    Core::Format::Enum causticIrradianceFormat = Core::Format::UNKNOWN;
    Core::Format::Enum causticAccumulatorFormat = Core::Format::UNKNOWN;
    Core::Format::Enum causticHistoryFormat = Core::Format::UNKNOWN;
    Core::Format::Enum csgCapNormalFormat = Core::Format::UNKNOWN;
    Core::Format::Enum csgIntervalDepthFormat = Core::Format::UNKNOWN;
    Core::Format::Enum csgIntervalIdFormat = Core::Format::UNKNOWN;
    Core::Format::Enum csgReceiverEventDataFormat = Core::Format::UNKNOWN;
    Core::Format::Enum csgReceiverEventCountFormat = Core::Format::UNKNOWN;
    Core::Format::Enum csgReceiverSpanDataFormat = Core::Format::UNKNOWN;
    Core::Format::Enum csgReceiverSpanCountFormat = Core::Format::UNKNOWN;
    Core::Format::Enum csgRemovedIntervalDepthFormat = Core::Format::UNKNOWN;
    Core::Format::Enum csgRemovedIntervalCapNormalFormat = Core::Format::UNKNOWN;
    Core::Format::Enum csgRemovedIntervalDataFormat = Core::Format::UNKNOWN;
    Core::Format::Enum csgRemovedIntervalCountFormat = Core::Format::UNKNOWN;
    u32 csgPeelLayerCount = 0u;
    u32 csgReceiverEventLayerCount = 0u;
    u32 csgReceiverSpanLayerCount = 0u;
    u32 csgRemovedIntervalLayerCount = 0u;
    Core::TextureHandle albedo;
    Core::TextureHandle normal;
    Core::TextureHandle worldPosition;
    Core::TextureHandle csgCapBackNormal;
    Core::TextureHandle csgIntervalDepth;
    Core::TextureHandle csgIntervalId;
    Core::TextureHandle csgReceiverEventData;
    Core::TextureHandle csgReceiverEventCount;
    Core::TextureHandle csgReceiverSpanData;
    Core::TextureHandle csgReceiverSpanCount;
    Core::TextureHandle csgRemovedIntervalDepth;
    Core::TextureHandle csgRemovedIntervalCapNormal;
    Core::TextureHandle csgRemovedIntervalData;
    Core::TextureHandle csgRemovedIntervalCount;
    Core::TextureHandle opaqueColor;
    Core::TextureHandle depth;
    Core::TextureHandle shadowVisibility;
    // Half-res shadow visibility the hardware ray-traced backend writes (1 occlusion ray per half-res pixel, 1/4 the
    // rays); shadow_upsample_cs edge-aware upsamples it into the full-res shadowVisibility the lighting samples. Same
    // RGBA16F format + NWB_SCENE_SHADOW_SLOT_COUNT layers, half the spatial extent.
    Core::TextureHandle shadowVisibilityHalf;
    // Caustic producer targets (additive, inverted lifecycle vs shadowVisibility): the RGBA16F resolved irradiance
    // the deferred lighting pass adds pre-tonemap, the R32_UINT splat accumulators (one Texture2DArray layer per RGB
    // channel) the producer's fixed-point InterlockedAdd lands in, and the RGBA16F a-trous wavelet scratch buffer.
    // The irradiance + accumulator are cleared to BLACK each frame (additive identity), so when no producer runs the
    // additive caustic term is a pixel-identical no-op.
    Core::TextureHandle causticIrradiance;
    Core::TextureHandle causticAccumulator;
    // The two HALF-res RGBA16F ping-pong buffers for the half-res a-trous wavelet (caustic_resolve_cs.slang): the prepare
    // pass writes causticHistory (half-A), the wavelet alternates causticHistory <-> causticResolveHalf, and the final
    // half-res result is bilinearly UPSAMPLED into the full-res causticIrradiance the lighting samples. Both are half the
    // full render extent (1/4 the pixels) and need no clear (every pass fully overwrites; the resolve is purely spatial).
    Core::TextureHandle causticHistory;
    Core::TextureHandle causticResolveHalf;
    // Half-res geometry cache (RGBA16F: xyz = world position, w = receiver validity) the geometry downsample pre-pass
    // fills once per frame so the resolve's wavelet passes tap one half-res texel instead of re-reading the full-res
    // world-position + depth G-buffer per a-trous tap (a read-bandwidth cut on the half-res dispatch).
    Core::TextureHandle causticResolveGeometry;
    Core::FramebufferHandle framebuffer;
    Core::FramebufferHandle opaqueLightingFramebuffer;
    Core::BindingSetHandle lightingBindingSet;
    Core::BindingSetHandle compositeBindingSet;
    AvboitFrameTargets avboit;

    [[nodiscard]] bool csgIntervalTargetsValid()const noexcept{
        return
            csgCapNormalFormat != Core::Format::UNKNOWN
            && csgIntervalDepthFormat != Core::Format::UNKNOWN
            && csgIntervalIdFormat != Core::Format::UNKNOWN
            && csgReceiverEventDataFormat != Core::Format::UNKNOWN
            && csgReceiverEventCountFormat != Core::Format::UNKNOWN
            && csgReceiverSpanDataFormat != Core::Format::UNKNOWN
            && csgReceiverSpanCountFormat != Core::Format::UNKNOWN
            && csgRemovedIntervalDepthFormat != Core::Format::UNKNOWN
            && csgRemovedIntervalCapNormalFormat != Core::Format::UNKNOWN
            && csgRemovedIntervalDataFormat != Core::Format::UNKNOWN
            && csgRemovedIntervalCountFormat != Core::Format::UNKNOWN
            && csgPeelLayerCount > 0u
            && csgReceiverEventLayerCount > 0u
            && csgReceiverSpanLayerCount > 0u
            && csgRemovedIntervalLayerCount > 0u
            && csgCapBackNormal != nullptr
            && csgIntervalDepth != nullptr
            && csgIntervalId != nullptr
            && csgReceiverEventData != nullptr
            && csgReceiverEventCount != nullptr
            && csgReceiverSpanData != nullptr
            && csgReceiverSpanCount != nullptr
            && csgRemovedIntervalDepth != nullptr
            && csgRemovedIntervalCapNormal != nullptr
            && csgRemovedIntervalData != nullptr
            && csgRemovedIntervalCount != nullptr
        ;
    }

    [[nodiscard]] bool valid()const noexcept{
#if defined(NWB_DEBUG)
        return
            width > 0
            && height > 0
            && albedoFormat != Core::Format::UNKNOWN
            && normalFormat != Core::Format::UNKNOWN
            && worldPositionFormat != Core::Format::UNKNOWN
            && opaqueColorFormat != Core::Format::UNKNOWN
            && depthFormat != Core::Format::UNKNOWN
            && csgCapNormalFormat != Core::Format::UNKNOWN
            && csgIntervalDepthFormat != Core::Format::UNKNOWN
            && csgIntervalIdFormat != Core::Format::UNKNOWN
            && csgReceiverEventDataFormat != Core::Format::UNKNOWN
            && csgReceiverEventCountFormat != Core::Format::UNKNOWN
            && csgReceiverSpanDataFormat != Core::Format::UNKNOWN
            && csgReceiverSpanCountFormat != Core::Format::UNKNOWN
            && csgRemovedIntervalDepthFormat != Core::Format::UNKNOWN
            && csgRemovedIntervalCapNormalFormat != Core::Format::UNKNOWN
            && csgRemovedIntervalDataFormat != Core::Format::UNKNOWN
            && csgRemovedIntervalCountFormat != Core::Format::UNKNOWN
            && csgPeelLayerCount > 0u
            && csgReceiverEventLayerCount > 0u
            && csgReceiverSpanLayerCount > 0u
            && csgRemovedIntervalLayerCount > 0u
            && csgIntervalTargetsValid()
            && albedo != nullptr
            && normal != nullptr
            && worldPosition != nullptr
            && opaqueColor != nullptr
            && depth != nullptr
            && framebuffer != nullptr
            && opaqueLightingFramebuffer != nullptr
            && lightingBindingSet != nullptr
            && compositeBindingSet != nullptr
            && avboit.valid()
        ;
#else
        return framebuffer != nullptr;
#endif
    }
};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

