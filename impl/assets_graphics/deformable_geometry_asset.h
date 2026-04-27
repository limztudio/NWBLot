// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#pragma once


#include "deformable_geometry_types.h"

#include <core/assets/asset.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


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

