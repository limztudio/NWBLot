// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "surface_patch_edit.h"

#include "frame_math.h"

#include <core/alloc/scratch.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_GEOMETRY_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace __hidden_geometry_surface_patch_edit{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


[[nodiscard]] bool NormalizeFrameNormal(const SIMDVector inputNormal, SIMDVector& outNormal){
    if(!FrameValidDirection(inputNormal))
        return false;

    outNormal = FrameNormalizeDirection(
        inputNormal,
        VectorSet(0.0f, 0.0f, 1.0f, 0.0f)
    );
    return FrameValidDirection(outNormal);
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


template<typename EdgeAllocator, typename PositionAllocator>
bool BuildSurfacePatchLoopDistancesWithNormalImpl(
    const Vector<MeshTopologyEdge, EdgeAllocator>& orderedBoundaryEdges,
    const Vector<Float3U, PositionAllocator>& positions,
    const SIMDVector normal,
    f32* outLoopDistances,
    const usize loopDistanceCount,
    f32& outLoopLength
){
    outLoopLength = 0.0f;
    if(
        !outLoopDistances
        || orderedBoundaryEdges.size() < 3u
        || orderedBoundaryEdges.size() != loopDistanceCount
        || positions.empty()
    )
        return false;

    if(!FrameValidDirection(normal))
        return false;

    const usize boundaryVertexCount = orderedBoundaryEdges.size();
    for(usize edgeIndex = 0u; edgeIndex < boundaryVertexCount; ++edgeIndex){
        const usize nextEdgeIndex = (edgeIndex + 1u) % boundaryVertexCount;
        const MeshTopologyEdge& edge = orderedBoundaryEdges[edgeIndex];
        if(!ValidMeshTopologyEdge(edge, positions.size()) || edge.b != orderedBoundaryEdges[nextEdgeIndex].a)
            return false;

        outLoopDistances[edgeIndex] = outLoopLength;
        const SIMDVector edgeDelta = FrameProjectOntoPlane(
            VectorSubtract(LoadFloat(positions[edge.b]), LoadFloat(positions[edge.a])),
            normal
        );
        const f32 edgeLength = VectorGetX(Vector3Length(edgeDelta));
        if(!IsFinite(edgeLength) || edgeLength <= s_FrameDirectionEpsilon)
            return false;

        outLoopLength += edgeLength;
        if(!IsFinite(outLoopLength))
            return false;
    }

    return outLoopLength > s_FrameDirectionEpsilon;
}

template<typename EdgeAllocator, typename PositionAllocator>
bool BuildSurfacePatchLoopDistancesImpl(
    const Vector<MeshTopologyEdge, EdgeAllocator>& orderedBoundaryEdges,
    const Vector<Float3U, PositionAllocator>& positions,
    const Float3U& frameNormal,
    f32* outLoopDistances,
    const usize loopDistanceCount,
    f32& outLoopLength
){
    outLoopLength = 0.0f;
    SIMDVector normal;
    if(!NormalizeFrameNormal(LoadFloat(frameNormal), normal))
        return false;

    return BuildSurfacePatchLoopDistancesWithNormalImpl(
        orderedBoundaryEdges,
        positions,
        normal,
        outLoopDistances,
        loopDistanceCount,
        outLoopLength
    );
}

template<typename EdgeAllocator>
bool BuildSurfacePatchRingEdgesImpl(
    const u32* ringVertices,
    const usize ringVertexCount,
    Vector<MeshTopologyEdge, EdgeAllocator>& outEdges
){
    outEdges.clear();
    if(!ringVertices || ringVertexCount < 3u || ringVertexCount > static_cast<usize>(Limit<u32>::s_Max))
        return false;

    for(usize vertexIndex = 0u; vertexIndex < ringVertexCount; ++vertexIndex){
        const usize nextVertexIndex = (vertexIndex + 1u) % ringVertexCount;
        if(ringVertices[vertexIndex] == ringVertices[nextVertexIndex])
            return false;
    }

    outEdges.reserve(ringVertexCount);
    for(usize vertexIndex = 0u; vertexIndex < ringVertexCount; ++vertexIndex){
        const usize nextVertexIndex = (vertexIndex + 1u) % ringVertexCount;
        MeshTopologyEdge edge;
        edge.a = ringVertices[vertexIndex];
        edge.b = ringVertices[nextVertexIndex];
        edge.fullCount = 2u;
        edge.removedCount = 1u;
        outEdges.push_back(edge);
    }
    return true;
}

template<typename EdgeAllocator, typename PositionAllocator>
bool BuildSurfacePatchWallVerticesImpl(
    const Vector<MeshTopologyEdge, EdgeAllocator>& orderedBoundaryEdges,
    const Vector<Float3U, PositionAllocator>& positions,
    const MeshTopologyBoundaryLoopFrame& frame,
    const SIMDVector frameNormal,
    const f32 depth,
    const usize wallBandCount,
    SurfacePatchWallVertex* outVertices,
    const usize outVertexCount
){
    if(
        orderedBoundaryEdges.size() < 3u
        || positions.empty()
        || !IsFinite(depth)
        || depth <= s_FrameDirectionEpsilon
        || wallBandCount == 0u
        || orderedBoundaryEdges.size() > Limit<usize>::s_Max / wallBandCount
        || !outVertices
    )
        return false;

    SIMDVector normal;
    if(!NormalizeFrameNormal(frameNormal, normal))
        return false;

    const usize boundaryVertexCount = orderedBoundaryEdges.size();
    const usize totalWallVertexCount = boundaryVertexCount * wallBandCount;
    if(
        totalWallVertexCount > static_cast<usize>(Limit<u32>::s_Max)
        || outVertexCount != totalWallVertexCount
    )
        return false;

    Core::Alloc::ScratchArena<> scratchArena;
    Vector<f32, Core::Alloc::ScratchAllocator<f32>> loopDistances{
        Core::Alloc::ScratchAllocator<f32>(scratchArena)
    };
    loopDistances.resize(boundaryVertexCount, 0.0f);

    f32 loopLength = 0.0f;
    if(
        !BuildSurfacePatchLoopDistancesWithNormalImpl(
            orderedBoundaryEdges,
            positions,
            normal,
            loopDistances.data(),
            loopDistances.size(),
            loopLength
        )
    )
        return false;

    const bool cacheLoopVertexFrames = wallBandCount > 1u;
    Vector<MeshTopologyLoopVertexFrame, Core::Alloc::ScratchAllocator<MeshTopologyLoopVertexFrame>> cachedLoopVertexFrames{
        Core::Alloc::ScratchAllocator<MeshTopologyLoopVertexFrame>(scratchArena)
    };
    if(cacheLoopVertexFrames){
        cachedLoopVertexFrames.reserve(boundaryVertexCount);
        for(usize edgeIndex = 0u; edgeIndex < boundaryVertexCount; ++edgeIndex){
            const usize previousEdgeIndex = edgeIndex == 0u ? boundaryVertexCount - 1u : edgeIndex - 1u;
            MeshTopologyLoopVertexFrame loopVertexFrame;
            if(
                !BuildBoundaryLoopVertexFrame(
                    positions,
                    frame,
                    orderedBoundaryEdges[previousEdgeIndex],
                    orderedBoundaryEdges[edgeIndex],
                    loopVertexFrame
                )
            )
                return false;
            cachedLoopVertexFrames.push_back(loopVertexFrame);
        }
    }

    for(usize ringIndex = 0u; ringIndex < wallBandCount; ++ringIndex){
        const f32 wallV = static_cast<f32>(ringIndex + 1u) / static_cast<f32>(wallBandCount);
        const usize vertexBase = ringIndex * boundaryVertexCount;
        for(usize edgeIndex = 0u; edgeIndex < boundaryVertexCount; ++edgeIndex){
            const usize previousEdgeIndex = edgeIndex == 0u ? boundaryVertexCount - 1u : edgeIndex - 1u;
            const MeshTopologyEdge& edge = orderedBoundaryEdges[edgeIndex];
            MeshTopologyLoopVertexFrame singleLoopVertexFrame{};
            if(
                !cacheLoopVertexFrames
                && !BuildBoundaryLoopVertexFrame(
                    positions,
                    frame,
                    orderedBoundaryEdges[previousEdgeIndex],
                    edge,
                    singleLoopVertexFrame
                )
            )
                return false;
            const MeshTopologyLoopVertexFrame& vertexFrame = cacheLoopVertexFrames
                ? cachedLoopVertexFrames[edgeIndex]
                : singleLoopVertexFrame
            ;

            SurfacePatchWallVertex vertex;
            vertex.sourceVertex = edge.a;
            vertex.attributeVertices[0u] = orderedBoundaryEdges[previousEdgeIndex].a;
            vertex.attributeVertices[1u] = edge.a;
            vertex.attributeVertices[2u] = edge.b;
            StoreFloat(
                VectorMultiplyAdd(
                    normal,
                    VectorReplicate(-depth * wallV),
                    LoadFloat(positions[edge.a])
                ),
                &vertex.position
            );
            vertex.normal = vertexFrame.normal;
            vertex.tangent = vertexFrame.tangent;
            vertex.uv0 = Float2U(loopDistances[edgeIndex] / loopLength, wallV);
            outVertices[vertexBase + edgeIndex] = vertex;
        }
    }

    return true;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


bool BuildSurfacePatchLoopDistances(
    const Vector<MeshTopologyEdge>& orderedBoundaryEdges,
    const Vector<Float3U>& positions,
    const Float3U& frameNormal,
    f32* outLoopDistances,
    const usize loopDistanceCount,
    f32& outLoopLength
){
    return __hidden_geometry_surface_patch_edit::BuildSurfacePatchLoopDistancesImpl(
        orderedBoundaryEdges,
        positions,
        frameNormal,
        outLoopDistances,
        loopDistanceCount,
        outLoopLength
    );
}

bool BuildSurfacePatchRingEdges(
    const u32* ringVertices,
    const usize ringVertexCount,
    Vector<MeshTopologyEdge>& outEdges
){
    return __hidden_geometry_surface_patch_edit::BuildSurfacePatchRingEdgesImpl(ringVertices, ringVertexCount, outEdges);
}

bool BuildSurfacePatchRingEdges(
    const u32* ringVertices,
    const usize ringVertexCount,
    Vector<MeshTopologyEdge, Core::Alloc::ScratchAllocator<MeshTopologyEdge>>& outEdges
){
    return __hidden_geometry_surface_patch_edit::BuildSurfacePatchRingEdgesImpl(ringVertices, ringVertexCount, outEdges);
}

bool BuildSurfacePatchWallVertices(
    const Vector<MeshTopologyEdge>& orderedBoundaryEdges,
    const Vector<Float3U>& positions,
    const MeshTopologyBoundaryLoopFrame& frame,
    const Float3U& frameNormal,
    const f32 depth,
    const usize wallBandCount,
    SurfacePatchWallVertex* outVertices,
    const usize outVertexCount
){
    return __hidden_geometry_surface_patch_edit::BuildSurfacePatchWallVerticesImpl(
        orderedBoundaryEdges,
        positions,
        frame,
        LoadFloat(frameNormal),
        depth,
        wallBandCount,
        outVertices,
        outVertexCount
    );
}

bool BuildSurfacePatchWallVertices(
    const Vector<MeshTopologyEdge, Core::Alloc::ScratchAllocator<MeshTopologyEdge>>& orderedBoundaryEdges,
    const Vector<Float3U, Core::Alloc::ScratchAllocator<Float3U>>& positions,
    const MeshTopologyBoundaryLoopFrame& frame,
    const SIMDVector frameNormal,
    const f32 depth,
    const usize wallBandCount,
    SurfacePatchWallVertex* outVertices,
    const usize outVertexCount
){
    return __hidden_geometry_surface_patch_edit::BuildSurfacePatchWallVerticesImpl(
        orderedBoundaryEdges,
        positions,
        frame,
        frameNormal,
        depth,
        wallBandCount,
        outVertices,
        outVertexCount
    );
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_GEOMETRY_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

