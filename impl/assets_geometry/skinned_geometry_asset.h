// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#pragma once


#include "skinned_geometry_types.h"

#include <core/assets/asset.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_BEGIN


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

    [[nodiscard]] const Vector<SkinnedGeometryVertex>& restVertices()const{ return m_restVertices; }
    [[nodiscard]] const Vector<u32>& indices()const{ return m_indices; }
    [[nodiscard]] u32 geometryClass()const{ return m_geometryClass; }
    [[nodiscard]] const Vector<SkinInfluence4>& skin()const{ return m_skin; }
    [[nodiscard]] u32 skeletonJointCount()const{ return m_skeletonJointCount; }
    [[nodiscard]] const Vector<SkinnedGeometryJointMatrix>& inverseBindMatrices()const{ return m_inverseBindMatrices; }


private:
    u32 m_geometryClass = GeometryClass::Invalid;
    Vector<SkinnedGeometryVertex> m_restVertices;
    Vector<u32> m_indices;
    Vector<SkinInfluence4> m_skin;
    u32 m_skeletonJointCount = 0;
    Vector<SkinnedGeometryJointMatrix> m_inverseBindMatrices;
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

NWB_IMPL_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

