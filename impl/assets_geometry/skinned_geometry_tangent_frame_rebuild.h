// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#pragma once


#include "skinned_geometry_validation.h"

#include <core/alloc/scratch.h>
#include <core/geometry/tangent_frame_rebuild.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace SkinnedGeometryTangentFrameRebuild{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


template<typename RebuildAllocator>
inline void BuildInput(
    const Vector<SkinnedGeometryVertex>& vertices,
    Vector<Core::Geometry::TangentFrameRebuildVertex, RebuildAllocator>& outRebuildVertices){
    outRebuildVertices.clear();
    outRebuildVertices.reserve(vertices.size());
    for(usize vertexIndex = 0u; vertexIndex < vertices.size(); ++vertexIndex){
        const SkinnedGeometryVertex& vertex = vertices[vertexIndex];
        Float4 position;
        Float4 normal;
        Float4 tangent;
        Float2U uv0;
        StoreFloat(LoadSkinnedGeometryVertexPosition(vertex), &position);
        StoreFloat(LoadSkinnedGeometryVertexNormal(vertex), &normal);
        StoreFloat(LoadSkinnedGeometryVertexTangent(vertex), &tangent);
        StoreFloat(LoadSkinnedGeometryVertexUv0(vertex), &uv0);
        outRebuildVertices.push_back(Core::Geometry::TangentFrameRebuildVertex{
            position,
            normal,
            tangent,
            uv0,
        });
    }
}

template<typename RebuildAllocator>
[[nodiscard]] inline bool ValidOutput(
    const Vector<SkinnedGeometryVertex>& vertices,
    const Vector<Core::Geometry::TangentFrameRebuildVertex, RebuildAllocator>& rebuildVertices){
    if(rebuildVertices.size() != vertices.size())
        return false;

    for(usize vertexIndex = 0u; vertexIndex < vertices.size(); ++vertexIndex){
        SkinnedGeometryVertex rebuiltVertex = vertices[vertexIndex];
        StoreSkinnedGeometryVertexNormal(rebuiltVertex, Float3U(rebuildVertices[vertexIndex].normal.raw));
        StoreSkinnedGeometryVertexTangent(rebuiltVertex, Float4U(rebuildVertices[vertexIndex].tangent.raw));
        if(SkinnedGeometryValidation::FindRestVertexPayloadFailure(rebuiltVertex) != SkinnedGeometryValidation::RestVertexPayloadFailure::None)
            return false;
    }
    return true;
}

template<typename RebuildAllocator>
inline void ApplyOutput(
    Vector<SkinnedGeometryVertex>& vertices,
    const Vector<Core::Geometry::TangentFrameRebuildVertex, RebuildAllocator>& rebuildVertices){
    for(usize vertexIndex = 0u; vertexIndex < vertices.size(); ++vertexIndex){
        SkinnedGeometryVertex& vertex = vertices[vertexIndex];
        StoreSkinnedGeometryVertexNormal(vertex, Float3U(rebuildVertices[vertexIndex].normal.raw));
        StoreSkinnedGeometryVertexTangent(vertex, Float4U(rebuildVertices[vertexIndex].tangent.raw));
    }
}

[[nodiscard]] inline bool Rebuild(
    Vector<SkinnedGeometryVertex>& vertices,
    const Vector<u32>& indices,
    Core::Geometry::TangentFrameRebuildResult* outResult = nullptr){
    Core::Alloc::ScratchArena<> scratchArena;
    using RebuildVertex = Core::Geometry::TangentFrameRebuildVertex;
    using RebuildAllocator = Core::Alloc::ScratchAllocator<RebuildVertex>;
    Vector<RebuildVertex, RebuildAllocator> rebuildVertices{ RebuildAllocator(scratchArena) };
    BuildInput(vertices, rebuildVertices);

    if(!Core::Geometry::RebuildTangentFrames(rebuildVertices, indices, outResult))
        return false;

    if(!ValidOutput(vertices, rebuildVertices))
        return false;

    ApplyOutput(vertices, rebuildVertices);
    return true;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

