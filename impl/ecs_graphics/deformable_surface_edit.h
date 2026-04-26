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
    // First newly-added inner wall-loop vertex; rim vertices are shared with the edited mesh boundary.
    u32 firstWallVertex = Limit<u32>::s_Max;
    // Number of newly-added inner wall-loop vertices.
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

namespace DeformableSurfaceEditPermission{
    enum Enum : u32{
        Allowed = 0u,
        Restricted = 1u,
        Forbidden = 2u,
    };
};

struct alignas(Float4) DeformableHolePreview{
    Float4 center = Float4(0.0f, 0.0f, 0.0f, 1.0f);
    Float4 normal = Float4(0.0f, 0.0f, 1.0f, 0.0f);
    Float4 tangent = Float4(1.0f, 0.0f, 0.0f, 0.0f);
    Float4 bitangent = Float4(0.0f, 1.0f, 0.0f, 0.0f);
    f32 radius = 0.0f;
    f32 ellipseRatio = 1.0f;
    f32 depth = 0.0f;
    u32 editRevision = 0;
    DeformableEditMaskFlags editMaskFlags = s_DeformableEditMaskDefault;
    DeformableSurfaceEditPermission::Enum editPermission = DeformableSurfaceEditPermission::Allowed;
    bool valid = false;
};
static_assert(IsStandardLayout_V<DeformableHolePreview>, "DeformableHolePreview must stay layout-stable");
static_assert(IsTriviallyCopyable_V<DeformableHolePreview>, "DeformableHolePreview must stay cheap to copy");
static_assert(alignof(DeformableHolePreview) >= alignof(Float4), "DeformableHolePreview must stay SIMD-aligned");

struct DeformableSurfaceHoleEditRecord{
    SourceSample restSample;
    Float3U restPosition = Float3U(0.0f, 0.0f, 0.0f);
    Float3U restNormal = Float3U(0.0f, 0.0f, 1.0f);
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
    DeformableSurfaceEditId editId = 0;
    DeformableSurfaceEditRecordType::Enum type = DeformableSurfaceEditRecordType::Hole;
    DeformableSurfaceHoleEditRecord hole;
    DeformableHoleEditResult result;
};
static_assert(IsStandardLayout_V<DeformableSurfaceEditRecord>, "DeformableSurfaceEditRecord must stay binary-serializable");
static_assert(IsTriviallyCopyable_V<DeformableSurfaceEditRecord>, "DeformableSurfaceEditRecord must stay binary-serializable");

struct DeformableAccessoryAttachmentRecord{
    Core::Assets::AssetRef<Geometry> geometry;
    Core::Assets::AssetRef<Material> material;
    CompactString geometryVirtualPathText;
    CompactString materialVirtualPathText;
    DeformableSurfaceEditId anchorEditId = 0;
    u32 firstWallVertex = Limit<u32>::s_Max;
    u32 wallVertexCount = 0;
    f32 normalOffset = 0.0f;
    f32 uniformScale = 1.0f;
};

struct DeformableSurfaceEditState{
    Vector<DeformableSurfaceEditRecord> edits;
    Vector<DeformableAccessoryAttachmentRecord> accessories;
};

struct DeformableSurfaceEditReplayResult{
    u32 appliedEditCount = 0;
    u32 restoredAccessoryCount = 0;
    u32 finalEditRevision = 0;
    bool topologyChanged = false;
};

struct DeformableSurfaceEditUndoResult{
    DeformableSurfaceEditId undoneEditId = 0;
    u32 removedAccessoryCount = 0;
    DeformableSurfaceEditReplayResult replay;
};

struct DeformableSurfaceEditReplayContext{
    Core::Assets::AssetManager* assetManager = nullptr;
    Core::ECS::World* world = nullptr;
    Core::ECS::EntityID targetEntity = Core::ECS::ENTITY_ID_INVALID;
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
[[nodiscard]] bool BuildSurfaceEditStateDebugDump(
    const DeformableSurfaceEditState& state,
    AString& outDump
);
[[nodiscard]] bool ApplySurfaceEditState(
    DeformableRuntimeMeshInstance& instance,
    const DeformableSurfaceEditState& state,
    const DeformableSurfaceEditReplayContext& context,
    DeformableSurfaceEditReplayResult* outResult = nullptr
);
[[nodiscard]] bool UndoLastSurfaceEdit(
    DeformableRuntimeMeshInstance& instance,
    const DeformableRuntimeMeshInstance& cleanBaseInstance,
    DeformableSurfaceEditState& state,
    DeformableSurfaceEditUndoResult* outResult = nullptr
);
[[nodiscard]] bool CommitDeformableRestSpaceHole(
    DeformableRuntimeMeshInstance& instance,
    const DeformableHoleEditParams& params,
    DeformableHoleEditResult* outResult = nullptr
);


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

