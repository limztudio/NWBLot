// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "source_mesh_streams.h"

#include <core/alloc/scratch.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_FBX_TO_NWB_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


void FbxSourceMeshStreams::ReserveSourceMeshStreams(
    SourceMeshStreams& mesh,
    const usize estimatedTriangleCorners,
    const bool wantsSkinning
){
    mesh.positions.reserve(estimatedTriangleCorners);
    mesh.normals.reserve(estimatedTriangleCorners);
    mesh.uv0.reserve(estimatedTriangleCorners);
    mesh.colors.reserve(estimatedTriangleCorners);
    mesh.tangents.reserve(estimatedTriangleCorners);
    mesh.indices.reserve(estimatedTriangleCorners);
    mesh.vertexRefs.reserve(estimatedTriangleCorners);
    if(wantsSkinning)
        mesh.skin.reserve(estimatedTriangleCorners);
}


void FbxSourceMeshStreams::ReserveSourceMeshBuildContext(
    SourceMeshBuildContext& context,
    const usize estimatedTriangleCorners,
    const bool wantsSkinning
){
    context.positions.reserve(estimatedTriangleCorners);
    context.normals.reserve(estimatedTriangleCorners);
    context.uv0.reserve(estimatedTriangleCorners);
    context.colors.reserve(estimatedTriangleCorners);
    context.tangents.reserve(estimatedTriangleCorners);
    context.vertexRefs.reserve(estimatedTriangleCorners);
    if(wantsSkinning)
        context.skin.reserve(estimatedTriangleCorners);
}


bool FbxSourceMeshStreams::SourceMeshHasCompleteTangents(const SourceMeshStreams& mesh){
    if(mesh.vertexRefs.empty() || mesh.tangents.empty())
        return false;

    for(const SourceVertexRef& ref : mesh.vertexRefs){
        if(ref.tangent >= mesh.tangents.size())
            return false;
    }
    return true;
}


void FbxSourceMeshStreams::DropSourceMeshTangents(SourceMeshStreams& mesh){
    mesh.tangents.clear();

    SourceVertexRefIndexMap compactLookup;
    compactLookup.reserve(mesh.vertexRefs.size());
    UtilityVector<SourceVertexRef> compactVertexRefs;
    compactVertexRefs.reserve(mesh.vertexRefs.size());
    UtilityVector<u32> vertexRefRemap;
    vertexRefRemap.reserve(mesh.vertexRefs.size());

    for(SourceVertexRef ref : mesh.vertexRefs){
        ref.tangent = s_MissingSourceStreamIndex;

        auto found = compactLookup.find(ref);
        if(found != compactLookup.end()){
            vertexRefRemap.push_back(found.value());
            continue;
        }

        const u32 compactIndex = static_cast<u32>(compactVertexRefs.size());
        compactVertexRefs.push_back(ref);
        compactLookup.emplace(ref, compactIndex);
        vertexRefRemap.push_back(compactIndex);
    }

    for(u32& index : mesh.indices){
        NWB_ASSERT(index < vertexRefRemap.size());
        index = vertexRefRemap[index];
    }
    mesh.vertexRefs = Move(compactVertexRefs);
}


bool FbxSourceMeshStreams::EnsureTriangleIndexScratchCapacity(
    const ufbx_mesh& mesh,
    UtilityVector<u32>& inOutTriangleIndices
){
    if(mesh.max_face_triangles > Limit<usize>::s_Max / s_TriangleIndexCount){
        NWB_LOGGER_ERROR(NWB_TEXT("Failed to build mesh: mesh face triangulation scratch size overflows"));
        return false;
    }

    const usize triangleIndexCapacity = static_cast<usize>(mesh.max_face_triangles) * s_TriangleIndexCount;
    if(inOutTriangleIndices.size() < triangleIndexCapacity)
        inOutTriangleIndices.resize(triangleIndexCapacity);
    return true;
}


