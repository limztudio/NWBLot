// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#if defined(NWB_COOK)


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#pragma once


#include "cook_meshlet_triangles.h"


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


// Deduplicates a meshlet's source vertices into local deformed-position and attribute stream refs.
namespace MeshletZippedRefBuilder{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


template<
    typename PositionRefVectorT,
    typename AttributeRefVectorT,
    typename LocalVertexRefVectorT
>
[[nodiscard]] b8 BuildZippedMeshletRefs(
    const Path& nwbFilePath,
    TStringView metaKind,
    const MeshCookEntry& entry,
    Span<const u32> sourceVertexRefs,
    PositionRefVectorT& outPositionRefs,
    AttributeRefVectorT& outAttributeRefs,
    LocalVertexRefVectorT& outLocalVertexRefs
);


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


template<
    typename PositionRefVectorT,
    typename AttributeRefVectorT,
    typename LocalVertexRefVectorT
>
[[nodiscard]] b8 MeshletZippedRefBuilder::BuildZippedMeshletRefs(
    const Path& nwbFilePath,
    const TStringView metaKind,
    const MeshCookEntry& entry,
    const Span<const u32> sourceVertexRefs,
    PositionRefVectorT& outPositionRefs,
    AttributeRefVectorT& outAttributeRefs,
    LocalVertexRefVectorT& outLocalVertexRefs
){
    outPositionRefs.clear();
    outAttributeRefs.clear();
    outLocalVertexRefs.clear();
    outPositionRefs.reserve(sourceVertexRefs.size());
    outAttributeRefs.reserve(sourceVertexRefs.size());
    outLocalVertexRefs.reserve(sourceVertexRefs.size());

    for(const u32 vertexRefIndex : sourceVertexRefs){
        const MeshVertexRef& source = entry.vertexRefs[vertexRefIndex];
        const MeshletPositionStreamRef positionRef{ source.position, s_MeshMissingStreamIndex };
        const MeshletAttributeStreamRef attributeRef{ source.normal, source.tangent, source.uv0, source.color };

        u16 localPosition = 0u;
        if(const Expected<u16> foundPosition = TryFindMeshletPositionRef(outPositionRefs, positionRef))
            localPosition = *foundPosition;
        else{
            if(outPositionRefs.size() >= s_MeshMaxMeshletVertices){
                NWB_LOGGER_ERROR(NWB_TEXT("{} meta '{}': meshlet deformed positions exceed local index limits"), metaKind, PathToString<tchar>(nwbFilePath));
                return false;
            }
            localPosition = static_cast<u16>(outPositionRefs.size());
            outPositionRefs.push_back(positionRef);
        }

        u16 localAttribute = 0u;
        if(const Expected<u16> foundAttribute = TryFindMeshletAttributeRef(outAttributeRefs, attributeRef))
            localAttribute = *foundAttribute;
        else{
            if(outAttributeRefs.size() >= s_MeshMaxMeshletVertices){
                NWB_LOGGER_ERROR(NWB_TEXT("{} meta '{}': meshlet attributes exceed local index limits"), metaKind, PathToString<tchar>(nwbFilePath));
                return false;
            }
            localAttribute = static_cast<u16>(outAttributeRefs.size());
            outAttributeRefs.push_back(attributeRef);
        }

        outLocalVertexRefs.push_back(MeshletLocalVertexRef{ localPosition, localAttribute });
    }

    return true;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#endif


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

