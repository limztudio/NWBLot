// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "fbx_to_nwb.h"

#include "fbx_skin.h"


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_FBX_TO_NWB_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace __hidden_fbx_import{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "fbx_import_source_stream.inl"
#include "fbx_import_mesh_build.inl"

};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


bool BuildMesh(
    const UtilityVector<MeshInstance>& instances,
    const UtilityVector<usize>& selection,
    const ImportOptions& options,
    const Vec4& defaultColor,
    SourceMeshStreams& outMesh,
    u32& outSkeletonJointCount,
    UtilityVector<MeshJointMatrix>& outInverseBindMatrices,
    bool& outSawVertexColors,
    bool& outSawVertexUvs,
    bool& outSawVertexTangents,
    AString& outError
){
    outMesh = SourceMeshStreams{};
    outSkeletonJointCount = 0u;
    outInverseBindMatrices.clear();
    outSawVertexColors = false;
    outSawVertexUvs = false;
    outSawVertexTangents = false;
    outError.clear();

    usize estimatedTriangleCorners = 0u;
    if(!__hidden_fbx_import::EstimateSelectedTriangleCorners(
        instances,
        selection,
        estimatedTriangleCorners,
        outError
    ))
        return false;

    u32 meshClass = 0u;
    if(!ParseMeshClassText(options.meshClass, meshClass)){
        outError = MeshClassErrorText();
        return false;
    }
    NormalMode::Enum normalMode = NormalMode::Imported;
    if(!ParseNormalModeText(options.normalMode, normalMode)){
        outError = NormalModeErrorText();
        return false;
    }

    const bool wantsSkinning = MeshClassUsesSkinning(meshClass);
    __hidden_fbx_import::ReserveSourceMeshStreams(outMesh, estimatedTriangleCorners, wantsSkinning);
    __hidden_fbx_import::SourceMeshBuildContext meshContext{ outMesh };
    __hidden_fbx_import::ReserveSourceMeshBuildContext(meshContext, estimatedTriangleCorners, wantsSkinning);

    UtilityVector<u32> triangleIndices;
    FbxSkinDetail::ExportContext skinContext;
    for(const usize instanceIndex : selection){
        if(instanceIndex >= instances.size()){
            outError = "selected mesh index is out of range";
            return false;
        }
        if(
            !__hidden_fbx_import::AppendInstanceMesh(
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
                outSawVertexTangents,
                outError
            )
        ){
            return false;
        }
    }

    if(outMesh.indices.empty()){
        outError = "selected meshes produced no triangles";
        return false;
    }
    if(__hidden_fbx_import::SourceMeshHasPartialTangents(outMesh)){
        __hidden_fbx_import::DropSourceMeshTangents(outMesh);
        outSawVertexTangents = false;
    }
    if(wantsSkinning){
        if(skinContext.joints.empty()){
            outError = "skinned mesh did not produce any skeleton joints";
            return false;
        }
        outSkeletonJointCount = static_cast<u32>(skinContext.joints.size());
        outInverseBindMatrices = Move(skinContext.inverseBindMatrices);
    }

    return true;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_FBX_TO_NWB_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

