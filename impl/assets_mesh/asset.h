// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#pragma once


#include "../global.h"
#include "geometry_payload.h"

#include <core/assets/module.h>
#include <core/mesh/classification.h>


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
    void setPayload(
        Core::Assets::AssetVector<Float3U>&& positions,
        Core::Assets::AssetVector<Half4U>&& normals,
        Core::Assets::AssetVector<Half4U>&& tangents,
        Core::Assets::AssetVector<Float2U>&& uv0,
        Core::Assets::AssetVector<Half4U>&& colors,
        Core::Assets::AssetVector<MeshletDesc>&& meshlets,
        Core::Assets::AssetVector<MeshletBounds>&& meshletBounds,
        Core::Assets::AssetVector<u8>&& meshletPositionRefDeltas,
        Core::Assets::AssetVector<u8>&& meshletAttributeRefDeltas,
        Core::Assets::AssetVector<MeshletLocalVertexRef>&& meshletLocalVertexRefs,
        Core::Assets::AssetVector<u8>&& meshletPrimitiveIndices
    ){
        setGeometryPayload(
            Move(positions),
            Move(normals),
            Move(tangents),
            Move(uv0),
            Move(colors),
            Move(meshlets),
            Move(meshletBounds),
            Move(meshletPositionRefDeltas),
            Move(meshletAttributeRefDeltas),
            Move(meshletLocalVertexRefs),
            Move(meshletPrimitiveIndices)
        );
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

