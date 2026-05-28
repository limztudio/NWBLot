// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#pragma once


#include "../global.h"
#include "payload_types.h"

#include <core/assets/module.h>
#include <core/mesh/classification.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


class Mesh final : public Core::Assets::TypedAsset<Mesh>{
public:
    NWB_DEFINE_ASSET_TYPE("mesh")


public:
    explicit Mesh(Core::Assets::AssetArena& arena)
        : m_positionStream(arena)
        , m_normalStream(arena)
        , m_tangentStream(arena)
        , m_uv0Stream(arena)
        , m_colorStream(arena)
        , m_meshlets(arena)
        , m_meshletBounds(arena)
        , m_meshletPositionRefs(arena)
        , m_meshletAttributeRefs(arena)
        , m_meshletLocalVertexRefs(arena)
        , m_meshletPrimitiveIndices(arena)
    {}
    Mesh(Core::Assets::AssetArena& arena, const Name& virtualPath)
        : Core::Assets::TypedAsset<Mesh>(virtualPath)
        , m_positionStream(arena)
        , m_normalStream(arena)
        , m_tangentStream(arena)
        , m_uv0Stream(arena)
        , m_colorStream(arena)
        , m_meshlets(arena)
        , m_meshletBounds(arena)
        , m_meshletPositionRefs(arena)
        , m_meshletAttributeRefs(arena)
        , m_meshletLocalVertexRefs(arena)
        , m_meshletPrimitiveIndices(arena)
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
        Core::Assets::AssetVector<MeshletDeformedPositionRef>&& meshletPositionRefs,
        Core::Assets::AssetVector<MeshletShadingAttributeRef>&& meshletAttributeRefs,
        Core::Assets::AssetVector<MeshletLocalVertexRef>&& meshletLocalVertexRefs,
        Core::Assets::AssetVector<u8>&& meshletPrimitiveIndices
    ){
        m_positionStream = Move(positions);
        m_normalStream = Move(normals);
        m_tangentStream = Move(tangents);
        m_uv0Stream = Move(uv0);
        m_colorStream = Move(colors);
        m_meshlets = Move(meshlets);
        m_meshletBounds = Move(meshletBounds);
        m_meshletPositionRefs = Move(meshletPositionRefs);
        m_meshletAttributeRefs = Move(meshletAttributeRefs);
        m_meshletLocalVertexRefs = Move(meshletLocalVertexRefs);
        m_meshletPrimitiveIndices = Move(meshletPrimitiveIndices);
    }
    [[nodiscard]] const Core::Assets::AssetVector<Float3U>& positionStream()const{ return m_positionStream; }
    [[nodiscard]] const Core::Assets::AssetVector<Half4U>& normalStream()const{ return m_normalStream; }
    [[nodiscard]] const Core::Assets::AssetVector<Half4U>& tangentStream()const{ return m_tangentStream; }
    [[nodiscard]] const Core::Assets::AssetVector<Float2U>& uv0Stream()const{ return m_uv0Stream; }
    [[nodiscard]] const Core::Assets::AssetVector<Half4U>& colorStream()const{ return m_colorStream; }
    [[nodiscard]] const Core::Assets::AssetVector<MeshletDesc>& meshlets()const{ return m_meshlets; }
    [[nodiscard]] const Core::Assets::AssetVector<MeshletBounds>& meshletBounds()const{ return m_meshletBounds; }
    [[nodiscard]] const Core::Assets::AssetVector<MeshletDeformedPositionRef>& meshletPositionRefs()const{
        return m_meshletPositionRefs;
    }
    [[nodiscard]] const Core::Assets::AssetVector<MeshletShadingAttributeRef>& meshletAttributeRefs()const{
        return m_meshletAttributeRefs;
    }
    [[nodiscard]] const Core::Assets::AssetVector<MeshletLocalVertexRef>& meshletLocalVertexRefs()const{
        return m_meshletLocalVertexRefs;
    }
    [[nodiscard]] const Core::Assets::AssetVector<u8>& meshletPrimitiveIndices()const{ return m_meshletPrimitiveIndices; }
    [[nodiscard]] u32 meshClass()const{ return Core::Mesh::MeshClass::Static; }


private:
    Core::Assets::AssetVector<Float3U> m_positionStream;
    Core::Assets::AssetVector<Half4U> m_normalStream;
    Core::Assets::AssetVector<Half4U> m_tangentStream;
    Core::Assets::AssetVector<Float2U> m_uv0Stream;
    Core::Assets::AssetVector<Half4U> m_colorStream;
    Core::Assets::AssetVector<MeshletDesc> m_meshlets;
    Core::Assets::AssetVector<MeshletBounds> m_meshletBounds;
    Core::Assets::AssetVector<MeshletDeformedPositionRef> m_meshletPositionRefs;
    Core::Assets::AssetVector<MeshletShadingAttributeRef> m_meshletAttributeRefs;
    Core::Assets::AssetVector<MeshletLocalVertexRef> m_meshletLocalVertexRefs;
    Core::Assets::AssetVector<u8> m_meshletPrimitiveIndices;
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

