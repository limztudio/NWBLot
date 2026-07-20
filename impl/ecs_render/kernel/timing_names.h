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
inline constexpr Core::GpuTimingScopeDefinition s_DeferredClear("render.deferred_clear");
inline constexpr Core::GpuTimingScopeDefinition s_ShadowVisibility("render.shadow_visibility");
inline constexpr Core::GpuTimingScopeDefinition s_SwBvhSort("render.sw_bvh_sort");
inline constexpr Core::GpuTimingScopeDefinition s_CausticPhotons("render.caustic_photons");
inline constexpr Core::GpuTimingScopeDefinition s_CausticResolve("render.caustic_resolve");
inline constexpr Core::GpuTimingScopeDefinition s_DeferredLighting("render.deferred_lighting");
inline constexpr Core::GpuTimingScopeDefinition s_DeferredComposite("render.deferred_composite");
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


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

