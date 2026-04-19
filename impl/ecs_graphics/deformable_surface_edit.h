// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#pragma once


#include "deformable_picking.h"

#include <core/assets/asset.h>
#include <core/assets/asset_ref.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


struct DeformableHoleEditParams{
    DeformablePosedHit posedHit;
    f32 radius = 0.0f;
    f32 ellipseRatio = 1.0f;
    f32 depth = 0.0f;
};

struct DeformableHoleEditResult{
    u32 removedTriangleCount = 0;
    u32 addedVertexCount = 0;
    u32 addedTriangleCount = 0;
    u32 editRevision = 0;
    u32 firstWallVertex = Limit<u32>::s_Max;
    u32 wallVertexCount = 0;
};

struct DeformableSurfaceEditSession{
    Core::ECS::EntityID entity = Core::ECS::ENTITY_ID_INVALID;
    RuntimeMeshHandle runtimeMesh;
    u32 editRevision = 0;
    DeformablePosedHit hit;
    DeformableHoleEditParams previewParams;
    bool active = false;
    bool previewed = false;
};

struct alignas(AlignedFloat4Data) DeformableHolePreview{
    AlignedFloat4Data center = AlignedFloat4Data(0.0f, 0.0f, 0.0f, 1.0f);
    AlignedFloat4Data normal = AlignedFloat4Data(0.0f, 0.0f, 1.0f, 0.0f);
    AlignedFloat4Data tangent = AlignedFloat4Data(1.0f, 0.0f, 0.0f, 0.0f);
    AlignedFloat4Data bitangent = AlignedFloat4Data(0.0f, 1.0f, 0.0f, 0.0f);
    f32 radius = 0.0f;
    f32 ellipseRatio = 1.0f;
    f32 depth = 0.0f;
    u32 editRevision = 0;
    bool valid = false;
};
static_assert(IsStandardLayout_V<DeformableHolePreview>, "DeformableHolePreview must stay layout-stable");
static_assert(IsTriviallyCopyable_V<DeformableHolePreview>, "DeformableHolePreview must stay cheap to copy");
static_assert(alignof(DeformableHolePreview) >= alignof(AlignedFloat4Data), "DeformableHolePreview must stay SIMD-aligned");

struct DeformableSurfaceHoleEditRecord{
    SourceSample restSample;
    Float3Data restPosition = Float3Data(0.0f, 0.0f, 0.0f);
    Float3Data restNormal = Float3Data(0.0f, 0.0f, 1.0f);
    u32 baseEditRevision = 0;
    f32 radius = 0.0f;
    f32 ellipseRatio = 1.0f;
    f32 depth = 0.0f;
};
static_assert(IsStandardLayout_V<DeformableSurfaceHoleEditRecord>, "DeformableSurfaceHoleEditRecord must stay binary-serializable");
static_assert(IsTriviallyCopyable_V<DeformableSurfaceHoleEditRecord>, "DeformableSurfaceHoleEditRecord must stay binary-serializable");

namespace DeformableSurfaceEditRecordType{
    enum Enum : u32{
        Hole = 1u,
    };
};

struct DeformableSurfaceEditRecord{
    DeformableSurfaceEditRecordType::Enum type = DeformableSurfaceEditRecordType::Hole;
    DeformableSurfaceHoleEditRecord hole;
    DeformableHoleEditResult result;
};
static_assert(IsStandardLayout_V<DeformableSurfaceEditRecord>, "DeformableSurfaceEditRecord must stay binary-serializable");
static_assert(IsTriviallyCopyable_V<DeformableSurfaceEditRecord>, "DeformableSurfaceEditRecord must stay binary-serializable");

struct DeformableAccessoryAttachmentRecord{
    Core::Assets::AssetRef<Geometry> geometry;
    Core::Assets::AssetRef<Material> material;
    u32 editRevision = 0;
    u32 firstWallVertex = Limit<u32>::s_Max;
    u32 wallVertexCount = 0;
    f32 normalOffset = 0.0f;
    f32 uniformScale = 1.0f;
};

struct DeformableSurfaceEditState{
    Vector<DeformableSurfaceEditRecord> edits;
    Vector<DeformableAccessoryAttachmentRecord> accessories;
};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


[[nodiscard]] bool BeginSurfaceEdit(
    const DeformableRuntimeMeshInstance& instance,
    const DeformablePosedHit& hit,
    DeformableSurfaceEditSession& outSession
);
[[nodiscard]] bool PreviewHole(
    const DeformableRuntimeMeshInstance& instance,
    DeformableSurfaceEditSession& session,
    const DeformableHoleEditParams& params,
    DeformableHolePreview& outPreview
);
[[nodiscard]] bool CommitHole(
    DeformableRuntimeMeshInstance& instance,
    const DeformableSurfaceEditSession& session,
    const DeformableHoleEditParams& params,
    DeformableHoleEditResult* outResult = nullptr,
    DeformableSurfaceEditRecord* outRecord = nullptr
);
[[nodiscard]] bool AttachAccessory(
    const DeformableRuntimeMeshInstance& instance,
    const DeformableHoleEditResult& holeResult,
    f32 normalOffset,
    f32 uniformScale,
    DeformableAccessoryAttachmentComponent& outAttachment
);
[[nodiscard]] bool ResolveAccessoryAttachmentTransform(
    const DeformableRuntimeMeshInstance& instance,
    const DeformablePickingInputs& inputs,
    const DeformableAccessoryAttachmentComponent& attachment,
    Core::Scene::TransformComponent& outTransform
);
[[nodiscard]] bool SerializeSurfaceEditState(
    const DeformableSurfaceEditState& state,
    Core::Assets::AssetBytes& outBinary
);
[[nodiscard]] bool DeserializeSurfaceEditState(
    const Core::Assets::AssetBytes& binary,
    DeformableSurfaceEditState& outState
);
[[nodiscard]] bool CommitDeformableRestSpaceHole(
    DeformableRuntimeMeshInstance& instance,
    const DeformableHoleEditParams& params,
    DeformableHoleEditResult* outResult = nullptr
);


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

