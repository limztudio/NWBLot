// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#pragma once


#include "geometry_payload_types.h"
#include "skinned_geometry_types.h"

#include <core/assets/asset.h>
#include <core/geometry/geometry_class.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


class SkinnedGeometry final : public Core::Assets::TypedAsset<SkinnedGeometry>{
public:
    NWB_DEFINE_ASSET_TYPE("skinned_geometry")


public:
    explicit SkinnedGeometry(Core::Assets::AssetArena& arena)
        : m_positionStream(arena)
        , m_normalStream(arena)
        , m_tangentStream(arena)
        , m_uv0Stream(arena)
        , m_colorStream(arena)
        , m_skin(arena)
        , m_inverseBindMatrices(arena)
        , m_vertexRefs(arena)
        , m_meshlets(arena)
        , m_meshletBounds(arena)
        , m_meshletVertexRefs(arena)
        , m_meshletPrimitiveIndices(arena)
    {}
    SkinnedGeometry(Core::Assets::AssetArena& arena, const Name& virtualPath)
        : Core::Assets::TypedAsset<SkinnedGeometry>(virtualPath)
        , m_positionStream(arena)
        , m_normalStream(arena)
        , m_tangentStream(arena)
        , m_uv0Stream(arena)
        , m_colorStream(arena)
        , m_skin(arena)
        , m_inverseBindMatrices(arena)
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
    void setGeometryClass(u32 geometryClass){ m_geometryClass = geometryClass; }
    void setSkeletonJointCount(u32 jointCount){ m_skeletonJointCount = jointCount; }
    void setPayload(
        Core::Assets::AssetVector<Float3U>&& positions,
        Core::Assets::AssetVector<Half4U>&& normals,
        Core::Assets::AssetVector<Half4U>&& tangents,
        Core::Assets::AssetVector<Float2U>&& uv0,
        Core::Assets::AssetVector<Half4U>&& colors,
        Core::Assets::AssetVector<SkinInfluence4>&& skin,
        Core::Assets::AssetVector<SkinnedGeometryJointMatrix>&& inverseBindMatrices,
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
        m_skin = Move(skin);
        m_inverseBindMatrices = Move(inverseBindMatrices);
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
    [[nodiscard]] u32 geometryClass()const{ return m_geometryClass; }
    [[nodiscard]] const Core::Assets::AssetVector<SkinInfluence4>& skinStream()const{ return m_skin; }
    [[nodiscard]] u32 skeletonJointCount()const{ return m_skeletonJointCount; }
    [[nodiscard]] const Core::Assets::AssetVector<SkinnedGeometryJointMatrix>& inverseBindMatrices()const{ return m_inverseBindMatrices; }
private:
    u32 m_geometryClass = Core::Geometry::GeometryClass::Invalid;
    Core::Assets::AssetVector<Float3U> m_positionStream;
    Core::Assets::AssetVector<Half4U> m_normalStream;
    Core::Assets::AssetVector<Half4U> m_tangentStream;
    Core::Assets::AssetVector<Float2U> m_uv0Stream;
    Core::Assets::AssetVector<Half4U> m_colorStream;
    Core::Assets::AssetVector<SkinInfluence4> m_skin;
    u32 m_skeletonJointCount = 0;
    Core::Assets::AssetVector<SkinnedGeometryJointMatrix> m_inverseBindMatrices;
    Core::Assets::AssetVector<GeometryVertexRef> m_vertexRefs;
    Core::Assets::AssetVector<GeometryMeshletDesc> m_meshlets;
    Core::Assets::AssetVector<GeometryMeshletBounds> m_meshletBounds;
    Core::Assets::AssetVector<u32> m_meshletVertexRefs;
    Core::Assets::AssetVector<u8> m_meshletPrimitiveIndices;
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
