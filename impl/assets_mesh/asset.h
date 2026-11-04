// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#pragma once


#include "../global.h"
#include <impl/assets_mesh/payload_validation.adhd.h>

#include <core/alloc/scratch.h>
#include <core/assets/module.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


class Mesh final : public Core::Assets::TypedAsset<Mesh>{
public:
    NWB_DEFINE_ASSET_TYPE("mesh")


public:
    explicit Mesh(Core::Assets::AssetArena& arena)noexcept
        : m_positionStream(arena)
        , m_normalStream(arena)
        , m_tangentStream(arena)
        , m_uv0Stream(arena)
        , m_colorStream(arena)
        , m_meshlets(arena)
        , m_meshletBounds(arena)
        , m_meshletPositionRefDeltas(arena)
        , m_meshletAttributeRefDeltas(arena)
        , m_meshletLocalVertexRefs(arena)
        , m_meshletPrimitiveIndices(arena)
        , m_solidTriangleWords(arena)
    {}
    Mesh(Core::Assets::AssetArena& arena, const Name& virtualPath)noexcept
        : Core::Assets::TypedAsset<Mesh>(virtualPath)
        , m_positionStream(arena)
        , m_normalStream(arena)
        , m_tangentStream(arena)
        , m_uv0Stream(arena)
        , m_colorStream(arena)
        , m_meshlets(arena)
        , m_meshletBounds(arena)
        , m_meshletPositionRefDeltas(arena)
        , m_meshletAttributeRefDeltas(arena)
        , m_meshletLocalVertexRefs(arena)
        , m_meshletPrimitiveIndices(arena)
        , m_solidTriangleWords(arena)
    {}


public:
    b8 loadBinary(Span<const u8> binary);
    [[nodiscard]] b8 validatePayload(Core::Alloc::ScratchArena& scratchArena)const;

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
        Core::Assets::AssetVector<u8>&& meshletPrimitiveIndices,
        Core::Assets::AssetVector<u32>&& solidTriangleWords
    )noexcept;


public:
    [[nodiscard]] const Core::Assets::AssetVector<Float3U>& positionStream()const noexcept{ return m_positionStream; }

    [[nodiscard]] const Core::Assets::AssetVector<Half4U>& normalStream()const noexcept{ return m_normalStream; }

    [[nodiscard]] const Core::Assets::AssetVector<Half4U>& tangentStream()const noexcept{ return m_tangentStream; }

    [[nodiscard]] const Core::Assets::AssetVector<Float2U>& uv0Stream()const noexcept{ return m_uv0Stream; }

    [[nodiscard]] const Core::Assets::AssetVector<Half4U>& colorStream()const noexcept{ return m_colorStream; }

    [[nodiscard]] const Core::Assets::AssetVector<MeshletDesc>& meshlets()const noexcept{ return m_meshlets; }

    [[nodiscard]] const Core::Assets::AssetVector<MeshletBounds>& meshletBounds()const noexcept{ return m_meshletBounds; }

    [[nodiscard]] const Core::Assets::AssetVector<u8>& meshletPositionRefDeltas()const noexcept{ return m_meshletPositionRefDeltas; }

    [[nodiscard]] const Core::Assets::AssetVector<u8>& meshletAttributeRefDeltas()const noexcept{ return m_meshletAttributeRefDeltas; }

    [[nodiscard]] const Core::Assets::AssetVector<MeshletLocalVertexRef>& meshletLocalVertexRefs()const noexcept{ return m_meshletLocalVertexRefs; }

    [[nodiscard]] const Core::Assets::AssetVector<u8>& meshletPrimitiveIndices()const noexcept{ return m_meshletPrimitiveIndices; }

    [[nodiscard]] const Core::Assets::AssetVector<u32>& solidTriangleWords()const noexcept{ return m_solidTriangleWords; }


private:
    [[nodiscard]] b8 hasIncompleteGeometryPayload()const noexcept;


private:
    Core::Assets::AssetVector<Float3U> m_positionStream;
    Core::Assets::AssetVector<Half4U> m_normalStream;
    Core::Assets::AssetVector<Half4U> m_tangentStream;
    Core::Assets::AssetVector<Float2U> m_uv0Stream;
    Core::Assets::AssetVector<Half4U> m_colorStream;
    Core::Assets::AssetVector<MeshletDesc> m_meshlets;
    Core::Assets::AssetVector<MeshletBounds> m_meshletBounds;
    Core::Assets::AssetVector<u8> m_meshletPositionRefDeltas;
    Core::Assets::AssetVector<u8> m_meshletAttributeRefDeltas;
    Core::Assets::AssetVector<MeshletLocalVertexRef> m_meshletLocalVertexRefs;
    Core::Assets::AssetVector<u8> m_meshletPrimitiveIndices;
    Core::Assets::AssetVector<u32> m_solidTriangleWords;
};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_DEFINE_ASSET_CODEC(MeshAssetCodec, Mesh);


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
