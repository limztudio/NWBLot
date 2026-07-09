// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#pragma once


#include "../global.h"
#include "binary_payload_io.h"
#include "payload_types.h"

#include <global/core/assets/module.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


class MeshGeometryPayload{
public:
    [[nodiscard]] const Core::Assets::AssetVector<Float3U>& positionStream()const{ return m_positionStream; }
    [[nodiscard]] const Core::Assets::AssetVector<Half4U>& normalStream()const{ return m_normalStream; }
    [[nodiscard]] const Core::Assets::AssetVector<Half4U>& tangentStream()const{ return m_tangentStream; }
    [[nodiscard]] const Core::Assets::AssetVector<Float2U>& uv0Stream()const{ return m_uv0Stream; }
    [[nodiscard]] const Core::Assets::AssetVector<Half4U>& colorStream()const{ return m_colorStream; }
    [[nodiscard]] const Core::Assets::AssetVector<MeshletDesc>& meshlets()const{ return m_meshlets; }
    [[nodiscard]] const Core::Assets::AssetVector<MeshletBounds>& meshletBounds()const{ return m_meshletBounds; }
    [[nodiscard]] const Core::Assets::AssetVector<u8>& meshletPositionRefDeltas()const{
        return m_meshletPositionRefDeltas;
    }
    [[nodiscard]] const Core::Assets::AssetVector<u8>& meshletAttributeRefDeltas()const{
        return m_meshletAttributeRefDeltas;
    }
    [[nodiscard]] const Core::Assets::AssetVector<MeshletLocalVertexRef>& meshletLocalVertexRefs()const{
        return m_meshletLocalVertexRefs;
    }
    [[nodiscard]] const Core::Assets::AssetVector<u8>& meshletPrimitiveIndices()const{ return m_meshletPrimitiveIndices; }


protected:
    explicit MeshGeometryPayload(Core::Assets::AssetArena& arena)
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
    {}

    void setGeometryPayload(
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
        m_positionStream = Move(positions);
        m_normalStream = Move(normals);
        m_tangentStream = Move(tangents);
        m_uv0Stream = Move(uv0);
        m_colorStream = Move(colors);
        m_meshlets = Move(meshlets);
        m_meshletBounds = Move(meshletBounds);
        m_meshletPositionRefDeltas = Move(meshletPositionRefDeltas);
        m_meshletAttributeRefDeltas = Move(meshletAttributeRefDeltas);
        m_meshletLocalVertexRefs = Move(meshletLocalVertexRefs);
        m_meshletPrimitiveIndices = Move(meshletPrimitiveIndices);
    }

    [[nodiscard]] bool hasIncompleteGeometryPayload()const{
        return m_positionStream.empty()
            || m_normalStream.empty()
            || m_tangentStream.empty()
            || m_uv0Stream.empty()
            || m_colorStream.empty()
            || m_meshlets.empty()
            || m_meshletBounds.empty()
            || m_meshletPositionRefDeltas.empty()
            || m_meshletAttributeRefDeltas.empty()
            || m_meshletLocalVertexRefs.empty()
            || m_meshletPrimitiveIndices.empty()
        ;
    }

    template<typename HeaderT>
    [[nodiscard]] bool readGeometryAttributeStreams(
        const Core::Assets::AssetBytes& binary,
        usize& inOutCursor,
        const HeaderT& header,
        const tchar* failureContext
    ){
        return MeshAssetBinaryPayload::ReadMeshAttributeStreams(
            binary,
            inOutCursor,
            header,
            m_positionStream,
            m_normalStream,
            m_tangentStream,
            m_uv0Stream,
            m_colorStream,
            failureContext
        );
    }

    template<typename HeaderT>
    [[nodiscard]] bool readGeometryMeshletStreams(
        const Core::Assets::AssetBytes& binary,
        usize& inOutCursor,
        const HeaderT& header,
        const tchar* failureContext
    ){
        return MeshAssetBinaryPayload::ReadMeshletStreams(
            binary,
            inOutCursor,
            header,
            m_meshlets,
            m_meshletBounds,
            m_meshletPositionRefDeltas,
            m_meshletAttributeRefDeltas,
            m_meshletLocalVertexRefs,
            m_meshletPrimitiveIndices,
            failureContext
        );
    }

    void clearGeometryPayload(){
        m_positionStream.clear();
        m_normalStream.clear();
        m_tangentStream.clear();
        m_uv0Stream.clear();
        m_colorStream.clear();
        m_meshlets.clear();
        m_meshletBounds.clear();
        m_meshletPositionRefDeltas.clear();
        m_meshletAttributeRefDeltas.clear();
        m_meshletLocalVertexRefs.clear();
        m_meshletPrimitiveIndices.clear();
    }


protected:
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
};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

