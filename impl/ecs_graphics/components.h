// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#pragma once


#include "../global.h"

#include <core/assets/asset_ref.h>
#include <core/ecs/entity_id.h>

#include <cstddef>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


class Geometry;
class DeformableGeometry;
class Material;


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace RuntimeMeshDirtyFlag{
    enum Enum : u8{
        None = 0,
        TopologyDirty = 1u << 0u,
        AttributesDirty = 1u << 1u,
        DeformerInputDirty = 1u << 2u,
        GpuUploadDirty = 1u << 3u,
        All = TopologyDirty | AttributesDirty | DeformerInputDirty | GpuUploadDirty,
    };
};
using RuntimeMeshDirtyFlags = u8;

struct RuntimeMeshHandle{
    u64 value = 0;

    [[nodiscard]] bool valid()const{ return value != 0u; }
    [[nodiscard]] explicit operator bool()const{ return valid(); }
    void reset(){ value = 0; }
};

[[nodiscard]] inline bool operator==(const RuntimeMeshHandle& lhs, const RuntimeMeshHandle& rhs){
    return lhs.value == rhs.value;
}
[[nodiscard]] inline bool operator!=(const RuntimeMeshHandle& lhs, const RuntimeMeshHandle& rhs){
    return !(lhs == rhs);
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


struct RendererComponent{
    Core::Assets::AssetRef<Geometry> geometry;
    Core::Assets::AssetRef<Material> material;
    bool visible = true;
};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


struct DeformableMorphWeight{
    Name morph = NAME_NONE;
    f32 weight = 0.0f;
};

struct DeformableMorphWeightsComponent{
    Vector<DeformableMorphWeight> weights;
};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


struct DeformableJointMatrix{
    Float4 column0 = Float4(1.0f, 0.0f, 0.0f, 0.0f);
    Float4 column1 = Float4(0.0f, 1.0f, 0.0f, 0.0f);
    Float4 column2 = Float4(0.0f, 0.0f, 1.0f, 0.0f);
    Float4 column3 = Float4(0.0f, 0.0f, 0.0f, 1.0f);
};
static_assert(IsStandardLayout_V<DeformableJointMatrix>, "DeformableJointMatrix must stay GPU-uploadable");
static_assert(IsTriviallyCopyable_V<DeformableJointMatrix>, "DeformableJointMatrix must stay GPU-uploadable");
static_assert(sizeof(DeformableJointMatrix) == sizeof(f32) * 16u, "DeformableJointMatrix GPU layout drifted");
static_assert(alignof(DeformableJointMatrix) >= alignof(Float4), "DeformableJointMatrix must stay SIMD-aligned");

struct DeformableJointPaletteComponent{
    Vector<DeformableJointMatrix> joints;
};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


struct DeformableDisplacementComponent{
    f32 amplitudeScale = 1.0f;
    bool enabled = true;
};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


struct DeformableRendererComponent{
    Core::Assets::AssetRef<DeformableGeometry> deformableGeometry;
    Core::Assets::AssetRef<Material> material;
    RuntimeMeshHandle runtimeMesh;
    bool visible = true;
};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


struct alignas(Float4) DeformableAccessoryAttachmentComponent{
    Core::ECS::EntityID targetEntity = Core::ECS::ENTITY_ID_INVALID;
    RuntimeMeshHandle runtimeMesh;
    u32 editRevision = 0;
    u32 firstWallVertex = Limit<u32>::s_Max;
    u32 wallVertexCount = 0;
    // x = normal offset, y = uniform scale.
    Float4 placement = Float4(0.0f, 1.0f, 0.0f, 0.0f);

    [[nodiscard]] f32 normalOffset()const{ return placement.x; }
    [[nodiscard]] f32 uniformScale()const{ return placement.y; }

    void setNormalOffset(const f32 value){ placement.x = value; }
    void setUniformScale(const f32 value){ placement.y = value; }
};
static_assert(
    IsStandardLayout_V<DeformableAccessoryAttachmentComponent>,
    "DeformableAccessoryAttachmentComponent must stay layout-stable for ECS storage"
);
static_assert(
    IsTriviallyCopyable_V<DeformableAccessoryAttachmentComponent>,
    "DeformableAccessoryAttachmentComponent must stay cheap to move in dense ECS storage"
);
static_assert(
    alignof(DeformableAccessoryAttachmentComponent) >= alignof(Float4),
    "DeformableAccessoryAttachmentComponent must stay SIMD-aligned"
);
static_assert(
    (offsetof(DeformableAccessoryAttachmentComponent, placement) % alignof(Float4)) == 0,
    "DeformableAccessoryAttachmentComponent::placement must stay aligned"
);


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

