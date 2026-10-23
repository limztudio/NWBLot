// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "module.h"
#include "mesh_build.h"
#include "source_mesh_streams.h"
#include "skin.h"

#include <core/common/log.h>

#include <global/overflow.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_FBX_TO_NWB_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


Expected<MeshBuildResult> BuildMesh(
    const UtilityVector<MeshInstance>& instances,
    const UtilityVector<usize>& selection,
    const ImportOptions& options,
    const bool wantsSkinning,
    const Vec4& defaultColor,
    Core::CpuTaskScheduler& cpuScheduler,
    Core::Alloc::ScratchArena& scratchArena
){
    MeshBuildResult result;
    const auto estimatedTriangleCorners = FbxMeshBuild::EstimateSelectedTriangleCorners(instances, selection);
    if(!estimatedTriangleCorners)
        return MakeUnexpected(Failure{});

    const auto normalMode = ParseNormalModeText(options.normalMode, scratchArena);
    if(!normalMode){
        NWB_LOGGER_ERROR(NWB_TEXT("Failed to build mesh: {}"), StringConvert(NormalModeErrorText(scratchArena)));
        return MakeUnexpected(Failure{});
    }

    bool usedDefaultUvs = false;
    FbxSourceMeshStreams::ReserveSourceMeshStreams(result.mesh, *estimatedTriangleCorners, wantsSkinning);
    SourceMeshBuildContext meshContext{ result.mesh, scratchArena };
    FbxSourceMeshStreams::ReserveSourceMeshBuildContext(meshContext, *estimatedTriangleCorners, wantsSkinning);

    usize maxFaceTriangles = 0u;
    for(const usize instanceIndex : selection)
        maxFaceTriangles = Max(maxFaceTriangles, static_cast<usize>(instances[instanceIndex].mesh->max_face_triangles));
    const auto triangleIndexCapacity = TryMultiply<usize>(maxFaceTriangles, s_TriangleIndexCount);
    if(!triangleIndexCapacity){
        NWB_LOGGER_ERROR(NWB_TEXT("Failed to build mesh: mesh face triangulation scratch size overflows"));
        return MakeUnexpected(Failure{});
    }
    UtilityScratchVector<u32> triangleIndices(*triangleIndexCapacity, scratchArena);
    FbxSkinDetail::ExportContext skinContext(scratchArena);
    usize jointCapacity = 0u;
    if(wantsSkinning){
        for(const usize instanceIndex : selection){
            NWB_ASSERT(instanceIndex < instances.size());
            const ufbx_mesh* mesh = instances[instanceIndex].mesh;
            if(mesh && mesh->skin_deformers.count == 1u && mesh->skin_deformers.data[0u]){
                const usize clusterCount = mesh->skin_deformers.data[0u]->clusters.count;
                const usize remaining = s_MaxSkeletonJointCount - jointCapacity;
                jointCapacity += clusterCount < remaining ? clusterCount : remaining;
            }
        }
    }
    skinContext.jointLookup.reserve(jointCapacity);
    for(const usize instanceIndex : selection){
        NWB_ASSERT(instanceIndex < instances.size());
        if(!FbxMeshBuild::AppendInstanceMesh(instances[instanceIndex], options, wantsSkinning, *normalMode, defaultColor, scratchArena, triangleIndices, meshContext, skinContext, result.sawVertexColors, result.sawVertexUvs, usedDefaultUvs)){
            return MakeUnexpected(Failure{});
        }
    }

    if(result.mesh.indices.empty()){
        NWB_LOGGER_ERROR(NWB_TEXT("Failed to build mesh: selected meshes produced no triangles"));
        return MakeUnexpected(Failure{});
    }
    if(*normalMode != NormalMode::Imported || !FbxSourceMeshStreams::SourceMeshHasCompleteTangents(result.mesh)){
        if(!FbxSourceMeshStreams::DropSourceMeshTangents(result.mesh, scratchArena))
            return MakeUnexpected(Failure{});
        const auto tangentReport = FbxSourceMeshStreams::GenerateSourceMeshTangents(result.mesh, usedDefaultUvs, scratchArena);
        if(!tangentReport)
            return MakeUnexpected(Failure{});
        result.tangentReport = *tangentReport;
    }
    if(wantsSkinning){
        if(skinContext.joints.empty()){
            NWB_LOGGER_ERROR(NWB_TEXT("Failed to build mesh: skinned mesh did not produce any skeleton joints"));
            return MakeUnexpected(Failure{});
        }
        result.skeletonJoints = Move(skinContext.joints);
        result.skeletonBindPoseMatrices = Move(skinContext.bindPoseMatrices);
        result.inverseBindMatrices = Move(skinContext.inverseBindMatrices);
    }

    if(!CanonicalizeSourceMeshStreams(result.mesh, cpuScheduler, scratchArena))
        return MakeUnexpected(Failure{});
    return result;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_FBX_TO_NWB_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

