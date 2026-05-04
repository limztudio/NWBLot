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


static constexpr f32 s_CapTriangulationAreaEpsilon = 0.000001f;

struct CapPolygonVertex{
    u32 vertex = 0u;
    f32 x = 0.0f;
    f32 y = 0.0f;
};


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

[[nodiscard]] f32 Cross2D(
    const CapPolygonVertex& a,
    const CapPolygonVertex& b,
    const CapPolygonVertex& c
){
    return ((b.x - a.x) * (c.y - a.y)) - ((b.y - a.y) * (c.x - a.x));
}

[[nodiscard]] f32 DistanceSq2D(const CapPolygonVertex& a, const CapPolygonVertex& b){
    const f32 dx = b.x - a.x;
    const f32 dy = b.y - a.y;
    return (dx * dx) + (dy * dy);
}

template<typename PolygonAllocator>
[[nodiscard]] f32 SignedArea2D(const Vector<CapPolygonVertex, PolygonAllocator>& polygon){
    if(polygon.size() < 3u)
        return 0.0f;

    f32 area = 0.0f;
    const usize vertexCount = polygon.size();
    for(usize vertexIndex = 1u; vertexIndex < vertexCount; ++vertexIndex){
        const usize previousVertexIndex = vertexIndex - 1u;
        area +=
            (polygon[previousVertexIndex].x * polygon[vertexIndex].y)
            - (polygon[previousVertexIndex].y * polygon[vertexIndex].x)
        ;
    }
    area +=
        (polygon[vertexCount - 1u].x * polygon[0u].y)
        - (polygon[vertexCount - 1u].y * polygon[0u].x)
    ;
    return area * 0.5f;
}

[[nodiscard]] bool PointInsideTriangle2D(
    const CapPolygonVertex& point,
    const CapPolygonVertex& a,
    const CapPolygonVertex& b,
    const CapPolygonVertex& c,
    const bool counterClockwise
){
    constexpr f32 epsilon = 0.0000001f;
    const f32 ab = Cross2D(a, b, point);
    const f32 bc = Cross2D(b, c, point);
    const f32 ca = Cross2D(c, a, point);
    return
        counterClockwise
            ? ab >= -epsilon && bc >= -epsilon && ca >= -epsilon
            : ab <= epsilon && bc <= epsilon && ca <= epsilon
    ;
}

template<typename PolygonAllocator>
[[nodiscard]] bool RemoveDuplicateCapVertices(Vector<CapPolygonVertex, PolygonAllocator>& polygon){
    constexpr f32 distanceEpsilonSq = 0.000000000001f;
    bool removed = true;
    while(removed && polygon.size() >= 3u){
        removed = false;
        for(usize vertexIndex = 0u; vertexIndex < polygon.size(); ++vertexIndex){
            const usize nextVertexIndex = (vertexIndex + 1u) % polygon.size();
            const CapPolygonVertex& current = polygon[vertexIndex];
            const CapPolygonVertex& next = polygon[nextVertexIndex];
            if(DistanceSq2D(current, next) <= distanceEpsilonSq){
                polygon.erase(polygon.begin() + static_cast<ptrdiff_t>(vertexIndex));
                removed = true;
                break;
            }
        }
    }
    return polygon.size() >= 3u;
}

template<typename IndexAllocator>
[[nodiscard]] bool AppendCapTriangle(
    const CapPolygonVertex& a,
    const CapPolygonVertex& b,
    const CapPolygonVertex& c,
    const bool counterClockwise,
    Vector<u32, IndexAllocator>& outIndices
){
    if(a.vertex == b.vertex || a.vertex == c.vertex || b.vertex == c.vertex)
        return false;

    outIndices.push_back(a.vertex);
    outIndices.push_back(counterClockwise ? b.vertex : c.vertex);
    outIndices.push_back(counterClockwise ? c.vertex : b.vertex);
    return true;
}

template<typename PolygonAllocator>
[[nodiscard]] bool IsCapEar(
    const Vector<CapPolygonVertex, PolygonAllocator>& polygon,
    const usize vertexIndex,
    const bool counterClockwise
){
    const usize previousVertexIndex = vertexIndex == 0u ? polygon.size() - 1u : vertexIndex - 1u;
    const usize nextVertexIndex = (vertexIndex + 1u) % polygon.size();
    const CapPolygonVertex& previous = polygon[previousVertexIndex];
    const CapPolygonVertex& current = polygon[vertexIndex];
    const CapPolygonVertex& next = polygon[nextVertexIndex];
    const f32 cross = Cross2D(previous, current, next);
    if(
        counterClockwise
            ? cross <= s_CapTriangulationAreaEpsilon
            : cross >= -s_CapTriangulationAreaEpsilon
    )
        return false;

    for(usize testVertexIndex = 0u; testVertexIndex < polygon.size(); ++testVertexIndex){
        if(
            testVertexIndex == previousVertexIndex
            || testVertexIndex == vertexIndex
            || testVertexIndex == nextVertexIndex
        )
            continue;

        if(PointInsideTriangle2D(polygon[testVertexIndex], previous, current, next, counterClockwise))
            return false;
    }
    return true;
}

