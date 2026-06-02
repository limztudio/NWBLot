// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "csg_plane_cap_builder.h"


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace __hidden_csg_plane_cap_builder{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


inline constexpr f32 s_PlaneDistanceEpsilon = 0.00005f;
inline constexpr f32 s_PointMergeEpsilon = 0.0001f;
inline constexpr f32 s_PointMergeEpsilonSquared = s_PointMergeEpsilon * s_PointMergeEpsilon;
inline constexpr Float4 s_DefaultCapColor = Float4(0.72f, 0.76f, 0.80f, 1.0f);


struct PlaneCapPoint{
    Float4 position;
    f32 u = 0.0f;
    f32 v = 0.0f;
};

struct PlaneCapEdge{
    u32 a = 0u;
    u32 b = 0u;
};

struct PlaneCapIntersection{
    SIMDVector points[3];
    u32 count = 0u;
};

using PlaneCapPointVector = Vector<PlaneCapPoint, Core::Alloc::ScratchArena>;
using PlaneCapEdgeVector = Vector<PlaneCapEdge, Core::Alloc::ScratchArena>;
using PlaneCapIndexVector = Vector<u32, Core::Alloc::ScratchArena>;
using PlaneCapByteVector = Vector<u8, Core::Alloc::ScratchArena>;


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


[[nodiscard]] static SIMDVector NormalizeVector3Or(const SIMDVector value, const SIMDVector fallback){
    const SIMDVector lengthSquared = Vector3LengthSq(value);
    const f32 scalarLengthSquared = VectorGetX(lengthSquared);
    if(!IsFinite(scalarLengthSquared) || scalarLengthSquared <= 0.00000001f)
        return fallback;

    return VectorMultiply(value, VectorReciprocalSqrt(lengthSquared));
}

[[nodiscard]] static SIMDVector TransformObjectToWorld(
    const SIMDVector objectPosition,
    const Scene::TransformComponent* transform
){
    if(!transform)
        return objectPosition;

    const SIMDVector scale = LoadFloat(transform->scale);
    const SIMDVector rotation = LoadFloat(transform->rotation);
    const SIMDVector translation = LoadFloat(transform->position);
    return VectorAdd(Vector3Rotate(VectorMultiply(objectPosition, scale), rotation), translation);
}

[[nodiscard]] static SIMDVector TransformTrianglePosition(
    const CsgPlaneCapMeshTriangle& triangle,
    const u32 corner,
    const Scene::TransformComponent* transform
){
    return TransformObjectToWorld(LoadFloat(triangle.positions[corner]), transform);
}

[[nodiscard]] static f32 PlaneDistance(
    const SIMDMatrix& worldToShape,
    const SIMDVector normalDistance,
    const SIMDVector worldPosition
){
    const SIMDVector shapePosition = Vector3Transform(worldPosition, worldToShape);
    return VectorGetX(VectorAdd(Vector3Dot(shapePosition, normalDistance), VectorSplatW(normalDistance)));
}

[[nodiscard]] static SIMDVector BuildWorldPlaneNormal(
    const SIMDMatrix& worldToShape,
    const SIMDVector shapeNormal
){
    const SIMDVector row0 = VectorSetW(worldToShape.v[0], 0.0f);
    const SIMDVector row1 = VectorSetW(worldToShape.v[1], 0.0f);
    const SIMDVector row2 = VectorSetW(worldToShape.v[2], 0.0f);
    SIMDVector worldNormal = VectorMultiply(row0, VectorSplatX(shapeNormal));
    worldNormal = VectorMultiplyAdd(row1, VectorSplatY(shapeNormal), worldNormal);
    worldNormal = VectorMultiplyAdd(row2, VectorSplatZ(shapeNormal), worldNormal);
    return NormalizeVector3Or(worldNormal, VectorSet(0.0f, 1.0f, 0.0f, 0.0f));
}

static void BuildPlaneBasis(const SIMDVector capNormal, SIMDVector& outU, SIMDVector& outV){
    const f32 normalY = Abs(VectorGetY(capNormal));
    const SIMDVector reference = normalY < 0.9f
        ? VectorSet(0.0f, 1.0f, 0.0f, 0.0f)
        : VectorSet(1.0f, 0.0f, 0.0f, 0.0f)
    ;
    outU = NormalizeVector3Or(Vector3Cross(reference, capNormal), VectorSet(1.0f, 0.0f, 0.0f, 0.0f));
    outV = NormalizeVector3Or(Vector3Cross(capNormal, outU), VectorSet(0.0f, 0.0f, 1.0f, 0.0f));
}

static void AddUniqueIntersectionPoint(PlaneCapIntersection& intersection, const SIMDVector point){
    for(u32 pointIndex = 0u; pointIndex < intersection.count; ++pointIndex){
        const SIMDVector delta = VectorSubtract(intersection.points[pointIndex], point);
        if(VectorGetX(Vector3LengthSq(delta)) <= s_PointMergeEpsilonSquared)
            return;
    }

    if(intersection.count >= 3u)
        return;

    intersection.points[intersection.count] = point;
    ++intersection.count;
}

static void IntersectPlaneEdge(
    PlaneCapIntersection& intersection,
    const SIMDVector a,
    const SIMDVector b,
    const f32 da,
    const f32 db
){
    const bool aOnPlane = Abs(da) <= s_PlaneDistanceEpsilon;
    const bool bOnPlane = Abs(db) <= s_PlaneDistanceEpsilon;
    if(aOnPlane && bOnPlane)
        return;
    if(aOnPlane){
        AddUniqueIntersectionPoint(intersection, a);
        return;
    }
    if(bOnPlane){
        AddUniqueIntersectionPoint(intersection, b);
        return;
    }
    if((da < 0.0f) == (db < 0.0f))
        return;

    const f32 t = da / (da - db);
    AddUniqueIntersectionPoint(intersection, VectorLerp(a, b, t));
}

[[nodiscard]] static bool FindOrAppendPoint(
    PlaneCapPointVector& points,
    const SIMDVector position,
    const SIMDVector basisU,
    const SIMDVector basisV,
    u32& outPointIndex
){
    outPointIndex = 0u;
    const f32 u = VectorGetX(Vector3Dot(position, basisU));
    const f32 v = VectorGetX(Vector3Dot(position, basisV));
    for(usize pointIndex = 0u; pointIndex < points.size(); ++pointIndex){
        const PlaneCapPoint& point = points[pointIndex];
        const f32 du = point.u - u;
        const f32 dv = point.v - v;
        if(du * du + dv * dv > s_PointMergeEpsilonSquared)
            continue;

        outPointIndex = static_cast<u32>(pointIndex);
        return true;
    }

    if(points.size() >= static_cast<usize>(Limit<u32>::s_Max))
        return false;

    PlaneCapPoint point;
    StoreFloat(position, &point.position);
    point.u = u;
    point.v = v;
    outPointIndex = static_cast<u32>(points.size());
    points.push_back(point);
    return true;
}

static void AddUniqueEdge(PlaneCapEdgeVector& edges, const u32 a, const u32 b){
    if(a == b)
        return;

    for(const PlaneCapEdge& edge : edges){
        if((edge.a == a && edge.b == b) || (edge.a == b && edge.b == a))
            return;
    }

    edges.push_back(PlaneCapEdge{ a, b });
}

[[nodiscard]] static bool AppendCapSegment(
    PlaneCapPointVector& points,
    PlaneCapEdgeVector& edges,
    const SIMDVector a,
    const SIMDVector b,
    const SIMDVector basisU,
    const SIMDVector basisV
){
    u32 pointA = 0u;
    u32 pointB = 0u;
    if(!FindOrAppendPoint(points, a, basisU, basisV, pointA))
        return false;
    if(!FindOrAppendPoint(points, b, basisU, basisV, pointB))
        return false;

    AddUniqueEdge(edges, pointA, pointB);
    return true;
}

[[nodiscard]] static bool BuildPlaneCapSegments(
    const CsgPlaneCapMeshTriangleVector& triangles,
    const Scene::TransformComponent* transform,
    const CsgCutterGpuData& cutter,
    const SIMDVector basisU,
    const SIMDVector basisV,
    PlaneCapPointVector& points,
    PlaneCapEdgeVector& edges
){
    const SIMDMatrix worldToShape = LoadFloat(cutter.worldToShape);
    const SIMDVector normalDistance = LoadFloat(cutter.parameter0);

    points.clear();
    edges.clear();
    points.reserve(triangles.size());
    edges.reserve(triangles.size());

    for(const CsgPlaneCapMeshTriangle& triangle : triangles){
        const SIMDVector positions[] = {
            TransformTrianglePosition(triangle, 0u, transform),
            TransformTrianglePosition(triangle, 1u, transform),
            TransformTrianglePosition(triangle, 2u, transform),
        };
        const f32 distances[] = {
            PlaneDistance(worldToShape, normalDistance, positions[0u]),
            PlaneDistance(worldToShape, normalDistance, positions[1u]),
            PlaneDistance(worldToShape, normalDistance, positions[2u]),
        };

        u32 positiveCount = 0u;
        u32 negativeCount = 0u;
        for(const f32 distance : distances){
            positiveCount += distance > s_PlaneDistanceEpsilon ? 1u : 0u;
            negativeCount += distance < -s_PlaneDistanceEpsilon ? 1u : 0u;
        }
        if(positiveCount == 3u || negativeCount == 3u || (positiveCount == 0u && negativeCount == 0u))
            continue;

        PlaneCapIntersection intersection;
        IntersectPlaneEdge(intersection, positions[0u], positions[1u], distances[0u], distances[1u]);
        IntersectPlaneEdge(intersection, positions[1u], positions[2u], distances[1u], distances[2u]);
        IntersectPlaneEdge(intersection, positions[2u], positions[0u], distances[2u], distances[0u]);
        if(intersection.count != 2u)
            continue;
        if(!AppendCapSegment(points, edges, intersection.points[0u], intersection.points[1u], basisU, basisV))
            return false;
    }

    return true;
}

[[nodiscard]] static f32 PolygonSignedArea(const PlaneCapPointVector& points, const PlaneCapIndexVector& polygon){
    f32 area = 0.0f;
    for(usize index = 0u; index < polygon.size(); ++index){
        const PlaneCapPoint& a = points[polygon[index]];
        const PlaneCapPoint& b = points[polygon[(index + 1u) % polygon.size()]];
        area += a.u * b.v - b.u * a.v;
    }
    return area * 0.5f;
}

static void ReversePolygon(PlaneCapIndexVector& polygon){
    if(polygon.size() < 2u)
        return;

    usize left = 0u;
    usize right = polygon.size() - 1u;
    while(left < right){
        Swap(polygon[left], polygon[right]);
        ++left;
        --right;
    }
}

[[nodiscard]] static f32 Cross2(const PlaneCapPoint& a, const PlaneCapPoint& b, const PlaneCapPoint& c){
    return (b.u - a.u) * (c.v - a.v) - (b.v - a.v) * (c.u - a.u);
}

[[nodiscard]] static bool PointInTriangle2(
    const PlaneCapPoint& p,
    const PlaneCapPoint& a,
    const PlaneCapPoint& b,
    const PlaneCapPoint& c
){
    const f32 ab = Cross2(a, b, p);
    const f32 bc = Cross2(b, c, p);
    const f32 ca = Cross2(c, a, p);
    return ab >= -s_PointMergeEpsilon && bc >= -s_PointMergeEpsilon && ca >= -s_PointMergeEpsilon;
}

[[nodiscard]] static bool AppendCapVertex(
    CsgPlaneCapVertexGpuDataVector& vertices,
    const PlaneCapPoint& point,
    const SIMDVector capNormal,
    const u32 receiverIndex,
    const u32 cutterIndex
){
    if(vertices.size() >= static_cast<usize>(Limit<u32>::s_Max))
        return false;

    CsgPlaneCapVertexGpuData vertex;
    StoreFloat(VectorSetW(LoadFloat(point.position), static_cast<f32>(receiverIndex)), &vertex.positionReceiverIndex);
    StoreFloat(VectorSetW(capNormal, static_cast<f32>(cutterIndex)), &vertex.normalCutterIndex);
    vertex.color = s_DefaultCapColor;
    vertices.push_back(vertex);
    return true;
}

[[nodiscard]] static bool AppendCapTriangle(
    CsgPlaneCapVertexGpuDataVector& vertices,
    const PlaneCapPointVector& points,
    const u32 a,
    const u32 b,
    const u32 c,
    const SIMDVector capNormal,
    const u32 receiverIndex,
    const u32 cutterIndex
){
    return
        AppendCapVertex(vertices, points[a], capNormal, receiverIndex, cutterIndex)
        && AppendCapVertex(vertices, points[b], capNormal, receiverIndex, cutterIndex)
        && AppendCapVertex(vertices, points[c], capNormal, receiverIndex, cutterIndex)
    ;
}

[[nodiscard]] static bool AppendFanTriangulation(
    CsgPlaneCapVertexGpuDataVector& vertices,
    const PlaneCapPointVector& points,
    const PlaneCapIndexVector& polygon,
    const SIMDVector capNormal,
    const u32 receiverIndex,
    const u32 cutterIndex
){
    if(polygon.size() < 3u)
        return true;

    for(usize index = 1u; index + 1u < polygon.size(); ++index){
        if(!AppendCapTriangle(vertices, points, polygon[0u], polygon[index], polygon[index + 1u], capNormal, receiverIndex, cutterIndex))
            return false;
    }
    return true;
}

[[nodiscard]] static bool AppendEarClippedTriangulation(
    CsgPlaneCapVertexGpuDataVector& vertices,
    const PlaneCapPointVector& points,
    const PlaneCapIndexVector& loop,
    const SIMDVector capNormal,
    const u32 receiverIndex,
    const u32 cutterIndex,
    Core::Alloc::ScratchArena& scratchArena
){
    if(loop.size() < 3u)
        return true;

    PlaneCapIndexVector polygon(scratchArena);
    polygon.assign(loop.begin(), loop.end());
    if(PolygonSignedArea(points, polygon) < 0.0f)
        ReversePolygon(polygon);

    usize guard = 0u;
    while(polygon.size() > 3u && guard < loop.size() * loop.size()){
        bool clippedEar = false;
        for(usize index = 0u; index < polygon.size(); ++index){
            const u32 previousIndex = polygon[(index + polygon.size() - 1u) % polygon.size()];
            const u32 currentIndex = polygon[index];
            const u32 nextIndex = polygon[(index + 1u) % polygon.size()];
            const PlaneCapPoint& previous = points[previousIndex];
            const PlaneCapPoint& current = points[currentIndex];
            const PlaneCapPoint& next = points[nextIndex];
            if(Cross2(previous, current, next) <= s_PointMergeEpsilon)
                continue;

            bool containsPoint = false;
            for(const u32 testIndex : polygon){
                if(testIndex == previousIndex || testIndex == currentIndex || testIndex == nextIndex)
                    continue;
                if(PointInTriangle2(points[testIndex], previous, current, next)){
                    containsPoint = true;
                    break;
                }
            }
            if(containsPoint)
                continue;

            if(!AppendCapTriangle(vertices, points, previousIndex, currentIndex, nextIndex, capNormal, receiverIndex, cutterIndex))
                return false;

            polygon.erase(polygon.begin() + static_cast<isize>(index));
            clippedEar = true;
            break;
        }

        if(!clippedEar)
            return AppendFanTriangulation(vertices, points, polygon, capNormal, receiverIndex, cutterIndex);
        ++guard;
    }

    if(polygon.size() == 3u)
        return AppendCapTriangle(vertices, points, polygon[0u], polygon[1u], polygon[2u], capNormal, receiverIndex, cutterIndex);

    return true;
}

[[nodiscard]] static bool FindNextLoopEdge(
    const PlaneCapEdgeVector& edges,
    const PlaneCapByteVector& usedEdges,
    const u32 currentPoint,
    const u32 previousPoint,
    usize& outEdgeIndex,
    u32& outNextPoint
){
    outEdgeIndex = 0u;
    outNextPoint = 0u;
    for(usize edgeIndex = 0u; edgeIndex < edges.size(); ++edgeIndex){
        if(usedEdges[edgeIndex])
            continue;

        const PlaneCapEdge& edge = edges[edgeIndex];
        if(edge.a == currentPoint){
            outEdgeIndex = edgeIndex;
            outNextPoint = edge.b;
            if(edge.b != previousPoint)
                return true;
        }
        else if(edge.b == currentPoint){
            outEdgeIndex = edgeIndex;
            outNextPoint = edge.a;
            if(edge.a != previousPoint)
                return true;
        }
    }

    return false;
}

[[nodiscard]] static bool AppendCapLoops(
    CsgPlaneCapVertexGpuDataVector& vertices,
    const PlaneCapPointVector& points,
    const PlaneCapEdgeVector& edges,
    const SIMDVector capNormal,
    const u32 receiverIndex,
    const u32 cutterIndex,
    Core::Alloc::ScratchArena& scratchArena
){
    PlaneCapByteVector usedEdges(scratchArena);
    usedEdges.resize(edges.size(), 0u);

    PlaneCapIndexVector loop(scratchArena);
    for(usize edgeIndex = 0u; edgeIndex < edges.size(); ++edgeIndex){
        if(usedEdges[edgeIndex])
            continue;

        const PlaneCapEdge& edge = edges[edgeIndex];
        loop.clear();
        loop.push_back(edge.a);
        loop.push_back(edge.b);
        usedEdges[edgeIndex] = 1u;

        bool closed = false;
        u32 previousPoint = edge.a;
        u32 currentPoint = edge.b;
        for(usize guard = 0u; guard < edges.size(); ++guard){
            usize nextEdgeIndex = 0u;
            u32 nextPoint = 0u;
            if(!FindNextLoopEdge(edges, usedEdges, currentPoint, previousPoint, nextEdgeIndex, nextPoint))
                break;

            usedEdges[nextEdgeIndex] = 1u;
            if(nextPoint == loop[0u]){
                closed = true;
                break;
            }

            loop.push_back(nextPoint);
            previousPoint = currentPoint;
            currentPoint = nextPoint;
        }

        if(!closed || loop.size() < 3u)
            continue;
        if(!AppendEarClippedTriangulation(vertices, points, loop, capNormal, receiverIndex, cutterIndex, scratchArena))
            return false;
    }

    return true;
}

[[nodiscard]] static bool AppendPlaneCapGeometry(
    const CsgPlaneCapMeshTriangleVector& triangles,
    const Scene::TransformComponent* transform,
    const CsgCutterGpuData& cutter,
    const u32 receiverIndex,
    const u32 cutterIndex,
    CsgPlaneCapVertexGpuDataVector& vertices,
    Core::Alloc::ScratchArena& scratchArena
){
    const SIMDMatrix worldToShape = LoadFloat(cutter.worldToShape);
    const SIMDVector shapeNormal = VectorSetW(LoadFloat(cutter.parameter0), 0.0f);
    const SIMDVector capNormal = VectorNegate(BuildWorldPlaneNormal(worldToShape, shapeNormal));

    SIMDVector basisU;
    SIMDVector basisV;
    BuildPlaneBasis(capNormal, basisU, basisV);

    PlaneCapPointVector points(scratchArena);
    PlaneCapEdgeVector edges(scratchArena);
    if(!BuildPlaneCapSegments(triangles, transform, cutter, basisU, basisV, points, edges))
        return false;
    if(points.empty() || edges.empty())
        return true;

    return AppendCapLoops(vertices, points, edges, capNormal, receiverIndex, cutterIndex, scratchArena);
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace ECSRenderCsgPlaneCapBuilder{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


bool AppendPlaneCapGeometry(
    const CsgPlaneCapMeshTriangleVector& triangles,
    const Scene::TransformComponent* transform,
    const u32 receiverIndex,
    const CsgCutterGpuData& cutter,
    const u32 cutterIndex,
    CsgPlaneCapVertexGpuDataVector& vertices,
    Core::Alloc::ScratchArena& scratchArena
){
    return __hidden_csg_plane_cap_builder::AppendPlaneCapGeometry(
        triangles,
        transform,
        cutter,
        receiverIndex,
        cutterIndex,
        vertices,
        scratchArena
    );
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

