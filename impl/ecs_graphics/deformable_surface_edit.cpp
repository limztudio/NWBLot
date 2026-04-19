// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "deformable_surface_edit.h"

#include "deformable_runtime_helpers.h"

#include <core/alloc/scratch.h>
#include <impl/assets_graphics/deformable_geometry_validation.h>

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace __hidden_deformable_surface_edit{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


using namespace DeformableRuntime;

static constexpr f32 s_WallRimTransferWeights[1] = { 1.0f };
static constexpr f32 s_WallInnerInpaintWeights[3] = { 0.25f, 0.5f, 0.25f };

struct HoleFrame{
    Vec3 center;
    Vec3 normal;
    Vec3 tangent;
    Vec3 bitangent;
};

struct EdgeRecord{
    u32 a = 0;
    u32 b = 0;
    u32 fullCount = 0;
    u32 removedCount = 0;
};

struct WallVertexFrame{
    Vec3 normal;
    Vec3 tangent;
};

struct SkinWeightSample{
    u16 joint = 0;
    f32 weight = 0.0f;
};

using MorphDeltaLookup = HashMap<
    u32,
    usize,
    Hasher<u32>,
    EqualTo<u32>,
    Core::Alloc::ScratchAllocator<Pair<const u32, usize>>
>;


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


[[nodiscard]] Vec3 BarycentricPoint(
    const DeformableRuntimeMeshInstance& instance,
    const u32 (&indices)[3],
    const f32 (&bary)[3])
{
    const Vec3 a = ToVec3(instance.restVertices[indices[0]].position);
    const Vec3 b = ToVec3(instance.restVertices[indices[1]].position);
    const Vec3 c = ToVec3(instance.restVertices[indices[2]].position);
    return Add(Add(Scale(a, bary[0]), Scale(b, bary[1])), Scale(c, bary[2]));
}

[[nodiscard]] Vec3 TriangleCentroid(const DeformableRuntimeMeshInstance& instance, const u32 (&indices)[3]){
    const Vec3 a = ToVec3(instance.restVertices[indices[0]].position);
    const Vec3 b = ToVec3(instance.restVertices[indices[1]].position);
    const Vec3 c = ToVec3(instance.restVertices[indices[2]].position);
    return Scale(Add(Add(a, b), c), 1.0f / 3.0f);
}

[[nodiscard]] u64 MakeEdgeKey(const u32 a, const u32 b){
    const u32 lo = a < b ? a : b;
    const u32 hi = a < b ? b : a;
    return (static_cast<u64>(lo) << 32u) | static_cast<u64>(hi);
}

template<typename EdgeMap>
void RegisterFullEdge(EdgeMap& edges, const u32 a, const u32 b){
    auto [it, inserted] = edges.emplace(MakeEdgeKey(a, b), EdgeRecord{});
    EdgeRecord& record = it.value();
    if(inserted){
        record.a = a;
        record.b = b;
    }
    ++record.fullCount;
}

template<typename EdgeMap>
[[nodiscard]] bool RegisterRemovedEdge(EdgeMap& edges, const u32 a, const u32 b){
    const auto found = edges.find(MakeEdgeKey(a, b));
    if(found == edges.end())
        return false;

    EdgeRecord& record = found.value();
    if(record.removedCount == 0u){
        record.a = a;
        record.b = b;
    }
    ++record.removedCount;
    return true;
}

template<typename VertexDegreeMap>
void IncrementVertexDegree(VertexDegreeMap& degrees, const u32 vertex){
    auto [it, inserted] = degrees.emplace(vertex, 0u);
    ++it.value();
}

[[nodiscard]] bool BuildHoleFrame(
    const DeformableRuntimeMeshInstance& instance,
    const u32 (&triangleIndices)[3],
    const f32 (&bary)[3],
    HoleFrame& outFrame)
{
    const Vec3 a = ToVec3(instance.restVertices[triangleIndices[0]].position);
    const Vec3 b = ToVec3(instance.restVertices[triangleIndices[1]].position);
    const Vec3 c = ToVec3(instance.restVertices[triangleIndices[2]].position);
    const Vec3 edge0 = Subtract(b, a);
    const Vec3 edge1 = Subtract(c, a);

    const Vec3 rawNormal = Cross(edge0, edge1);
    if(LengthSquared(rawNormal) <= s_FrameEpsilon)
        return false;

    outFrame.center = BarycentricPoint(instance, triangleIndices, bary);
    outFrame.normal = Normalize(rawNormal, Vec3{ 0.0f, 0.0f, 1.0f });

    const DeformableVertexRest& vertex0 = instance.restVertices[triangleIndices[0]];
    const DeformableVertexRest& vertex1 = instance.restVertices[triangleIndices[1]];
    const DeformableVertexRest& vertex2 = instance.restVertices[triangleIndices[2]];
    Vec3 tangent{
        (bary[0] * vertex0.tangent.x) + (bary[1] * vertex1.tangent.x) + (bary[2] * vertex2.tangent.x),
        (bary[0] * vertex0.tangent.y) + (bary[1] * vertex1.tangent.y) + (bary[2] * vertex2.tangent.y),
        (bary[0] * vertex0.tangent.z) + (bary[1] * vertex1.tangent.z) + (bary[2] * vertex2.tangent.z),
    };
    tangent = Subtract(tangent, Scale(outFrame.normal, Dot(tangent, outFrame.normal)));
    if(LengthSquared(tangent) <= s_FrameEpsilon)
        tangent = Subtract(edge0, Scale(outFrame.normal, Dot(edge0, outFrame.normal)));
    if(LengthSquared(tangent) <= s_FrameEpsilon)
        tangent = FallbackTangent(outFrame.normal);

    outFrame.tangent = Normalize(tangent, FallbackTangent(outFrame.normal));
    outFrame.bitangent = Normalize(Cross(outFrame.normal, outFrame.tangent), Vec3{ 0.0f, 1.0f, 0.0f });
    return LengthSquared(outFrame.normal) > s_FrameEpsilon
        && LengthSquared(outFrame.tangent) > s_FrameEpsilon
        && LengthSquared(outFrame.bitangent) > s_FrameEpsilon
        && DeformableValidation::AbsF32(Dot(outFrame.normal, outFrame.tangent)) <= 0.001f
    ;
}

[[nodiscard]] bool ValidateRuntimePayload(const DeformableRuntimeMeshInstance& instance){
    if(!instance.entity.valid() || !instance.handle.valid() || instance.restVertices.empty() || instance.indices.empty())
        return false;
    if(!ValidDeformableDisplacementDescriptor(instance.displacement))
        return false;
    return DeformableValidation::ValidRuntimePayloadArrays(
        instance.restVertices,
        instance.indices,
        instance.sourceTriangleCount,
        instance.skin,
        instance.sourceSamples,
        instance.morphs
    );
}

[[nodiscard]] bool MatchingSourceSample(const SourceSample& lhs, const SourceSample& rhs){
    return lhs.sourceTri == rhs.sourceTri
        && DeformableValidation::AbsF32(lhs.bary[0] - rhs.bary[0]) <= DeformableValidation::s_BarycentricSumEpsilon
        && DeformableValidation::AbsF32(lhs.bary[1] - rhs.bary[1]) <= DeformableValidation::s_BarycentricSumEpsilon
        && DeformableValidation::AbsF32(lhs.bary[2] - rhs.bary[2]) <= DeformableValidation::s_BarycentricSumEpsilon
    ;
}

[[nodiscard]] bool ValidateHitRestSample(
    const DeformableRuntimeMeshInstance& instance,
    const DeformablePosedHit& hit)
{
    SourceSample resolvedSample{};
    if(!ResolveDeformableRestSurfaceSample(instance, hit.triangle, hit.bary, resolvedSample))
        return false;

    return MatchingSourceSample(hit.restSample, resolvedSample);
}

[[nodiscard]] bool ValidatePosedHitFrame(const DeformablePosedHit& hit){
    const f32 normalLengthSquared = LengthSquared(ToVec3(hit.normal));
    return IsFinite(hit.distance)
        && hit.distance >= 0.0f
        && DeformableValidation::IsFiniteFloat3(hit.position)
        && DeformableValidation::IsFiniteFloat3(hit.normal)
        && DeformableValidation::NearlyUnitLengthSquared(normalLengthSquared)
    ;
}

[[nodiscard]] bool ValidateHitIdentity(
    const DeformableRuntimeMeshInstance& instance,
    const DeformablePosedHit& hit)
{
    if(!DeformableValidation::ValidLooseBarycentric(hit.bary))
        return false;
    if(!ValidatePosedHitFrame(hit))
        return false;
    if(hit.entity != instance.entity)
        return false;
    if(hit.runtimeMesh != instance.handle)
        return false;
    if(hit.editRevision != instance.editRevision)
        return false;
    if(!ValidateHitRestSample(instance, hit))
        return false;
    return true;
}

[[nodiscard]] bool ValidateHoleShape(const DeformableHoleEditParams& params){
    return IsFinite(params.radius)
        && IsFinite(params.ellipseRatio)
        && IsFinite(params.depth)
        && IsFinite(params.radius * params.ellipseRatio)
        && ActiveLength(params.radius)
        && ActiveLength(params.radius * params.ellipseRatio)
        && params.depth >= 0.0f
    ;
}

[[nodiscard]] bool ValidateParams(const DeformableRuntimeMeshInstance& instance, const DeformableHoleEditParams& params){
    return ValidateHitIdentity(instance, params.posedHit)
        && ValidateHoleShape(params)
        && instance.editRevision != Limit<u32>::s_Max
    ;
}

template<typename EdgeVector>
[[nodiscard]] f32 ProjectedSignedLoopArea(
    const DeformableRuntimeMeshInstance& instance,
    const HoleFrame& frame,
    const EdgeVector& orderedEdges)
{
    f32 signedArea = 0.0f;
    for(const EdgeRecord& edge : orderedEdges){
        const Vec3 aOffset = Subtract(ToVec3(instance.restVertices[edge.a].position), frame.center);
        const Vec3 bOffset = Subtract(ToVec3(instance.restVertices[edge.b].position), frame.center);
        const f32 ax = Dot(aOffset, frame.tangent);
        const f32 ay = Dot(aOffset, frame.bitangent);
        const f32 bx = Dot(bOffset, frame.tangent);
        const f32 by = Dot(bOffset, frame.bitangent);
        signedArea += (ax * by) - (bx * ay);
    }
    return signedArea * 0.5f;
}

template<typename EdgeVector>
void ReverseBoundaryLoop(EdgeVector& edges){
    if(edges.empty())
        return;

    usize left = 0u;
    usize right = edges.size() - 1u;
    while(left < right){
        EdgeRecord leftEdge = edges[left];
        EdgeRecord rightEdge = edges[right];
        const u32 leftA = leftEdge.a;
        const u32 rightA = rightEdge.a;
        leftEdge.a = leftEdge.b;
        leftEdge.b = leftA;
        rightEdge.a = rightEdge.b;
        rightEdge.b = rightA;
        edges[left] = rightEdge;
        edges[right] = leftEdge;
        ++left;
        --right;
    }
    if(left == right){
        const u32 a = edges[left].a;
        edges[left].a = edges[left].b;
        edges[left].b = a;
    }
}

template<typename EdgeVector>
void CanonicalizeBoundaryLoopStart(EdgeVector& edges){
    if(edges.empty())
        return;

    usize startEdgeIndex = 0u;
    for(usize edgeIndex = 1u; edgeIndex < edges.size(); ++edgeIndex){
        const EdgeRecord& edge = edges[edgeIndex];
        const EdgeRecord& startEdge = edges[startEdgeIndex];
        if(edge.a < startEdge.a || (edge.a == startEdge.a && edge.b < startEdge.b))
            startEdgeIndex = edgeIndex;
    }
    if(startEdgeIndex == 0u)
        return;

    EdgeVector rotatedEdges(edges.get_allocator());
    rotatedEdges.reserve(edges.size());
    for(usize edgeOffset = 0u; edgeOffset < edges.size(); ++edgeOffset)
        rotatedEdges.push_back(edges[(startEdgeIndex + edgeOffset) % edges.size()]);
    for(usize edgeIndex = 0u; edgeIndex < edges.size(); ++edgeIndex)
        edges[edgeIndex] = rotatedEdges[edgeIndex];
}

template<typename BoundaryEdgeVector, typename OrderedEdgeVector>
[[nodiscard]] bool BuildOrderedBoundaryLoop(
    const BoundaryEdgeVector& boundaryEdges,
    const DeformableRuntimeMeshInstance& instance,
    const HoleFrame& frame,
    OrderedEdgeVector& outOrderedEdges)
{
    outOrderedEdges.clear();
    if(boundaryEdges.empty())
        return false;

    Core::Alloc::ScratchArena<> scratchArena;
    Vector<u8, Core::Alloc::ScratchAllocator<u8>> visitedEdges{
        Core::Alloc::ScratchAllocator<u8>(scratchArena)
    };
    visitedEdges.resize(boundaryEdges.size(), 0u);

    const u32 startVertex = boundaryEdges[0].a;
    u32 currentVertex = startVertex;
    outOrderedEdges.reserve(boundaryEdges.size());
    while(outOrderedEdges.size() < boundaryEdges.size()){
        usize nextEdgeIndex = Limit<usize>::s_Max;
        EdgeRecord nextEdge;
        for(usize edgeIndex = 0; edgeIndex < boundaryEdges.size(); ++edgeIndex){
            if(visitedEdges[edgeIndex] != 0u)
                continue;

            const EdgeRecord& edge = boundaryEdges[edgeIndex];
            if(edge.a == currentVertex){
                nextEdgeIndex = edgeIndex;
                nextEdge = edge;
                break;
            }
            if(edge.b == currentVertex){
                nextEdgeIndex = edgeIndex;
                nextEdge = edge;
                nextEdge.a = edge.b;
                nextEdge.b = edge.a;
                break;
            }
        }

        if(nextEdgeIndex == Limit<usize>::s_Max)
            return false;

        visitedEdges[nextEdgeIndex] = 1u;
        outOrderedEdges.push_back(nextEdge);
        currentVertex = nextEdge.b;
        if(currentVertex == startVertex && outOrderedEdges.size() != boundaryEdges.size())
            return false;
    }

    if(currentVertex != startVertex)
        return false;

    const f32 signedArea = ProjectedSignedLoopArea(instance, frame, outOrderedEdges);
    if(!IsFinite(signedArea) || DeformableValidation::AbsF32(signedArea) <= s_FrameEpsilon)
        return false;
    if(signedArea < 0.0f)
        ReverseBoundaryLoop(outOrderedEdges);
    CanonicalizeBoundaryLoopStart(outOrderedEdges);
    return true;
}

[[nodiscard]] bool ActiveMorphDelta(const DeformableMorphDelta& delta){
    return ActiveWeight(delta.deltaPosition.x)
        || ActiveWeight(delta.deltaPosition.y)
        || ActiveWeight(delta.deltaPosition.z)
        || ActiveWeight(delta.deltaNormal.x)
        || ActiveWeight(delta.deltaNormal.y)
        || ActiveWeight(delta.deltaNormal.z)
        || ActiveWeight(delta.deltaTangent.x)
        || ActiveWeight(delta.deltaTangent.y)
        || ActiveWeight(delta.deltaTangent.z)
        || ActiveWeight(delta.deltaTangent.w)
    ;
}

void AccumulateMorphDelta(
    DeformableMorphDelta& target,
    const DeformableMorphDelta& source,
    const f32 weight)
{
    target.deltaPosition.x += source.deltaPosition.x * weight;
    target.deltaPosition.y += source.deltaPosition.y * weight;
    target.deltaPosition.z += source.deltaPosition.z * weight;
    target.deltaNormal.x += source.deltaNormal.x * weight;
    target.deltaNormal.y += source.deltaNormal.y * weight;
    target.deltaNormal.z += source.deltaNormal.z * weight;
    target.deltaTangent.x += source.deltaTangent.x * weight;
    target.deltaTangent.y += source.deltaTangent.y * weight;
    target.deltaTangent.z += source.deltaTangent.z * weight;
    target.deltaTangent.w += source.deltaTangent.w * weight;
}

[[nodiscard]] bool BuildMorphDeltaLookup(
    const DeformableMorph& morph,
    const usize sourceDeltaCount,
    MorphDeltaLookup& outLookup)
{
    outLookup.reserve(sourceDeltaCount);
    for(usize deltaIndex = 0u; deltaIndex < sourceDeltaCount; ++deltaIndex){
        const auto [it, inserted] = outLookup.emplace(morph.deltas[deltaIndex].vertexId, deltaIndex);
        (void)it;
        if(!inserted)
            return false;
    }
    return true;
}

[[nodiscard]] const DeformableMorphDelta* FindMorphDelta(
    const DeformableMorph& morph,
    const MorphDeltaLookup& lookup,
    const u32 vertex)
{
    const auto found = lookup.find(vertex);
    if(found == lookup.end())
        return nullptr;

    const usize deltaIndex = found.value();
    if(deltaIndex >= morph.deltas.size())
        return nullptr;
    return &morph.deltas[deltaIndex];
}

template<usize sourceCount>
[[nodiscard]] bool AppendBlendedMorphDelta(
    Vector<DeformableMorphDelta>& outDeltas,
    const DeformableMorph& sourceMorph,
    const MorphDeltaLookup& lookup,
    const u32 (&sourceVertices)[sourceCount],
    const f32 (&sourceWeights)[sourceCount],
    const u32 outputVertex)
{
    static_assert(sourceCount > 0u, "morph transfer requires source samples");
    DeformableMorphDelta blendedDelta{};
    blendedDelta.vertexId = outputVertex;
    bool hasDelta = false;
    for(usize sourceIndex = 0u; sourceIndex < sourceCount; ++sourceIndex){
        const f32 weight = sourceWeights[sourceIndex];
        if(!IsFinite(weight) || weight < 0.0f)
            return false;
        if(!ActiveWeight(weight))
            continue;

        const DeformableMorphDelta* sourceDelta = FindMorphDelta(
            sourceMorph,
            lookup,
            sourceVertices[sourceIndex]
        );
        if(!sourceDelta)
            continue;

        AccumulateMorphDelta(blendedDelta, *sourceDelta, weight);
        hasDelta = true;
    }

    if(!hasDelta || !ActiveMorphDelta(blendedDelta))
        return true;

    outDeltas.push_back(blendedDelta);
    return true;
}

template<typename EdgeVector, typename VertexVector>
[[nodiscard]] bool TransferWallMorphDeltas(
    Vector<DeformableMorph>& morphs,
    const EdgeVector& orderedBoundaryEdges,
    const VertexVector& rimVertices,
    const VertexVector& innerVertices)
{
    if(rimVertices.size() != innerVertices.size() || orderedBoundaryEdges.size() != rimVertices.size())
        return false;
    if(rimVertices.empty())
        return true;
    if(rimVertices.size() > Limit<usize>::s_Max / 2u)
        return false;

    const usize maxAddedDeltaCount = rimVertices.size() * 2u;
    Core::Alloc::ScratchArena<> scratchArena;
    for(DeformableMorph& morph : morphs){
        const usize sourceDeltaCount = morph.deltas.size();
        if(morph.deltas.size() > static_cast<usize>(Limit<u32>::s_Max)
            || maxAddedDeltaCount > static_cast<usize>(Limit<u32>::s_Max) - morph.deltas.size()
        )
            return false;

        MorphDeltaLookup lookup(
            0,
            Hasher<u32>(),
            EqualTo<u32>(),
            Core::Alloc::ScratchAllocator<Pair<const u32, usize>>(scratchArena)
        );
        if(!BuildMorphDeltaLookup(morph, sourceDeltaCount, lookup))
            return false;

        morph.deltas.reserve(morph.deltas.size() + maxAddedDeltaCount);
        for(usize edgeIndex = 0u; edgeIndex < orderedBoundaryEdges.size(); ++edgeIndex){
            const usize previousEdgeIndex = edgeIndex == 0u ? orderedBoundaryEdges.size() - 1u : edgeIndex - 1u;
            const EdgeRecord& edge = orderedBoundaryEdges[edgeIndex];
            const u32 rimSourceVertex[1] = { edge.a };
            const u32 innerSourceVertices[3] = {
                orderedBoundaryEdges[previousEdgeIndex].a,
                edge.a,
                edge.b,
            };

            if(!AppendBlendedMorphDelta(
                    morph.deltas,
                    morph,
                    lookup,
                    rimSourceVertex,
                    s_WallRimTransferWeights,
                    rimVertices[edgeIndex]
                )
                || !AppendBlendedMorphDelta(
                    morph.deltas,
                    morph,
                    lookup,
                    innerSourceVertices,
                    s_WallInnerInpaintWeights,
                    innerVertices[edgeIndex]
                )
            )
                return false;
        }
    }
    return true;
}

[[nodiscard]] bool AccumulateSkinWeight(
    SkinWeightSample (&samples)[12],
    u32& sampleCount,
    const u16 joint,
    const f32 weight)
{
    if(!IsFinite(weight) || weight < 0.0f)
        return false;
    if(!ActiveWeight(weight))
        return true;

    for(u32 sampleIndex = 0u; sampleIndex < sampleCount; ++sampleIndex){
        SkinWeightSample& sample = samples[sampleIndex];
        if(sample.joint != joint)
            continue;

        sample.weight += weight;
        return IsFinite(sample.weight);
    }

    if(sampleCount >= 12u)
        return false;

    samples[sampleCount].joint = joint;
    samples[sampleCount].weight = weight;
    ++sampleCount;
    return true;
}

[[nodiscard]] bool ExtractStrongestSkinWeight(
    SkinWeightSample (&samples)[12],
    const u32 sampleCount,
    SkinWeightSample& outSample)
{
    u32 bestIndex = Limit<u32>::s_Max;
    for(u32 sampleIndex = 0u; sampleIndex < sampleCount; ++sampleIndex){
        const SkinWeightSample& sample = samples[sampleIndex];
        if(!ActiveWeight(sample.weight))
            continue;
        if(bestIndex == Limit<u32>::s_Max || sample.weight > samples[bestIndex].weight)
            bestIndex = sampleIndex;
    }
    if(bestIndex == Limit<u32>::s_Max)
        return false;

    outSample = samples[bestIndex];
    samples[bestIndex].weight = 0.0f;
    return true;
}

template<usize sourceCount>
[[nodiscard]] bool BuildBlendedSkinInfluence(
    const Vector<SkinInfluence4>& skin,
    const u32 (&sourceVertices)[sourceCount],
    const f32 (&sourceWeights)[sourceCount],
    SkinInfluence4& outSkin)
{
    static_assert(sourceCount > 0u, "skin transfer requires source samples");
    outSkin = SkinInfluence4{};
    if(skin.empty())
        return false;

    SkinWeightSample samples[12] = {};
    u32 sampleCount = 0u;
    for(usize sourceIndex = 0u; sourceIndex < sourceCount; ++sourceIndex){
        const f32 sourceWeight = sourceWeights[sourceIndex];
        if(!IsFinite(sourceWeight) || sourceWeight < 0.0f)
            return false;
        if(!ActiveWeight(sourceWeight))
            continue;

        const u32 vertex = sourceVertices[sourceIndex];
        if(vertex >= skin.size())
            return false;

        const SkinInfluence4& sourceSkin = skin[vertex];
        if(!DeformableValidation::ValidSkinInfluence(sourceSkin))
            return false;

        for(u32 influenceIndex = 0u; influenceIndex < 4u; ++influenceIndex){
            if(!AccumulateSkinWeight(
                    samples,
                    sampleCount,
                    sourceSkin.joint[influenceIndex],
                    sourceSkin.weight[influenceIndex] * sourceWeight
                )
            )
                return false;
        }
    }

    f32 selectedWeightSum = 0.0f;
    for(u32 influenceIndex = 0u; influenceIndex < 4u; ++influenceIndex){
        SkinWeightSample selectedSample{};
        if(!ExtractStrongestSkinWeight(samples, sampleCount, selectedSample))
            break;

        outSkin.joint[influenceIndex] = selectedSample.joint;
        outSkin.weight[influenceIndex] = selectedSample.weight;
        selectedWeightSum += selectedSample.weight;
        if(!IsFinite(selectedWeightSum))
            return false;
    }

    if(!ActiveWeight(selectedWeightSum))
        return false;

    const f32 invSelectedWeightSum = 1.0f / selectedWeightSum;
    for(u32 influenceIndex = 0u; influenceIndex < 4u; ++influenceIndex)
        outSkin.weight[influenceIndex] *= invSelectedWeightSum;

    return DeformableValidation::ValidSkinInfluence(outSkin);
}

template<usize sourceCount>
[[nodiscard]] bool BuildBlendedVertexColor(
    const Vector<DeformableVertexRest>& vertices,
    const u32 (&sourceVertices)[sourceCount],
    const f32 (&sourceWeights)[sourceCount],
    Float4Data& outColor)
{
    static_assert(sourceCount > 0u, "color transfer requires source samples");
    outColor = Float4Data(0.0f, 0.0f, 0.0f, 0.0f);

    f32 weightSum = 0.0f;
    for(usize sourceIndex = 0u; sourceIndex < sourceCount; ++sourceIndex){
        const f32 sourceWeight = sourceWeights[sourceIndex];
        if(!IsFinite(sourceWeight) || sourceWeight < 0.0f)
            return false;
        if(!ActiveWeight(sourceWeight))
            continue;

        const u32 vertex = sourceVertices[sourceIndex];
        if(vertex >= vertices.size())
            return false;

        const Float4Data& sourceColor = vertices[vertex].color0;
        if(!DeformableValidation::IsFiniteFloat4(sourceColor))
            return false;

        outColor.x += sourceColor.x * sourceWeight;
        outColor.y += sourceColor.y * sourceWeight;
        outColor.z += sourceColor.z * sourceWeight;
        outColor.w += sourceColor.w * sourceWeight;
        weightSum += sourceWeight;
        if(!IsFinite(outColor.x)
            || !IsFinite(outColor.y)
            || !IsFinite(outColor.z)
            || !IsFinite(outColor.w)
            || !IsFinite(weightSum)
        )
            return false;
    }

    if(!ActiveWeight(weightSum))
        return false;

    const f32 invWeightSum = 1.0f / weightSum;
    outColor.x *= invWeightSum;
    outColor.y *= invWeightSum;
    outColor.z *= invWeightSum;
    outColor.w *= invWeightSum;
    return DeformableValidation::IsFiniteFloat4(outColor);
}

[[nodiscard]] SourceSample MakeFallbackSourceSample(const u32 sourceTriangle, const u32 corner){
    SourceSample sample{};
    sample.sourceTri = sourceTriangle;
    sample.bary[corner] = 1.0f;
    return sample;
}

template<typename AssignedSampleVector>
[[nodiscard]] bool AssignFallbackSourceSample(
    Vector<SourceSample>& sourceSamples,
    AssignedSampleVector& assignedSamples,
    const u32 vertex,
    const SourceSample& sample)
{
    if(vertex >= sourceSamples.size() || vertex >= assignedSamples.size())
        return false;

    if(assignedSamples[vertex] == 0u){
        sourceSamples[vertex] = sample;
        assignedSamples[vertex] = 1u;
        return true;
    }
    return MatchingSourceSample(sourceSamples[vertex], sample);
}

template<typename AssignedSampleVector>
void FillUnassignedFallbackSourceSamples(
    Vector<SourceSample>& sourceSamples,
    AssignedSampleVector& assignedSamples)
{
    const SourceSample fallbackSample = MakeFallbackSourceSample(0u, 0u);
    for(usize vertex = 0; vertex < assignedSamples.size(); ++vertex){
        if(assignedSamples[vertex] != 0u)
            continue;

        sourceSamples[vertex] = fallbackSample;
        assignedSamples[vertex] = 1u;
    }
}

[[nodiscard]] bool TriangleHasRecoverableSourceSamples(
    const DeformableRuntimeMeshInstance& instance,
    const u32 triangle,
    const u32 sourceTriangleCount,
    const Vector<SourceSample>& sourceSamples)
{
    if(sourceSamples.size() != instance.restVertices.size() || sourceTriangleCount == 0u)
        return false;

    u32 sourceVertices[3] = {};
    if(!DeformableRuntime::ValidateTriangleIndex(instance, triangle, sourceVertices))
        return false;

    const SourceSample& sample0 = sourceSamples[sourceVertices[0]];
    const SourceSample& sample1 = sourceSamples[sourceVertices[1]];
    const SourceSample& sample2 = sourceSamples[sourceVertices[2]];
    return DeformableValidation::ValidSourceSample(sample0, sourceTriangleCount)
        && DeformableValidation::ValidSourceSample(sample1, sourceTriangleCount)
        && DeformableValidation::ValidSourceSample(sample2, sourceTriangleCount)
        && sample0.sourceTri == sample1.sourceTri
        && sample0.sourceTri == sample2.sourceTri
    ;
}

[[nodiscard]] bool CopyMorphDeltasForDuplicateVertex(
    Vector<DeformableMorph>& morphs,
    const u32 sourceVertex,
    const u32 duplicateVertex)
{
    for(DeformableMorph& morph : morphs){
        const usize sourceDeltaCount = morph.deltas.size();
        for(usize deltaIndex = 0u; deltaIndex < sourceDeltaCount; ++deltaIndex){
            const DeformableMorphDelta& sourceDelta = morph.deltas[deltaIndex];
            if(sourceDelta.vertexId != sourceVertex)
                continue;
            if(morph.deltas.size() >= static_cast<usize>(Limit<u32>::s_Max))
                return false;

            DeformableMorphDelta duplicateDelta = sourceDelta;
            duplicateDelta.vertexId = duplicateVertex;
            morph.deltas.push_back(duplicateDelta);
            break;
        }
    }
    return true;
}

[[nodiscard]] bool AppendFallbackProvenanceVertex(
    Vector<DeformableVertexRest>& vertices,
    Vector<SkinInfluence4>& skin,
    Vector<SourceSample>& sourceSamples,
    Vector<DeformableMorph>& morphs,
    const u32 sourceVertex,
    const SourceSample& sample,
    u32& outVertex)
{
    if(sourceVertex >= vertices.size() || vertices.size() >= static_cast<usize>(Limit<u32>::s_Max))
        return false;
    if(!skin.empty() && sourceVertex >= skin.size())
        return false;
    if(sourceSamples.size() != vertices.size())
        return false;

    outVertex = static_cast<u32>(vertices.size());
    const DeformableVertexRest vertex = vertices[sourceVertex];
    vertices.push_back(vertex);
    if(!skin.empty())
        skin.push_back(skin[sourceVertex]);
    sourceSamples.push_back(sample);
    return CopyMorphDeltasForDuplicateVertex(morphs, sourceVertex, outVertex);
}

template<typename AssignedSampleVector>
[[nodiscard]] bool AppendKeptTriangleWithFallbackProvenance(
    const DeformableRuntimeMeshInstance& instance,
    const u32 sourceTriangleCount,
    const u32 triangle,
    Vector<DeformableVertexRest>& vertices,
    Vector<SkinInfluence4>& skin,
    Vector<SourceSample>& sourceSamples,
    Vector<DeformableMorph>& morphs,
    AssignedSampleVector& assignedSamples,
    Vector<u32>& outIndices,
    u32& inOutDuplicateVertexCount)
{
    if(triangle >= sourceTriangleCount)
        return false;

    u32 sourceVertices[3] = {};
    if(!DeformableRuntime::ValidateTriangleIndex(instance, triangle, sourceVertices))
        return false;

    for(u32 corner = 0u; corner < 3u; ++corner){
        const SourceSample sample = MakeFallbackSourceSample(triangle, corner);
        u32 outputVertex = sourceVertices[corner];
        if(!AssignFallbackSourceSample(sourceSamples, assignedSamples, outputVertex, sample)){
            if(!AppendFallbackProvenanceVertex(
                    vertices,
                    skin,
                    sourceSamples,
                    morphs,
                    outputVertex,
                    sample,
                    outputVertex
                )
            )
                return false;
            if(inOutDuplicateVertexCount == Limit<u32>::s_Max)
                return false;
            ++inOutDuplicateVertexCount;
        }
        outIndices.push_back(outputVertex);
    }
    return true;
}

[[nodiscard]] Vec3 ProjectedEdgeDirection(
    const Vector<DeformableVertexRest>& vertices,
    const HoleFrame& frame,
    const EdgeRecord& edge)
{
    Vec3 direction = Subtract(ToVec3(vertices[edge.b].position), ToVec3(vertices[edge.a].position));
    direction = Subtract(direction, Scale(frame.normal, Dot(direction, frame.normal)));
    return Normalize(direction, frame.tangent);
}

[[nodiscard]] bool BuildWallVertexFrame(
    const Vector<DeformableVertexRest>& vertices,
    const HoleFrame& frame,
    const EdgeRecord& previousEdge,
    const EdgeRecord& currentEdge,
    WallVertexFrame& outFrame)
{
    const Vec3 previousDirection = ProjectedEdgeDirection(vertices, frame, previousEdge);
    const Vec3 currentDirection = ProjectedEdgeDirection(vertices, frame, currentEdge);
    const Vec3 previousInward = Normalize(Cross(frame.normal, previousDirection), frame.bitangent);
    const Vec3 currentInward = Normalize(Cross(frame.normal, currentDirection), previousInward);
    Vec3 normal = Normalize(Add(previousInward, currentInward), currentInward);

    const Vec3 position = ToVec3(vertices[currentEdge.a].position);
    const Vec3 centerOffset = Subtract(frame.center, position);
    if(Dot(normal, centerOffset) < 0.0f)
        normal = Scale(normal, -1.0f);

    Vec3 tangent = Cross(frame.normal, normal);
    tangent = Subtract(tangent, Scale(normal, Dot(tangent, normal)));
    tangent = Normalize(tangent, currentDirection);

    outFrame.normal = normal;
    outFrame.tangent = tangent;
    return LengthSquared(outFrame.normal) > s_FrameEpsilon
        && LengthSquared(outFrame.tangent) > s_FrameEpsilon
        && DeformableValidation::AbsF32(Dot(outFrame.normal, outFrame.tangent)) <= 0.001f
    ;
}

[[nodiscard]] bool AppendWallVertex(
    Vector<DeformableVertexRest>& vertices,
    Vector<SkinInfluence4>& skin,
    Vector<SourceSample>& sourceSamples,
    const u32 sourceVertex,
    const SkinInfluence4* wallSkin,
    const SourceSample& wallSourceSample,
    const Float4Data& wallColor,
    const Vec3& position,
    const Vec3& normal,
    const Vec3& tangent,
    const f32 uvU,
    const f32 uvV,
    u32& outVertex)
{
    if(sourceVertex >= vertices.size() || vertices.size() >= static_cast<usize>(Limit<u32>::s_Max))
        return false;
    if(!skin.empty() && sourceVertex >= skin.size())
        return false;
    if(!skin.empty() && (!wallSkin || !DeformableValidation::ValidSkinInfluence(*wallSkin)))
        return false;
    if(!sourceSamples.empty() && sourceVertex >= sourceSamples.size())
        return false;

    DeformableVertexRest wallVertex = vertices[sourceVertex];
    wallVertex.position = ToFloat3(position);
    wallVertex.normal = ToFloat3(normal);
    wallVertex.tangent.x = tangent.x;
    wallVertex.tangent.y = tangent.y;
    wallVertex.tangent.z = tangent.z;
    wallVertex.tangent.w = TangentHandedness(wallVertex.tangent.w);
    wallVertex.uv0 = Float2Data(uvU, uvV);
    wallVertex.color0 = wallColor;
    if(!DeformableValidation::ValidRestVertexFrame(wallVertex))
        return false;

    outVertex = static_cast<u32>(vertices.size());
    vertices.push_back(wallVertex);
    if(!skin.empty())
        skin.push_back(*wallSkin);
    if(!sourceSamples.empty())
        sourceSamples.push_back(wallSourceSample);
    return true;
}

[[nodiscard]] bool TriangleInsideFootprint(
    const DeformableRuntimeMeshInstance& instance,
    const HoleFrame& frame,
    const f32 radiusX,
    const f32 radiusY,
    const u32 (&triangleIndices)[3])
{
    const Vec3 centroid = TriangleCentroid(instance, triangleIndices);
    const Vec3 offset = Subtract(centroid, frame.center);
    const f32 x = Dot(offset, frame.tangent) / radiusX;
    const f32 y = Dot(offset, frame.bitangent) / radiusY;
    return ((x * x) + (y * y)) <= 1.0f;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////





bool CommitDeformableRestSpaceHole(
    DeformableRuntimeMeshInstance& instance,
    const DeformableHoleEditParams& params,
    DeformableHoleEditResult* outResult)
{
    if(outResult)
        *outResult = DeformableHoleEditResult{};
    if(!__hidden_deformable_surface_edit::ValidateRuntimePayload(instance)
        || !__hidden_deformable_surface_edit::ValidateParams(instance, params)
    )
        return false;

    const usize triangleCount = instance.indices.size() / 3u;
    u32 hitTriangleIndices[3] = {};
    if(!DeformableRuntime::ValidateTriangleIndex(instance, params.posedHit.triangle, hitTriangleIndices))
        return false;

    f32 hitBary[3] = {};
    if(!DeformableValidation::NormalizeSourceBarycentric(params.posedHit.bary, hitBary))
        return false;

    __hidden_deformable_surface_edit::HoleFrame frame;
    if(!__hidden_deformable_surface_edit::BuildHoleFrame(instance, hitTriangleIndices, hitBary, frame))
        return false;

    SourceSample wallSourceSample{};
    if(!ResolveDeformableRestSurfaceSample(instance, params.posedHit.triangle, hitBary, wallSourceSample))
        return false;

    const f32 radiusX = params.radius;
    const f32 radiusY = params.radius * params.ellipseRatio;
    Core::Alloc::ScratchArena<> scratchArena;
    Vector<u8, Core::Alloc::ScratchAllocator<u8>> removeTriangle{
        Core::Alloc::ScratchAllocator<u8>(scratchArena)
    };
    removeTriangle.resize(triangleCount, 0u);

    u32 removedTriangleCount = 0;
    for(usize triangle = 0; triangle < triangleCount; ++triangle){
        u32 indices[3] = {};
        if(!DeformableRuntime::ValidateTriangleIndex(instance, static_cast<u32>(triangle), indices))
            return false;

        const bool selectedTriangle = triangle == static_cast<usize>(params.posedHit.triangle);
        if(selectedTriangle
            || __hidden_deformable_surface_edit::TriangleInsideFootprint(instance, frame, radiusX, radiusY, indices)
        ){
            removeTriangle[triangle] = 1u;
            ++removedTriangleCount;
        }
    }

    if(removedTriangleCount == 0u || removedTriangleCount >= triangleCount)
        return false;

    using EdgeRecord = __hidden_deformable_surface_edit::EdgeRecord;
    using EdgeRecordVector = Vector<EdgeRecord, Core::Alloc::ScratchAllocator<EdgeRecord>>;
    using EdgeRecordMap = HashMap<
        u64,
        EdgeRecord,
        Hasher<u64>,
        EqualTo<u64>,
        Core::Alloc::ScratchAllocator<Pair<const u64, EdgeRecord>>
    >;
    using VertexDegreeMap = HashMap<
        u32,
        u32,
        Hasher<u32>,
        EqualTo<u32>,
        Core::Alloc::ScratchAllocator<Pair<const u32, u32>>
    >;

    EdgeRecordMap edges(
        0,
        Hasher<u64>(),
        EqualTo<u64>(),
        Core::Alloc::ScratchAllocator<Pair<const u64, EdgeRecord>>(scratchArena)
    );
    edges.reserve(instance.indices.size());
    for(usize triangle = 0; triangle < triangleCount; ++triangle){
        u32 indices[3] = {};
        if(!DeformableRuntime::ValidateTriangleIndex(instance, static_cast<u32>(triangle), indices))
            return false;

        __hidden_deformable_surface_edit::RegisterFullEdge(edges, indices[0], indices[1]);
        __hidden_deformable_surface_edit::RegisterFullEdge(edges, indices[1], indices[2]);
        __hidden_deformable_surface_edit::RegisterFullEdge(edges, indices[2], indices[0]);
    }
    for(usize triangle = 0; triangle < triangleCount; ++triangle){
        if(removeTriangle[triangle] == 0u)
            continue;

        u32 indices[3] = {};
        if(!DeformableRuntime::ValidateTriangleIndex(instance, static_cast<u32>(triangle), indices))
            return false;

        if(!__hidden_deformable_surface_edit::RegisterRemovedEdge(edges, indices[0], indices[1])
            || !__hidden_deformable_surface_edit::RegisterRemovedEdge(edges, indices[1], indices[2])
            || !__hidden_deformable_surface_edit::RegisterRemovedEdge(edges, indices[2], indices[0])
        )
            return false;
    }

    EdgeRecordVector boundaryEdges{Core::Alloc::ScratchAllocator<EdgeRecord>(scratchArena)};
    boundaryEdges.reserve(removedTriangleCount * 3u);
    VertexDegreeMap boundaryDegrees(
        0,
        Hasher<u32>(),
        EqualTo<u32>(),
        Core::Alloc::ScratchAllocator<Pair<const u32, u32>>(scratchArena)
    );
    boundaryDegrees.reserve(removedTriangleCount * 3u);
    for(const auto& [edgeKey, edge] : edges){
        (void)edgeKey;
        if(edge.removedCount == 0u)
            continue;
        if(edge.removedCount > edge.fullCount || edge.fullCount > 2u)
            return false;
        if(edge.removedCount == 1u){
            if(edge.fullCount != 2u)
                return false;
            boundaryEdges.push_back(edge);
            __hidden_deformable_surface_edit::IncrementVertexDegree(boundaryDegrees, edge.a);
            __hidden_deformable_surface_edit::IncrementVertexDegree(boundaryDegrees, edge.b);
        }
    }
    if(boundaryEdges.empty())
        return false;
    for(const auto& [vertex, degree] : boundaryDegrees){
        (void)vertex;
        if(degree != 2u)
            return false;
    }
    EdgeRecordVector orderedBoundaryEdges{Core::Alloc::ScratchAllocator<EdgeRecord>(scratchArena)};
    if(!__hidden_deformable_surface_edit::BuildOrderedBoundaryLoop(boundaryEdges, instance, frame, orderedBoundaryEdges))
        return false;

    Vector<DeformableVertexRest> newRestVertices = instance.restVertices;
    Vector<SkinInfluence4> newSkin = instance.skin;
    Vector<SourceSample> newSourceSamples = instance.sourceSamples;
    Vector<DeformableMorph> newMorphs = instance.morphs;
    Vector<u32> newIndices;
    u32 newSourceTriangleCount = instance.sourceTriangleCount;
    const bool synthesizeFallbackSourceSamples = newSourceSamples.empty();
    const bool canMaterializeCurrentTriangleFallback =
        instance.editRevision == 0u
        && newSourceTriangleCount == static_cast<u32>(triangleCount)
    ;
    Vector<u8, Core::Alloc::ScratchAllocator<u8>> fallbackAssignedSamples{
        Core::Alloc::ScratchAllocator<u8>(scratchArena)
    };
    if(synthesizeFallbackSourceSamples){
        newSourceTriangleCount = instance.sourceTriangleCount;
        if(newSourceTriangleCount == 0u || newSourceTriangleCount != static_cast<u32>(triangleCount))
            return false;

        newSourceSamples.resize(instance.restVertices.size());
        fallbackAssignedSamples.resize(instance.restVertices.size(), 0u);
    }
    else if(canMaterializeCurrentTriangleFallback){
        fallbackAssignedSamples.resize(instance.restVertices.size(), 1u);
    }

    usize wallVertexCount = 0u;
    if(DeformableRuntime::ActiveLength(params.depth)){
        if(orderedBoundaryEdges.size() > Limit<usize>::s_Max / 6u)
            return false;

        wallVertexCount = orderedBoundaryEdges.size() * 2u;
        if(wallVertexCount > Limit<usize>::s_Max - instance.restVertices.size())
            return false;
        if(instance.restVertices.size() + wallVertexCount > static_cast<usize>(Limit<u32>::s_Max))
            return false;
    }

    const usize removedIndexCount = static_cast<usize>(removedTriangleCount) * 3u;
    const usize wallIndexCount = DeformableRuntime::ActiveLength(params.depth)
        ? orderedBoundaryEdges.size() * 6u
        : 0u
    ;
    const usize keptIndexCount = instance.indices.size() - removedIndexCount;
    if(wallIndexCount > Limit<usize>::s_Max - keptIndexCount
        || keptIndexCount + wallIndexCount > static_cast<usize>(Limit<u32>::s_Max)
    )
        return false;

    const usize fallbackDuplicateVertexCapacity =
        (synthesizeFallbackSourceSamples || canMaterializeCurrentTriangleFallback) ? keptIndexCount : 0u
    ;
    usize reservedVertexCount = instance.restVertices.size();
    if(wallVertexCount > Limit<usize>::s_Max - reservedVertexCount)
        return false;
    reservedVertexCount += wallVertexCount;
    if(fallbackDuplicateVertexCapacity > Limit<usize>::s_Max - reservedVertexCount)
        return false;
    reservedVertexCount += fallbackDuplicateVertexCapacity;
    if(reservedVertexCount > static_cast<usize>(Limit<u32>::s_Max))
        return false;
    if(reservedVertexCount != instance.restVertices.size()){
        newRestVertices.reserve(reservedVertexCount);
        if(!newSkin.empty())
            newSkin.reserve(reservedVertexCount);
        if(!newSourceSamples.empty())
            newSourceSamples.reserve(reservedVertexCount);
    }

    newIndices.reserve(keptIndexCount + wallIndexCount);
    u32 addedTriangleCount = 0;
    u32 addedVertexCount = 0;
    for(usize triangle = 0; triangle < triangleCount; ++triangle){
        if(removeTriangle[triangle] != 0u)
            continue;

        const bool materializeFallbackProvenance =
            synthesizeFallbackSourceSamples
            || (canMaterializeCurrentTriangleFallback
                && !__hidden_deformable_surface_edit::TriangleHasRecoverableSourceSamples(
                    instance,
                    static_cast<u32>(triangle),
                    newSourceTriangleCount,
                    newSourceSamples
                ))
        ;
        if(materializeFallbackProvenance){
            if(!__hidden_deformable_surface_edit::AppendKeptTriangleWithFallbackProvenance(
                    instance,
                    newSourceTriangleCount,
                    static_cast<u32>(triangle),
                    newRestVertices,
                    newSkin,
                    newSourceSamples,
                    newMorphs,
                    fallbackAssignedSamples,
                    newIndices,
                    addedVertexCount
                )
            )
                return false;
        }
        else{
            const usize indexBase = triangle * 3u;
            newIndices.push_back(instance.indices[indexBase + 0u]);
            newIndices.push_back(instance.indices[indexBase + 1u]);
            newIndices.push_back(instance.indices[indexBase + 2u]);
        }
    }
    if(synthesizeFallbackSourceSamples){
        __hidden_deformable_surface_edit::FillUnassignedFallbackSourceSamples(
            newSourceSamples,
            fallbackAssignedSamples
        );
    }

    if(DeformableRuntime::ActiveLength(params.depth)){
        const usize boundaryVertexCount = orderedBoundaryEdges.size();
        Vector<f32, Core::Alloc::ScratchAllocator<f32>> boundaryU{
            Core::Alloc::ScratchAllocator<f32>(scratchArena)
        };
        boundaryU.resize(boundaryVertexCount, 0.0f);

        f32 boundaryLength = 0.0f;
        for(usize edgeIndex = 0; edgeIndex < boundaryVertexCount; ++edgeIndex){
            boundaryU[edgeIndex] = boundaryLength;

            const __hidden_deformable_surface_edit::EdgeRecord& edge = orderedBoundaryEdges[edgeIndex];
            DeformableRuntime::Vec3 edgeDelta =
                DeformableRuntime::Subtract(
                    DeformableRuntime::ToVec3(newRestVertices[edge.b].position),
                    DeformableRuntime::ToVec3(newRestVertices[edge.a].position)
                )
            ;
            edgeDelta = DeformableRuntime::Subtract(
                edgeDelta,
                DeformableRuntime::Scale(frame.normal, DeformableRuntime::Dot(edgeDelta, frame.normal))
            )
            ;

            const f32 edgeLength = DeformableRuntime::Length(edgeDelta);
            if(!IsFinite(edgeLength) || !DeformableRuntime::ActiveLength(edgeLength))
                return false;
            boundaryLength += edgeLength;
            if(!IsFinite(boundaryLength))
                return false;
        }

        if(!DeformableRuntime::ActiveLength(boundaryLength))
            return false;

        Vector<u32, Core::Alloc::ScratchAllocator<u32>> rimVertices{
            Core::Alloc::ScratchAllocator<u32>(scratchArena)
        };
        Vector<u32, Core::Alloc::ScratchAllocator<u32>> innerVertices{
            Core::Alloc::ScratchAllocator<u32>(scratchArena)
        };
        rimVertices.resize(boundaryVertexCount, 0u);
        innerVertices.resize(boundaryVertexCount, 0u);

        for(usize edgeIndex = 0; edgeIndex < boundaryVertexCount; ++edgeIndex){
            const usize previousEdgeIndex = edgeIndex == 0u ? boundaryVertexCount - 1u : edgeIndex - 1u;
            const __hidden_deformable_surface_edit::EdgeRecord& edge = orderedBoundaryEdges[edgeIndex];

            __hidden_deformable_surface_edit::WallVertexFrame vertexFrame;
            if(!__hidden_deformable_surface_edit::BuildWallVertexFrame(
                    newRestVertices,
                    frame,
                    orderedBoundaryEdges[previousEdgeIndex],
                    edge,
                    vertexFrame
                )
            )
                return false;

            const DeformableRuntime::Vec3 rimPosition =
                DeformableRuntime::ToVec3(newRestVertices[edge.a].position)
            ;
            const DeformableRuntime::Vec3 innerPosition =
                DeformableRuntime::Subtract(
                    rimPosition,
                    DeformableRuntime::Scale(frame.normal, params.depth)
                )
            ;
            const f32 uvU = boundaryU[edgeIndex] / boundaryLength;

            const u32 rimAttributeVertex[1] = { edge.a };
            const u32 innerAttributeVertices[3] = {
                orderedBoundaryEdges[previousEdgeIndex].a,
                edge.a,
                edge.b,
            };
            Float4Data rimColor;
            Float4Data innerColor;
            if(!__hidden_deformable_surface_edit::BuildBlendedVertexColor(
                    newRestVertices,
                    rimAttributeVertex,
                    __hidden_deformable_surface_edit::s_WallRimTransferWeights,
                    rimColor
                )
                || !__hidden_deformable_surface_edit::BuildBlendedVertexColor(
                    newRestVertices,
                    innerAttributeVertices,
                    __hidden_deformable_surface_edit::s_WallInnerInpaintWeights,
                    innerColor
                )
            )
                return false;

            SkinInfluence4 rimSkin;
            SkinInfluence4 innerSkin;
            const SkinInfluence4* rimSkinPtr = nullptr;
            const SkinInfluence4* innerSkinPtr = nullptr;
            if(!newSkin.empty()){
                if(!__hidden_deformable_surface_edit::BuildBlendedSkinInfluence(
                        newSkin,
                        rimAttributeVertex,
                        __hidden_deformable_surface_edit::s_WallRimTransferWeights,
                        rimSkin
                    )
                    || !__hidden_deformable_surface_edit::BuildBlendedSkinInfluence(
                        newSkin,
                        innerAttributeVertices,
                        __hidden_deformable_surface_edit::s_WallInnerInpaintWeights,
                        innerSkin
                    )
                )
                    return false;

                rimSkinPtr = &rimSkin;
                innerSkinPtr = &innerSkin;
            }

            if(!__hidden_deformable_surface_edit::AppendWallVertex(
                    newRestVertices,
                    newSkin,
                    newSourceSamples,
                    edge.a,
                    rimSkinPtr,
                    wallSourceSample,
                    rimColor,
                    rimPosition,
                    vertexFrame.normal,
                    vertexFrame.tangent,
                    uvU,
                    0.0f,
                    rimVertices[edgeIndex]
                )
                || !__hidden_deformable_surface_edit::AppendWallVertex(
                    newRestVertices,
                    newSkin,
                    newSourceSamples,
                    edge.a,
                    innerSkinPtr,
                    wallSourceSample,
                    innerColor,
                    innerPosition,
                    vertexFrame.normal,
                    vertexFrame.tangent,
                    uvU,
                    1.0f,
                    innerVertices[edgeIndex]
                )
            )
                return false;

            addedVertexCount += 2u;
        }

        if(!__hidden_deformable_surface_edit::TransferWallMorphDeltas(
                newMorphs,
                orderedBoundaryEdges,
                rimVertices,
                innerVertices
            )
        )
            return false;

        for(usize edgeIndex = 0; edgeIndex < boundaryVertexCount; ++edgeIndex){
            const usize nextEdgeIndex = (edgeIndex + 1u) % boundaryVertexCount;
            const u32 rimA = rimVertices[edgeIndex];
            const u32 rimB = rimVertices[nextEdgeIndex];
            const u32 innerB = innerVertices[nextEdgeIndex];
            const u32 innerA = innerVertices[edgeIndex];

            newIndices.push_back(rimA);
            newIndices.push_back(rimB);
            newIndices.push_back(innerB);
            newIndices.push_back(rimA);
            newIndices.push_back(innerB);
            newIndices.push_back(innerA);
            addedTriangleCount += 2u;
        }
    }

    if(!DeformableValidation::ValidRuntimePayloadArrays(
            newRestVertices,
            newIndices,
            newSourceTriangleCount,
            newSkin,
            newSourceSamples,
            newMorphs
        )
    )
        return false;

    instance.restVertices = Move(newRestVertices);
    instance.indices = Move(newIndices);
    instance.sourceTriangleCount = newSourceTriangleCount;
    instance.skin = Move(newSkin);
    instance.sourceSamples = Move(newSourceSamples);
    instance.morphs = Move(newMorphs);
    ++instance.editRevision;
    instance.dirtyFlags = static_cast<RuntimeMeshDirtyFlags>(instance.dirtyFlags | RuntimeMeshDirtyFlag::All);

    if(outResult){
        outResult->removedTriangleCount = removedTriangleCount;
        outResult->addedVertexCount = addedVertexCount;
        outResult->addedTriangleCount = addedTriangleCount;
        outResult->editRevision = instance.editRevision;
    }
    return true;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

