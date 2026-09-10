// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "deform_edit.h"


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace __hidden_csg_deform_edit{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


using ScratchArena = Core::Alloc::ScratchArena;

inline constexpr f32 s_MinEpsilon = 0.0000001f;
inline constexpr f32 s_SplitDenominatorEpsilon = 0.0000001f;
inline constexpr f32 s_NormalizeEpsilon = 0.000001f;
inline constexpr f32 s_NormalizeEpsilonSq = s_NormalizeEpsilon * s_NormalizeEpsilon;
inline constexpr f32 s_LoopAreaEpsilonSq = s_MinEpsilon * s_MinEpsilon;
inline constexpr f32 s_OptionEpsilonLow = 0.0f;
inline constexpr f32 s_OptionEpsilonHigh = 1.0f;
inline constexpr f32 s_KeepDistanceZero = 0.0f;
inline constexpr f32 s_OneWeight = 1.0f;
inline constexpr f32 s_AffineW = 1.0f;
inline constexpr f32 s_ShapeWMask = 0.0f;
inline constexpr f32 s_NegativeOne = -1.0f;
inline constexpr f32 s_UpAxisX = 0.0f;
inline constexpr f32 s_UpAxisY = 1.0f;
inline constexpr f32 s_UpAxisZ = 0.0f;
inline constexpr f32 s_FallbackTangentX = 1.0f;
inline constexpr f32 s_FallbackTangentY = 0.0f;
inline constexpr f32 s_FallbackTangentZ = 0.0f;
inline constexpr f32 s_FallbackTangentW = 1.0f;
inline constexpr f32 s_CapNormalW = 0.0f;
inline constexpr u32 s_DeformCapacityShift = 20u;
inline constexpr usize s_MaxDeformVertices = 1u << s_DeformCapacityShift;
inline constexpr usize s_MaxDeformTriangles = 1u << s_DeformCapacityShift;
inline constexpr u32 s_EdgeKeyHalfBits = 32u;
inline constexpr u32 s_EdgeHashShift = 33u;
inline constexpr u32 s_TriangleCornerCount = 3u;
inline constexpr u32 s_MinLoopVertices = 3u;
inline constexpr u32 s_EdgesPerTriangle = 3u;
inline constexpr u32 s_KeptReserveMultiplier = 2u;
inline constexpr u32 s_ReserveSlack = 1u;

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


[[nodiscard]] bool FiniteFloat(const f32 value){
    return value == value && value != Limit<f32>::s_Infinity && value != -Limit<f32>::s_Infinity;
}

[[nodiscard]] bool FiniteVertex(const CsgDeformVertex& vertex){
    return FiniteFloat(vertex.position.x)
        && FiniteFloat(vertex.position.y)
        && FiniteFloat(vertex.position.z)
        && FiniteFloat(vertex.normal.x)
        && FiniteFloat(vertex.normal.y)
        && FiniteFloat(vertex.normal.z)
        && FiniteFloat(vertex.normal.w)
        && FiniteFloat(vertex.tangent.x)
        && FiniteFloat(vertex.tangent.y)
        && FiniteFloat(vertex.tangent.z)
        && FiniteFloat(vertex.tangent.w)
        && FiniteFloat(vertex.uv0.x)
        && FiniteFloat(vertex.uv0.y)
        && FiniteFloat(vertex.color.x)
        && FiniteFloat(vertex.color.y)
        && FiniteFloat(vertex.color.z)
        && FiniteFloat(vertex.color.w)
    ;
}

[[nodiscard]] f32 SaturateFloat(const f32 value){
    // SIMD clamp keeps the scalar weight on vector lanes (min/max, no branches).
    return VectorGetX(VectorSaturate(VectorReplicate(value)));
}

[[nodiscard]] f32 ShapeEpsilon(const CsgDeformBuildOptions& options){
    return options.distanceEpsilon > s_MinEpsilon ? options.distanceEpsilon : s_MinEpsilon;
}

[[nodiscard]] bool ValidOptions(const CsgDeformBuildOptions& options){
    return options.distanceEpsilon > s_OptionEpsilonLow && options.distanceEpsilon < s_OptionEpsilonHigh;
}

[[nodiscard]] bool ValidTopology(
    const CsgDeformVertex* vertices,
    const usize vertexCount,
    const CsgDeformTriangle* triangles,
    const usize triangleCount
){
    NWB_ASSERT(vertices != nullptr);
    NWB_ASSERT(triangles != nullptr);
    for(usize triangleIndex = 0u; triangleIndex < triangleCount; ++triangleIndex){
        const CsgDeformTriangle& triangle = triangles[triangleIndex];
        for(usize corner = 0u; corner < s_TriangleCornerCount; ++corner){
            if(triangle.indices[corner] >= vertexCount)
                return false;
        }
    }
    return true;
}

[[nodiscard]] bool FiniteInput(
    const CsgDeformVertex* vertices,
    const usize vertexCount,
    CsgDeformViabilityReason::Enum& outReason
){
    outReason = CsgDeformViabilityReason::Ok;
    NWB_ASSERT(vertices != nullptr);
    for(usize vertexIndex = 0u; vertexIndex < vertexCount; ++vertexIndex){
        if(!FiniteVertex(vertices[vertexIndex])){
            outReason = CsgDeformViabilityReason::NonFiniteInput;
            return false;
        }
    }
    return true;
}

namespace CsgDeformShapeKind{
    enum Enum : u8{
        Invalid,
        Plane,
        Box,
        Sphere,
        Capsule,
    };
};

[[nodiscard]] CsgDeformShapeKind::Enum ClassifyDeformShape(const Name& shapeType){
    static const Name s_PlaneShape("engine/csg/plane");
    static const Name s_BoxShape("engine/csg/box");
    static const Name s_SphereShape("engine/csg/sphere");
    static const Name s_CapsuleShape("engine/csg/capsule");
    if(shapeType == s_PlaneShape)
        return CsgDeformShapeKind::Plane;
    if(shapeType == s_BoxShape)
        return CsgDeformShapeKind::Box;
    if(shapeType == s_SphereShape)
        return CsgDeformShapeKind::Sphere;
    if(shapeType == s_CapsuleShape)
        return CsgDeformShapeKind::Capsule;
    return CsgDeformShapeKind::Invalid;
}

// SIMD SDFs take shape-space position directly. Dot/abs/length stay on SIMD
// lanes; only the final distance crosses back to scalar for the epsilon snap.
[[nodiscard]] f32 PlaneSignedDistance(SIMDVector shapePosition, SIMDVector parameter0){
    return VectorGetX(Vector3Dot(shapePosition, parameter0)) + VectorGetW(parameter0);
}

[[nodiscard]] f32 BoxSignedDistance(SIMDVector shapePosition, SIMDVector parameter0){
    // 3-lane helpers ignore w, so the affine w=1 lane needs no masking.
    const SIMDVector halfExtents = VectorSetW(parameter0, s_ShapeWMask);
    const SIMDVector q = VectorSubtract(VectorAbs(shapePosition), halfExtents);
    const SIMDVector outsideVec = VectorMax(q, VectorZero());
    const f32 outside = VectorGetX(Vector3Length(outsideVec));
    const f32 insideComp = VectorGetX(Vector3MinComponent(q));
    const f32 inside = insideComp < s_KeepDistanceZero ? insideComp : s_KeepDistanceZero;
    return outside + inside;
}

[[nodiscard]] f32 SphereSignedDistance(SIMDVector shapePosition, SIMDVector parameter0){
    return VectorGetX(Vector3Length(shapePosition)) - VectorGetX(parameter0);
}

[[nodiscard]] f32 CapsuleSignedDistance(SIMDVector shapePosition, SIMDVector parameter0){
    const f32 halfHeight = VectorGetY(parameter0);
    const f32 shapeY = VectorGetY(shapePosition);
    const f32 clampedY = shapeY < -halfHeight ? -halfHeight : (shapeY > halfHeight ? halfHeight : shapeY);
    const SIMDVector delta = VectorSubtract(shapePosition, VectorSet(s_ShapeWMask, clampedY, s_ShapeWMask, s_ShapeWMask));
    return VectorGetX(Vector3Length(delta)) - VectorGetX(parameter0);
}

[[nodiscard]] bool ShapeDistances(
    const CsgDeformShape& shape,
    const CsgDeformVertexVector<ScratchArena>& vertices,
    const f32 epsilon,
    Vector<f32, ScratchArena>& outDistances,
    CsgDeformViabilityReason::Enum& outReason
){
    outReason = CsgDeformViabilityReason::Ok;
    if(!shape.shapeType){
        outReason = CsgDeformViabilityReason::InvalidCutter;
        return false;
    }
    const CsgDeformShapeKind::Enum shapeKind = ClassifyDeformShape(shape.shapeType);
    if(shapeKind == CsgDeformShapeKind::Invalid){
        outReason = CsgDeformViabilityReason::InvalidCutter;
        return false;
    }
    const usize vertexCount = vertices.size();
    outDistances.clear();
    outDistances.resize(vertexCount, 0.0f);

    // Cutter dispatch happens once per cut. World-to-shape and SDF eval stay on
    // SIMD lanes; only the snapped distance crosses back to scalar, so
    // preview/commit observe identical distances with no second pass.
    const SIMDMatrix worldToShape = LoadFloat(shape.worldToShape);
    const SIMDVector parameter0 = LoadFloat(shape.parameter0);
    switch(shapeKind){
    case CsgDeformShapeKind::Plane:{
        for(usize vertexIndex = 0u; vertexIndex < vertexCount; ++vertexIndex){
            const CsgDeformVertex& vertex = vertices[vertexIndex];
            const SIMDVector shapePosition = Vector4Transform(VectorSetW(LoadFloat(vertex.position), s_AffineW), worldToShape);
            f32 distance = PlaneSignedDistance(shapePosition, parameter0);
            if(!FiniteFloat(distance)){
                outReason = CsgDeformViabilityReason::NonFiniteInput;
                return false;
            }
            if(Abs(distance) <= epsilon)
                distance = s_KeepDistanceZero;
            outDistances[vertexIndex] = distance;
        }
        return true;
    }
    case CsgDeformShapeKind::Box:{
        for(usize vertexIndex = 0u; vertexIndex < vertexCount; ++vertexIndex){
            const CsgDeformVertex& vertex = vertices[vertexIndex];
            const SIMDVector shapePosition = Vector4Transform(VectorSetW(LoadFloat(vertex.position), s_AffineW), worldToShape);
            f32 distance = BoxSignedDistance(shapePosition, parameter0);
            if(!FiniteFloat(distance)){
                outReason = CsgDeformViabilityReason::NonFiniteInput;
                return false;
            }
            if(Abs(distance) <= epsilon)
                distance = s_KeepDistanceZero;
            outDistances[vertexIndex] = distance;
        }
        return true;
    }
    case CsgDeformShapeKind::Sphere:{
        for(usize vertexIndex = 0u; vertexIndex < vertexCount; ++vertexIndex){
            const CsgDeformVertex& vertex = vertices[vertexIndex];
            const SIMDVector shapePosition = Vector4Transform(VectorSetW(LoadFloat(vertex.position), s_AffineW), worldToShape);
            f32 distance = SphereSignedDistance(shapePosition, parameter0);
            if(!FiniteFloat(distance)){
                outReason = CsgDeformViabilityReason::NonFiniteInput;
                return false;
            }
            if(Abs(distance) <= epsilon)
                distance = s_KeepDistanceZero;
            outDistances[vertexIndex] = distance;
        }
        return true;
    }
    case CsgDeformShapeKind::Capsule:{
        for(usize vertexIndex = 0u; vertexIndex < vertexCount; ++vertexIndex){
            const CsgDeformVertex& vertex = vertices[vertexIndex];
            const SIMDVector shapePosition = Vector4Transform(VectorSetW(LoadFloat(vertex.position), s_AffineW), worldToShape);
            f32 distance = CapsuleSignedDistance(shapePosition, parameter0);
            if(!FiniteFloat(distance)){
                outReason = CsgDeformViabilityReason::NonFiniteInput;
                return false;
            }
            if(Abs(distance) <= epsilon)
                distance = s_KeepDistanceZero;
            outDistances[vertexIndex] = distance;
        }
        return true;
    }
    default:
        break;
    }
    outReason = CsgDeformViabilityReason::InvalidCutter;
    return false;
}

// Canonical edge id keeps (a,b) and (b,a) identical without hashing pointers.
[[nodiscard]] u64 EdgeKey(const u32 first, const u32 second){
    const u32 lo = first < second ? first : second;
    const u32 hi = first < second ? second : first;
    return (static_cast<u64>(lo) << s_EdgeKeyHalfBits) | static_cast<u64>(hi);
}

struct EdgeSplitRecord{
    u64 edge = 0u;
    u32 vertex = 0u;
};

struct EdgeSplitKeyHash{
    [[nodiscard]] usize operator()(const u64 key)const noexcept{ return static_cast<usize>(key ^ (key >> s_EdgeHashShift)); }
};

using EdgeSplitMap = HashMap<u64, u32, EdgeSplitKeyHash, EqualTo<u64>, ScratchArena>;

[[nodiscard]] CsgDeformVertex MixVertices(const CsgDeformVertex& first, const CsgDeformVertex& second, const f32 firstWeight){
    // SIMD blend keeps positions/normals/tangents/uvs/colors on vector lanes.
    // Op order matches the scalar form (first*blend + second*(1-blend)) lane-wise.
    const f32 blend = SaturateFloat(firstWeight);
    const f32 other = s_OneWeight - blend;
    const SIMDVector blendVec = VectorReplicate(blend);
    const SIMDVector otherVec = VectorReplicate(other);
    CsgDeformVertex mixed;
    const SIMDVector mixedPosition = VectorAdd(VectorMultiply(LoadFloat(first.position), blendVec), VectorMultiply(LoadFloat(second.position), otherVec));
    StoreFloat(mixedPosition, mixed.position);
    const SIMDVector mixedNormal = VectorAdd(VectorMultiply(LoadFloat(first.normal), blendVec), VectorMultiply(LoadFloat(second.normal), otherVec));
    StoreFloat(mixedNormal, mixed.normal);
    const SIMDVector mixedTangent = VectorAdd(VectorMultiply(LoadFloat(first.tangent), blendVec), VectorMultiply(LoadFloat(second.tangent), otherVec));
    StoreFloat(mixedTangent, mixed.tangent);
    const SIMDVector mixedUv = VectorAdd(VectorMultiply(LoadFloat(first.uv0), blendVec), VectorMultiply(LoadFloat(second.uv0), otherVec));
    StoreFloat(mixedUv, mixed.uv0);
    const SIMDVector mixedColor = VectorAdd(VectorMultiply(LoadFloat(first.color), blendVec), VectorMultiply(LoadFloat(second.color), otherVec));
    StoreFloat(mixedColor, mixed.color);
    return mixed;
}

[[nodiscard]] bool NormalizeDeformVertex(CsgDeformVertex& vertex){
    // SIMD normalize keeps xyz length/normalize on vector lanes. The degenerate
    // fallback and w/handedness stay scalar so both preview and commit pick the
    // identical deterministic branch.
    // s_NormalizeEpsilonSq is file-scoped so preview and commit share one threshold.
    const SIMDVector normalVec = LoadFloat(vertex.normal);
    const f32 normalLengthSq = VectorGetX(Vector3LengthSq(normalVec));
    if(normalLengthSq > s_NormalizeEpsilonSq){
        const SIMDVector normalized = Vector3Normalize(normalVec);
        const f32 fallbackW = VectorGetW(normalVec);
        vertex.normal.x = VectorGetX(normalized);
        vertex.normal.y = VectorGetY(normalized);
        vertex.normal.z = VectorGetZ(normalized);
        vertex.normal.w = fallbackW;
    }
    else{
        vertex.normal.x = s_UpAxisX;
        vertex.normal.y = s_UpAxisY;
        vertex.normal.z = s_UpAxisZ;
    }
    const SIMDVector tangentVec = LoadFloat(vertex.tangent);
    const f32 tangentLengthSq = VectorGetX(Vector3LengthSq(tangentVec));
    if(tangentLengthSq > s_NormalizeEpsilonSq){
        const SIMDVector normalized = Vector3Normalize(tangentVec);
        const f32 handedness = VectorGetW(tangentVec) < s_KeepDistanceZero ? s_NegativeOne : s_OneWeight;
        vertex.tangent.x = VectorGetX(normalized);
        vertex.tangent.y = VectorGetY(normalized);
        vertex.tangent.z = VectorGetZ(normalized);
        vertex.tangent.w = handedness;
    }
    else{
        vertex.tangent.x = s_FallbackTangentX;
        vertex.tangent.y = s_FallbackTangentY;
        vertex.tangent.z = s_FallbackTangentZ;
        vertex.tangent.w = s_FallbackTangentW;
    }
    return FiniteVertex(vertex);
}

[[nodiscard]] bool SplitEdgeVertex(
    CsgDeformVertexVector<ScratchArena>& vertices,
    EdgeSplitMap& edgeSplits,
    const u32 first,
    const u32 second,
    const f32 firstDistance,
    const f32 secondDistance,
    u32& outVertex
){
    outVertex = 0u;
    if(vertices.size() + 1u > s_MaxDeformVertices)
        return false;
    const u64 edge = EdgeKey(first, second);
    const auto found = edgeSplits.find(edge);
    if(found != edgeSplits.end()){
        outVertex = found.value();
        return true;
    }
    const f32 denominator = firstDistance - secondDistance;
    if(!FiniteFloat(denominator) || Abs(denominator) < s_SplitDenominatorEpsilon)
        return false;
    // firstWeight lands on second when secondDistance is zero.
    const f32 firstWeight = SaturateFloat(Abs(secondDistance / denominator));
    CsgDeformVertex mixed = MixVertices(vertices[first], vertices[second], firstWeight);
    if(!NormalizeDeformVertex(mixed))
        return false;
    const u32 created = static_cast<u32>(vertices.size());
    vertices.push_back(mixed);
    edgeSplits.emplace(edge, created);
    outVertex = created;
    return true;
}

void EmitTriangle(
    CsgDeformTriangleVector<ScratchArena>& triangles,
    const u32 first,
    const u32 second,
    const u32 third
){
    CsgDeformTriangle triangle;
    triangle.indices[0u] = first;
    triangle.indices[1u] = second;
    triangle.indices[2u] = third;
    triangles.push_back(triangle);
}

// Keep side is distance >= 0; caller snaps |distance| <= epsilon to zero first.
[[nodiscard]] bool ClipShell(
    ScratchArena& scratchArena,
    const CsgDeformShape& shape,
    const f32 epsilon,
    CsgDeformVertexVector<ScratchArena>& inOutVertices,
    CsgDeformTriangleVector<ScratchArena>& inOutTriangles,
    CsgDeformTriangleVector<ScratchArena>& scratchKept,
    Vector<f32, ScratchArena>& scratchDistances,
    CsgDeformViabilityReason::Enum& outReason
){
    outReason = CsgDeformViabilityReason::Ok;
    if(!ShapeDistances(shape, inOutVertices, epsilon, scratchDistances, outReason))
        return false;

    EdgeSplitMap edgeSplits(0, EdgeSplitKeyHash(), EqualTo<u64>(), scratchArena);
    edgeSplits.reserve(inOutTriangles.size() * s_EdgesPerTriangle + s_ReserveSlack);
    scratchKept.clear();
    scratchKept.reserve(inOutTriangles.size() * s_KeptReserveMultiplier + s_ReserveSlack);

    const usize triangleCount = inOutTriangles.size();
    for(usize triangleIndex = 0u; triangleIndex < triangleCount; ++triangleIndex){
        const CsgDeformTriangle triangle = inOutTriangles[triangleIndex];
        NWB_ASSERT(triangle.indices[0u] < inOutVertices.size());
        NWB_ASSERT(triangle.indices[1u] < inOutVertices.size());
        NWB_ASSERT(triangle.indices[2u] < inOutVertices.size());
        const f32 distances[s_TriangleCornerCount] = {
            scratchDistances[triangle.indices[0u]],
            scratchDistances[triangle.indices[1u]],
            scratchDistances[triangle.indices[2u]],
        };
        const bool kept[s_TriangleCornerCount] = { distances[0u] >= s_KeepDistanceZero, distances[1u] >= s_KeepDistanceZero, distances[2u] >= s_KeepDistanceZero };
        const u32 keepCount = (kept[0u] ? 1u : 0u) + (kept[1u] ? 1u : 0u) + (kept[2u] ? 1u : 0u);
        if(keepCount == 3u){
            EmitTriangle(scratchKept, triangle.indices[0u], triangle.indices[1u], triangle.indices[2u]);
            continue;
        }
        if(keepCount == 0u)
            continue;
        if(keepCount == 1u){
            u32 keepCorner = 0u;
            for(u32 corner = 0u; corner < s_TriangleCornerCount; ++corner){
                if(kept[corner]){
                    keepCorner = corner;
                    break;
                }
            }
            const u32 keepVertex = triangle.indices[keepCorner];
            const u32 dropA = triangle.indices[(keepCorner + 1u) % s_TriangleCornerCount];
            const u32 dropB = triangle.indices[(keepCorner + 2u) % s_TriangleCornerCount];
            const f32 keepDistance = distances[keepCorner];
            const f32 dropDistanceA = distances[(keepCorner + 1u) % s_TriangleCornerCount];
            const f32 dropDistanceB = distances[(keepCorner + 2u) % s_TriangleCornerCount];
            u32 splitA = 0u;
            u32 splitB = 0u;
            if(!SplitEdgeVertex(inOutVertices, edgeSplits, keepVertex, dropA, keepDistance, dropDistanceA, splitA))
                return false;
            if(!SplitEdgeVertex(inOutVertices, edgeSplits, keepVertex, dropB, keepDistance, dropDistanceB, splitB))
                return false;
            EmitTriangle(scratchKept, keepVertex, splitA, splitB);
            continue;
        }
        u32 dropCorner = 0u;
        for(u32 corner = 0u; corner < s_TriangleCornerCount; ++corner){
            if(!kept[corner]){
                dropCorner = corner;
                break;
            }
        }
        const u32 dropVertex = triangle.indices[dropCorner];
        const u32 keepA = triangle.indices[(dropCorner + 1u) % s_TriangleCornerCount];
        const u32 keepB = triangle.indices[(dropCorner + 2u) % s_TriangleCornerCount];
        const f32 dropDistance = distances[dropCorner];
        const f32 keepDistanceA = distances[(dropCorner + 1u) % s_TriangleCornerCount];
        const f32 keepDistanceB = distances[(dropCorner + 2u) % s_TriangleCornerCount];
        u32 splitA = 0u;
        u32 splitB = 0u;
        if(!SplitEdgeVertex(inOutVertices, edgeSplits, keepA, dropVertex, keepDistanceA, dropDistance, splitA))
            return false;
        if(!SplitEdgeVertex(inOutVertices, edgeSplits, keepB, dropVertex, keepDistanceB, dropDistance, splitB))
            return false;
        EmitTriangle(scratchKept, keepA, splitA, splitB);
        EmitTriangle(scratchKept, keepA, splitB, keepB);
        if(scratchKept.size() > s_MaxDeformTriangles)
            return false;
    }
    inOutTriangles = scratchKept;
    return true;
}

struct CutLoopEdge{
    u32 first = 0u;
    u32 second = 0u;
};

void CollectBoundaryEdges(
    ScratchArena& scratchArena,
    const CsgDeformTriangleVector<ScratchArena>& triangles,
    Vector<CutLoopEdge, ScratchArena>& outEdges
){
    outEdges.clear();
    if(triangles.empty())
        return;
    HashMap<u64, u32, EdgeSplitKeyHash, EqualTo<u64>, ScratchArena> edgeUses(0, EdgeSplitKeyHash(), EqualTo<u64>(), scratchArena);
    edgeUses.reserve(triangles.size() * s_EdgesPerTriangle + s_ReserveSlack);
    for(const CsgDeformTriangle& triangle : triangles){
        const u64 edges[s_TriangleCornerCount] = {
            EdgeKey(triangle.indices[0u], triangle.indices[1u]),
            EdgeKey(triangle.indices[1u], triangle.indices[2u]),
            EdgeKey(triangle.indices[2u], triangle.indices[0u]),
        };
        for(u32 corner = 0u; corner < s_TriangleCornerCount; ++corner){
            const auto found = edgeUses.find(edges[corner]);
            if(found == edgeUses.end())
                edgeUses.emplace(edges[corner], 1u);
            else
                found.value() = found.value() + 1u;
        }
    }
    outEdges.reserve(triangles.size());
    for(const CsgDeformTriangle& triangle : triangles){
        const u32 corners[s_TriangleCornerCount][2u] = {
            { triangle.indices[0u], triangle.indices[1u] },
            { triangle.indices[1u], triangle.indices[2u] },
            { triangle.indices[2u], triangle.indices[0u] },
        };
        for(u32 corner = 0u; corner < s_TriangleCornerCount; ++corner){
            const auto found = edgeUses.find(EdgeKey(corners[corner][0u], corners[corner][1u]));
            if(found != edgeUses.end() && found.value() == 1u)
                outEdges.push_back(CutLoopEdge{ corners[corner][0u], corners[corner][1u] });
        }
    }
    // Deterministic boundary order: traversal input matches emitted triangle order.
}

[[nodiscard]] bool OrderBoundaryLoop(
    ScratchArena& scratchArena,
    const Vector<CutLoopEdge, ScratchArena>& edges,
    Vector<u32, ScratchArena>& outLoop
){
    outLoop.clear();
    if(edges.empty())
        return true;
    // Boundary edges keep source-triangle winding, so consecutive edges meet
    // head-to-tail or tail-to-tail. Walk them undirected for one closed ring.
    Vector<CutLoopEdge, ScratchArena> remaining(scratchArena);
    remaining = edges;
    u32 start = remaining.front().first;
    u32 cursor = start;
    for(const CutLoopEdge& edge : remaining){
        if(edge.first < start)
            start = edge.first;
        if(edge.second < start)
            start = edge.second;
    }
    cursor = start;
    outLoop.reserve(edges.size());
    outLoop.push_back(cursor);
    {
        const auto seed = ::FindIf(
            remaining.begin(),
            remaining.end(),
            [cursor](const CutLoopEdge& edge){ return edge.first == cursor || edge.second == cursor; }
        );
        if(seed == remaining.end())
            return false;
        cursor = seed->first == cursor ? seed->second : seed->first;
        outLoop.push_back(cursor);
        remaining.erase(seed);
    }
    while(!remaining.empty()){
        bool advanced = false;
        for(auto it = remaining.begin(); it != remaining.end(); ++it){
            if(it->first == cursor || it->second == cursor){
                const u32 next = it->first == cursor ? it->second : it->first;
                remaining.erase(it);
                cursor = next;
                if(cursor == start)
                    break;
                outLoop.push_back(cursor);
                advanced = true;
                break;
            }
        }
        if(cursor == start || !advanced)
            break;
    }
    if(cursor != start || !remaining.empty())
        return false;
    for(usize vertexIndex = 1u; vertexIndex < outLoop.size(); ++vertexIndex){
        for(usize other = 0u; other < vertexIndex; ++other){
            if(outLoop[vertexIndex] == outLoop[other])
                return false;
        }
    }
    return true;
}

[[nodiscard]] bool CapNormal(
    const CsgDeformVertexVector<ScratchArena>& vertices,
    const Vector<u32, ScratchArena>& loop,
    Float4& outNormal
){
    outNormal = Float4(s_UpAxisX, s_UpAxisY, s_UpAxisZ, s_CapNormalW);
    if(loop.size() < s_MinLoopVertices)
        return false;
    // SIMD fan-area accumulation keeps edge subtract/cross/add on vector lanes.
    const SIMDVector originVec = LoadFloat(vertices[loop[0u]].position);
    SIMDVector areaVec = VectorZero();
    for(usize vertexIndex = 1u; vertexIndex + 1u < loop.size(); ++vertexIndex){
        const SIMDVector firstVec = LoadFloat(vertices[loop[vertexIndex]].position);
        const SIMDVector secondVec = LoadFloat(vertices[loop[vertexIndex + 1u]].position);
        const SIMDVector edgeA = VectorSubtract(firstVec, originVec);
        const SIMDVector edgeB = VectorSubtract(secondVec, originVec);
        areaVec = VectorAdd(areaVec, Vector3Cross(edgeA, edgeB));
    }
    const f32 areaLengthSq = VectorGetX(Vector3LengthSq(areaVec));
    if(!(areaLengthSq > s_LoopAreaEpsilonSq))
        return false;
    const SIMDVector normalized = Vector3Normalize(areaVec);
    outNormal = Float4(VectorGetX(normalized), VectorGetY(normalized), VectorGetZ(normalized), s_CapNormalW);
    return true;
}

[[nodiscard]] bool FillCapLoop(
    const Float4& loopNormal,
    CsgDeformVertexVector<ScratchArena>& inOutVertices,
    CsgDeformTriangleVector<ScratchArena>& inOutTriangles,
    const Vector<u32, ScratchArena>& loop,
    u32& outCapTriangles
){
    outCapTriangles = 0u;
    if(loop.size() < s_MinLoopVertices)
        return false;
    if(inOutVertices.size() + 1u > s_MaxDeformVertices)
        return false;
    // SIMD center accumulation keeps position/uv/color sums on vector lanes.
    SIMDVector centerPositionVec = VectorZero();
    SIMDVector centerUvVec = VectorZero();
    SIMDVector centerColorVec = VectorZero();
    for(const u32 vertexIndex : loop){
        NWB_ASSERT(vertexIndex < inOutVertices.size());
        const CsgDeformVertex& vertex = inOutVertices[vertexIndex];
        centerPositionVec = VectorAdd(centerPositionVec, LoadFloat(vertex.position));
        centerUvVec = VectorAdd(centerUvVec, LoadFloat(vertex.uv0));
        centerColorVec = VectorAdd(centerColorVec, LoadFloat(vertex.color));
    }
    const SIMDVector loopSizeVec = VectorReplicate(static_cast<f32>(loop.size()));
    const SIMDVector centerPositionAvg = VectorDivide(centerPositionVec, loopSizeVec);
    const SIMDVector centerUvAvg = VectorDivide(centerUvVec, loopSizeVec);
    const SIMDVector centerColorAvg = VectorDivide(centerColorVec, loopSizeVec);
    CsgDeformVertex center;
    StoreFloat(centerPositionAvg, center.position);
    center.normal = loopNormal;
    center.tangent = Float4(s_FallbackTangentX, s_FallbackTangentY, s_FallbackTangentZ, s_FallbackTangentW);
    StoreFloat(centerUvAvg, center.uv0);
    StoreFloat(centerColorAvg, center.color);
    if(!NormalizeDeformVertex(center))
        return false;
    const u32 centerIndex = static_cast<u32>(inOutVertices.size());
    inOutVertices.push_back(center);

    // One orientation rule for every loop: reverse kept winding so caps face the cut.
    for(usize vertexIndex = 0u; vertexIndex < loop.size(); ++vertexIndex){
        if(inOutTriangles.size() + 1u > s_MaxDeformTriangles)
            return false;
        const u32 first = loop[vertexIndex];
        const u32 second = loop[(vertexIndex + 1u) % loop.size()];
        EmitTriangle(inOutTriangles, centerIndex, second, first);
        ++outCapTriangles;
    }
    return true;
}

[[nodiscard]] bool FillCutCaps(
    ScratchArena& scratchArena,
    CsgDeformVertexVector<ScratchArena>& inOutVertices,
    CsgDeformTriangleVector<ScratchArena>& inOutTriangles,
    Vector<CutLoopEdge, ScratchArena>& scratchEdges,
    u32& outCapTriangles
){
    outCapTriangles = 0u;
    CollectBoundaryEdges(scratchArena, inOutTriangles, scratchEdges);
    if(scratchEdges.empty())
        return true;
    // Peel one closed loop at a time from the boundary set; leftover edges fail viability.
    Vector<CutLoopEdge, ScratchArena> remaining(scratchArena);
    remaining = scratchEdges;
    Vector<CutLoopEdge, ScratchArena> loopEdges(scratchArena);
    Vector<u32, ScratchArena> loop(scratchArena);
    while(!remaining.empty()){
        loopEdges.clear();
        loopEdges.push_back(remaining.back());
        remaining.pop_back();
        for(bool grown = true; grown;){
            grown = false;
            for(usize edgeIndex = 0u; edgeIndex < remaining.size();){
                const CutLoopEdge& candidate = remaining[edgeIndex];
                bool adjacent = false;
                for(const CutLoopEdge& owned : loopEdges){
                    if(candidate.first == owned.first || candidate.first == owned.second || candidate.second == owned.first || candidate.second == owned.second){
                        adjacent = true;
                        break;
                    }
                }
                if(adjacent){
                    loopEdges.push_back(candidate);
                    remaining.erase(remaining.begin() + static_cast<typename decltype(remaining)::difference_type>(edgeIndex));
                    grown = true;
                }
                else
                    ++edgeIndex;
            }
        }
        // Open boundary chains (e.g. cuts across an open sheet) carry no closable
        // volume loop; they stay viable cap-free. Only a closed but degenerate
        // loop fails viability.
        if(!OrderBoundaryLoop(scratchArena, loopEdges, loop))
            continue;
        Float4 loopNormal;
        if(!CapNormal(inOutVertices, loop, loopNormal))
            return false;
        u32 loopCaps = 0u;
        if(!FillCapLoop(loopNormal, inOutVertices, inOutTriangles, loop, loopCaps))
            return false;
        outCapTriangles += loopCaps;
    }
    return true;
}

struct DeformRebuildResult{
    CsgDeformViability viability;
    CsgDeformStats stats;
};

[[nodiscard]] bool RebuildSequentialCuts(
    ScratchArena& scratchArena,
    const CsgDeformVertex* inputVertices,
    const usize inputVertexCount,
    const CsgDeformTriangle* inputTriangles,
    const usize inputTriangleCount,
    const CsgDeformCutDesc* cuts,
    const usize cutCount,
    const CsgDeformBuildOptions& options,
    CsgDeformVertexVector<ScratchArena>& outVertices,
    CsgDeformTriangleVector<ScratchArena>& outTriangles,
    DeformRebuildResult& outResult
){
    outResult = DeformRebuildResult{};
    outVertices.clear();
    outTriangles.clear();
    outResult.stats.inputVertexCount = static_cast<u32>(inputVertexCount);
    outResult.stats.inputTriangleCount = static_cast<u32>(inputTriangleCount);
    if(!inputVertices || !inputTriangles || inputVertexCount == 0u || inputTriangleCount == 0u){
        outResult.viability.viable = false;
        outResult.viability.reason = CsgDeformViabilityReason::EmptyInput;
        return false;
    }
    if(inputVertexCount > s_MaxDeformVertices || inputTriangleCount > s_MaxDeformTriangles){
        outResult.viability.viable = false;
        outResult.viability.reason = CsgDeformViabilityReason::TooLarge;
        return false;
    }
    if(!ValidOptions(options)){
        outResult.viability.viable = false;
        outResult.viability.reason = CsgDeformViabilityReason::InvalidCutter;
        return false;
    }
    CsgDeformViabilityReason::Enum inputReason = CsgDeformViabilityReason::Ok;
    if(!FiniteInput(inputVertices, inputVertexCount, inputReason)){
        outResult.viability.viable = false;
        outResult.viability.reason = inputReason;
        return false;
    }
    if(!ValidTopology(inputVertices, inputVertexCount, inputTriangles, inputTriangleCount)){
        outResult.viability.viable = false;
        outResult.viability.reason = CsgDeformViabilityReason::InvalidTopology;
        return false;
    }

    const f32 epsilon = ShapeEpsilon(options);
    outVertices.reserve(inputVertexCount + cutCount * 16u);
    outTriangles.reserve(inputTriangleCount * 2u + cutCount * 16u);
    for(usize vertexIndex = 0u; vertexIndex < inputVertexCount; ++vertexIndex)
        outVertices.push_back(inputVertices[vertexIndex]);
    for(usize triangleIndex = 0u; triangleIndex < inputTriangleCount; ++triangleIndex)
        outTriangles.push_back(inputTriangles[triangleIndex]);

    CsgDeformTriangleVector<ScratchArena> scratchKept(scratchArena);
    Vector<f32, ScratchArena> scratchDistances(scratchArena);
    Vector<CutLoopEdge, ScratchArena> scratchEdges(scratchArena);

    u32 appliedCuts = 0u;
    u32 capTriangles = 0u;
    if(cuts){
        for(usize cutIndex = 0u; cutIndex < cutCount; ++cutIndex){
            const CsgDeformCutDesc& cut = cuts[cutIndex];
            if(!cut.active)
                continue;
            CsgDeformViabilityReason::Enum cutReason = CsgDeformViabilityReason::Ok;
            if(!ClipShell(scratchArena, cut.shape, epsilon, outVertices, outTriangles, scratchKept, scratchDistances, cutReason)){
                outResult.viability.viable = false;
                outResult.viability.reason = cutReason == CsgDeformViabilityReason::Ok
                    ? CsgDeformViabilityReason::NoKeptGeometry
                    : cutReason
                ;
                return false;
            }
            ++appliedCuts;
            if(outTriangles.empty() || outVertices.size() > s_MaxDeformVertices || outTriangles.size() > s_MaxDeformTriangles){
                outResult.viability.viable = false;
                outResult.viability.reason = outTriangles.empty()
                    ? CsgDeformViabilityReason::NoKeptGeometry
                    : CsgDeformViabilityReason::TooLarge
                ;
                return false;
            }
            if(options.fillCaps){
                u32 cutCaps = 0u;
                if(!FillCutCaps(scratchArena, outVertices, outTriangles, scratchEdges, cutCaps)){
                    outResult.viability.viable = false;
                    outResult.viability.reason = CsgDeformViabilityReason::CapLoopFailed;
                    return false;
                }
                capTriangles += cutCaps;
            }
        }
    }

    if(outVertices.empty() || outTriangles.empty()){
        outResult.viability.viable = false;
        outResult.viability.reason = CsgDeformViabilityReason::NoKeptGeometry;
        return false;
    }
    for(const CsgDeformVertex& vertex : outVertices){
        if(!FiniteVertex(vertex)){
            outResult.viability.viable = false;
            outResult.viability.reason = CsgDeformViabilityReason::NonFiniteInput;
            return false;
        }
    }
    outResult.viability.viable = true;
    outResult.viability.reason = CsgDeformViabilityReason::Ok;
    outResult.stats.outputVertexCount = static_cast<u32>(outVertices.size());
    outResult.stats.outputTriangleCount = static_cast<u32>(outTriangles.size());
    outResult.stats.appliedCutCount = appliedCuts;
    outResult.stats.capTriangleCount = capTriangles;
    return true;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


CsgDeformViability CheckCsgDeformCutsViability(
    Core::Alloc::ScratchArena& scratchArena,
    const CsgDeformVertex* inputVertices,
    const usize inputVertexCount,
    const CsgDeformTriangle* inputTriangles,
    const usize inputTriangleCount,
    const CsgDeformCutDesc* cuts,
    const usize cutCount,
    const CsgDeformBuildOptions& options
){
    CsgDeformVertexVector<Core::Alloc::ScratchArena> vertices(scratchArena);
    CsgDeformTriangleVector<Core::Alloc::ScratchArena> triangles(scratchArena);
    __hidden_csg_deform_edit::DeformRebuildResult result;
    if(!__hidden_csg_deform_edit::RebuildSequentialCuts(
        scratchArena,
        inputVertices,
        inputVertexCount,
        inputTriangles,
        inputTriangleCount,
        cuts,
        cutCount,
        options,
        vertices,
        triangles,
        result
    ))
        return result.viability;
    return result.viability;
}

bool PreviewCsgDeformCuts(
    Core::Alloc::ScratchArena& scratchArena,
    const CsgDeformVertex* inputVertices,
    const usize inputVertexCount,
    const CsgDeformTriangle* inputTriangles,
    const usize inputTriangleCount,
    const CsgDeformCutDesc* cuts,
    const usize cutCount,
    const CsgDeformBuildOptions& options,
    CsgDeformVertexVector<Core::Alloc::ScratchArena>& outVertices,
    CsgDeformTriangleVector<Core::Alloc::ScratchArena>& outTriangles,
    CsgDeformStats& outStats
){
    outStats = CsgDeformStats{};
    __hidden_csg_deform_edit::DeformRebuildResult result;
    if(!__hidden_csg_deform_edit::RebuildSequentialCuts(
        scratchArena,
        inputVertices,
        inputVertexCount,
        inputTriangles,
        inputTriangleCount,
        cuts,
        cutCount,
        options,
        outVertices,
        outTriangles,
        result
    )){
        outStats = result.stats;
        return false;
    }
    outStats = result.stats;
    return result.viability.viable;
}

bool CommitCsgDeformCuts(
    Core::Alloc::ScratchArena& scratchArena,
    Core::Alloc::GlobalArena& commitArena,
    const CsgDeformVertex* inputVertices,
    const usize inputVertexCount,
    const CsgDeformTriangle* inputTriangles,
    const usize inputTriangleCount,
    const CsgDeformCutDesc* cuts,
    const usize cutCount,
    const CsgDeformBuildOptions& options,
    CsgDeformVertexVector<Core::Alloc::GlobalArena>& outVertices,
    CsgDeformTriangleVector<Core::Alloc::GlobalArena>& outTriangles,
    CsgDeformStats& outStats
){
    static_cast<void>(commitArena);
    outStats = CsgDeformStats{};
    outVertices.clear();
    outTriangles.clear();
    // Commit reuses the preview entry point so both always observe the same
    // rebuild, viability classifier, and stats for identical inputs.
    CsgDeformVertexVector<Core::Alloc::ScratchArena> previewVertices(scratchArena);
    CsgDeformTriangleVector<Core::Alloc::ScratchArena> previewTriangles(scratchArena);
    CsgDeformStats previewStats{};
    if(!PreviewCsgDeformCuts(
        scratchArena,
        inputVertices,
        inputVertexCount,
        inputTriangles,
        inputTriangleCount,
        cuts,
        cutCount,
        options,
        previewVertices,
        previewTriangles,
        previewStats
    )){
        outStats = previewStats;
        return false;
    }
    outVertices.reserve(previewVertices.size());
    outTriangles.reserve(previewTriangles.size());
    for(const CsgDeformVertex& vertex : previewVertices)
        outVertices.push_back(vertex);
    for(const CsgDeformTriangle& triangle : previewTriangles)
        outTriangles.push_back(triangle);
    outStats = previewStats;
    return true;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

