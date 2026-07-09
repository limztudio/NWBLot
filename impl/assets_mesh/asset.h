// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#pragma once


#include "../global.h"
#include "geometry_payload.h"

#include <global/core/assets/module.h>
#include <global/core/mesh/classification.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


class Mesh final : public Core::Assets::TypedAsset<Mesh>, public MeshGeometryPayload{
public:
    NWB_DEFINE_ASSET_TYPE("mesh")


public:
    explicit Mesh(Core::Assets::AssetArena& arena)
        : MeshGeometryPayload(arena)
    {}
    Mesh(Core::Assets::AssetArena& arena, const Name& virtualPath)
        : Core::Assets::TypedAsset<Mesh>(virtualPath)
        , MeshGeometryPayload(arena)
    {}


public:
    bool loadBinary(const Core::Assets::AssetBytes& binary);
    [[nodiscard]] bool validatePayload()const;

public:
    template<typename... GeometryPayloadArgT>
    void setPayload(GeometryPayloadArgT&&... geometryPayloadArgs){
        setGeometryPayload(Forward<GeometryPayloadArgT>(geometryPayloadArgs)...);
    }
    [[nodiscard]] u32 meshClass()const{ return Core::Mesh::MeshClass::Static; }
};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


class MeshAssetCodec final : public Core::Assets::AssetCodec<Mesh>{
public:
    MeshAssetCodec() = default;


#if defined(NWB_COOK)
public:
    virtual bool serialize(const Core::Assets::IAsset& asset, Core::Assets::AssetBytes& outBinary)const override;
#endif
};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

