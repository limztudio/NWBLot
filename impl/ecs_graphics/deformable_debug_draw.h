// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#pragma once


#include "deformable_surface_edit.h"


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


struct DeformableDebugLine{
    Float3U begin = Float3U(0.0f, 0.0f, 0.0f);
    Float3U end = Float3U(0.0f, 0.0f, 0.0f);
    Float3U color = Float3U(1.0f, 1.0f, 1.0f);
};

struct DeformableDebugPoint{
    Float3U position = Float3U(0.0f, 0.0f, 0.0f);
    Float3U color = Float3U(1.0f, 1.0f, 1.0f);
    u32 id = Limit<u32>::s_Max;
};

struct DeformableSurfaceEditDebugSnapshot{
    Core::ECS::EntityID entity = Core::ECS::ENTITY_ID_INVALID;
    RuntimeMeshHandle runtimeMesh;
    u32 editRevision = 0;
    u32 vertexCount = 0;
    u32 triangleCount = 0;
    u32 sourceTriangleCount = 0;
    u32 editableTriangleCount = 0;
    u32 restrictedTriangleCount = 0;
    u32 forbiddenTriangleCount = 0;
    u32 invalidTriangleCount = 0;
    u32 wallVertexCount = 0;
    u32 accessoryAnchorCount = 0;
    bool previewValid = false;
    u32 previewTriangle = Limit<u32>::s_Max;
    u32 previewSourceTriangle = Limit<u32>::s_Max;
    DeformableSurfaceEditPermission::Enum previewPermission = DeformableSurfaceEditPermission::Allowed;
    Float3U previewCenter = Float3U(0.0f, 0.0f, 0.0f);
    Float3U previewNormal = Float3U(0.0f, 0.0f, 1.0f);
    Float3U previewTangent = Float3U(1.0f, 0.0f, 0.0f);
    Float3U previewBitangent = Float3U(0.0f, 1.0f, 0.0f);
    f32 previewBarycentric[3] = { 0.0f, 0.0f, 0.0f };
    f32 previewRadius = 0.0f;
    f32 previewDepth = 0.0f;
    Vector<DeformableDebugLine> lines;
    Vector<DeformableDebugPoint> points;
};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


[[nodiscard]] bool BuildDeformableSurfaceEditDebugSnapshot(
    const DeformableRuntimeMeshInstance& instance,
    const DeformableSurfaceEditSession* session,
    const DeformableHolePreview* preview,
    const DeformableSurfaceEditState* state,
    DeformableSurfaceEditDebugSnapshot& outSnapshot
);
[[nodiscard]] bool BuildDeformableSurfaceEditDebugDump(
    const DeformableSurfaceEditDebugSnapshot& snapshot,
    AString& outDump
);


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

