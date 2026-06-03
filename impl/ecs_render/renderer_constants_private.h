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
inline constexpr u32 s_MeshDispatchFlagScissorCull = NWB_MESH_DISPATCH_FLAG_SCISSOR_CULL;
inline constexpr u32 s_MeshDispatchFlagMeshletFrustumCull = NWB_MESH_DISPATCH_FLAG_MESHLET_FRUSTUM_CULL;
inline constexpr u32 s_MeshDispatchFlagMeshletConeCull = NWB_MESH_DISPATCH_FLAG_MESHLET_CONE_CULL;
inline constexpr Core::TextureSubresourceSet s_FramebufferSubresources = Core::TextureSubresourceSet(0, 1, 0, 1);


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


inline constexpr Name s_MeshEmulationVertexShaderName("engine/graphics/mesh/emulation_vs");
inline constexpr Name s_InstanceBufferName("ecs_render/instance_data");
inline constexpr Name s_MaterialTypedBufferName("ecs_render/material_typed_data");
inline constexpr Name s_CsgReceiverRangeBufferName("ecs_render/csg_receiver_ranges");
inline constexpr Name s_CsgCutterBufferName("ecs_render/csg_cutters");
inline constexpr Name s_CsgParameterByteBufferName("ecs_render/csg_parameter_bytes");
inline constexpr Name s_CsgCapVertexBufferName("ecs_render/csg_cap_vertices");
inline constexpr Name s_CsgCapProxyBufferName("ecs_render/csg_cap_proxies");
inline constexpr Name s_CsgCapVertexShaderName("engine/graphics/csg/cap_vs");
inline constexpr Name s_CsgCapPixelShaderName("engine/graphics/csg/cap_ps");
inline constexpr Name s_CsgCapProxyPixelShaderName("engine/graphics/csg/cap_proxy_ps");
inline constexpr Name s_CsgCapProxyPlaneMeshShaderName("engine/graphics/csg/cap_proxy_plane_ms");
inline constexpr Name s_CsgCapProxyBoxMeshShaderName("engine/graphics/csg/cap_proxy_box_ms");
inline constexpr Name s_CsgCapProxySphereMeshShaderName("engine/graphics/csg/cap_proxy_sphere_ms");
inline constexpr Name s_CsgCapProxyCapsuleMeshShaderName("engine/graphics/csg/cap_proxy_capsule_ms");
inline constexpr Name s_CsgTransparentCapOccupancyPixelShaderName("engine/graphics/csg/transparent_cap_occupancy_ps");
inline constexpr Name s_CsgTransparentCapExtinctionPixelShaderName("engine/graphics/csg/transparent_cap_extinction_ps");
inline constexpr Name s_CsgTransparentCapAccumulatePixelShaderName("engine/graphics/csg/transparent_cap_accumulate_ps");
inline constexpr Name s_SceneShadingBufferName("ecs_render/scene_shading_data");
inline constexpr Name s_DeferredCompositeVertexShaderName("engine/graphics/deferred/composite_vs");
inline constexpr Name s_DeferredLightingPixelShaderName("engine/graphics/deferred/lighting_ps");
inline constexpr Name s_DeferredCompositePixelShaderName("engine/graphics/deferred/composite_ps");


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
