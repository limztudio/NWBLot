// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#pragma once


#include "../global.h"

#include <core/assets/asset.h>
#include <global/matrix_math.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


struct DeformableVertexRest{
    Float3Data position;
    Float3Data normal;
    Float4Data tangent;
    Float2Data uv0;
    Float4Data color0;
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

struct SourceSample{
    u32 sourceTri = 0;
    f32 bary[3] = {};
};
static_assert(IsStandardLayout_V<SourceSample>, "SourceSample must stay binary-serializable");
static_assert(IsTriviallyCopyable_V<SourceSample>, "SourceSample must stay binary-serializable");

struct DeformableMorphDelta{
    u32 vertexId = 0;
    Float3Data deltaPosition;
    Float3Data deltaNormal;
    Float4Data deltaTangent;
};
static_assert(IsStandardLayout_V<DeformableMorphDelta>, "DeformableMorphDelta must stay binary-serializable");
static_assert(IsTriviallyCopyable_V<DeformableMorphDelta>, "DeformableMorphDelta must stay binary-serializable");

struct DeformableMorph{
    Name name = NAME_NONE;
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
    void setSourceSamples(Vector<SourceSample>&& sourceSamples){ m_sourceSamples = Move(sourceSamples); }
    void setMorphs(Vector<DeformableMorph>&& morphs){ m_morphs = Move(morphs); }

    [[nodiscard]] const Vector<DeformableVertexRest>& restVertices()const{ return m_restVertices; }
    [[nodiscard]] const Vector<u32>& indices()const{ return m_indices; }
    [[nodiscard]] const Vector<SkinInfluence4>& skin()const{ return m_skin; }
    [[nodiscard]] const Vector<SourceSample>& sourceSamples()const{ return m_sourceSamples; }
    [[nodiscard]] const Vector<DeformableMorph>& morphs()const{ return m_morphs; }


private:
    Vector<DeformableVertexRest> m_restVertices;
    Vector<u32> m_indices;
    Vector<SkinInfluence4> m_skin;
    Vector<SourceSample> m_sourceSamples;
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


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

