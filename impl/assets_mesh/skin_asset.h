// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#pragma once


#include "asset.h"
#include "skin_types.h"

#include <impl/assets_skeleton/asset.h>

#include <global/core/assets/module.h>
#include <global/core/assets/ref.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


class Skin final : public Core::Assets::TypedAsset<Skin>{
public:
    NWB_DEFINE_ASSET_TYPE("skin")


public:
    using InfluenceVector = Core::Assets::AssetVector<SkinInfluence4>;
    using InverseBindMatrixVector = Core::Assets::AssetVector<SkeletonJointMatrix>;


public:
    explicit Skin(Core::Assets::AssetArena& arena)
        : m_influences(arena)
        , m_inverseBindMatrices(arena)
    {}
    Skin(Core::Assets::AssetArena& arena, const Name& virtualPath)
        : Core::Assets::TypedAsset<Skin>(virtualPath)
        , m_influences(arena)
        , m_inverseBindMatrices(arena)
    {}


public:
    bool loadBinary(const Core::Assets::AssetBytes& binary);
    [[nodiscard]] bool validatePayload()const;

public:
    void setMesh(const Core::Assets::AssetRef<Mesh>& mesh){ m_mesh = mesh; }
    void setSkeleton(const Core::Assets::AssetRef<Skeleton>& skeleton){ m_skeleton = skeleton; }
    void setPayload(InfluenceVector&& influences, InverseBindMatrixVector&& inverseBindMatrices){
        m_influences = Move(influences);
        m_inverseBindMatrices = Move(inverseBindMatrices);
    }

public:
    [[nodiscard]] const Core::Assets::AssetRef<Mesh>& mesh()const{ return m_mesh; }
    [[nodiscard]] const Core::Assets::AssetRef<Skeleton>& skeleton()const{ return m_skeleton; }
    [[nodiscard]] const InfluenceVector& influences()const{ return m_influences; }
    [[nodiscard]] const InverseBindMatrixVector& inverseBindMatrices()const{ return m_inverseBindMatrices; }


private:
    Core::Assets::AssetRef<Mesh> m_mesh;
    Core::Assets::AssetRef<Skeleton> m_skeleton;
    InfluenceVector m_influences;
    InverseBindMatrixVector m_inverseBindMatrices;
};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


class SkinAssetCodec final : public Core::Assets::AssetCodec<Skin>{
public:
    SkinAssetCodec() = default;


#if defined(NWB_COOK)
public:
    virtual bool serialize(const Core::Assets::IAsset& asset, Core::Assets::AssetBytes& outBinary)const override;
#endif
};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

