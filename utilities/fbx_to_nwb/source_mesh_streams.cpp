// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "source_mesh_streams.h"

#include <impl/assets_mesh/cook_source_streams.h>

#include <core/alloc/scratch.h>
#include <core/common/log.h>
#include <core/mesh/tangent_frame_rebuild.h>

#include <global/simd/collision_triangle.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_FBX_TO_NWB_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


template<typename Value, typename Lookup>
[[nodiscard]] static Expected<u32> InternSourceValue(const Value& value, const AStringView streamName, UtilityVector<Value>& inOutStream, Lookup& inOutLookup){
    auto found = inOutLookup.find(value);
    if(found != inOutLookup.end())
        return found.value();

    if(inOutStream.size() >= static_cast<usize>(Impl::s_MeshMissingStreamIndex)){
        NWB_LOGGER_ERROR(NWB_TEXT("Failed to build mesh: {} stream has too many unique values"), ConvertString(streamName));
        return MakeUnexpected(Failure{});
    }

    const u32 index = static_cast<u32>(inOutStream.size());
    inOutStream.push_back(value);
    const auto inserted = inOutLookup.emplace(value, index);
    if(!inserted.second){
        NWB_ASSERT(false);
        return MakeUnexpected(Failure{});
    }
    return index;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


b8 MeshSkinInfluenceEqual::operator()(const MeshSkinInfluence& lhs, const MeshSkinInfluence& rhs)const noexcept{
    return EqualMeshSkinInfluence(lhs, rhs);
}

usize MeshSkinInfluenceHasher::operator()(const MeshSkinInfluence& value)const noexcept{
    return HashMeshSkinInfluence(value);
}

SourceMeshBuildContext::SourceMeshBuildContext(SourceMeshStreams& sourceMesh, Core::Alloc::ScratchArena& sourceScratchArena)
    : mesh(sourceMesh)
    , scratchArena(sourceScratchArena)
    , positions(sourceScratchArena)
    , normals(sourceScratchArena)
    , tangents(sourceScratchArena)
    , uv0(sourceScratchArena)
    , colors(sourceScratchArena)
    , skin(sourceScratchArena)
    , vertexRefs(sourceScratchArena)
{}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


void FbxSourceMeshStreams::ReserveSourceMeshStreams(const usize estimatedTriangleCorners, const b8 wantsSkinning, SourceMeshStreams& outMesh){
    outMesh.positions.reserve(estimatedTriangleCorners);
    outMesh.normals.reserve(estimatedTriangleCorners);
    outMesh.uv0.reserve(estimatedTriangleCorners);
    outMesh.colors.reserve(estimatedTriangleCorners);
    outMesh.tangents.reserve(estimatedTriangleCorners);
    outMesh.indices.reserve(estimatedTriangleCorners);
    outMesh.vertexRefs.reserve(estimatedTriangleCorners);
    if(wantsSkinning)
        outMesh.skin.reserve(estimatedTriangleCorners);
}

void FbxSourceMeshStreams::ReserveSourceMeshBuildContext(const usize estimatedTriangleCorners, const b8 wantsSkinning, SourceMeshBuildContext& inOutContext){
    inOutContext.positions.reserve(estimatedTriangleCorners);
    inOutContext.normals.reserve(estimatedTriangleCorners);
    inOutContext.tangents.reserve(estimatedTriangleCorners);
    inOutContext.uv0.reserve(estimatedTriangleCorners);
    inOutContext.colors.reserve(estimatedTriangleCorners);
    if(wantsSkinning)
        inOutContext.skin.reserve(estimatedTriangleCorners);
    inOutContext.vertexRefs.reserve(estimatedTriangleCorners);
}

b8 FbxSourceMeshStreams::DropSourceMeshTangents(Core::Alloc::ScratchArena&, SourceMeshStreams& inOutMesh){
    return DropSourceMeshTangentsStreams(inOutMesh.tangents, inOutMesh.vertexRefs, inOutMesh.indices);
}

b8 FbxSourceMeshStreams::EnsureTriangleIndexScratchCapacity(const ufbx_mesh& mesh, UtilityScratchVector<u32>& inOutTriangleIndices){
    return EnsureTriangleIndexScratchCapacityCount(mesh.max_face_triangles, inOutTriangleIndices);
}

Expected<SourceTangentReport> FbxSourceMeshStreams::GenerateSourceMeshTangents(const b8 usedDefaultUvs, Core::Alloc::ScratchArena& scratchArena, SourceMeshStreams& inOutMesh){
    using RebuildVertex = Core::Mesh::TangentFrameRebuildVertex;

    SourceTangentReport outTangentReport;
    NWB_ASSERT(!inOutMesh.vertexRefs.empty());
    NWB_ASSERT(!inOutMesh.indices.empty());
    NWB_ASSERT((inOutMesh.indices.size() % Impl::s_MeshletTriangleIndexCount) == 0u);

    UtilityScratchVector<RebuildVertex> rebuildVertices(scratchArena);
    rebuildVertices.reserve(inOutMesh.vertexRefs.size());
    for(const SourceVertexRef& ref : inOutMesh.vertexRefs){
        NWB_ASSERT(ref.position < inOutMesh.positions.size());
        NWB_ASSERT(ref.normal < inOutMesh.normals.size());
        NWB_ASSERT(ref.uv0 < inOutMesh.uv0.size());

        const Vec3& position = inOutMesh.positions[ref.position];
        const Vec3& normal = inOutMesh.normals[ref.normal];
        rebuildVertices.push_back(RebuildVertex{ Float4(position.x, position.y, position.z, 0.0f), Float4(normal.x, normal.y, normal.z, 0.0f), Float4(1.0f, 0.0f, 0.0f, 1.0f), inOutMesh.uv0[ref.uv0] });
    }

    UtilityScratchVector<u32> rebuildIndices(scratchArena);
    rebuildIndices.reserve(inOutMesh.indices.size());
    for(usize indexBase = 0u; indexBase < inOutMesh.indices.size(); indexBase += Impl::s_MeshletTriangleIndexCount){
        const u32 i0 = inOutMesh.indices[indexBase + 0u];
        const u32 i1 = inOutMesh.indices[indexBase + 1u];
        const u32 i2 = inOutMesh.indices[indexBase + 2u];
        NWB_ASSERT(i0 < rebuildVertices.size());
        NWB_ASSERT(i1 < rebuildVertices.size());
        NWB_ASSERT(i2 < rebuildVertices.size());
        if(i0 == i1 || i0 == i2 || i1 == i2)
            continue;

        const SIMDVectorSingle p0 = LoadFloat(rebuildVertices[i0].position);
        const SIMDVectorSingle p1 = LoadFloat(rebuildVertices[i1].position);
        const SIMDVectorSingle p2 = LoadFloat(rebuildVertices[i2].position);
        const SIMDVectorDouble areaNormal = BuildTriangleAreaNormal64(p0, p1, p2);
        const f64 areaLengthSquared = VectorDoubleGetX(TriangleAreaNormalLengthSquared(areaNormal));
        if(!IsFinite(areaLengthSquared) || areaLengthSquared <= static_cast<f64>(::s_FrameDirectionEpsilon))
            continue;

        rebuildIndices.push_back(i0);
        rebuildIndices.push_back(i1);
        rebuildIndices.push_back(i2);
    }
    if(rebuildIndices.empty()){
        NWB_LOGGER_ERROR(NWB_TEXT("Failed to build mesh: mesh has no valid triangles for tangent generation"));
        return MakeUnexpected(Failure{});
    }

    const auto rebuildResult = Core::Mesh::RebuildTangentFrames(scratchArena, rebuildIndices, rebuildVertices);
    if(!rebuildResult){
        NWB_LOGGER_ERROR(NWB_TEXT("Failed to build mesh: failed to generate source tangent stream"));
        return MakeUnexpected(Failure{});
    }

    Vec4IndexMap tangentLookup(scratchArena);
    tangentLookup.reserve(inOutMesh.vertexRefs.size());
    inOutMesh.tangents.clear();
    for(usize vertexRefIndex = 0u; vertexRefIndex < inOutMesh.vertexRefs.size(); ++vertexRefIndex){
        SourceVertexRef& ref = inOutMesh.vertexRefs[vertexRefIndex];
        const Vec3& storedNormal = inOutMesh.normals[ref.normal];
        const SIMDVectorSingle storedTangent = LoadFloat(rebuildVertices[vertexRefIndex].tangent);
        const SIMDVectorSingle normal = ::FrameNormalizeDirection(LoadFloat(storedNormal), VectorSet(0.0f, 0.0f, 1.0f, 0.0f));
        const SIMDVectorSingle tangent = ::FrameResolveTangent(normal, VectorSetW(storedTangent, 0.0f), ::FrameFallbackTangent(normal));
        if(!::FrameValidDirection(tangent)){
            NWB_LOGGER_ERROR(NWB_TEXT("Failed to build mesh: failed to resolve generated source tangent"));
            return MakeUnexpected(Failure{});
        }

        Vec4 generatedTangent;
        StoreFloat(VectorSelect(tangent, ::FrameTangentHandedness(storedTangent, s_SIMDOne), s_SIMDMaskW), generatedTangent);
        const auto tangentIndex = InternSourceValue(generatedTangent, Impl::s_VertexRefTangentName, inOutMesh.tangents, tangentLookup);
        if(!tangentIndex)
            return MakeUnexpected(tangentIndex.error());
        ref.tangent = *tangentIndex;
    }
    outTangentReport.degenerateUvTriangleCount = rebuildResult->degenerateUvTriangleCount;
    outTangentReport.fallbackTangentVertexCount = rebuildResult->fallbackTangentVertexCount;
    outTangentReport.mode = ResolveGeneratedSourceTangentMode(usedDefaultUvs, rebuildResult->degenerateUvTriangleCount, rebuildResult->fallbackTangentVertexCount);
    return outTangentReport;
}

Expected<u32> FbxSourceMeshStreams::InternSourceCorner(const SourceTriangleCorner& corner, const b8 wantsSkinning, SourceMeshBuildContext& inOutContext){
    SourceVertexRef ref;
    const auto positionIndex = InternSourceValue(corner.position, Impl::s_VertexRefPositionName, inOutContext.mesh.positions, inOutContext.positions);
    if(!positionIndex)
        return MakeUnexpected(positionIndex.error());
    ref.position = *positionIndex;
    const auto normalIndex = InternSourceValue(corner.normal, Impl::s_VertexRefNormalName, inOutContext.mesh.normals, inOutContext.normals);
    if(!normalIndex)
        return MakeUnexpected(normalIndex.error());
    ref.normal = *normalIndex;
    if(corner.hasTangent){
        const auto tangentIndex = InternSourceValue(corner.tangent, Impl::s_VertexRefTangentName, inOutContext.mesh.tangents, inOutContext.tangents);
        if(!tangentIndex)
            return MakeUnexpected(tangentIndex.error());
        ref.tangent = *tangentIndex;
    }
    const auto uv0Index = InternSourceValue(corner.uv0, Impl::s_MeshUv0FieldNameView, inOutContext.mesh.uv0, inOutContext.uv0);
    if(!uv0Index)
        return MakeUnexpected(uv0Index.error());
    ref.uv0 = *uv0Index;
    const auto colorIndex = InternSourceValue(corner.color, Impl::s_VertexRefColorName, inOutContext.mesh.colors, inOutContext.colors);
    if(!colorIndex)
        return MakeUnexpected(colorIndex.error());
    ref.color = *colorIndex;
    if(wantsSkinning){
        const auto skinIndex = InternSourceValue(corner.skin, s_SkinAssetTypeText, inOutContext.mesh.skin, inOutContext.skin);
        if(!skinIndex)
            return MakeUnexpected(skinIndex.error());
        ref.skin = *skinIndex;
    }

    return InternSourceValue(ref, s_SourceVertexRefLabel, inOutContext.mesh.vertexRefs, inOutContext.vertexRefs);
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_FBX_TO_NWB_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
