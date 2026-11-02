// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "mesh_refresh_parse.h"

#include <impl/assets_mesh/cook_metadata.h>
#include <impl/assets_mesh/cook_source_streams.h>

#include <core/common/log.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_FBX_TO_NWB_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace MeshRefreshParseDetail{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


[[nodiscard]] static b8 ValidateMeshAssetFields(const ScratchPath& nwbFilePath, const AStringView meshVariableName, const Core::Metascript::Value& asset){
    if(!asset.isMap()){
        NWB_LOGGER_ERROR(NWB_TEXT("{} meta '{}': '{}' is not a map"), Impl::s_MeshMetaKind, PathToString<tchar>(nwbFilePath), ConvertString(meshVariableName));
        return false;
    }

    for(const auto& field : asset.asMap()){
        const Core::Metascript::MStringView fieldName(field.first);
        if(Impl::IsAllowedMeshAssetField(fieldName))
            continue;

        NWB_LOGGER_ERROR(NWB_TEXT("{} meta '{}': unsupported field '{}.{}' would be dropped by refresh"), Impl::s_MeshMetaKind, PathToString<tchar>(nwbFilePath), ConvertString(meshVariableName), ConvertString(fieldName));
        return false;
    }
    return true;
}

[[nodiscard]] static b8 ValidateMesh(const ScratchPath& nwbFilePath, const SourceMeshStreams& mesh){
    if(!Impl::MeshCookSourceStreams::ValidateSourceVertexRefStreams(
        nwbFilePath,
        Impl::s_MeshMetaKind,
        Span<const SourceVertexRef>(mesh.vertexRefs.data(), mesh.vertexRefs.size()),
        mesh.positions.size(),
        mesh.normals.size(),
        mesh.tangents.size(),
        mesh.uv0.size(),
        mesh.colors.size()
    ))
        return false;

    for(const SourceVertexRef& ref : mesh.vertexRefs){
        if(ref.skin != Impl::s_MeshMissingStreamIndex){
            NWB_LOGGER_ERROR(NWB_TEXT("{} meta '{}': plain mesh vertex_ref cannot contain a skin index"), Impl::s_MeshMetaKind, PathToString<tchar>(nwbFilePath));
            return false;
        }
    }

    return Impl::MeshCookSourceStreams::ValidateSourceIndexStream(
        nwbFilePath,
        Impl::s_MeshMetaKind,
        Span<const u32>(mesh.indices.data(), mesh.indices.size()),
        mesh.vertexRefs.size()
    );
}

[[nodiscard]] Expected<UtilityVector<MeshSkinInfluence>> ParseSkinInfluences(const ScratchPath& nwbFilePath, const Core::Metascript::Value& skinAsset, const AStringView skinVariableName){
    UtilityVector<MeshSkinInfluence> outInfluences;
    if(!Impl::MeshCookSourceStreams::ParseSourceSkinInfluencesInto(nwbFilePath, skinAsset, Impl::s_MeshMetaKind, skinVariableName, false, outInfluences))
        return MakeUnexpected(Failure{});
    return outInfluences;
}

[[nodiscard]] Expected<SourceMeshStreams> ParseMeshValue(const ScratchPath& nwbFilePath, const AStringView meshVariableName, const Core::Metascript::Value& asset, Core::Alloc::ScratchArena& scratchArena){
    if(!ValidateMeshAssetFields(nwbFilePath, meshVariableName, asset))
        return MakeUnexpected(Failure{});
    SourceMeshStreams mesh;
    if(!Impl::MeshCookMetadata::FillMetadataFloatListField<Vec3, s_Vector3ComponentCount>(nwbFilePath, asset, Impl::s_MeshMetaKind, Impl::s_MeshPositionsFieldNameView, scratchArena, mesh.positions))
        return MakeUnexpected(Failure{});
    if(!Impl::MeshCookMetadata::FillMetadataFloatListField<Vec3, s_Vector3ComponentCount>(nwbFilePath, asset, Impl::s_MeshMetaKind, Impl::s_MeshNormalsFieldNameView, scratchArena, mesh.normals))
        return MakeUnexpected(Failure{});
    if(!Impl::MeshCookMetadata::FillMetadataFloatListField<Vec4, s_Vector4ComponentCount>(nwbFilePath, asset, Impl::s_MeshMetaKind, Impl::s_MeshTangentsFieldNameView, scratchArena, mesh.tangents))
        return MakeUnexpected(Failure{});
    if(!Impl::MeshCookMetadata::FillMetadataFloatListField<Vec2, s_Vector2ComponentCount>(nwbFilePath, asset, Impl::s_MeshMetaKind, Impl::s_MeshUv0FieldNameView, scratchArena, mesh.uv0))
        return MakeUnexpected(Failure{});
    if(!Impl::MeshCookMetadata::FillMetadataFloatListField<Vec4, s_Vector4ComponentCount>(nwbFilePath, asset, Impl::s_MeshMetaKind, Impl::s_MeshColorsFieldNameView, scratchArena, mesh.colors))
        return MakeUnexpected(Failure{});
    if(!Impl::MeshCookSourceStreams::ParseSourceVertexRefsInto(nwbFilePath, asset, Impl::s_MeshMetaKind, scratchArena, mesh.vertexRefs))
        return MakeUnexpected(Failure{});
    if(!Impl::MeshCookMetadata::ParseMetadataIndexFieldInto(nwbFilePath, asset, Impl::s_MeshMetaKind, scratchArena, mesh.indices))
        return MakeUnexpected(Failure{});
    if(!ValidateMesh(nwbFilePath, mesh))
        return MakeUnexpected(Failure{});
    return mesh;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_FBX_TO_NWB_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

