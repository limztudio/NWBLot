// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "module.h"
#include "mesh_build.h"
#include "source_mesh_streams.h"
#include "skin.h"

#include <core/common/log.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_FBX_TO_NWB_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


Expected<MeshBuildResult> BuildMesh(
    const UtilityVector<MeshInstance>& instances,
    const UtilityVector<usize>& selection,
    const ImportOptions& options,
    const bool wantsSkinning,
    const Vec4& defaultColor,
    Core::CpuTaskScheduler& cpuScheduler
){
    MeshBuildResult result;
    const auto estimatedTriangleCorners = FbxMeshBuild::EstimateSelectedTriangleCorners(instances, selection);
    if(!estimatedTriangleCorners)
        return MakeUnexpected(Failure{});

    const auto normalMode = ParseNormalModeText(options.normalMode);
    if(!normalMode){
        NWB_LOGGER_ERROR(NWB_TEXT("Failed to build mesh: {}"), StringConvert(NormalModeErrorText()));
        return MakeUnexpected(Failure{});
    }

    bool usedDefaultUvs = false;
    FbxSourceMeshStreams::ReserveSourceMeshStreams(result.mesh, *estimatedTriangleCorners, wantsSkinning);
    SourceMeshBuildContext meshContext{ result.mesh };
    FbxSourceMeshStreams::ReserveSourceMeshBuildContext(meshContext, *estimatedTriangleCorners, wantsSkinning);

    UtilityVector<u32> triangleIndices;
    FbxSkinDetail::ExportContext skinContext;
    for(const usize instanceIndex : selection){
        NWB_ASSERT(instanceIndex < instances.size());
        if(
            !FbxMeshBuild::AppendInstanceMesh(
                instances[instanceIndex],
                options,
                wantsSkinning,
                *normalMode,
                defaultColor,
                triangleIndices,
                meshContext,
                skinContext,
                result.sawVertexColors,
                result.sawVertexUvs,
                usedDefaultUvs
            )
        ){
            return MakeUnexpected(Failure{});
        }
    }

    if(result.mesh.indices.empty()){
        NWB_LOGGER_ERROR(NWB_TEXT("Failed to build mesh: selected meshes produced no triangles"));
        return MakeUnexpected(Failure{});
    }
    if(*normalMode != NormalMode::Imported || !FbxSourceMeshStreams::SourceMeshHasCompleteTangents(result.mesh)){
        FbxSourceMeshStreams::DropSourceMeshTangents(result.mesh);
        const auto tangentReport = FbxSourceMeshStreams::GenerateSourceMeshTangents(result.mesh, usedDefaultUvs);
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

    if(!CanonicalizeSourceMeshStreams(result.mesh, cpuScheduler))
        return MakeUnexpected(Failure{});
    return result;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_FBX_TO_NWB_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

