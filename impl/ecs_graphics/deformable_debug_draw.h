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
    u32 invalidFrameCount = 0;
    u32 skinnedVertexCount = 0;
    u32 maxSkinInfluenceCount = 0;
    u32 skinWeightLineCount = 0;
    f32 maxSkinWeight = 0.0f;
    u32 morphCount = 0;
    u32 morphDeltaCount = 0;
    u32 morphDeltaLineCount = 0;
    f32 maxMorphPositionDelta = 0.0f;
    u32 displacementMode = DeformableDisplacementMode::None;
    f32 displacementAmplitude = 0.0f;
    f32 displacementBias = 0.0f;
    bool displacementTextureBound = false;
    u32 displacementMagnitudeLineCount = 0;
    f32 maxDisplacementMagnitude = 0.0f;
    u32 editableTriangleCount = 0;
    u32 restrictedTriangleCount = 0;
    u32 forbiddenTriangleCount = 0;
    u32 restrictedMaskPointCount = 0;
    u32 repairMaskPointCount = 0;
    u32 forbiddenMaskPointCount = 0;
    u32 invalidTriangleCount = 0;
    u32 removedTriangleCount = 0;
    u32 wallVertexCount = 0;
    u32 accessoryAnchorCount = 0;
    u32 wallNormalBasisLineCount = 0;
    u32 wallTangentBasisLineCount = 0;
    bool previewValid = false;
    u32 previewTriangle = Limit<u32>::s_Max;
    u32 previewSourceTriangle = Limit<u32>::s_Max;
    DeformableSurfaceEditPermission::Enum previewPermission = DeformableSurfaceEditPermission::Allowed;
    Float3U previewCenter = Float3U(0.0f, 0.0f, 0.0f);
    Float3U previewRestHitPoint = Float3U(0.0f, 0.0f, 0.0f);
    Float3U previewPosedHitPoint = Float3U(0.0f, 0.0f, 0.0f);
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
[[nodiscard]] bool BuildDeformableSurfaceEditDebugDump(const DeformableSurfaceEditDebugSnapshot& snapshot, AString& outDump);


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

