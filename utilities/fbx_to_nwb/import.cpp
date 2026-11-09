// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "module.h"
#include "mesh_build.h"
#include "mesh_stream_canonicalizer.h"
#include "source_mesh_streams.h"
#include "skin.h"

#include <core/common/log.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_FBX_TO_NWB_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


Expected<MeshBuildResult> BuildMesh(
    const Span<const MeshInstance> instances,
    const Span<const usize> selection,
    const ImportOptions& options,
    const b8 wantsSkinning,
    const Vec4& defaultColor,
    Core::CpuTaskScheduler& cpuScheduler,
    Core::Alloc::ScratchArena& scratchArena
){
    MeshBuildResult result;
    const auto estimatedTriangleCorners = FbxMeshBuild::EstimateSelectedTriangleCorners(instances, selection);
    if(!estimatedTriangleCorners)
        return MakeUnexpected(Failure{});

    const auto normalMode = ParseNormalModeText(options.normalMode);
    if(!normalMode){
        NWB_LOGGER_ERROR(FOUNDATION_TEXT("Failed to build mesh: {}"), ConvertString(NormalModeErrorText(scratchArena)));
        return MakeUnexpected(Failure{});
    }

    b8 usedDefaultUvs = false;
    FbxSourceMeshStreams::ReserveSourceMeshStreams(*estimatedTriangleCorners, wantsSkinning, result.mesh);
    SourceMeshBuildContext meshContext{ result.mesh, scratchArena };
    FbxSourceMeshStreams::ReserveSourceMeshBuildContext(*estimatedTriangleCorners, wantsSkinning, meshContext);

    usize maxFaceTriangles = 0u;
    for(const usize instanceIndex : selection)
        maxFaceTriangles = Max(maxFaceTriangles, static_cast<usize>(instances[instanceIndex].mesh->max_face_triangles));
    const auto triangleIndexCapacity = TryComputeTriangleIndexScratchCapacity(maxFaceTriangles);
    if(!triangleIndexCapacity)
        return MakeUnexpected(Failure{});
    UtilityScratchVector<u32> triangleIndices(*triangleIndexCapacity, scratchArena);
    FbxSkinDetail::ExportContext skinContext(scratchArena);
    usize jointCapacity = 0u;
    if(wantsSkinning){
        for(const usize instanceIndex : selection){
            FOUNDATION_ASSERT(instanceIndex < instances.size());
            const ufbx_mesh* mesh = instances[instanceIndex].mesh;
            if(mesh && mesh->skin_deformers.count == 1u && mesh->skin_deformers.data[0u]){
                const usize clusterCount = mesh->skin_deformers.data[0u]->clusters.count;
                jointCapacity = AccumulateFbxJointCapacity(jointCapacity, clusterCount);
            }
        }
    }
    skinContext.jointLookup.reserve(jointCapacity);
    for(const usize instanceIndex : selection){
        FOUNDATION_ASSERT(instanceIndex < instances.size());
        if(!FbxMeshBuild::AppendInstanceMesh(instances[instanceIndex], options, wantsSkinning, *normalMode, defaultColor, scratchArena, triangleIndices, meshContext, skinContext, result.sawVertexColors, result.sawVertexUvs, usedDefaultUvs)){
            return MakeUnexpected(Failure{});
        }
    }

    if(result.mesh.indices.empty()){
        NWB_LOGGER_ERROR(FOUNDATION_TEXT("Failed to build mesh: selected meshes produced no triangles"));
        return MakeUnexpected(Failure{});
    }
    if(*normalMode != NormalMode::Imported || !SourceVertexRefsHaveCompleteTangents(result.mesh.vertexRefs, result.mesh.tangents.size())){
        if(!FbxSourceMeshStreams::DropSourceMeshTangents(scratchArena, result.mesh))
            return MakeUnexpected(Failure{});
        const auto tangentReport = FbxSourceMeshStreams::GenerateSourceMeshTangents(usedDefaultUvs, scratchArena, result.mesh);
        if(!tangentReport)
            return MakeUnexpected(Failure{});
        result.tangentReport = *tangentReport;
    }
    if(wantsSkinning){
        if(skinContext.joints.empty()){
            NWB_LOGGER_ERROR(FOUNDATION_TEXT("Failed to build mesh: skinned mesh did not produce any skeleton joints"));
            return MakeUnexpected(Failure{});
        }
        result.skeletonJoints = Move(skinContext.joints);
        result.skeletonBindPoseMatrices = Move(skinContext.bindPoseMatrices);
        result.inverseBindMatrices = Move(skinContext.inverseBindMatrices);
    }

    if(!SourceMeshCanonicalizer::Canonicalize(cpuScheduler, scratchArena, result.mesh))
        return MakeUnexpected(Failure{});
    return result;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_FBX_TO_NWB_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

