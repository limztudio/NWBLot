// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "deform_wall_builder.h"

#include "deform_cutter_field.h"
#include "deform_validator.h"

#include <global/scope_exit.h>
#include <global/simd/vector.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


using ScratchArena = Core::Alloc::ScratchArena;


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


SIMDVectorSingle CsgDeformWallBuilder::MixAttributeVec(SIMDVectorSingle firstVec, SIMDVectorSingle secondVec, SIMDVectorSingle blendVec, SIMDVectorSingle otherVec)noexcept{
    return VectorAdd(VectorMultiply(firstVec, blendVec), VectorMultiply(secondVec, otherVec));
}

SIMDVectorSingle CsgDeformWallBuilder::KeepWVec(SIMDVectorSingle normalizedVec, SIMDVectorSingle sourceVec)noexcept{
    return VectorSelect(normalizedVec, sourceVec, s_SIMDMaskW);
}

SIMDVectorSingle CsgDeformWallBuilder::TangentHandednessVec(SIMDVectorSingle normalizedTangent, SIMDVectorSingle tangentVec)noexcept{
    const SIMDVectorSingle wNegative = VectorLess(VectorSplatW(tangentVec), VectorZero());
    const SIMDVectorSingle sign = VectorSelect(VectorReplicate(s_OneWeight), VectorReplicate(s_NegativeOne), wNegative);
    return VectorSelect(normalizedTangent, sign, s_SIMDMaskW);
}

SIMDVectorSingle CsgDeformWallBuilder::UpAxisVec()noexcept{
    return VectorSet(0.0f, 1.0f, 0.0f, 0.0f);
}

void CsgDeformWallBuilder::NormalizeDeformVertex(SIMDVectorSingle& normal, SIMDVectorSingle& tangent)noexcept{
    // Preview and commit share the same degenerate fallback and tangent handedness.
    const SIMDVectorSingle epsilonSq = VectorReplicate(s_NormalizeEpsilonSq);
    normal = Vector4Greater(Vector3LengthSq(normal), epsilonSq) ? KeepWVec(Vector3Normalize(normal), normal) : VectorSelect(normal, UpAxisVec(), s_SIMDMask3);
    tangent = Vector4Greater(Vector3LengthSq(tangent), epsilonSq) ? TangentHandednessVec(Vector3Normalize(tangent), tangent) : VectorSet(1.0f, 0.0f, 0.0f, 1.0f);
}

Expected<u32> CsgDeformWallBuilder::SplitEdgeVertex(const u32 first, const u32 second, const f32 firstDistance, const f32 secondDistance, CsgDeformVertexVector<ScratchArena>& vertices, CsgDeformEdgeSplitMap& edgeSplits){
    if(vertices.size() + 1u > s_MaxDeformVertices)
        return MakeUnexpected(Failure{});
    const u64 edge = CsgDeformEdgeKey(first, second);
    const auto found = edgeSplits.find(edge);
    if(found != edgeSplits.end()){
        return found.value();
    }
    const SIMDVectorSingle denominator = VectorSubtract(VectorReplicate(firstDistance), VectorReplicate(secondDistance));
    if(!CsgDeformValidator::FiniteFloat(VectorGetX(denominator)) || VectorGetX(VectorAbs(denominator)) < s_SplitDenominatorEpsilon)
        return MakeUnexpected(Failure{});
    // A zero secondDistance selects the second endpoint.
    const SIMDVectorSingle blend = VectorSaturate(VectorAbs(VectorDivide(VectorReplicate(secondDistance), denominator)));
    const SIMDVectorSingle other = VectorSubtract(s_SIMDOne, blend);
    const CsgDeformVertex& firstVertex = vertices[first];
    const CsgDeformVertex& secondVertex = vertices[second];
    const SIMDVectorSingle position = MixAttributeVec(LoadFloat(firstVertex.position), LoadFloat(secondVertex.position), blend, other);
    SIMDVectorSingle normal = MixAttributeVec(LoadFloat(firstVertex.normal), LoadFloat(secondVertex.normal), blend, other);
    SIMDVectorSingle tangent = MixAttributeVec(LoadFloat(firstVertex.tangent), LoadFloat(secondVertex.tangent), blend, other);
    const SIMDVectorSingle uv = MixAttributeVec(LoadFloat(firstVertex.uv0), LoadFloat(secondVertex.uv0), blend, other);
    const SIMDVectorSingle color = MixAttributeVec(LoadFloat(firstVertex.color), LoadFloat(secondVertex.color), blend, other);
    NormalizeDeformVertex(normal, tangent);
    if(!CsgDeformValidator::FiniteVertexVec(position, normal, tangent, uv, color))
        return MakeUnexpected(Failure{});
    CsgDeformVertex mixed;
    StoreFloat(position, mixed.position);
    StoreFloat(normal, mixed.normal);
    StoreFloat(tangent, mixed.tangent);
    StoreFloat(uv, mixed.uv0);
    StoreFloat(color, mixed.color);
    const u32 created = static_cast<u32>(vertices.size());
    vertices.push_back(mixed);
    ScopeExit discardVertex([&]()noexcept{ vertices.pop_back(); });
    if(!edgeSplits.emplace(edge, created).second){
        NWB_ASSERT(false);
        return MakeUnexpected(Failure{});
    }
    discardVertex.release();
    return created;
}

