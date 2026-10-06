// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#pragma once


#include "asset.h"
#include "skin_types.h"

#include <impl/assets_skeleton/asset.h>

#include <core/assets/module.h>
#include <core/assets/ref.h>


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
    explicit Skin(Core::Assets::AssetArena& arena)noexcept
        : m_influences(arena)
        , m_inverseBindMatrices(arena)
    {}
    Skin(Core::Assets::AssetArena& arena, const Name& virtualPath)noexcept
        : Core::Assets::TypedAsset<Skin>(virtualPath)
        , m_influences(arena)
        , m_inverseBindMatrices(arena)
    {}


public:
    bool loadBinary(const Core::Assets::AssetBytes& binary);
    [[nodiscard]] bool validatePayload()const;

public:
    void setMesh(const Core::Assets::AssetRef<Mesh>& mesh)noexcept{ m_mesh = mesh; }
    void setSkeleton(const Core::Assets::AssetRef<Skeleton>& skeleton)noexcept{ m_skeleton = skeleton; }
    void setPayload(InfluenceVector&& influences, InverseBindMatrixVector&& inverseBindMatrices)noexcept{
        m_influences = Move(influences);
        m_inverseBindMatrices = Move(inverseBindMatrices);
    }

public:
    [[nodiscard]] const Core::Assets::AssetRef<Mesh>& mesh()const noexcept{ return m_mesh; }
    [[nodiscard]] const Core::Assets::AssetRef<Skeleton>& skeleton()const noexcept{ return m_skeleton; }
    [[nodiscard]] const InfluenceVector& influences()const noexcept{ return m_influences; }
    [[nodiscard]] const InverseBindMatrixVector& inverseBindMatrices()const noexcept{ return m_inverseBindMatrices; }


private:
    Core::Assets::AssetRef<Mesh> m_mesh;
    Core::Assets::AssetRef<Skeleton> m_skeleton;
    InfluenceVector m_influences;
    InverseBindMatrixVector m_inverseBindMatrices;
};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_DEFINE_ASSET_CODEC(SkinAssetCodec, Skin);


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

