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


[[nodiscard]] bool ParseFiniteF32(const Path& nwbFilePath, const Core::Metascript::Value& value, const AStringView label, f32& outValue);
[[nodiscard]] bool ParseU32(const Path& nwbFilePath, const Core::Metascript::Value& value, const AStringView label, u32& outValue);
[[nodiscard]] const Core::Metascript::Value* FindRequiredListField(const Path& nwbFilePath, const Core::Metascript::Value& asset, const AStringView fieldName);
[[nodiscard]] bool ParseVertexRefs(const Path& nwbFilePath, const Core::Metascript::Value& asset, UtilityVector<SourceVertexRef>& outVertexRefs);
[[nodiscard]] bool ParseSkinInfluences(const Path& nwbFilePath, const Core::Metascript::Value& skinAsset, const AStringView skinVariableName, UtilityVector<MeshSkinInfluence>& outInfluences);
[[nodiscard]] bool ParseIndices(const Path& nwbFilePath, const Core::Metascript::Value& asset, UtilityVector<u32>& outIndices);
[[nodiscard]] bool ValidateMesh(const Path& nwbFilePath, const SourceMeshStreams& mesh);
[[nodiscard]] bool ParseMeshValue(const Path& nwbFilePath, const AStringView meshVariableName, const Core::Metascript::Value& asset, SourceMeshStreams& outMesh);


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_FBX_TO_NWB_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

