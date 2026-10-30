// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#pragma once


#include "../global.h"

#include <impl/assets_material/asset.h>
#include <impl/assets_mesh/asset.h>
#include <impl/assets_mesh/skin_asset.h>
#include <impl/assets_model/types.adhd.h>
#include <impl/assets_skeleton/asset.h>

#include <core/assets/module.h>
#include <core/assets/ref.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


inline constexpr TStringView s_ModelAssetKindLabel = NWB_TEXT("model");
inline constexpr TStringView s_ModelLoadBinaryContext = NWB_TEXT("Model::loadBinary");
inline constexpr TStringView s_ModelSerializeContext = NWB_TEXT("ModelAssetCodec::serialize");
inline constexpr TStringView s_ModelSkeletonObjectsPayloadLabel = NWB_TEXT("skeleton objects");
inline constexpr TStringView s_ModelStaticMeshObjectsPayloadLabel = NWB_TEXT("static mesh objects");
inline constexpr TStringView s_ModelSkinnedMeshObjectsPayloadLabel = NWB_TEXT("skinned mesh objects");


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


class Model final : public Core::Assets::TypedAsset<Model>{
public:
    using SkeletonObjectVector = Core::Assets::AssetVector<ModelSkeletonObject>;
    using StaticMeshObjectVector = Core::Assets::AssetVector<ModelStaticMeshObject>;
    using SkinnedMeshObjectVector = Core::Assets::AssetVector<ModelSkinnedMeshObject>;


public:
    NWB_DEFINE_ASSET_TYPE("model")


public:
    explicit Model(Core::Assets::AssetArena& arena)noexcept
        : m_skeletonObjects(arena)
        , m_staticMeshObjects(arena)
        , m_skinnedMeshObjects(arena)
    {}
    Model(Core::Assets::AssetArena& arena, const Name& virtualPath)noexcept
        : Core::Assets::TypedAsset<Model>(virtualPath)
        , m_skeletonObjects(arena)
        , m_staticMeshObjects(arena)
        , m_skinnedMeshObjects(arena)
    {}


public:
    [[nodiscard]] b8 validatePayload(Core::Alloc::ScratchArena& scratchArena)const;
    b8 loadBinary(Span<const u8> binary);

public:
    void setObjects(SkeletonObjectVector&& skeletonObjects, StaticMeshObjectVector&& staticMeshObjects, SkinnedMeshObjectVector&& skinnedMeshObjects)noexcept{
        m_skeletonObjects = Move(skeletonObjects);
        m_staticMeshObjects = Move(staticMeshObjects);
        m_skinnedMeshObjects = Move(skinnedMeshObjects);
    }

public:
    [[nodiscard]] const SkeletonObjectVector& skeletonObjects()const noexcept{ return m_skeletonObjects; }

    [[nodiscard]] const StaticMeshObjectVector& staticMeshObjects()const noexcept{ return m_staticMeshObjects; }

    [[nodiscard]] const SkinnedMeshObjectVector& skinnedMeshObjects()const noexcept{ return m_skinnedMeshObjects; }


private:
    SkeletonObjectVector m_skeletonObjects;
    StaticMeshObjectVector m_staticMeshObjects;
    SkinnedMeshObjectVector m_skinnedMeshObjects;
};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_DEFINE_ASSET_CODEC(ModelAssetCodec, Model);


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

