// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#pragma once


#include "skinned_geometry_types.h"

#include <core/assets/asset.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


class SkinnedGeometryDisplacementTexture final : public Core::Assets::TypedAsset<SkinnedGeometryDisplacementTexture>{
public:
    NWB_DEFINE_ASSET_TYPE("skinned_geometry_displacement_texture")


public:
    SkinnedGeometryDisplacementTexture() = default;
    explicit SkinnedGeometryDisplacementTexture(const Name& virtualPath)
        : Core::Assets::TypedAsset<SkinnedGeometryDisplacementTexture>(virtualPath)
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


class SkinnedGeometry final : public Core::Assets::TypedAsset<SkinnedGeometry>{
public:
    NWB_DEFINE_ASSET_TYPE("skinned_geometry")


public:
    SkinnedGeometry() = default;
    explicit SkinnedGeometry(const Name& virtualPath)
        : Core::Assets::TypedAsset<SkinnedGeometry>(virtualPath)
    {}


public:
    bool loadBinary(const Core::Assets::AssetBytes& binary);
    [[nodiscard]] bool validatePayload()const;

public:
    void setRestVertices(Vector<SkinnedGeometryVertex>&& vertices){ m_restVertices = Move(vertices); }
    void setIndices(Vector<u32>&& indices){ m_indices = Move(indices); }
    void setGeometryClass(u32 geometryClass){ m_geometryClass = geometryClass; }
    void setSkin(Vector<SkinInfluence4>&& skin){ m_skin = Move(skin); }
    void setSkeletonJointCount(u32 jointCount){ m_skeletonJointCount = jointCount; }
    void setInverseBindMatrices(Vector<SkinnedGeometryJointMatrix>&& inverseBindMatrices){ m_inverseBindMatrices = Move(inverseBindMatrices); }
    void setSourceSamples(Vector<SourceSample>&& sourceSamples){ m_sourceSamples = Move(sourceSamples); }
    void setEditMaskPerTriangle(Vector<SkinnedGeometryEditMaskFlags>&& editMaskPerTriangle){ m_editMaskPerTriangle = Move(editMaskPerTriangle); }
    void setDisplacement(const SkinnedGeometryDisplacement& displacement){ m_displacement = displacement; }
    [[nodiscard]] bool setDisplacementTextureVirtualPathText(AStringView text){ return m_displacementTextureVirtualPathText.assign(text); }
    void setDisplacementTextureVirtualPathText(CompactString&& text){ m_displacementTextureVirtualPathText = Move(text); }
    void setMorphs(Vector<SkinnedGeometryMorph>&& morphs){ m_morphs = Move(morphs); }

    [[nodiscard]] const Vector<SkinnedGeometryVertex>& restVertices()const{ return m_restVertices; }
    [[nodiscard]] const Vector<u32>& indices()const{ return m_indices; }
    [[nodiscard]] u32 geometryClass()const{ return m_geometryClass; }
    [[nodiscard]] const Vector<SkinInfluence4>& skin()const{ return m_skin; }
    [[nodiscard]] u32 skeletonJointCount()const{ return m_skeletonJointCount; }
    [[nodiscard]] const Vector<SkinnedGeometryJointMatrix>& inverseBindMatrices()const{ return m_inverseBindMatrices; }
    [[nodiscard]] const Vector<SourceSample>& sourceSamples()const{ return m_sourceSamples; }
    [[nodiscard]] const Vector<SkinnedGeometryEditMaskFlags>& editMaskPerTriangle()const{ return m_editMaskPerTriangle; }
    [[nodiscard]] const SkinnedGeometryDisplacement& displacement()const{ return m_displacement; }
    [[nodiscard]] const CompactString& displacementTextureVirtualPathText()const{ return m_displacementTextureVirtualPathText; }
    [[nodiscard]] const Vector<SkinnedGeometryMorph>& morphs()const{ return m_morphs; }


private:
    u32 m_geometryClass = GeometryClass::Invalid;
    Vector<SkinnedGeometryVertex> m_restVertices;
    Vector<u32> m_indices;
    Vector<SkinInfluence4> m_skin;
    u32 m_skeletonJointCount = 0;
    Vector<SkinnedGeometryJointMatrix> m_inverseBindMatrices;
    Vector<SourceSample> m_sourceSamples;
    Vector<SkinnedGeometryEditMaskFlags> m_editMaskPerTriangle;
    SkinnedGeometryDisplacement m_displacement;
    CompactString m_displacementTextureVirtualPathText;
    Vector<SkinnedGeometryMorph> m_morphs;
};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


class SkinnedGeometryAssetCodec final : public Core::Assets::AssetCodec<SkinnedGeometry>{
public:
    SkinnedGeometryAssetCodec() = default;


#if defined(NWB_COOK)
public:
    virtual bool serialize(const Core::Assets::IAsset& asset, Core::Assets::AssetBytes& outBinary)const override;
#endif
};

class SkinnedGeometryDisplacementTextureAssetCodec final : public Core::Assets::AssetCodec<SkinnedGeometryDisplacementTexture>{
public:
    SkinnedGeometryDisplacementTextureAssetCodec() = default;


#if defined(NWB_COOK)
public:
    virtual bool serialize(const Core::Assets::IAsset& asset, Core::Assets::AssetBytes& outBinary)const override;
#endif
};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

