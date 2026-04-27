// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#pragma once


#include "../global.h"

#include <core/assets/asset.h>
#include <core/assets/asset_ref.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


struct DeformableVertexRest{
    Float3U position;
    Float3U normal;
    Float4U tangent;
    Float2U uv0;
    Float4U color0;
};
static_assert(IsStandardLayout_V<DeformableVertexRest>, "DeformableVertexRest must stay binary-serializable");
static_assert(IsTriviallyCopyable_V<DeformableVertexRest>, "DeformableVertexRest must stay binary-serializable");
static_assert(sizeof(DeformableVertexRest) == sizeof(f32) * 16u, "DeformableVertexRest GPU/source layout drifted");

struct SkinInfluence4{
    u16 joint[4] = {};
    f32 weight[4] = {};
};
static_assert(IsStandardLayout_V<SkinInfluence4>, "SkinInfluence4 must stay binary-serializable");
static_assert(IsTriviallyCopyable_V<SkinInfluence4>, "SkinInfluence4 must stay binary-serializable");

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

struct SourceSample{
    u32 sourceTri = 0;
    f32 bary[3] = {};
};
static_assert(IsStandardLayout_V<SourceSample>, "SourceSample must stay binary-serializable");
static_assert(IsTriviallyCopyable_V<SourceSample>, "SourceSample must stay binary-serializable");

namespace DeformableEditMaskFlag{
    enum Enum : u8{
        Editable = 1u << 0u,
        Restricted = 1u << 1u,
        Forbidden = 1u << 2u,
        RequiresRepair = 1u << 3u,
    };
};
using DeformableEditMaskFlags = u8;

static constexpr DeformableEditMaskFlags s_DeformableEditMaskDefault = DeformableEditMaskFlag::Editable;
static constexpr DeformableEditMaskFlags s_DeformableEditMaskKnownFlags =
    DeformableEditMaskFlag::Editable
    | DeformableEditMaskFlag::Restricted
    | DeformableEditMaskFlag::Forbidden
    | DeformableEditMaskFlag::RequiresRepair
;

[[nodiscard]] inline bool ValidDeformableEditMaskFlags(const DeformableEditMaskFlags flags){
    if(flags == 0u || (flags & ~s_DeformableEditMaskKnownFlags) != 0u)
        return false;

    return (flags & (DeformableEditMaskFlag::Editable | DeformableEditMaskFlag::Forbidden))
        != (DeformableEditMaskFlag::Editable | DeformableEditMaskFlag::Forbidden)
    ;
}

[[nodiscard]] inline bool DeformableEditMaskAllowsCommit(const DeformableEditMaskFlags flags){
    return ValidDeformableEditMaskFlags(flags)
        && (flags & DeformableEditMaskFlag::Forbidden) == 0u
    ;
}

namespace DeformableDisplacementMode{
    enum Enum : u32{
        None = 0,
        ScalarUvRamp = 1,
        ScalarTexture = 2,
        VectorTangentTexture = 3,
        VectorObjectTexture = 4,
        PoseDrivenScalar = 5,
        PoseDrivenVector = 6,
    };
};

class DeformableDisplacementTexture final : public Core::Assets::TypedAsset<DeformableDisplacementTexture>{
public:
    NWB_DEFINE_ASSET_TYPE("deformable_displacement_texture")


public:
    DeformableDisplacementTexture() = default;
    explicit DeformableDisplacementTexture(const Name& virtualPath)
        : Core::Assets::TypedAsset<DeformableDisplacementTexture>(virtualPath)
    {}


public:
    bool loadBinary(const Core::Assets::AssetBytes& binary);
    [[nodiscard]] bool validatePayload()const;

public:
    void setSize(u32 width, u32 height);
    void setTexels(Vector<Float4U>&& texels){ m_texels = Move(texels); }

    [[nodiscard]] u32 width()const{ return m_width; }
    [[nodiscard]] u32 height()const{ return m_height; }
    [[nodiscard]] const Vector<Float4U>& texels()const{ return m_texels; }


private:
    u32 m_width = 0;
    u32 m_height = 0;
    Vector<Float4U> m_texels;
};

struct DeformableDisplacement{
    Core::Assets::AssetRef<DeformableDisplacementTexture> texture;
    u32 mode = DeformableDisplacementMode::None;
    f32 amplitude = 0.0f;
    f32 bias = 0.0f;
    Float2U uvScale = Float2U(1.0f, 1.0f);
    Float2U uvOffset = Float2U(0.0f, 0.0f);
};
static_assert(IsStandardLayout_V<DeformableDisplacement>, "DeformableDisplacement must stay layout-stable");
static_assert(IsTriviallyCopyable_V<DeformableDisplacement>, "DeformableDisplacement must stay cheap to copy");

[[nodiscard]] inline bool ValidDeformableDisplacementDescriptor(const DeformableDisplacement& displacement){
    if(!IsFinite(displacement.amplitude)
        || !IsFinite(displacement.bias)
        || !IsFinite(displacement.uvScale.x)
        || !IsFinite(displacement.uvScale.y)
        || !IsFinite(displacement.uvOffset.x)
        || !IsFinite(displacement.uvOffset.y)
    )
        return false;

    if(displacement.mode == DeformableDisplacementMode::None){
        return displacement.amplitude == 0.0f
            && displacement.bias == 0.0f
            && !displacement.texture.valid()
        ;
    }
    if(displacement.mode == DeformableDisplacementMode::ScalarUvRamp)
        return !displacement.texture.valid() && displacement.bias == 0.0f;
    if(displacement.mode == DeformableDisplacementMode::ScalarTexture
        || displacement.mode == DeformableDisplacementMode::VectorTangentTexture
        || displacement.mode == DeformableDisplacementMode::VectorObjectTexture
    )
        return displacement.texture.valid();

    return false;
}