bool FbxSourceMeshStreams::GenerateSourceMeshTangents(
    SourceMeshStreams& mesh,
    const bool usedDefaultUvs,
    SourceTangentReport& outTangentReport
){
    using RebuildVertex = ::TangentFrameRebuildVertex;

    outTangentReport = SourceTangentReport{};
    NWB_ASSERT(!mesh.vertexRefs.empty());
    NWB_ASSERT(!mesh.indices.empty());
    NWB_ASSERT((mesh.indices.size() % s_TriangleIndexCount) == 0u);

    UtilityVector<RebuildVertex> rebuildVertices;
    rebuildVertices.reserve(mesh.vertexRefs.size());
    for(const SourceVertexRef& ref : mesh.vertexRefs){
        NWB_ASSERT(ref.position < mesh.positions.size());
        NWB_ASSERT(ref.normal < mesh.normals.size());
        NWB_ASSERT(ref.uv0 < mesh.uv0.size());

        const Vec3& position = mesh.positions[ref.position];
        const Vec3& normal = mesh.normals[ref.normal];
        rebuildVertices.push_back(RebuildVertex{
            Float4(position.x, position.y, position.z, 0.0f),
            Float4(normal.x, normal.y, normal.z, 0.0f),
            Float4(1.0f, 0.0f, 0.0f, 1.0f),
            mesh.uv0[ref.uv0],
        });
    }

    UtilityVector<u32> rebuildIndices;
    rebuildIndices.reserve(mesh.indices.size());
    for(usize indexBase = 0u; indexBase < mesh.indices.size(); indexBase += s_TriangleIndexCount){
        const u32 i0 = mesh.indices[indexBase + 0u];
        const u32 i1 = mesh.indices[indexBase + 1u];
        const u32 i2 = mesh.indices[indexBase + 2u];
        NWB_ASSERT(i0 < rebuildVertices.size());
        NWB_ASSERT(i1 < rebuildVertices.size());
        NWB_ASSERT(i2 < rebuildVertices.size());
        if(i0 == i1 || i0 == i2 || i1 == i2)
            continue;

        const Float4& p0 = rebuildVertices[i0].position;
        const Float4& p1 = rebuildVertices[i1].position;
        const Float4& p2 = rebuildVertices[i2].position;
        const TriangleAreaNormal64 areaNormal = BuildStoredTriangleAreaNormal64(p0.x, p0.y, p0.z, p1.x, p1.y, p1.z, p2.x, p2.y, p2.z);
        const f64 areaLengthSquared = TriangleAreaNormalLengthSquared(areaNormal);
        if(!IsFinite(areaLengthSquared) || areaLengthSquared <= static_cast<f64>(::s_FrameDirectionEpsilon))
            continue;

        rebuildIndices.push_back(i0);
        rebuildIndices.push_back(i1);
        rebuildIndices.push_back(i2);
    }
    if(rebuildIndices.empty()){
        NWB_LOGGER_ERROR(NWB_TEXT("Failed to build mesh: mesh has no valid triangles for tangent generation"));
        return false;
    }

    Core::Alloc::ScratchArena scratchArena(UtilityDetail::s_SourceTangentRebuildScratch);
    TangentFrameRebuildResult rebuildResult;
    if(!::RebuildTangentFrames(scratchArena, rebuildVertices, rebuildIndices, &rebuildResult)){
        NWB_LOGGER_ERROR(NWB_TEXT("Failed to build mesh: failed to generate source tangent stream"));
        return false;
    }

    Vec4IndexMap tangentLookup;
    tangentLookup.reserve(mesh.vertexRefs.size());
    mesh.tangents.clear();
    for(usize vertexRefIndex = 0u; vertexRefIndex < mesh.vertexRefs.size(); ++vertexRefIndex){
        SourceVertexRef& ref = mesh.vertexRefs[vertexRefIndex];
        const Vec3& storedNormal = mesh.normals[ref.normal];
        const Float4& storedTangent = rebuildVertices[vertexRefIndex].tangent;
        const SIMDVector normal = ::FrameNormalizeDirection(
            LoadFloat(storedNormal),
            VectorSet(0.0f, 0.0f, 1.0f, 0.0f)
        );
        const SIMDVector tangent = ::FrameResolveTangent(
            normal,
            VectorSetW(LoadFloat(storedTangent), 0.0f),
            ::FrameFallbackTangent(normal)
        );
        if(!::FrameValidDirection(tangent)){
            NWB_LOGGER_ERROR(NWB_TEXT("Failed to build mesh: failed to resolve generated source tangent"));
            return false;
        }

        const f32 handedness = ::FrameTangentHandedness(rebuildVertices[vertexRefIndex].tangent.w, 1.0f);
        Vec4 generatedTangent;
        StoreFloat(VectorSetW(tangent, handedness), generatedTangent);
        if(!InternSourceValue(mesh.tangents, tangentLookup, generatedTangent, s_SourceTangentLabel, ref.tangent))
            return false;
    }
    outTangentReport.degenerateUvTriangleCount = rebuildResult.degenerateUvTriangleCount;
    outTangentReport.fallbackTangentVertexCount = rebuildResult.fallbackTangentVertexCount;
    outTangentReport.mode =
        usedDefaultUvs || rebuildResult.degenerateUvTriangleCount != 0u || rebuildResult.fallbackTangentVertexCount != 0u
        ? SourceTangentMode::GeneratedFallback
        : SourceTangentMode::GeneratedUv
    ;
    return true;
}


bool FbxSourceMeshStreams::InternSourceCorner(
    SourceMeshBuildContext& context,
    const SourceTriangleCorner& corner,
    const bool wantsSkinning,
    u32& outVertexRefIndex
){
    SourceVertexRef ref;
    if(!InternSourceValue(context.mesh.positions, context.positions, corner.position, s_SourcePositionLabel, ref.position))
        return false;
    if(!InternSourceValue(context.mesh.normals, context.normals, corner.normal, s_SourceNormalLabel, ref.normal))
        return false;
    if(corner.hasTangent){
        if(!InternSourceValue(context.mesh.tangents, context.tangents, corner.tangent, s_SourceTangentLabel, ref.tangent))
            return false;
    }
    if(!InternSourceValue(context.mesh.uv0, context.uv0, corner.uv0, s_SourceUv0Label, ref.uv0))
        return false;
    if(!InternSourceValue(context.mesh.colors, context.colors, corner.color, s_SourceColorLabel, ref.color))
        return false;
    if(wantsSkinning){
        if(!InternSourceValue(context.mesh.skin, context.skin, corner.skin, s_SkinAssetTypeText, ref.skin))
            return false;
    }

    return InternSourceValue(context.mesh.vertexRefs, context.vertexRefs, ref, s_SourceVertexRefLabel, outVertexRefIndex);
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_FBX_TO_NWB_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

