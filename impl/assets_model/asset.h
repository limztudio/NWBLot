// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#pragma once


#include "../global.h"

#include <impl/assets_mesh/asset.h>
#include <impl/assets_mesh/skin_asset.h>
#include <impl/assets_skeleton/asset.h>

#include <core/assets/module.h>
#include <core/assets/ref.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


[[nodiscard]] inline SkinnedMeshJointMatrix MakeIdentityModelMatrix(){
    return MakeIdentitySkinnedMeshJointMatrix();
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


struct ModelSkeletonObject{
    Name name = NAME_NONE;
    Core::Assets::AssetRef<Skeleton> skeleton;
    SkinnedMeshJointMatrix transform = MakeIdentityModelMatrix();
};

struct ModelStaticMeshObject{
    Name name = NAME_NONE;
    Core::Assets::AssetRef<Mesh> mesh;
    Name parentObject = NAME_NONE;
    Name parentJoint = NAME_NONE;
    SkinnedMeshJointMatrix transform = MakeIdentityModelMatrix();
};

struct ModelSkinnedMeshObject{
    Name name = NAME_NONE;
    Core::Assets::AssetRef<Mesh> mesh;
    Core::Assets::AssetRef<Skin> skin;
    Name skeletonObject = NAME_NONE;
    SkinnedMeshJointMatrix transform = MakeIdentityModelMatrix();
};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


class Model final : public Core::Assets::TypedAsset<Model>{
public:
    NWB_DEFINE_ASSET_TYPE("model")


public:
    using SkeletonObjectVector = Core::Assets::AssetVector<ModelSkeletonObject>;
    using StaticMeshObjectVector = Core::Assets::AssetVector<ModelStaticMeshObject>;
    using SkinnedMeshObjectVector = Core::Assets::AssetVector<ModelSkinnedMeshObject>;


public:
    explicit Model(Core::Assets::AssetArena& arena)
        : m_skeletonObjects(arena)
        , m_staticMeshObjects(arena)
        , m_skinnedMeshObjects(arena)
    {}
    Model(Core::Assets::AssetArena& arena, const Name& virtualPath)
        : Core::Assets::TypedAsset<Model>(virtualPath)
        , m_skeletonObjects(arena)
        , m_staticMeshObjects(arena)
        , m_skinnedMeshObjects(arena)
    {}


public:
    bool loadBinary(const Core::Assets::AssetBytes& binary);
    [[nodiscard]] bool validatePayload()const;

public:
    void setObjects(
        SkeletonObjectVector&& skeletonObjects,
        StaticMeshObjectVector&& staticMeshObjects,
        SkinnedMeshObjectVector&& skinnedMeshObjects
    ){
        m_skeletonObjects = Move(skeletonObjects);
        m_staticMeshObjects = Move(staticMeshObjects);
        m_skinnedMeshObjects = Move(skinnedMeshObjects);
    }

public:
    [[nodiscard]] const SkeletonObjectVector& skeletonObjects()const{ return m_skeletonObjects; }
    [[nodiscard]] const StaticMeshObjectVector& staticMeshObjects()const{ return m_staticMeshObjects; }
    [[nodiscard]] const SkinnedMeshObjectVector& skinnedMeshObjects()const{ return m_skinnedMeshObjects; }


private:
    SkeletonObjectVector m_skeletonObjects;
    StaticMeshObjectVector m_staticMeshObjects;
    SkinnedMeshObjectVector m_skinnedMeshObjects;
};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


class ModelAssetCodec final : public Core::Assets::AssetCodec<Model>{
public:
    ModelAssetCodec() = default;


#if defined(NWB_COOK)
public:
    virtual bool serialize(const Core::Assets::IAsset& asset, Core::Assets::AssetBytes& outBinary)const override;
#endif
};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
