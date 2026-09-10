// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "deform_wall_builder.h"

#include "deform_cutter_field.h"
#include "deform_validator.h"

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

NWB_IMPL_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

using ScratchArena = Core::Alloc::ScratchArena;

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

CsgDeformVertex CsgDeformWallBuilder::MixVertices(const CsgDeformVertex& first, const CsgDeformVertex& second, const f32 firstWeight){
    // SIMD blend keeps positions/normals/tangents/uvs/colors on vector lanes.
    // Op order matches the scalar form (first*blend + second*(1-blend)) lane-wise.
    const f32 blend = CsgDeformValidator::SaturateFloat(firstWeight);
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

bool CsgDeformWallBuilder::NormalizeDeformVertex(CsgDeformVertex& vertex){
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
        vertex.normal.x = s_UpAxis.x;
        vertex.normal.y = s_UpAxis.y;
        vertex.normal.z = s_UpAxis.z;
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
        vertex.tangent = s_FallbackTangent;
    }
    return CsgDeformValidator::FiniteVertex(vertex);
}

bool CsgDeformWallBuilder::SplitEdgeVertex(
    CsgDeformVertexVector<ScratchArena>& vertices,
    CsgDeformEdgeSplitMap& edgeSplits,
    const u32 first,
    const u32 second,
    const f32 firstDistance,
    const f32 secondDistance,
    u32& outVertex
){
    outVertex = 0u;
    if(vertices.size() + 1u > s_MaxDeformVertices)
        return false;
    const u64 edge = CsgDeformEdgeKey(first, second);
    const auto found = edgeSplits.find(edge);
    if(found != edgeSplits.end()){
        outVertex = found.value();
        return true;
    }
    const f32 denominator = firstDistance - secondDistance;
    if(!CsgDeformValidator::FiniteFloat(denominator) || Abs(denominator) < s_SplitDenominatorEpsilon)
        return false;
    // firstWeight lands on second when secondDistance is zero.
    const f32 firstWeight = CsgDeformValidator::SaturateFloat(Abs(secondDistance / denominator));
    CsgDeformVertex mixed = CsgDeformWallBuilder::MixVertices(vertices[first], vertices[second], firstWeight);
    if(!CsgDeformWallBuilder::NormalizeDeformVertex(mixed))
        return false;
    const u32 created = static_cast<u32>(vertices.size());
    vertices.push_back(mixed);
    edgeSplits.emplace(edge, created);
    outVertex = created;
    return true;
}

void CsgDeformWallBuilder::EmitTriangle(
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
bool CsgDeformWallBuilder::ClipShell(
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
    if(!CsgDeformCutterField::ShapeDistances(shape, inOutVertices, epsilon, scratchDistances, outReason))
        return false;

    CsgDeformEdgeSplitMap edgeSplits(0, CsgDeformEdgeSplitKeyHash(), EqualTo<u64>(), scratchArena);
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
            CsgDeformWallBuilder::EmitTriangle(scratchKept, triangle.indices[0u], triangle.indices[1u], triangle.indices[2u]);
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
            if(!CsgDeformWallBuilder::SplitEdgeVertex(inOutVertices, edgeSplits, keepVertex, dropA, keepDistance, dropDistanceA, splitA))
                return false;
            if(!CsgDeformWallBuilder::SplitEdgeVertex(inOutVertices, edgeSplits, keepVertex, dropB, keepDistance, dropDistanceB, splitB))
                return false;
            CsgDeformWallBuilder::EmitTriangle(scratchKept, keepVertex, splitA, splitB);
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
        if(!CsgDeformWallBuilder::SplitEdgeVertex(inOutVertices, edgeSplits, keepA, dropVertex, keepDistanceA, dropDistance, splitA))
            return false;
        if(!CsgDeformWallBuilder::SplitEdgeVertex(inOutVertices, edgeSplits, keepB, dropVertex, keepDistanceB, dropDistance, splitB))
            return false;
        CsgDeformWallBuilder::EmitTriangle(scratchKept, keepA, splitA, splitB);
        CsgDeformWallBuilder::EmitTriangle(scratchKept, keepA, splitB, keepB);
        if(scratchKept.size() > s_MaxDeformTriangles)
            return false;
    }
    inOutTriangles = scratchKept;
    return true;
}

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

NWB_IMPL_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

