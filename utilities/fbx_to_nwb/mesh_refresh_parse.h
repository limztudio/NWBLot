// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#pragma once


#include "module.h"

#include <core/metascript/parser.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_FBX_TO_NWB_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace MeshRefreshParseDetail{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


[[nodiscard]] Expected<f32> ParseFiniteF32(const ScratchPath& nwbFilePath, const Core::Metascript::Value& value, const AStringView label);
[[nodiscard]] Expected<u32> ParseU32(const ScratchPath& nwbFilePath, const Core::Metascript::Value& value, const AStringView label);
[[nodiscard]] Expected<NotNull<const Core::Metascript::Value*>> FindRequiredListField(const ScratchPath& nwbFilePath, const Core::Metascript::Value& asset, const AStringView fieldName);
[[nodiscard]] Expected<UtilityVector<SourceVertexRef>> ParseVertexRefs(const ScratchPath& nwbFilePath, const Core::Metascript::Value& asset, Core::Alloc::ScratchArena& scratchArena);
[[nodiscard]] Expected<UtilityVector<MeshSkinInfluence>> ParseSkinInfluences(const ScratchPath& nwbFilePath, const Core::Metascript::Value& skinAsset, const AStringView skinVariableName, Core::Alloc::ScratchArena& scratchArena);
[[nodiscard]] Expected<UtilityVector<u32>> ParseIndices(const ScratchPath& nwbFilePath, const Core::Metascript::Value& asset, Core::Alloc::ScratchArena& scratchArena);
[[nodiscard]] bool ValidateMesh(const ScratchPath& nwbFilePath, const SourceMeshStreams& mesh);
[[nodiscard]] Expected<SourceMeshStreams> ParseMeshValue(const ScratchPath& nwbFilePath, const AStringView meshVariableName, const Core::Metascript::Value& asset, Core::Alloc::ScratchArena& scratchArena);


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_FBX_TO_NWB_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