template<typename IndexAllocator>
[[nodiscard]] bool AppendSurfacePatchCapTrianglesImpl(
    const u32* capVertices,
    const usize capVertexCount,
    const Float3U* positions,
    const usize positionCount,
    const SIMDVector tangent,
    const SIMDVector bitangent,
    Vector<u32, IndexAllocator>& outIndices,
    u32* outAddedTriangleCount
){
    if(outAddedTriangleCount)
        *outAddedTriangleCount = 0u;
    if(
        !capVertices
        || !positions
        || capVertexCount < 3u
        || capVertexCount > static_cast<usize>(Limit<u32>::s_Max)
        || capVertexCount - 2u > static_cast<usize>(Limit<u32>::s_Max)
        || ((capVertexCount - 2u) * 3u) > Limit<usize>::s_Max - outIndices.size()
    )
        return false;

    Core::Alloc::ScratchArena<> scratchArena;
    Vector<CapPolygonVertex, Core::Alloc::ScratchAllocator<CapPolygonVertex>> polygon{
        Core::Alloc::ScratchAllocator<CapPolygonVertex>(scratchArena)
    };
    polygon.reserve(capVertexCount);
    if(capVertices[0u] >= positionCount)
        return false;

    const SIMDVector origin = LoadFloat(positions[capVertices[0u]]);
    for(usize capVertexIndex = 0u; capVertexIndex < capVertexCount; ++capVertexIndex){
        const u32 capVertex = capVertices[capVertexIndex];
        if(capVertex >= positionCount)
            return false;

        const SIMDVector offset = VectorSubtract(LoadFloat(positions[capVertex]), origin);
        CapPolygonVertex polygonVertex;
        polygonVertex.vertex = capVertex;
        polygonVertex.x = VectorGetX(Vector3Dot(offset, tangent));
        polygonVertex.y = VectorGetX(Vector3Dot(offset, bitangent));
        if(!IsFinite(polygonVertex.x) || !IsFinite(polygonVertex.y))
            return false;
        polygon.push_back(polygonVertex);
    }

    if(!RemoveDuplicateCapVertices(polygon))
        return false;

    const f32 signedArea = SignedArea2D(polygon);
    if(!IsFinite(signedArea) || Abs(signedArea) <= s_CapTriangulationAreaEpsilon)
        return false;

    const bool counterClockwise = signedArea > 0.0f;
    const usize capTriangleCount = polygon.size() - 2u;
    outIndices.reserve(outIndices.size() + capTriangleCount * 3u);

    u32 addedTriangleCount = 0u;
    while(polygon.size() > 3u){
        bool clippedEar = false;
        for(usize vertexIndex = 0u; vertexIndex < polygon.size(); ++vertexIndex){
            if(!IsCapEar(polygon, vertexIndex, counterClockwise))
                continue;

            const usize previousVertexIndex = vertexIndex == 0u ? polygon.size() - 1u : vertexIndex - 1u;
            const usize nextVertexIndex = (vertexIndex + 1u) % polygon.size();
            if(!AppendCapTriangle(
                polygon[previousVertexIndex],
                polygon[vertexIndex],
                polygon[nextVertexIndex],
                counterClockwise,
                outIndices
            ))
                return false;

            polygon.erase(polygon.begin() + static_cast<ptrdiff_t>(vertexIndex));
            addedTriangleCount += 1u;
            clippedEar = true;
            break;
        }

        if(!clippedEar)
            return false;
    }

    if(!AppendCapTriangle(polygon[0u], polygon[1u], polygon[2u], counterClockwise, outIndices))
        return false;
    addedTriangleCount += 1u;

    if(addedTriangleCount != capTriangleCount || addedTriangleCount > static_cast<u32>(Limit<u32>::s_Max))
        return false;

    if(outAddedTriangleCount)
        *outAddedTriangleCount = addedTriangleCount;
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
    Vector<f32, Core::Alloc::ScratchAllocator<f32>> loopDistances{ Core::Alloc::ScratchAllocator<f32>(scratchArena) };
    loopDistances.resize(boundaryVertexCount, 0.0f);

    f32 loopLength = 0.0f;
    if(!BuildSurfacePatchLoopDistancesWithNormalImpl(
        orderedBoundaryEdges,
        positions,
        normal,
        loopDistances.data(),
        loopDistances.size(),
        loopLength
    ))
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
            if(!BuildBoundaryLoopVertexFrame(
                positions,
                frame,
                orderedBoundaryEdges[previousEdgeIndex],
                orderedBoundaryEdges[edgeIndex],
                loopVertexFrame
            ))
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

bool AppendSurfacePatchCapTriangles(
    const u32* capVertices,
    const usize capVertexCount,
    const Float3U* positions,
    const usize positionCount,
    const Float3U& tangent,
    const Float3U& bitangent,
    Vector<u32>& outIndices,
    u32* outAddedTriangleCount
){
    return __hidden_geometry_surface_patch_edit::AppendSurfacePatchCapTrianglesImpl(
        capVertices,
        capVertexCount,
        positions,
        positionCount,
        LoadFloat(tangent),
        LoadFloat(bitangent),
        outIndices,
        outAddedTriangleCount
    );
}

bool AppendSurfacePatchCapTriangles(
    const u32* capVertices,
    const usize capVertexCount,
    const Float3U* positions,
    const usize positionCount,
    const SIMDVector tangent,
    const SIMDVector bitangent,
    Vector<u32, Core::Alloc::ScratchAllocator<u32>>& outIndices,
    u32* outAddedTriangleCount
){
    return __hidden_geometry_surface_patch_edit::AppendSurfacePatchCapTrianglesImpl(
        capVertices,
        capVertexCount,
        positions,
        positionCount,
        tangent,
        bitangent,
        outIndices,
        outAddedTriangleCount
    );
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

