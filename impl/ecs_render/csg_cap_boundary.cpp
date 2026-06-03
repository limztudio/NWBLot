// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "csg_cap_private.h"


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace ECSRenderCsgCapDetail{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace __hidden_csg_cap_boundary{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


[[nodiscard]] static SIMDVector TransformObjectToWorld(
    const SIMDVector objectPosition,
    const Scene::TransformComponent* transform
){
    if(!transform)
        return objectPosition;

    const SIMDVector scale = LoadFloat(transform->scale);
    const SIMDVector rotation = LoadFloat(transform->rotation);
    const SIMDVector translation = LoadFloat(transform->position);
    return VectorSetW(VectorAdd(Vector3Rotate(VectorMultiply(objectPosition, scale), rotation), translation), 0.0f);
}

[[nodiscard]] static SIMDVector TransformObjectDirectionToWorld(
    const SIMDVector objectDirection,
    const Scene::TransformComponent* transform,
    const SIMDVector fallback
){
    if(!transform)
        return Vector3NormalizeOr(objectDirection, fallback, s_NormalizeMinLengthSquared);

    const SIMDVector scale = LoadFloat(transform->scale);
    const SIMDVector rotation = LoadFloat(transform->rotation);
    return Vector3NormalizeOr(
        Vector3Rotate(VectorMultiply(objectDirection, scale), rotation),
        fallback,
        s_NormalizeMinLengthSquared
    );
}

[[nodiscard]] static CapSourceVertex LoadCapSourceVertex(const CsgCapMeshVertex& vertex){
    return CapSourceVertex{
        LoadFloat(vertex.position),
        LoadFloat(vertex.normal),
        LoadFloat(vertex.tangent),
        LoadFloat(vertex.uv0),
        LoadFloat(vertex.color),
    };
}

[[nodiscard]] static CapSourceVertex TransformCapSourceVertex(
    const CsgCapMeshVertex& vertex,
    const Scene::TransformComponent* transform
){
    const CapSourceVertex source = LoadCapSourceVertex(vertex);
    const SIMDVector tangent = TransformObjectDirectionToWorld(
        VectorSetW(source.tangent, 0.0f),
        transform,
        VectorSet(1.0f, 0.0f, 0.0f, 0.0f)
    );
    return CapSourceVertex{
        TransformObjectToWorld(source.position, transform),
        TransformObjectDirectionToWorld(source.normal, transform, VectorSet(0.0f, 0.0f, 1.0f, 0.0f)),
        VectorSetW(tangent, VectorGetW(source.tangent)),
        VectorSetW(source.uv0, 0.0f),
        source.color,
    };
}

[[nodiscard]] static CapSourceVertex LerpCapSourceVertex(
    const CapSourceVertex& a,
    const CapSourceVertex& b,
    const f32 t
){
    const SIMDVector normal = Vector3NormalizeOr(
        VectorLerp(a.normal, b.normal, t),
        VectorSet(0.0f, 0.0f, 1.0f, 0.0f),
        s_NormalizeMinLengthSquared
    );
    const SIMDVector tangent = Vector3NormalizeOr(
        VectorLerp(VectorSetW(a.tangent, 0.0f), VectorSetW(b.tangent, 0.0f), t),
        VectorSet(1.0f, 0.0f, 0.0f, 0.0f),
        s_NormalizeMinLengthSquared
    );

    return CapSourceVertex{
        VectorSetW(VectorLerp(a.position, b.position, t), 0.0f),
        normal,
        VectorSetW(tangent, VectorGetW(a.tangent)),
        VectorSetW(VectorLerp(a.uv0, b.uv0, t), 0.0f),
        VectorLerp(a.color, b.color, t),
    };
}

static void AddUniqueIntersectionPoint(CapIntersection& intersection, const CapSourceVertex& vertex){
    for(u32 pointIndex = 0u; pointIndex < intersection.count; ++pointIndex){
        const SIMDVector delta = VectorSubtract(intersection.vertices[pointIndex].position, vertex.position);
        if(VectorGetX(Vector3LengthSq(delta)) <= s_PointMergeEpsilonSquared)
            return;
    }

    if(intersection.count >= 3u)
        return;

    intersection.vertices[intersection.count] = vertex;
    ++intersection.count;
}

[[nodiscard]] static bool IntersectShapeEdge(
    CapIntersection& intersection,
    const CapSourceVertex& a,
    const CapSourceVertex& b,
    const f32 da,
    const f32 db,
    const CapCutterEval& cutterEval
){
    const bool aOnSurface = Abs(da) <= s_CapDistanceEpsilon;
    const bool bOnSurface = Abs(db) <= s_CapDistanceEpsilon;
    if(aOnSurface && bOnSurface)
        return true;
    if(aOnSurface){
        AddUniqueIntersectionPoint(intersection, a);
        return true;
    }
    if(bOnSurface){
        AddUniqueIntersectionPoint(intersection, b);
        return true;
    }
    if((da < 0.0f) == (db < 0.0f))
        return true;

    f32 t0 = 0.0f;
    f32 t1 = 1.0f;
    f32 d0 = da;
    f32 t = Max(0.0f, Min(1.0f, da / (da - db)));
    for(u32 iteration = 0u; iteration < s_EdgeIntersectionRefineIterations; ++iteration){
        const CapSourceVertex midpoint = LerpCapSourceVertex(a, b, t);
        SIMDVector signedDistance;
        if(!EvaluateShapeDistance(cutterEval, midpoint.position, midpoint.normal, signedDistance))
            return false;
        const f32 distance = VectorGetX(signedDistance);
        if(Abs(distance) <= s_CapDistanceEpsilon)
            break;

        if((distance < 0.0f) == (d0 < 0.0f)){
            t0 = t;
            d0 = distance;
        }
        else{
            t1 = t;
        }
        t = (t0 + t1) * 0.5f;
    }

    AddUniqueIntersectionPoint(intersection, LerpCapSourceVertex(a, b, t));
    return true;
}

[[nodiscard]] static bool FindOrAppendPoint(
    CapPointVector& points,
    const CapSourceVertex& vertex,
    u32& outPointIndex
){
    outPointIndex = 0u;
    for(usize pointIndex = 0u; pointIndex < points.size(); ++pointIndex){
        const SIMDVector delta = VectorSubtract(LoadFloat(points[pointIndex].position), vertex.position);
        if(VectorGetX(Vector3LengthSq(delta)) > s_PointMergeEpsilonSquared)
            continue;

        outPointIndex = static_cast<u32>(pointIndex);
        return true;
    }

    if(points.size() >= static_cast<usize>(Limit<u32>::s_Max))
        return false;

    CapPoint point;
    StoreFloat(vertex.position, &point.position);
    StoreFloat(vertex.normal, &point.normal);
    StoreFloat(vertex.tangent, &point.tangent);
    StoreFloat(vertex.uv0, &point.uv0);
    StoreFloat(vertex.color, &point.color);
    outPointIndex = static_cast<u32>(points.size());
    points.push_back(point);
    return true;
}

static void AddUniqueEdge(CapEdgeVector& edges, const u32 a, const u32 b){
    if(a == b)
        return;

    for(const CapEdge& edge : edges){
        if((edge.a == a && edge.b == b) || (edge.a == b && edge.b == a))
            return;
    }

    edges.push_back(CapEdge{ a, b });
}

[[nodiscard]] static bool AppendCapSegment(
    CapPointVector& points,
    CapEdgeVector& edges,
    const CapSourceVertex& a,
    const CapSourceVertex& b
){
    u32 pointA = 0u;
    u32 pointB = 0u;
    if(!FindOrAppendPoint(points, a, pointA))
        return false;
    if(!FindOrAppendPoint(points, b, pointB))
        return false;

    AddUniqueEdge(edges, pointA, pointB);
    return true;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


bool BuildCapSegments(
    const CsgCapMeshTriangleVector& triangles,
    const Scene::TransformComponent* transform,
    const CapCutterEval& cutterEval,
    CapPointVector& points,
    CapEdgeVector& edges
){
    points.clear();
    edges.clear();
    points.reserve(triangles.size());
    edges.reserve(triangles.size());

    for(const CsgCapMeshTriangle& triangle : triangles){
        const CapSourceVertex vertices[] = {
            __hidden_csg_cap_boundary::TransformCapSourceVertex(triangle.vertices[0u], transform),
            __hidden_csg_cap_boundary::TransformCapSourceVertex(triangle.vertices[1u], transform),
            __hidden_csg_cap_boundary::TransformCapSourceVertex(triangle.vertices[2u], transform),
        };
        SIMDVector signedDistances[3];
        if(
            !EvaluateShapeDistance(cutterEval, vertices[0u].position, vertices[0u].normal, signedDistances[0u])
            || !EvaluateShapeDistance(cutterEval, vertices[1u].position, vertices[1u].normal, signedDistances[1u])
            || !EvaluateShapeDistance(cutterEval, vertices[2u].position, vertices[2u].normal, signedDistances[2u])
        )
            return false;

        const f32 distances[] = {
            VectorGetX(signedDistances[0u]),
            VectorGetX(signedDistances[1u]),
            VectorGetX(signedDistances[2u]),
        };

        u32 positiveCount = 0u;
        u32 negativeCount = 0u;
        for(const f32 distance : distances){
            positiveCount += distance > s_CapDistanceEpsilon ? 1u : 0u;
            negativeCount += distance < -s_CapDistanceEpsilon ? 1u : 0u;
        }
        if(positiveCount == 3u || negativeCount == 3u || (positiveCount == 0u && negativeCount == 0u))
            continue;

        CapIntersection intersection;
        if(
            !__hidden_csg_cap_boundary::IntersectShapeEdge(intersection, vertices[0u], vertices[1u], distances[0u], distances[1u], cutterEval)
            || !__hidden_csg_cap_boundary::IntersectShapeEdge(intersection, vertices[1u], vertices[2u], distances[1u], distances[2u], cutterEval)
            || !__hidden_csg_cap_boundary::IntersectShapeEdge(intersection, vertices[2u], vertices[0u], distances[2u], distances[0u], cutterEval)
        )
            return false;
        if(intersection.count != 2u)
            continue;
        if(!__hidden_csg_cap_boundary::AppendCapSegment(points, edges, intersection.vertices[0u], intersection.vertices[1u]))
            return false;
    }

    return true;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

