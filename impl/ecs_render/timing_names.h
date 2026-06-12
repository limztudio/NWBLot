// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#pragma once


#include <impl/global.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace RendererGpuTimingScope{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


inline constexpr Name s_MeshDispatch("render.mesh_dispatch");
inline constexpr Name s_Raster("render.raster");
inline constexpr Name s_Frame("render.frame");
inline constexpr Name s_DeferredClear("render.deferred_clear");
inline constexpr Name s_DeferredLighting("render.deferred_lighting");
inline constexpr Name s_DeferredComposite("render.deferred_composite");
inline constexpr Name s_MaterialUpload("render.material_upload");
inline constexpr Name s_OpaqueRegular("render.opaque_regular");
inline constexpr Name s_OpaqueCsgReceiverSurface("render.opaque_csg_receiver_surface");
inline constexpr Name s_OpaqueCsg("render.opaque_csg");
inline constexpr Name s_CsgUpload("render.csg_upload");
inline constexpr Name s_CsgSampleStateUpload("render.csg_sample_state_upload");
inline constexpr Name s_CsgIntervalClear("render.csg_interval_clear");
inline constexpr Name s_CsgIntervalPeel("render.csg_interval_peel");
inline constexpr Name s_CsgReceiverSpanBuild("render.csg_receiver_span_build");
inline constexpr Name s_CsgIntervalCombine("render.csg_interval_combine");
inline constexpr Name s_CsgCapFill("render.csg_cap_fill");
inline constexpr Name s_TransparentCsgIntervals("render.transparent_csg_intervals");
inline constexpr Name s_AvboitClear("render.avboit_clear");
inline constexpr Name s_AvboitOccupancy("render.avboit_occupancy");
inline constexpr Name s_AvboitDepthWarp("render.avboit_depth_warp");
inline constexpr Name s_AvboitExtinction("render.avboit_extinction");
inline constexpr Name s_AvboitIntegration("render.avboit_integration");
inline constexpr Name s_AvboitAccumulate("render.avboit_accumulate");


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

