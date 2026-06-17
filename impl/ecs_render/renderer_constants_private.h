// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#pragma once


#include "renderer_types.h"

#include <core/graphics/api.h>
#include <impl/assets/graphics/csg/constants.h>
#include <impl/assets/graphics/mesh/runtime_constants.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace ECSRenderDetail{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


inline constexpr Core::Color s_ClearColor = Core::Color(0.07f, 0.09f, 0.13f, 1.f);
inline constexpr u32 s_EmulatedVertexStride = sizeof(f32) * NWB_MESH_EMULATION_VERTEX_FLOAT_COUNT;
inline constexpr u32 s_CsgPeelLayerCount = NWB_CSG_PEEL_LAYER_COUNT;
inline constexpr u32 s_CsgReceiverEventLayerCount = NWB_CSG_RECEIVER_EVENT_LAYER_COUNT;
inline constexpr u32 s_CsgReceiverSpanLayerCount = NWB_CSG_RECEIVER_SPAN_LAYER_COUNT;
inline constexpr u32 s_CsgRemovedIntervalLayerCount = NWB_CSG_REMOVED_INTERVAL_LAYER_COUNT;
inline constexpr u32 s_MeshDispatchFlagScissorCull = NWB_MESH_DISPATCH_FLAG_SCISSOR_CULL;
inline constexpr u32 s_MeshDispatchFlagMeshletFrustumCull = NWB_MESH_DISPATCH_FLAG_MESHLET_FRUSTUM_CULL;
inline constexpr u32 s_MeshDispatchFlagMeshletConeCull = NWB_MESH_DISPATCH_FLAG_MESHLET_CONE_CULL;
inline constexpr u32 s_MeshDispatchFlagCsgMeshletFullyRemovedCull = NWB_MESH_DISPATCH_FLAG_CSG_MESHLET_FULLY_REMOVED_CULL;
inline constexpr Core::TextureSubresourceSet s_FramebufferSubresources = Core::TextureSubresourceSet(0, 1, 0, 1);


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


inline constexpr Name s_InstanceBufferName("ecs_render/instance_data");
inline constexpr Name s_MaterialTypedBufferName("ecs_render/material_typed_data");
inline constexpr Name s_CsgReceiverRangeBufferName("ecs_render/csg_receiver_ranges");
inline constexpr Name s_CsgCutterBufferName("ecs_render/csg_cutters");
inline constexpr Name s_SceneShadingBufferName("ecs_render/scene_shading_data");
inline constexpr Name s_SceneLightBufferName("ecs_render/scene_light_data");


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

