// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#pragma once


#include <impl/ecs_render/material/renderer_pipeline_types.h>

#include <core/graphics/api.h>
#include <core/graphics/rhi/gpu_descriptor_heap.h>


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
    // All material-driven AVBOIT passes use the target-generation bindless resource slots, including CSG.
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

// Per-frame slot indirection for ordinary-pass bindless consumers. The data is a std140-compatible sequence of four
// uint4 lanes in deferred/bindless_resources.slangi. Descriptor handles retain their class tag on
// the CPU; shaders need only the global slot because each field names the descriptor array it indexes.
struct DeferredBindlessResourceSlots{
    u32 gbufferBaseColor = 0u;
    u32 gbufferNormal = 0u;
    u32 gbufferWorldPosition = 0u;
    u32 gbufferDepth = 0u;

    u32 shadowVisibility = 0u;
    u32 causticIrradiance = 0u;
    u32 surfelIrradiance = 0u;
    u32 sampler = 0u;

    u32 opaqueColor = 0u;
    u32 avboitAccumColor = 0u;
    u32 avboitAccumExtinction = 0u;
    u32 _pad = 0u;

    u32 avboitTransmittance = 0u;
    u32 avboitLinearSampler = 0u;
    u32 _avboitPad0 = 0u;
    u32 _avboitPad1 = 0u;
};
static_assert(sizeof(DeferredBindlessResourceSlots) == sizeof(u32) * 16u, "Deferred bindless slots must match four std140 uint4 lanes");

// Heap registrations are owned by the deferred-target generation. Resize/recreate frees each handle through the
// heap's deferred retirement path before releasing the texture it points at; the slot buffer is shared by shadows,
// lighting, and compositing because all consume the same frame generation.
struct DeferredBindlessFrameResources{
    Core::BufferHandle slotsBuffer;
    DeferredBindlessResourceSlots slots;
    Core::GpuDescriptorHandle gbufferBaseColor = Core::GpuDescriptorHandle::invalid();
    Core::GpuDescriptorHandle gbufferNormal = Core::GpuDescriptorHandle::invalid();
    Core::GpuDescriptorHandle gbufferWorldPosition = Core::GpuDescriptorHandle::invalid();
    Core::GpuDescriptorHandle gbufferDepth = Core::GpuDescriptorHandle::invalid();
    Core::GpuDescriptorHandle shadowVisibility = Core::GpuDescriptorHandle::invalid();
    Core::GpuDescriptorHandle causticIrradiance = Core::GpuDescriptorHandle::invalid();
    Core::GpuDescriptorHandle surfelIrradiance = Core::GpuDescriptorHandle::invalid();
    // The surfel resolve/upsample compute pair selects this half-resolution input together with the existing G-buffer
    // slots through push constants. The full-resolution irradiance above remains the deferred-lighting heap input.
    Core::GpuDescriptorHandle surfelIrradianceHalf = Core::GpuDescriptorHandle::invalid();
    Core::GpuDescriptorHandle sampler = Core::GpuDescriptorHandle::invalid();
    Core::GpuDescriptorHandle opaqueColor = Core::GpuDescriptorHandle::invalid();
    Core::GpuDescriptorHandle avboitAccumColor = Core::GpuDescriptorHandle::invalid();
    Core::GpuDescriptorHandle avboitAccumExtinction = Core::GpuDescriptorHandle::invalid();
    Core::GpuDescriptorHandle avboitTransmittance = Core::GpuDescriptorHandle::invalid();
    Core::GpuDescriptorHandle avboitLinearSampler = Core::GpuDescriptorHandle::invalid();
    // Caustic resolve inputs use target-generation slots directly in push constants. causticAccumulator is sampled
    // through the heap's dedicated typed uint Texture2DArray table; the remaining three are floating-point Texture2D
    // resources.
    Core::GpuDescriptorHandle causticAccumulator = Core::GpuDescriptorHandle::invalid();
    Core::GpuDescriptorHandle causticHistory = Core::GpuDescriptorHandle::invalid();
    Core::GpuDescriptorHandle causticResolveHalf = Core::GpuDescriptorHandle::invalid();
    Core::GpuDescriptorHandle causticResolveGeometry = Core::GpuDescriptorHandle::invalid();
    // Soft-shadow denoise intermediates are target-generation resources too. The resolve passes carry these slots in
    // their push constants, rather than keeping seven sampled images in each local descriptor-buffer set.
    Core::GpuDescriptorHandle shadowSoftGeometry = Core::GpuDescriptorHandle::invalid();
    Core::GpuDescriptorHandle shadowSoftGeometryPrev = Core::GpuDescriptorHandle::invalid();
    Core::GpuDescriptorHandle shadowSoftHalfA = Core::GpuDescriptorHandle::invalid();
    Core::GpuDescriptorHandle shadowSoftHalfB = Core::GpuDescriptorHandle::invalid();
    Core::GpuDescriptorHandle shadowHistA = Core::GpuDescriptorHandle::invalid();
    Core::GpuDescriptorHandle shadowHistB = Core::GpuDescriptorHandle::invalid();
    Core::GpuDescriptorHandle shadowMomentsA = Core::GpuDescriptorHandle::invalid();
    Core::GpuDescriptorHandle shadowMomentsB = Core::GpuDescriptorHandle::invalid();
    Core::GpuDescriptorHandle transparentSoftHalf = Core::GpuDescriptorHandle::invalid();
    Core::GpuDescriptorHandle transparentHistA = Core::GpuDescriptorHandle::invalid();
    Core::GpuDescriptorHandle transparentHistB = Core::GpuDescriptorHandle::invalid();
    Core::GpuDescriptorHandle transparentMomentsA = Core::GpuDescriptorHandle::invalid();
    Core::GpuDescriptorHandle transparentMomentsB = Core::GpuDescriptorHandle::invalid();
    bool slotsUploaded = false;