void CsgDeformWallBuilder::EmitTriangle(CsgDeformTriangleVector<ScratchArena>& triangles, const u32 first, const u32 second, const u32 third){
    CsgDeformTriangle triangle;
    triangle.indices[0u] = first;
    triangle.indices[1u] = second;
    triangle.indices[2u] = third;
    triangles.push_back(triangle);
}

// Keep side is distance >= 0; caller snaps |distance| <= epsilon to zero first.
Expected<void, CsgDeformViabilityReason::Enum> CsgDeformWallBuilder::ClipShell(
    ScratchArena& scratchArena,
    const CsgDeformShape& shape,
    const f32 epsilon,
    CsgDeformTriangleVector<ScratchArena>& scratchKept,
    Vector<f32, ScratchArena>& scratchDistances,
    CsgDeformVertexVector<ScratchArena>& inOutVertices,
    CsgDeformTriangleVector<ScratchArena>& inOutTriangles
){
    const auto shapeDistances = CsgDeformCutterField::ShapeDistances(shape, inOutVertices, epsilon, scratchDistances);
    if(!shapeDistances)
        return MakeUnexpected(shapeDistances.error());

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
        const f32 distances[s_TriangleCornerCount] = { scratchDistances[triangle.indices[0u]], scratchDistances[triangle.indices[1u]], scratchDistances[triangle.indices[2u]], };
        const bool kept[s_TriangleCornerCount] = { distances[0u] >= s_KeepDistanceZero, distances[1u] >= s_KeepDistanceZero, distances[2u] >= s_KeepDistanceZero };
        const u32 keepCount = (kept[0u] ? 1u : 0u) + (kept[1u] ? 1u : 0u) + (kept[2u] ? 1u : 0u);
        if(keepCount == s_TriangleCornerCount){
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
            const auto splitA = CsgDeformWallBuilder::SplitEdgeVertex(keepVertex, dropA, keepDistance, dropDistanceA, inOutVertices, edgeSplits);
            if(!splitA)
                return MakeUnexpected(CsgDeformViabilityReason::NoKeptGeometry);
            const auto splitB = CsgDeformWallBuilder::SplitEdgeVertex(keepVertex, dropB, keepDistance, dropDistanceB, inOutVertices, edgeSplits);
            if(!splitB)
                return MakeUnexpected(CsgDeformViabilityReason::NoKeptGeometry);
            CsgDeformWallBuilder::EmitTriangle(scratchKept, keepVertex, *splitA, *splitB);
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
        const auto splitA = CsgDeformWallBuilder::SplitEdgeVertex(keepA, dropVertex, keepDistanceA, dropDistance, inOutVertices, edgeSplits);
        if(!splitA)
            return MakeUnexpected(CsgDeformViabilityReason::NoKeptGeometry);
        const auto splitB = CsgDeformWallBuilder::SplitEdgeVertex(keepB, dropVertex, keepDistanceB, dropDistance, inOutVertices, edgeSplits);
        if(!splitB)
            return MakeUnexpected(CsgDeformViabilityReason::NoKeptGeometry);
        CsgDeformWallBuilder::EmitTriangle(scratchKept, keepA, *splitA, *splitB);
        CsgDeformWallBuilder::EmitTriangle(scratchKept, keepA, *splitB, keepB);
        if(scratchKept.size() > s_MaxDeformTriangles)
            return MakeUnexpected(CsgDeformViabilityReason::NoKeptGeometry);
    }
    inOutTriangles = scratchKept;
    return {};
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