struct DeformableMorphDelta{
    u32 vertexId = 0;
    Float3U deltaPosition;
    Float3U deltaNormal;
    Float4U deltaTangent;
};
static_assert(IsStandardLayout_V<DeformableMorphDelta>, "DeformableMorphDelta must stay binary-serializable");
static_assert(IsTriviallyCopyable_V<DeformableMorphDelta>, "DeformableMorphDelta must stay binary-serializable");

struct DeformableMorph{
    Name name = NAME_NONE;
    CompactString nameText;
    Vector<DeformableMorphDelta> deltas;
};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


class DeformableGeometry final : public Core::Assets::TypedAsset<DeformableGeometry>{
public:
    NWB_DEFINE_ASSET_TYPE("deformable_geometry")


public:
    DeformableGeometry() = default;
    explicit DeformableGeometry(const Name& virtualPath)
        : Core::Assets::TypedAsset<DeformableGeometry>(virtualPath)
    {}


public:
    bool loadBinary(const Core::Assets::AssetBytes& binary);
    [[nodiscard]] bool validatePayload()const;

public:
    void setRestVertices(Vector<DeformableVertexRest>&& vertices){ m_restVertices = Move(vertices); }
    void setIndices(Vector<u32>&& indices){ m_indices = Move(indices); }
    void setSkin(Vector<SkinInfluence4>&& skin){ m_skin = Move(skin); }
    void setSkeletonJointCount(u32 jointCount){ m_skeletonJointCount = jointCount; }
    void setInverseBindMatrices(Vector<DeformableJointMatrix>&& inverseBindMatrices){ m_inverseBindMatrices = Move(inverseBindMatrices); }
    void setSourceSamples(Vector<SourceSample>&& sourceSamples){ m_sourceSamples = Move(sourceSamples); }
    void setEditMaskPerTriangle(Vector<DeformableEditMaskFlags>&& editMaskPerTriangle){ m_editMaskPerTriangle = Move(editMaskPerTriangle); }
    void setDisplacement(const DeformableDisplacement& displacement){ m_displacement = displacement; }
    [[nodiscard]] bool setDisplacementTextureVirtualPathText(AStringView text){ return m_displacementTextureVirtualPathText.assign(text); }
    void setDisplacementTextureVirtualPathText(CompactString&& text){ m_displacementTextureVirtualPathText = Move(text); }
    void setMorphs(Vector<DeformableMorph>&& morphs){ m_morphs = Move(morphs); }

    [[nodiscard]] const Vector<DeformableVertexRest>& restVertices()const{ return m_restVertices; }
    [[nodiscard]] const Vector<u32>& indices()const{ return m_indices; }
    [[nodiscard]] const Vector<SkinInfluence4>& skin()const{ return m_skin; }
    [[nodiscard]] u32 skeletonJointCount()const{ return m_skeletonJointCount; }
    [[nodiscard]] const Vector<DeformableJointMatrix>& inverseBindMatrices()const{ return m_inverseBindMatrices; }
    [[nodiscard]] const Vector<SourceSample>& sourceSamples()const{ return m_sourceSamples; }
    [[nodiscard]] const Vector<DeformableEditMaskFlags>& editMaskPerTriangle()const{ return m_editMaskPerTriangle; }
    [[nodiscard]] const DeformableDisplacement& displacement()const{ return m_displacement; }
    [[nodiscard]] const CompactString& displacementTextureVirtualPathText()const{ return m_displacementTextureVirtualPathText; }
    [[nodiscard]] const Vector<DeformableMorph>& morphs()const{ return m_morphs; }


private:
    Vector<DeformableVertexRest> m_restVertices;
    Vector<u32> m_indices;
    Vector<SkinInfluence4> m_skin;
    u32 m_skeletonJointCount = 0;
    Vector<DeformableJointMatrix> m_inverseBindMatrices;
    Vector<SourceSample> m_sourceSamples;
    Vector<DeformableEditMaskFlags> m_editMaskPerTriangle;
    DeformableDisplacement m_displacement;
    CompactString m_displacementTextureVirtualPathText;
    Vector<DeformableMorph> m_morphs;
};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


class DeformableGeometryAssetCodec final : public Core::Assets::TypedAssetCodec<DeformableGeometry>{
public:
    DeformableGeometryAssetCodec() = default;

public:
    virtual bool deserialize(
        const Name& virtualPath,
        const Core::Assets::AssetBytes& binary,
        UniquePtr<Core::Assets::IAsset>& outAsset
    )const override;
#if defined(NWB_COOK)
    virtual bool serialize(const Core::Assets::IAsset& asset, Core::Assets::AssetBytes& outBinary)const override;
#endif
};

class DeformableDisplacementTextureAssetCodec final : public Core::Assets::TypedAssetCodec<DeformableDisplacementTexture>{
public:
    DeformableDisplacementTextureAssetCodec() = default;

public:
    virtual bool deserialize(
        const Name& virtualPath,
        const Core::Assets::AssetBytes& binary,
        UniquePtr<Core::Assets::IAsset>& outAsset
    )const override;
#if defined(NWB_COOK)
    virtual bool serialize(const Core::Assets::IAsset& asset, Core::Assets::AssetBytes& outBinary)const override;
#endif
};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