    [[nodiscard]] bool valid()const noexcept{
        return
            slotsBuffer != nullptr
            && gbufferBaseColor.valid()
            && gbufferNormal.valid()
            && gbufferWorldPosition.valid()
            && gbufferDepth.valid()
            && shadowVisibility.valid()
            && causticIrradiance.valid()
            && surfelIrradiance.valid()
            && surfelIrradianceHalf.valid()
            && sampler.valid()
            && opaqueColor.valid()
            && avboitAccumColor.valid()
            && avboitAccumExtinction.valid()
            && avboitTransmittance.valid()
            && avboitLinearSampler.valid()
            && causticAccumulator.valid()
            && causticHistory.valid()
            && causticResolveHalf.valid()
            && causticResolveGeometry.valid()
            && shadowSoftGeometry.valid()
            && shadowSoftGeometryPrev.valid()
            && shadowSoftHalfA.valid()
            && shadowSoftHalfB.valid()
            && shadowHistA.valid()
            && shadowHistB.valid()
            && shadowMomentsA.valid()
            && shadowMomentsB.valid()
            && transparentSoftHalf.valid()
            && transparentHistA.valid()
            && transparentHistB.valid()
            && transparentMomentsA.valid()
            && transparentMomentsB.valid()
        ;
    }
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
    Core::Format::Enum surfelIrradianceFormat = Core::Format::UNKNOWN;
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
    Core::Format::Enum shadowCoarseTransmittanceFormat = Core::Format::UNKNOWN;
    Core::Format::Enum shadowSoftFormat = Core::Format::UNKNOWN;
    Core::Format::Enum shadowSoftGeometryFormat = Core::Format::UNKNOWN;
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
    // Per-light shadow visibility (RGBA16F Texture2DArray, NWB_SCENE_SHADOW_SLOT_COUNT layers). On RT hardware the HW
    // RayQuery pass writes the binary opaque mask and the software traversal multiplies the colored transparent shadow
    // onto it; on the no-RT path the software traversal writes it directly. Full-resolution.
    Core::TextureHandle shadowVisibility;
    // Stage-2 adaptive transparent shadow scratch (RGBA16F Texture2DArray, NWB_SCENE_SHADOW_SLOT_COUNT layers, HALF the
    // render extent). The coarse software trace writes one colored transmittance per 2x2 block here; the adaptive resolve
    // reads it to bilinearly interpolate the flat shadow interior and re-trace only the edge blocks at full resolution.
    // Soft/low-frequency colored signal, so half-res sampling + edge refine is the soft-shadow analog of an irradiance
    // cache. Needs no clear (the coarse trace fully overwrites every block it samples). Null when adaptive is disabled.
    Core::TextureHandle shadowCoarseTransmittance;
    // Soft opaque shadow HALF-res targets:
    //  - shadowSoftHalfA / shadowSoftHalfB: two HALF-res RGBA16F Texture2DArrays (NWB_SCENE_SHADOW_SLOT_COUNT layers),
    //    the a-trous ping-pong buffers. The jittered opaque trace writes shadowSoftHalfA; the resolve's
    //    PREPARE copies it, the wavelet alternates A<->B, the final wavelet lands in B, and the upsample reads B into
    //    the full-res shadowVisibility. Half the render extent (1/4 the pixels); need no clear (every pass fully
    //    overwrites, and the trace writes the LIT identity for background or inactive slots).
    //  - shadowSoftGeometry: a HALF-res RGBA16F single-layer geometry cache (xy = octahedral receiver normal, z =
    //    camera distance, w = validity) the geometry downsample pre-pass fills once per frame for the edge-stop.
    Core::TextureHandle shadowSoftHalfA;
    Core::TextureHandle shadowSoftHalfB;
    Core::TextureHandle shadowSoftGeometry;
    // Soft opaque shadow TEMPORAL accumulation HALF-res targets. The
    // reproject-merge pass (inserted per slot between the soft trace and the a-trous resolve) accumulates the noisy per-
    // frame trace over time -- so the per-frame SPP can drop while a STATIC receiver converges smooth AND a MOVING occluder
    // leaves no ghost trail -- by reprojecting the current world pos through a STASHED previous-frame worldToClip (there are
    // NO motion vectors / prev-G-buffer in this engine) then variance-clamping + antilag-blending the reprojected history:
    //  - shadowHistA / shadowHistB: the accumulated-visibility ping-pong (RGBA16F Texture2DArray, NWB_SCENE_SHADOW_SLOT_COUNT
    //    layers). The merge writes one (out) and reads the other (prev history in); the a-trous resolve reads whichever the
    //    merge just wrote as its "raw trace" input; the frame-end swap makes this frame's out become next frame's history in.
    //  - shadowMomentsA / shadowMomentsB: the ping-pong moments (RGBA16F Texture2DArray, same layers): .x = m1 (luma),
    //    .y = m2 (luma^2), .z = n (history length in frames). n drives the blend alpha (1/(n+1)) so convergence tightens.
    //  - shadowSoftGeometryPrev: the PREVIOUS-frame single-layer geometry cache (a ping-pong sibling of shadowSoftGeometry,
    //    swapped at frame end) the merge samples at the reprojected texel for the disocclusion / moving-receiver gate.
    // All allocated with the visibility target so they share the resize lifecycle (createShadowVisibilityTarget). Guarded by
    // m_softShadowTemporalReady; a first frame / resize / temporal-off falls back to the raw trace feeding the a-trous.
    Core::TextureHandle shadowHistA;
    Core::TextureHandle shadowHistB;
    Core::TextureHandle shadowMomentsA;
    Core::TextureHandle shadowMomentsB;
    Core::TextureHandle shadowSoftGeometryPrev;
    // Soft COLORED TRANSPARENT shadow HALF-res targets: the PARALLEL colored
    // pipeline kept SEPARATE from the opaque soft signal through the whole denoise, folded (multiplied) onto the opaque
    // visibility only at the final full-res upsample (opaque binary Bernoulli vs colored chord-variance RGB product have
    // different noise stats, so denoise(A)*denoise(B) != denoise(A*B) -- they must be denoised independently). Mirrors the
    // opaque allocation exactly (RGBA16F Texture2DArray, NWB_SCENE_SHADOW_SLOT_COUNT layers, half the render extent):
    //  - transparentSoftHalf: the RAW colored soft trace output (analog of shadowSoftHalfA). The soft transparent trace
    //    (sw_shadow_transparent_soft_cs) writes one cone-jittered colored transmittance per half-res pixel here; the merge
    //    reads it as its "current" sample and the RGB a-trous reads whichever the merge just wrote.
    //  - transparentHistA / transparentHistB: the accumulated-visibility ping-pong (analog of shadowHistA/B), sharing the
    //    SAME frontIsA selector as the opaque history so both stay in lockstep (one frame-end flip covers both).
    //  - transparentMomentsA / transparentMomentsB: the ping-pong luma moments (analog of shadowMomentsA/B).
    // The geometry cache (shadowSoftGeometry/Prev) + the stashed prevWorldToClip are SHARED (same receivers) -- NOT
    // duplicated here. Allocated with the visibility target so they share the resize lifecycle (createShadowVisibilityTarget).
    // Guarded by m_softTransparentReady / m_softTransparentTemporalReady; a first frame / resize / temporal-off degrades to
    // raw-colored-soft-trace -> spatial RGB a-trous -> fold-multiply (always valid, never black -- white identity everywhere).
    Core::TextureHandle transparentSoftHalf;
    Core::TextureHandle transparentHistA;
    Core::TextureHandle transparentHistB;
    Core::TextureHandle transparentMomentsA;
    Core::TextureHandle transparentMomentsB;
    // Caustic producer targets (additive, inverted lifecycle vs shadowVisibility): the RGBA16F resolved irradiance
    // the deferred lighting pass adds pre-tonemap, the R32_UINT splat accumulators (one Texture2DArray layer per RGB
    // channel) the producer's fixed-point InterlockedAdd lands in, and the RGBA16F a-trous wavelet scratch buffer.
    // The irradiance + accumulator are cleared to BLACK each frame (additive identity), so when no producer runs the
    // additive caustic term is a pixel-identical no-op.
    Core::TextureHandle causticIrradiance;
    // Resolved surfel-GI indirect irradiance (RGBA16F, screen space): the surfel_resolve_cs COMPUTE pass gathers the
    // surfel field once per pixel into this (rgb = indirect irradiance, a = coverage flag), and the deferred lighting
    // PIXEL shader samples it -- so the read-write surfel pool never touches the pixel shader (compute-only, like the
    // caustic accumulator), eliminating the frames-in-flight pool race. Cleared to 0 each frame (a = 0 -> hemiAmbient
    // fallback) so a disabled/absent GI producer is a pixel-identical no-op.
    Core::TextureHandle surfelIrradiance;
    // U6 half-res producer: the resolve gathers into this HALF-res (RGBA16F, DivideUp(w/h, HALF_FACTOR)) target, and the
    // surfel_upsample_cs pass reconstructs the full-res surfelIrradiance above with a surface-gated joint-bilinear filter.
    // The costly (2*EXTENT+1)^3 gather thus runs at 1/FACTOR^2 the pixels. Transient (written + read within the GI block
    // each frame) so it needs no clear. surfelIrradiance stays the lighting-facing SRV, so the consumer is unchanged.
    Core::TextureHandle surfelIrradianceHalf;
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
    DeferredBindlessFrameResources bindless;
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
            && bindless.valid()
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

