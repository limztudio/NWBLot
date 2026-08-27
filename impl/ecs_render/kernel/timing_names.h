// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#pragma once


#include <impl/global.h>

#include <core/graphics/gpu_timing.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace RendererGpuTimingScope{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


inline constexpr Core::GpuTimingScopeDefinition s_MeshDispatch("render.mesh_dispatch");
inline constexpr Core::GpuTimingScopeDefinition s_Raster("render.raster");
inline constexpr Core::GpuTimingScopeDefinition s_Frame("render.frame");
// Dedicated-queue packet envelopes. render.frame remains the end-to-end critical path while these isolate graph and
// remaining packet submissions without assuming a particular physical route.
inline constexpr Core::GpuTimingScopeDefinition s_AsyncPrefix("render.async_prefix");
inline constexpr Core::GpuTimingScopeDefinition s_AsyncShadow("render.async_shadow");
inline constexpr Core::GpuTimingScopeDefinition s_AsyncSurfelGi("render.async_surfel_gi");
inline constexpr Core::GpuTimingScopeDefinition s_AsyncFinal("render.async_final");
inline constexpr Core::GpuTimingScopeDefinition s_DeferredGraphQueueOverlap("render.deferred_graph.queue_overlap");
inline constexpr Core::GpuTimingScopeDefinition s_DeferredClear("render.deferred_clear");
inline constexpr Core::GpuTimingScopeDefinition s_ShadowVisibility("render.shadow_visibility");
// Keep the aggregate shadow envelope for frame-level ranking, and publish its expensive compute phases separately so
// a performance investigation can distinguish ray traversal from temporal filtering and reconstruction.
inline constexpr Core::GpuTimingScopeDefinition s_ShadowOpaqueTrace("render.shadow_opaque_trace");
inline constexpr Core::GpuTimingScopeDefinition s_ShadowGeometryDownsample("render.shadow_geometry_downsample");
inline constexpr Core::GpuTimingScopeDefinition s_ShadowOpaqueTemporal("render.shadow_opaque_temporal");
inline constexpr Core::GpuTimingScopeDefinition s_ShadowOpaqueResolve("render.shadow_opaque_resolve");
inline constexpr Core::GpuTimingScopeDefinition s_ShadowTransparentTrace("render.shadow_transparent_trace");
inline constexpr Core::GpuTimingScopeDefinition s_ShadowTransparentTemporal("render.shadow_transparent_temporal");
inline constexpr Core::GpuTimingScopeDefinition s_ShadowTransparentResolve("render.shadow_transparent_resolve");
inline constexpr Core::GpuTimingScopeDefinition s_SwBvhSort("render.sw_bvh_sort");
inline constexpr Core::GpuTimingScopeDefinition s_CausticPhotons("render.caustic_photons");
inline constexpr Core::GpuTimingScopeDefinition s_CausticResolve("render.caustic_resolve");
inline constexpr Core::GpuTimingScopeDefinition s_DeferredLighting("render.deferred_lighting");
inline constexpr Core::GpuTimingScopeDefinition s_DeferredComposite("render.deferred_composite");
inline constexpr Core::GpuTimingScopeDefinition s_DeferredPresent("render.deferred_present");
inline constexpr Core::GpuTimingScopeDefinition s_MaterialUpload("render.material_upload");
inline constexpr Core::GpuTimingScopeDefinition s_OpaqueRegular("render.opaque_regular");
inline constexpr Core::GpuTimingScopeDefinition s_OpaqueCsgReceiverSurface("render.opaque_csg_receiver_surface");
inline constexpr Core::GpuTimingScopeDefinition s_OpaqueCsg("render.opaque_csg");
inline constexpr Core::GpuTimingScopeDefinition s_CsgUpload("render.csg_upload");
inline constexpr Core::GpuTimingScopeDefinition s_CsgSampleStateUpload("render.csg_sample_state_upload");
inline constexpr Core::GpuTimingScopeDefinition s_CsgIntervalClear("render.csg_interval_clear");
inline constexpr Core::GpuTimingScopeDefinition s_CsgIntervalPeel("render.csg_interval_peel");
inline constexpr Core::GpuTimingScopeDefinition s_CsgReceiverSpanBuild("render.csg_receiver_span_build");
inline constexpr Core::GpuTimingScopeDefinition s_CsgIntervalCombine("render.csg_interval_combine");
inline constexpr Core::GpuTimingScopeDefinition s_CsgCapFill("render.csg_cap_fill");
inline constexpr Core::GpuTimingScopeDefinition s_TransparentCsgIntervals("render.transparent_csg_intervals");
inline constexpr Core::GpuTimingScopeDefinition s_AvboitClear("render.avboit_clear");
inline constexpr Core::GpuTimingScopeDefinition s_AvboitOccupancy("render.avboit_occupancy");
inline constexpr Core::GpuTimingScopeDefinition s_AvboitDepthWarp("render.avboit_depth_warp");
inline constexpr Core::GpuTimingScopeDefinition s_AvboitExtinction("render.avboit_extinction");
inline constexpr Core::GpuTimingScopeDefinition s_AvboitIntegration("render.avboit_integration");
inline constexpr Core::GpuTimingScopeDefinition s_AvboitAccumulate("render.avboit_accumulate");
inline constexpr Core::GpuTimingScopeDefinition s_SurfelSpawn("render.surfel_spawn");
inline constexpr Core::GpuTimingScopeDefinition s_SurfelAgeFree("render.surfel_age_free");
inline constexpr Core::GpuTimingScopeDefinition s_SurfelHashBuild("render.surfel_hash_build");
inline constexpr Core::GpuTimingScopeDefinition s_SurfelTrace("render.surfel_trace");
inline constexpr Core::GpuTimingScopeDefinition s_SurfelResolve("render.surfel_resolve");
inline constexpr Core::GpuTimingScopeDefinition s_SurfelUpsample("render.surfel_upsample");


[[nodiscard]] inline Name DeferredGraphQueueInternalIdle(
    const Core::GpuPhysicalQueueId& queue,
    Core::Alloc::ScratchArena& scratchArena
){
    if(!queue.valid())
        return NAME_NONE;

    const AString<Core::Alloc::ScratchArena> scopeName = StringFormat(
        scratchArena,
        "render.deferred_graph.queue_{}.internal_idle",
        queue.index
    );
    return ToName(scopeName);
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

