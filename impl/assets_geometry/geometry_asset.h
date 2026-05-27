// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#pragma once


#include "../global.h"
#include "geometry_payload_types.h"

#include <core/assets/asset.h>
#include <core/geometry/geometry_class.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


[[nodiscard]] inline Half4U MakeGeometryNormalStreamValue(const Float3U& normal)noexcept{
    return MakeHalf4U(normal.x, normal.y, normal.z, 0.0f);
}

[[nodiscard]] inline Half4U MakeGeometryColorStreamValue(const Float4U& color0)noexcept{
    return MakeHalf4U(color0.x, color0.y, color0.z, color0.w);
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


class Geometry final : public Core::Assets::TypedAsset<Geometry>{
public:
    NWB_DEFINE_ASSET_TYPE("geometry")


public:
    explicit Geometry(Core::Assets::AssetArena& arena)
        : m_positionStream(arena)
        , m_normalStream(arena)
        , m_tangentStream(arena)
        , m_uv0Stream(arena)
        , m_colorStream(arena)
        , m_vertexRefs(arena)
        , m_meshlets(arena)
        , m_meshletBounds(arena)
        , m_meshletVertexRefs(arena)
        , m_meshletPrimitiveIndices(arena)
    {}
    Geometry(Core::Assets::AssetArena& arena, const Name& virtualPath)
        : Core::Assets::TypedAsset<Geometry>(virtualPath)
        , m_positionStream(arena)
        , m_normalStream(arena)
        , m_tangentStream(arena)
        , m_uv0Stream(arena)
        , m_colorStream(arena)
        , m_vertexRefs(arena)
        , m_meshlets(arena)
        , m_meshletBounds(arena)
        , m_meshletVertexRefs(arena)
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
        Core::Assets::AssetVector<GeometryVertexRef>&& vertexRefs,
        Core::Assets::AssetVector<GeometryMeshletDesc>&& meshlets,
        Core::Assets::AssetVector<GeometryMeshletBounds>&& meshletBounds,
        Core::Assets::AssetVector<u32>&& meshletVertexRefs,
        Core::Assets::AssetVector<u8>&& meshletPrimitiveIndices
    ){
        m_positionStream = Move(positions);
        m_normalStream = Move(normals);
        m_tangentStream = Move(tangents);
        m_uv0Stream = Move(uv0);
        m_colorStream = Move(colors);
        m_vertexRefs = Move(vertexRefs);
        m_meshlets = Move(meshlets);
        m_meshletBounds = Move(meshletBounds);
        m_meshletVertexRefs = Move(meshletVertexRefs);
        m_meshletPrimitiveIndices = Move(meshletPrimitiveIndices);
    }
    [[nodiscard]] const Core::Assets::AssetVector<Float3U>& positionStream()const{ return m_positionStream; }
    [[nodiscard]] const Core::Assets::AssetVector<Half4U>& normalStream()const{ return m_normalStream; }
    [[nodiscard]] const Core::Assets::AssetVector<Half4U>& tangentStream()const{ return m_tangentStream; }
    [[nodiscard]] const Core::Assets::AssetVector<Float2U>& uv0Stream()const{ return m_uv0Stream; }
    [[nodiscard]] const Core::Assets::AssetVector<Half4U>& colorStream()const{ return m_colorStream; }
    [[nodiscard]] const Core::Assets::AssetVector<GeometryVertexRef>& vertexRefs()const{ return m_vertexRefs; }
    [[nodiscard]] const Core::Assets::AssetVector<GeometryMeshletDesc>& meshlets()const{ return m_meshlets; }
    [[nodiscard]] const Core::Assets::AssetVector<GeometryMeshletBounds>& meshletBounds()const{ return m_meshletBounds; }
    [[nodiscard]] const Core::Assets::AssetVector<u32>& meshletVertexRefs()const{ return m_meshletVertexRefs; }
    [[nodiscard]] const Core::Assets::AssetVector<u8>& meshletPrimitiveIndices()const{ return m_meshletPrimitiveIndices; }
    [[nodiscard]] usize vertexCount()const{ return m_vertexRefs.size(); }
    [[nodiscard]] u32 geometryClass()const{ return Core::Geometry::GeometryClass::Static; }


private:
    Core::Assets::AssetVector<Float3U> m_positionStream;
    Core::Assets::AssetVector<Half4U> m_normalStream;
    Core::Assets::AssetVector<Half4U> m_tangentStream;
    Core::Assets::AssetVector<Float2U> m_uv0Stream;
    Core::Assets::AssetVector<Half4U> m_colorStream;
    Core::Assets::AssetVector<GeometryVertexRef> m_vertexRefs;
    Core::Assets::AssetVector<GeometryMeshletDesc> m_meshlets;
    Core::Assets::AssetVector<GeometryMeshletBounds> m_meshletBounds;
    Core::Assets::AssetVector<u32> m_meshletVertexRefs;
    Core::Assets::AssetVector<u8> m_meshletPrimitiveIndices;
};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


class GeometryAssetCodec final : public Core::Assets::AssetCodec<Geometry>{
public:
    GeometryAssetCodec() = default;


#if defined(NWB_COOK)
public:
    virtual bool serialize(const Core::Assets::IAsset& asset, Core::Assets::AssetBytes& outBinary)const override;
#endif
};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
