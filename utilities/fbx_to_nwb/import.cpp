// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "module.h"

#include "skin.h"

#include <core/common/log.h>
#include <core/mesh/frame_math.h>
#include <core/mesh/tangent_frame_rebuild.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_FBX_TO_NWB_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace __hidden_import{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "source_stream.inl"
#include "mesh_build.inl"

};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


bool BuildMesh(
    const UtilityVector<MeshInstance>& instances,
    const UtilityVector<usize>& selection,
    const ImportOptions& options,
    const bool wantsSkinning,
    const Vec4& defaultColor,
    Core::Alloc::ThreadPool& threadPool,
    SourceMeshStreams& outMesh,
    UtilityVector<ufbx_node*>& outSkeletonJoints,
    UtilityVector<JointMatrix>& outSkeletonBindPoseMatrices,
    UtilityVector<JointMatrix>& outInverseBindMatrices,
    bool& outSawVertexColors,
    bool& outSawVertexUvs,
    SourceTangentReport& outTangentReport
){
    outMesh = SourceMeshStreams{};
    outSkeletonJoints.clear();
    outSkeletonBindPoseMatrices.clear();
    outInverseBindMatrices.clear();
    outSawVertexColors = false;
    outSawVertexUvs = false;
    outTangentReport = SourceTangentReport{};

    usize estimatedTriangleCorners = 0u;
    if(!__hidden_import::EstimateSelectedTriangleCorners(
        instances,
        selection,
        estimatedTriangleCorners
    ))
        return false;

    NormalMode::Enum normalMode = NormalMode::Imported;
    if(!ParseNormalModeText(options.normalMode, normalMode)){
        NWB_LOGGER_ERROR(NWB_TEXT("Failed to build mesh: {}"), StringConvert(NormalModeErrorText()));
        return false;
    }

    bool usedDefaultUvs = false;
    __hidden_import::ReserveSourceMeshStreams(outMesh, estimatedTriangleCorners, wantsSkinning);
    __hidden_import::SourceMeshBuildContext meshContext{ outMesh };
    __hidden_import::ReserveSourceMeshBuildContext(meshContext, estimatedTriangleCorners, wantsSkinning);

    UtilityVector<u32> triangleIndices;
    FbxSkinDetail::ExportContext skinContext;
    for(const usize instanceIndex : selection){
        NWB_ASSERT(instanceIndex < instances.size());
        if(
            !__hidden_import::AppendInstanceMesh(
                instances[instanceIndex],
                options,
                wantsSkinning,
                normalMode,
                defaultColor,
                triangleIndices,
                meshContext,
                skinContext,
                outSawVertexColors,
                outSawVertexUvs,
                usedDefaultUvs
            )
        ){
            return false;
        }
    }

    if(outMesh.indices.empty()){
        NWB_LOGGER_ERROR(NWB_TEXT("Failed to build mesh: selected meshes produced no triangles"));
        return false;
    }
    if(normalMode != NormalMode::Imported || !__hidden_import::SourceMeshHasCompleteTangents(outMesh)){
        __hidden_import::DropSourceMeshTangents(outMesh);
        if(!__hidden_import::GenerateSourceMeshTangents(outMesh, usedDefaultUvs, outTangentReport))
            return false;
    }
    if(wantsSkinning){
        if(skinContext.joints.empty()){
            NWB_LOGGER_ERROR(NWB_TEXT("Failed to build mesh: skinned mesh did not produce any skeleton joints"));
            return false;
        }
        outSkeletonJoints = Move(skinContext.joints);
        outSkeletonBindPoseMatrices = Move(skinContext.bindPoseMatrices);
        outInverseBindMatrices = Move(skinContext.inverseBindMatrices);
    }

    return CanonicalizeSourceMeshStreams(outMesh, threadPool, nullptr);
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_FBX_TO_NWB_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

