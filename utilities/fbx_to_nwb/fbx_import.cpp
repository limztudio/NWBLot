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


bool BuildGeometry(
    const UtilityVector<MeshInstance>& instances,
    const UtilityVector<usize>& selection,
    const ImportOptions& options,
    const Vec4& defaultColor,
    SourceGeometryStreams& outGeometry,
    u32& outSkeletonJointCount,
    UtilityVector<GeometryJointMatrix>& outInverseBindMatrices,
    bool& outSawVertexColors,
    bool& outSawVertexUvs,
    bool& outSawVertexTangents,
    AString& outError
){
    outGeometry = SourceGeometryStreams{};
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

    u32 geometryClass = 0u;
    if(!ParseGeometryClassText(options.geometryClass, geometryClass)){
        outError = GeometryClassErrorText();
        return false;
    }
    NormalMode::Enum normalMode = NormalMode::Imported;
    if(!ParseNormalModeText(options.normalMode, normalMode)){
        outError = NormalModeErrorText();
        return false;
    }

    const bool wantsSkinning = GeometryClassUsesSkinning(geometryClass);
    __hidden_fbx_import::ReserveSourceGeometryStreams(outGeometry, estimatedTriangleCorners, wantsSkinning);
    __hidden_fbx_import::SourceGeometryBuildContext geometryContext{ outGeometry };
    __hidden_fbx_import::ReserveSourceGeometryBuildContext(geometryContext, estimatedTriangleCorners, wantsSkinning);

    UtilityVector<u32> triangleIndices;
    FbxSkinDetail::ExportContext skinContext;
    for(const usize instanceIndex : selection){
        if(instanceIndex >= instances.size()){
            outError = "selected mesh index is out of range";
            return false;
        }
        if(
            !__hidden_fbx_import::AppendInstanceGeometry(
                instances[instanceIndex],
                options,
                wantsSkinning,
                normalMode,
                defaultColor,
                triangleIndices,
                geometryContext,
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

    if(outGeometry.indices.empty()){
        outError = "selected meshes produced no triangles";
        return false;
    }
    if(__hidden_fbx_import::SourceGeometryHasPartialTangents(outGeometry)){
        __hidden_fbx_import::DropSourceGeometryTangents(outGeometry);
        outSawVertexTangents = false;
    }
    if(wantsSkinning){
        if(skinContext.joints.empty()){
            outError = "skinned geometry did not produce any skeleton joints";
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

