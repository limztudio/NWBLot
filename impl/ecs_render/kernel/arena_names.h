// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#pragma once


#include <impl/global.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace RendererArenaScope{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


inline constexpr Name s_PrepareArena("impl/ecs_render/prepare");
inline constexpr Name s_RenderArena("impl/ecs_render/render");
inline constexpr Name s_TaskGraphArena("impl/ecs_render/task_graph");
inline constexpr Name s_TransparentCsgIntervalArena("impl/ecs_render/avboit_transparent_csg");
inline constexpr Name s_PreparePassArena("impl/ecs_render/material_pass_prepare");
inline constexpr Name s_RenderPassArena("impl/ecs_render/material_pass_render");
inline constexpr Name s_MutableTypedBytesArena("impl/ecs_render/material_instance_mutable");
inline constexpr Name s_RayTracingBuildArena("impl/ecs_render/ray_tracing_build");
inline constexpr Name s_RayTracingAttributeArena("impl/ecs_render/ray_tracing_attribute");

// Runtime mesh buffer debug names shared by the renderer mesh uploader and the skinning runtime cache.
inline constexpr AStringView s_PositionsBufferName = ":positions";
inline constexpr AStringView s_NormalsBufferName = ":normals";
inline constexpr AStringView s_TangentsBufferName = ":tangents";
inline constexpr AStringView s_Uv0BufferName = ":uv0";
inline constexpr AStringView s_ColorsBufferName = ":colors";
inline constexpr AStringView s_MeshletsBufferName = ":meshlets";
inline constexpr AStringView s_MeshletBoundsBufferName = ":meshlet_bounds";
inline constexpr AStringView s_MeshletPositionRefDeltasBufferName = ":meshlet_position_ref_deltas";
inline constexpr AStringView s_MeshletAttributeRefDeltasBufferName = ":meshlet_attribute_ref_deltas";
inline constexpr AStringView s_MeshletLocalVertexRefsBufferName = ":meshlet_local_vertex_refs";
inline constexpr AStringView s_MeshletPrimitiveIndicesBufferName = ":meshlet_primitive_indices";
inline constexpr TStringView s_PositionBufferLabel = NWB_TEXT("position");
inline constexpr TStringView s_NormalBufferLabel = NWB_TEXT("normal");
inline constexpr TStringView s_TangentBufferLabel = NWB_TEXT("tangent");
inline constexpr TStringView s_Uv0BufferLabel = NWB_TEXT("uv0");
inline constexpr TStringView s_ColorBufferLabel = NWB_TEXT("color");
inline constexpr TStringView s_MeshletDescriptorBufferLabel = NWB_TEXT("meshlet descriptor");
inline constexpr TStringView s_MeshletBoundsBufferLabel = NWB_TEXT("meshlet bounds");
inline constexpr TStringView s_MeshletPositionRefDeltaBufferLabel = NWB_TEXT("meshlet position ref delta");
inline constexpr TStringView s_MeshletAttributeRefDeltaBufferLabel = NWB_TEXT("meshlet attribute ref delta");
inline constexpr TStringView s_MeshletLocalVertexRefBufferLabel = NWB_TEXT("meshlet local vertex ref");
inline constexpr TStringView s_MeshletPrimitiveIndexBufferLabel = NWB_TEXT("meshlet primitive index");
inline constexpr AStringView s_RtTriangleIndicesBufferName = ":rt_triangle_indices";
inline constexpr AStringView s_RtTriangleAttributesBufferName = ":rt_triangle_attributes";


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

