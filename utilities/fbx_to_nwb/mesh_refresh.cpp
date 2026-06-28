// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "module.h"
#include "text_write.h"

#include <core/common/log.h>
#include <core/metascript/parser.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_FBX_TO_NWB_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace __hidden_mesh_refresh{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


using __hidden_text_write::WriteFloat;
using __hidden_text_write::WriteVec2;
using __hidden_text_write::WriteVec3;
using __hidden_text_write::WriteVec4;

static constexpr AStringView s_MeshMetaKind = "Mesh";
static constexpr usize s_DeduplicateParallelGrainSize = 4096u;

struct MeshSkinInfluenceEqual{
    bool operator()(const MeshSkinInfluence& lhs, const MeshSkinInfluence& rhs)const{
        for(usize i = 0u; i < 4u; ++i){
            if(lhs.joint[i] != rhs.joint[i] || FloatHashBits(lhs.weight[i]) != FloatHashBits(rhs.weight[i]))
                return false;
        }
        return true;
    }
};

struct TextReplacement{
    usize begin = 0u;
    usize end = 0u;
    AString text;
};

template<typename Value>
struct StreamSortEntry{
    Value value;
    usize sourceIndex = 0u;
};

[[nodiscard]] u32 FloatSortKey(const f32 value){
    return FloatHashBits(value);
}

[[nodiscard]] bool LessF32(const f32 lhs, const f32 rhs){
    return FloatSortKey(lhs) < FloatSortKey(rhs);
}

[[nodiscard]] bool SameF32(const f32 lhs, const f32 rhs){
    return FloatSortKey(lhs) == FloatSortKey(rhs);
}

[[nodiscard]] bool LessValue(const Vec2& lhs, const Vec2& rhs){
    if(!SameF32(lhs.x, rhs.x))
        return LessF32(lhs.x, rhs.x);
    return LessF32(lhs.y, rhs.y);
}

[[nodiscard]] bool LessValue(const Vec3& lhs, const Vec3& rhs){
    if(!SameF32(lhs.x, rhs.x))
        return LessF32(lhs.x, rhs.x);
    if(!SameF32(lhs.y, rhs.y))
        return LessF32(lhs.y, rhs.y);
    return LessF32(lhs.z, rhs.z);
}

[[nodiscard]] bool LessValue(const Vec4& lhs, const Vec4& rhs){
    if(!SameF32(lhs.x, rhs.x))
        return LessF32(lhs.x, rhs.x);
    if(!SameF32(lhs.y, rhs.y))
        return LessF32(lhs.y, rhs.y);
    if(!SameF32(lhs.z, rhs.z))
        return LessF32(lhs.z, rhs.z);
    return LessF32(lhs.w, rhs.w);
}

[[nodiscard]] bool LessValue(const MeshSkinInfluence& lhs, const MeshSkinInfluence& rhs){
    for(usize i = 0u; i < 4u; ++i){
        if(lhs.joint[i] != rhs.joint[i])
            return lhs.joint[i] < rhs.joint[i];
    }
    for(usize i = 0u; i < 4u; ++i){
        if(!SameF32(lhs.weight[i], rhs.weight[i]))
            return LessF32(lhs.weight[i], rhs.weight[i]);
    }
    return false;
}

[[nodiscard]] bool SameValue(const Vec2& lhs, const Vec2& rhs){
    return SameF32(lhs.x, rhs.x) && SameF32(lhs.y, rhs.y);
}

[[nodiscard]] bool SameValue(const Vec3& lhs, const Vec3& rhs){
    return SameF32(lhs.x, rhs.x) && SameF32(lhs.y, rhs.y) && SameF32(lhs.z, rhs.z);
}

[[nodiscard]] bool SameValue(const Vec4& lhs, const Vec4& rhs){
    return SameF32(lhs.x, rhs.x) && SameF32(lhs.y, rhs.y) && SameF32(lhs.z, rhs.z) && SameF32(lhs.w, rhs.w);
}

[[nodiscard]] bool SameValue(const MeshSkinInfluence& lhs, const MeshSkinInfluence& rhs){
    MeshSkinInfluenceEqual equal;
    return equal(lhs, rhs);
}

template<typename Value>
[[nodiscard]] bool DeduplicateStream(
    UtilityVector<Value>& stream,
    Core::Alloc::ThreadPool& threadPool,
    UtilityVector<u32>& outRemap,
    const char* streamName
){
    outRemap.clear();
    outRemap.reserve(stream.size());
    outRemap.resize(stream.size());

    if(stream.empty())
        return true;

    UtilityVector<StreamSortEntry<Value>> sortedEntries;
    sortedEntries.resize(stream.size());

    auto fillEntry = [&](const usize index){
        sortedEntries[index].value = stream[index];
        sortedEntries[index].sourceIndex = index;
    };
    threadPool.parallelFor(static_cast<usize>(0), stream.size(), s_DeduplicateParallelGrainSize, fillEntry);

    Sort(
        sortedEntries.begin(),
        sortedEntries.end(),
        [](const StreamSortEntry<Value>& lhs, const StreamSortEntry<Value>& rhs){
            if(LessValue(lhs.value, rhs.value))
                return true;
            if(LessValue(rhs.value, lhs.value))
                return false;
            return lhs.sourceIndex < rhs.sourceIndex;
        }
    );

    usize uniqueCount = 0u;
    for(usize sortedIndex = 0u; sortedIndex < sortedEntries.size(); ++sortedIndex){
        if(sortedIndex == 0u || !SameValue(sortedEntries[sortedIndex].value, sortedEntries[sortedIndex - 1u].value))
            ++uniqueCount;
    }

    if(uniqueCount == stream.size()){
        for(usize i = 0u; i < outRemap.size(); ++i)
            outRemap[i] = static_cast<u32>(i);
        return true;
    }

    if(uniqueCount >= static_cast<usize>(s_MissingSourceStreamIndex)){
        NWB_LOGGER_ERROR(NWB_TEXT("Failed to canonicalize mesh: {} stream has too many unique values"), StringConvert(streamName));
        return false;
    }

    UtilityVector<Value> compact;
    compact.reserve(uniqueCount);

    u32 compactIndex = 0u;
    for(usize sortedIndex = 0u; sortedIndex < sortedEntries.size(); ++sortedIndex){
        if(sortedIndex == 0u || !SameValue(sortedEntries[sortedIndex].value, sortedEntries[sortedIndex - 1u].value)){
            if(compact.size() >= static_cast<usize>(s_MissingSourceStreamIndex)){
                NWB_LOGGER_ERROR(NWB_TEXT("Failed to canonicalize mesh: {} stream has too many unique values"), StringConvert(streamName));
                return false;
            }

            compactIndex = static_cast<u32>(compact.size());
            compact.push_back(sortedEntries[sortedIndex].value);
        }

        outRemap[sortedEntries[sortedIndex].sourceIndex] = compactIndex;
    }

    stream = Move(compact);
    return true;
}

[[nodiscard]] bool RemapRequiredIndex(u32& inOutIndex, const UtilityVector<u32>& remap, const char* streamName){
    if(inOutIndex < remap.size()){
        inOutIndex = remap[inOutIndex];
        return true;
    }

    NWB_LOGGER_ERROR(NWB_TEXT("Failed to canonicalize mesh: vertex_ref {} index is out of range"), StringConvert(streamName));
    return false;
}

[[nodiscard]] bool RemapOptionalIndex(u32& inOutIndex, const UtilityVector<u32>& remap, const char* streamName){
    if(inOutIndex == s_MissingSourceStreamIndex)
        return true;
    return RemapRequiredIndex(inOutIndex, remap, streamName);
}

[[nodiscard]] bool RemapComponentRefs(
    SourceMeshStreams& mesh,
    const UtilityVector<u32>& positions,
    const UtilityVector<u32>& normals,
    const UtilityVector<u32>& tangents,
    const UtilityVector<u32>& uv0,
    const UtilityVector<u32>& colors,
    const UtilityVector<u32>& skin
){
    for(SourceVertexRef& ref : mesh.vertexRefs){
        if(!RemapRequiredIndex(ref.position, positions, "position"))
            return false;
        if(!RemapRequiredIndex(ref.normal, normals, "normal"))
            return false;
        if(!RemapRequiredIndex(ref.tangent, tangents, "tangent"))
            return false;
        if(!RemapRequiredIndex(ref.uv0, uv0, "uv0"))
            return false;
        if(!RemapRequiredIndex(ref.color, colors, "color"))
            return false;
        if(!RemapOptionalIndex(ref.skin, skin, "skin"))
            return false;
    }
    return true;
}

[[nodiscard]] bool CanonicalizeSkinnedMeshStreams(
    SourceMeshStreams& mesh,
    UtilityVector<MeshSkinInfluence>& skinInfluences,
    Core::Alloc::ThreadPool& threadPool,
    SourceMeshCanonicalizeReport* const outReport
){
    if(mesh.positions.size() != skinInfluences.size()){
        NWB_LOGGER_ERROR(NWB_TEXT("Failed to canonicalize skinned mesh: position and skin influence counts must match"));
        return false;
    }

    if(outReport){
        outReport->before = CountSourceMeshStreams(mesh);
        outReport->before.skin = skinInfluences.size();
    }

    UtilityVector<u32> positionRemap(mesh.positions.size());
    UtilityVector<u32> normalRemap;
    UtilityVector<u32> tangentRemap;
    UtilityVector<u32> uv0Remap;
    UtilityVector<u32> colorRemap;
    UtilityVector<u32> skinRemap(skinInfluences.size());

    for(usize i = 0u; i < positionRemap.size(); ++i)
        positionRemap[i] = static_cast<u32>(i);
    for(usize i = 0u; i < skinRemap.size(); ++i)
        skinRemap[i] = static_cast<u32>(i);

    if(!DeduplicateStream(mesh.normals, threadPool, normalRemap, "normal"))
        return false;
    if(!DeduplicateStream(mesh.tangents, threadPool, tangentRemap, "tangent"))
        return false;
    if(!DeduplicateStream(mesh.uv0, threadPool, uv0Remap, "uv0"))
        return false;
    if(!DeduplicateStream(mesh.colors, threadPool, colorRemap, "color"))
        return false;

    if(!RemapComponentRefs(mesh, positionRemap, normalRemap, tangentRemap, uv0Remap, colorRemap, skinRemap))
        return false;

    if(outReport){
        outReport->after = CountSourceMeshStreams(mesh);
        outReport->after.skin = skinInfluences.size();
    }
    return true;
}

[[nodiscard]] bool ParseFiniteF32(
    const Path& nwbFilePath,
    const Core::Metascript::Value& value,
    const AStringView label,
    f32& outValue
){
    if(!value.isNumeric()){
        NWB_LOGGER_ERROR(NWB_TEXT("{} meta '{}': '{}' must contain only numeric values")
            , StringConvert(s_MeshMetaKind)
            , PathToString<tchar>(nwbFilePath)
            , StringConvert(label)
        );
        return false;
    }

    const f64 numericValue = value.toDouble();
    if(!IsFinite(numericValue) || numericValue < static_cast<f64>(s_MinF32) || numericValue > static_cast<f64>(s_MaxF32)){
        NWB_LOGGER_ERROR(NWB_TEXT("{} meta '{}': '{}' contains a non-finite or out-of-range f32 value")
            , StringConvert(s_MeshMetaKind)
            , PathToString<tchar>(nwbFilePath)
            , StringConvert(label)
        );
        return false;
    }

    outValue = static_cast<f32>(numericValue);
    return true;
}

[[nodiscard]] bool ParseU32(
    const Path& nwbFilePath,
    const Core::Metascript::Value& value,
    const AStringView label,
    u32& outValue
){
    if(!value.isNumeric()){
        NWB_LOGGER_ERROR(NWB_TEXT("{} meta '{}': '{}' must contain only integer values")
            , StringConvert(s_MeshMetaKind)
            , PathToString<tchar>(nwbFilePath)
            , StringConvert(label)
        );
        return false;
    }

    const f64 numericValue = value.toDouble();
    if(!IsFinite(numericValue) || numericValue < 0.0 || numericValue != Floor(numericValue) || numericValue > static_cast<f64>(s_MaxU32)){
        NWB_LOGGER_ERROR(NWB_TEXT("{} meta '{}': '{}' contains a non-integer, negative, or out-of-range u32 value")
            , StringConvert(s_MeshMetaKind)
            , PathToString<tchar>(nwbFilePath)
            , StringConvert(label)
        );
        return false;
    }

    outValue = static_cast<u32>(numericValue);
    return true;
}

[[nodiscard]] AString MakeIndexedLabel(const AStringView fieldName, const usize index){
    AStringStream out;
    out << fieldName << "[" << index << "]";
    return out.str();
}

[[nodiscard]] const Core::Metascript::Value* FindRequiredListField(
    const Path& nwbFilePath,
    const Core::Metascript::Value& asset,
    const AStringView fieldName
){
    const Core::Metascript::Value* field = Core::Metascript::FindField(asset, fieldName);
    if(!field){
        NWB_LOGGER_ERROR(NWB_TEXT("{} meta '{}': missing required '{}' field")
            , StringConvert(s_MeshMetaKind)
            , PathToString<tchar>(nwbFilePath)
            , StringConvert(fieldName)
        );
        return nullptr;
    }
    if(!field->isList()){
        NWB_LOGGER_ERROR(NWB_TEXT("{} meta '{}': '{}' must be a list")
            , StringConvert(s_MeshMetaKind)
            , PathToString<tchar>(nwbFilePath)
            , StringConvert(fieldName)
        );
        return nullptr;
    }
    return field;
}

template<typename ElementT, usize ComponentCount>
[[nodiscard]] bool ParseFloatListField(
    const Path& nwbFilePath,
    const Core::Metascript::Value& asset,
    const AStringView fieldName,
    UtilityVector<ElementT>& outValues
){
    outValues.clear();

    const Core::Metascript::Value* field = FindRequiredListField(nwbFilePath, asset, fieldName);
    if(!field)
        return false;

    const auto& list = field->asList();
    outValues.reserve(list.size());
    for(usize i = 0u; i < list.size(); ++i){
        const Core::Metascript::Value& value = list[i];
        if(!value.isList() || value.asList().size() != ComponentCount){
            NWB_LOGGER_ERROR(NWB_TEXT("{} meta '{}': '{}' must be a {}-component list")
                , StringConvert(s_MeshMetaKind)
                , PathToString<tchar>(nwbFilePath)
                , StringConvert(MakeIndexedLabel(fieldName, i))
                , ComponentCount
            );
            return false;
        }

        f32 tuple[ComponentCount] = {};
        const auto& components = value.asList();
        for(usize componentIndex = 0u; componentIndex < ComponentCount; ++componentIndex){
            AStringStream label;
            label << fieldName << "[" << i << "][" << componentIndex << "]";
            if(!ParseFiniteF32(nwbFilePath, components[componentIndex], label.str(), tuple[componentIndex]))
                return false;
        }

        ElementT element;
        element.x = tuple[0u];
        element.y = tuple[1u];
        if constexpr(ComponentCount >= 3u)
            element.z = tuple[2u];
        if constexpr(ComponentCount >= 4u)
            element.w = tuple[3u];
        outValues.push_back(element);
    }

    if(outValues.empty()){
        NWB_LOGGER_ERROR(NWB_TEXT("{} meta '{}': '{}' must not be empty")
            , StringConvert(s_MeshMetaKind)
            , PathToString<tchar>(nwbFilePath)
            , StringConvert(fieldName)
        );
        return false;
    }
    return true;
}

[[nodiscard]] bool ParseVertexRefs(
    const Path& nwbFilePath,
    const Core::Metascript::Value& asset,
    UtilityVector<SourceVertexRef>& outVertexRefs
){
    outVertexRefs.clear();

    const Core::Metascript::Value* field = FindRequiredListField(nwbFilePath, asset, "vertex_refs");
    if(!field)
        return false;

    const auto& list = field->asList();
    outVertexRefs.reserve(list.size());
    for(usize i = 0u; i < list.size(); ++i){
        const Core::Metascript::Value& value = list[i];
        if(!value.isList() || value.asList().size() != 5u){
            NWB_LOGGER_ERROR(NWB_TEXT("{} meta '{}': 'vertex_refs[{}]' must contain 5 integer stream indices")
                , StringConvert(s_MeshMetaKind)
                , PathToString<tchar>(nwbFilePath)
                , i
            );
            return false;
        }

        SourceVertexRef ref;
        const auto& components = value.asList();
        u32* const componentValues[] = {
            &ref.position,
            &ref.normal,
            &ref.tangent,
            &ref.uv0,
            &ref.color,
        };
        for(usize componentIndex = 0u; componentIndex < 5u; ++componentIndex){
            AStringStream label;
            label << "vertex_refs[" << i << "][" << componentIndex << "]";
            if(!ParseU32(nwbFilePath, components[componentIndex], label.str(), *componentValues[componentIndex]))
                return false;
        }

        outVertexRefs.push_back(ref);
    }

    if(outVertexRefs.empty()){
        NWB_LOGGER_ERROR(NWB_TEXT("{} meta '{}': 'vertex_refs' must not be empty")
            , StringConvert(s_MeshMetaKind)
            , PathToString<tchar>(nwbFilePath)
        );
        return false;
    }
    return true;
}

[[nodiscard]] bool ParseSkinInfluences(
    const Path& nwbFilePath,
    const Core::Metascript::Value& skinAsset,
    const AStringView skinVariableName,
    UtilityVector<MeshSkinInfluence>& outInfluences
){
    outInfluences.clear();

    const Core::Metascript::Value* field = FindRequiredListField(nwbFilePath, skinAsset, "influences");
    if(!field)
        return false;

    const auto& list = field->asList();
    outInfluences.reserve(list.size());
    for(usize i = 0u; i < list.size(); ++i){
        const Core::Metascript::Value& influenceValue = list[i];
        if(!influenceValue.isMap()){
            NWB_LOGGER_ERROR(NWB_TEXT("{} meta '{}': '{}.influences[{}]' must be a map")
                , StringConvert(s_MeshMetaKind)
                , PathToString<tchar>(nwbFilePath)
                , StringConvert(skinVariableName)
                , i
            );
            return false;
        }

        const Core::Metascript::Value* jointsValue = Core::Metascript::FindField(influenceValue, "joints");
        const Core::Metascript::Value* weightsValue = Core::Metascript::FindField(influenceValue, "weights");
        if(!jointsValue || !jointsValue->isList() || jointsValue->asList().size() != 4u){
            NWB_LOGGER_ERROR(NWB_TEXT("{} meta '{}': '{}.influences[{}].joints' must contain four integers")
                , StringConvert(s_MeshMetaKind)
                , PathToString<tchar>(nwbFilePath)
                , StringConvert(skinVariableName)
                , i
            );
            return false;
        }
        if(!weightsValue || !weightsValue->isList() || weightsValue->asList().size() != 4u){
            NWB_LOGGER_ERROR(NWB_TEXT("{} meta '{}': '{}.influences[{}].weights' must contain four numeric values")
                , StringConvert(s_MeshMetaKind)
                , PathToString<tchar>(nwbFilePath)
                , StringConvert(skinVariableName)
                , i
            );
            return false;
        }

        MeshSkinInfluence influence;
        for(usize componentIndex = 0u; componentIndex < 4u; ++componentIndex){
            u32 jointIndex = 0u;
            AStringStream jointLabel;
            jointLabel << skinVariableName << ".influences[" << i << "].joints[" << componentIndex << "]";
            if(!ParseU32(nwbFilePath, jointsValue->asList()[componentIndex], jointLabel.str(), jointIndex))
                return false;
            if(jointIndex > static_cast<u32>(Limit<u16>::s_Max)){
                NWB_LOGGER_ERROR(NWB_TEXT("{} meta '{}': '{}' contains an out-of-range joint index")
                    , StringConvert(s_MeshMetaKind)
                    , PathToString<tchar>(nwbFilePath)
                    , StringConvert(jointLabel.str())
                );
                return false;
            }

            AStringStream weightLabel;
            weightLabel << skinVariableName << ".influences[" << i << "].weights[" << componentIndex << "]";
            influence.joint[componentIndex] = static_cast<u16>(jointIndex);
            if(!ParseFiniteF32(nwbFilePath, weightsValue->asList()[componentIndex], weightLabel.str(), influence.weight[componentIndex]))
                return false;
        }

        outInfluences.push_back(influence);
    }

    if(outInfluences.empty()){
        NWB_LOGGER_ERROR(NWB_TEXT("{} meta '{}': '{}.influences' must not be empty")
            , StringConvert(s_MeshMetaKind)
            , PathToString<tchar>(nwbFilePath)
            , StringConvert(skinVariableName)
        );
        return false;
    }
    return true;
}

[[nodiscard]] bool FillIndicesRecursive(
    const Path& nwbFilePath,
    const Core::Metascript::Value& value,
    const AStringView label,
    UtilityVector<u32>& outIndices
){
    if(value.isList()){
        const auto& list = value.asList();
        for(usize i = 0u; i < list.size(); ++i){
            AStringStream childLabel;
            childLabel << label << "[" << i << "]";
            if(!FillIndicesRecursive(nwbFilePath, list[i], childLabel.str(), outIndices))
                return false;
        }
        return true;
    }

    u32 index = 0u;
    if(!ParseU32(nwbFilePath, value, label, index))
        return false;
    outIndices.push_back(index);
    return true;
}

[[nodiscard]] bool ParseIndices(
    const Path& nwbFilePath,
    const Core::Metascript::Value& asset,
    UtilityVector<u32>& outIndices
){
    outIndices.clear();

    const Core::Metascript::Value* field = FindRequiredListField(nwbFilePath, asset, "indices");
    if(!field)
        return false;
    if(!FillIndicesRecursive(nwbFilePath, *field, "indices", outIndices))
        return false;
    if(outIndices.empty() || (outIndices.size() % 3u) != 0u){
        NWB_LOGGER_ERROR(NWB_TEXT("{} meta '{}': 'indices' must contain whole triangles")
            , StringConvert(s_MeshMetaKind)
            , PathToString<tchar>(nwbFilePath)
        );
        return false;
    }
    return true;
}

[[nodiscard]] bool ValidateStreamIndex(
    const Path& nwbFilePath,
    const AStringView streamName,
    const u32 index,
    const usize count
){
    if(index < count)
        return true;

    NWB_LOGGER_ERROR(NWB_TEXT("{} meta '{}': vertex_ref {} index is out of range")
        , StringConvert(s_MeshMetaKind)
        , PathToString<tchar>(nwbFilePath)
        , StringConvert(streamName)
    );
    return false;
}

[[nodiscard]] bool ValidateMesh(const Path& nwbFilePath, const SourceMeshStreams& mesh){
    for(const SourceVertexRef& ref : mesh.vertexRefs){
        if(!ValidateStreamIndex(nwbFilePath, "position", ref.position, mesh.positions.size()))
            return false;
        if(!ValidateStreamIndex(nwbFilePath, "normal", ref.normal, mesh.normals.size()))
            return false;
        if(!ValidateStreamIndex(nwbFilePath, "tangent", ref.tangent, mesh.tangents.size()))
            return false;
        if(!ValidateStreamIndex(nwbFilePath, "uv0", ref.uv0, mesh.uv0.size()))
            return false;
        if(!ValidateStreamIndex(nwbFilePath, "color", ref.color, mesh.colors.size()))
            return false;
        if(ref.skin != s_MissingSourceStreamIndex){
            NWB_LOGGER_ERROR(NWB_TEXT("{} meta '{}': plain mesh vertex_ref cannot contain a skin index")
                , StringConvert(s_MeshMetaKind)
                , PathToString<tchar>(nwbFilePath)
            );
            return false;
        }
    }

    for(const u32 index : mesh.indices){
        if(index < mesh.vertexRefs.size())
            continue;

        NWB_LOGGER_ERROR(NWB_TEXT("{} meta '{}': 'indices' references an out-of-range vertex_ref")
            , StringConvert(s_MeshMetaKind)
            , PathToString<tchar>(nwbFilePath)
        );
        return false;
    }
    return true;
}

[[nodiscard]] bool IsAllowedMeshAssetField(const Core::Metascript::MStringView fieldName){
    return fieldName == Core::Metascript::MStringView("positions", 9u)
        || fieldName == Core::Metascript::MStringView("normals", 7u)
        || fieldName == Core::Metascript::MStringView("tangents", 8u)
        || fieldName == Core::Metascript::MStringView("uv0", 3u)
        || fieldName == Core::Metascript::MStringView("colors", 6u)
        || fieldName == Core::Metascript::MStringView("vertex_refs", 11u)
        || fieldName == Core::Metascript::MStringView("indices", 7u)
    ;
}

[[nodiscard]] bool ValidateMeshAssetFields(
    const Path& nwbFilePath,
    const AStringView meshVariableName,
    const Core::Metascript::Value& asset
){
    if(!asset.isMap()){
        NWB_LOGGER_ERROR(NWB_TEXT("{} meta '{}': '{}' is not a map")
            , StringConvert(s_MeshMetaKind)
            , PathToString<tchar>(nwbFilePath)
            , StringConvert(meshVariableName)
        );
        return false;
    }

    for(const auto& field : asset.asMap()){
        const Core::Metascript::MStringView fieldName(field.first.data(), field.first.size());
        if(IsAllowedMeshAssetField(fieldName))
            continue;

        NWB_LOGGER_ERROR(NWB_TEXT("{} meta '{}': unsupported field '{}.{}' would be dropped by refresh")
            , StringConvert(s_MeshMetaKind)
            , PathToString<tchar>(nwbFilePath)
            , StringConvert(meshVariableName)
            , StringConvert(AStringView(field.first.data(), field.first.size()))
        );
        return false;
    }
    return true;
}

[[nodiscard]] bool ParseMeshValue(
    const Path& nwbFilePath,
    const AStringView meshVariableName,
    const Core::Metascript::Value& asset,
    SourceMeshStreams& outMesh
){
    if(!ValidateMeshAssetFields(nwbFilePath, meshVariableName, asset))
        return false;
    return ParseFloatListField<Vec3, 3u>(nwbFilePath, asset, "positions", outMesh.positions)
        && ParseFloatListField<Vec3, 3u>(nwbFilePath, asset, "normals", outMesh.normals)
        && ParseFloatListField<Vec4, 4u>(nwbFilePath, asset, "tangents", outMesh.tangents)
        && ParseFloatListField<Vec2, 2u>(nwbFilePath, asset, "uv0", outMesh.uv0)
        && ParseFloatListField<Vec4, 4u>(nwbFilePath, asset, "colors", outMesh.colors)
        && ParseVertexRefs(nwbFilePath, asset, outMesh.vertexRefs)
        && ParseIndices(nwbFilePath, asset, outMesh.indices)
        && ValidateMesh(nwbFilePath, outMesh)
    ;
}

[[nodiscard]] bool ReadMetascriptSource(const Path& nwbFilePath, AString& outText){
    outText.clear();
    if(!ReadTextFile(nwbFilePath, outText)){
        NWB_LOGGER_ERROR(NWB_TEXT("Failed to refresh NWB mesh: failed to read '{}'"), PathToString<tchar>(nwbFilePath));
        return false;
    }

    StripUtf8Bom(outText);
    return true;
}

[[nodiscard]] bool ParseMetascriptDocument(
    const Path& nwbFilePath,
    const AString& text,
    Core::Metascript::Document& outDoc
){
    if(outDoc.parse(AStringView(text)))
        return true;

    for(const Core::Metascript::ParseError& error : outDoc.errors()){
        NWB_LOGGER_ERROR(NWB_TEXT("Failed to refresh NWB mesh: '{}' parse error at {}:{}: {}")
            , PathToString<tchar>(nwbFilePath)
            , error.line
            , error.column
            , StringConvert(AStringView(error.message.data(), error.message.size()))
        );
    }
    return false;
}

[[nodiscard]] AString ToAString(const Core::Metascript::MStringView text){
    return AString(text.data(), text.size());
}

[[nodiscard]] bool IsSameText(const Core::Metascript::MStringView lhs, const AStringView rhs){
    if(lhs.size() != rhs.size())
        return false;
    for(usize i = 0u; i < lhs.size(); ++i){
        if(lhs[i] != rhs[i])
            return false;
    }
    return true;
}

[[nodiscard]] bool IsWhitespace(const char c){
    return c == ' ' || c == '\t' || c == '\r' || c == '\n';
}

[[nodiscard]] usize SkipWhitespace(const AString& text, usize offset){
    while(offset < text.size() && IsWhitespace(text[offset]))
        ++offset;
    return offset;
}

[[nodiscard]] bool IsIdentifierChar(const char c){
    return (c >= 'a' && c <= 'z')
        || (c >= 'A' && c <= 'Z')
        || (c >= '0' && c <= '9')
        || c == '_'
    ;
}

[[nodiscard]] bool HasIdentifierBoundary(const AString& text, const usize begin, const usize end){
    const bool leftOk = begin == 0u || !IsIdentifierChar(text[begin - 1u]);
    const bool rightOk = end >= text.size() || !IsIdentifierChar(text[end]);
    return leftOk && rightOk;
}

[[nodiscard]] bool FindListAssignmentRange(
    const AString& source,
    const AStringView variableName,
    const AStringView fieldName,
    TextReplacement& outRange
){
    AString pattern;
    pattern.reserve(variableName.size() + fieldName.size() + 1u);
    pattern.append(variableName.data(), variableName.size());
    pattern.push_back('.');
    pattern.append(fieldName.data(), fieldName.size());

    usize offset = 0u;
    while(offset < source.size()){
        const usize found = source.find(pattern, offset);
        if(found == AString::npos)
            break;

        const usize patternEnd = found + pattern.size();
        if(!HasIdentifierBoundary(source, found, patternEnd)){
            offset = patternEnd;
            continue;
        }

        usize cursor = SkipWhitespace(source, patternEnd);
        if(cursor >= source.size() || source[cursor] != '='){
            offset = patternEnd;
            continue;
        }
        cursor = SkipWhitespace(source, cursor + 1u);
        if(cursor >= source.size() || source[cursor] != '['){
            offset = patternEnd;
            continue;
        }

        const usize listBegin = cursor;
        u32 depth = 0u;
        bool inString = false;
        bool escaped = false;
        for(; cursor < source.size(); ++cursor){
            const char c = source[cursor];
            if(inString){
                if(escaped){
                    escaped = false;
                    continue;
                }
                if(c == '\\'){
                    escaped = true;
                    continue;
                }
                if(c == '"')
                    inString = false;
                continue;
            }

            if(c == '"'){
                inString = true;
                continue;
            }
            if(c == '['){
                ++depth;
                continue;
            }
            if(c != ']')
                continue;

            if(depth == 0u)
                return false;
            --depth;
            if(depth == 0u){
                outRange.begin = listBegin;
                outRange.end = cursor + 1u;
                return true;
            }
        }
        return false;
    }

    return false;
}

template<typename Value, typename WriteValue>
[[nodiscard]] AString WriteValueList(const UtilityVector<Value>& values, WriteValue&& writeValue){
    AStringStream out;
    out.precision(9);
    out << "[\n";
    for(const Value& value : values){
        out << "    ";
        writeValue(out, value);
        out << ",\n";
    }
    out << "]";
    return out.str();
}

[[nodiscard]] AString WriteIndexList(const UtilityVector<u32>& indices){
    AStringStream out;
    out.precision(9);
    out << "[\n";
    for(usize i = 0u; i < indices.size(); i += 3u)
        out << "    [" << indices[i] << ", " << indices[i + 1u] << ", " << indices[i + 2u] << "],\n";
    out << "]";
    return out.str();
}

[[nodiscard]] AString WriteVertexRefList(const UtilityVector<SourceVertexRef>& refs){
    AStringStream out;
    out.precision(9);
    out << "[\n";
    for(const SourceVertexRef& ref : refs){
        out
            << "    ["
            << ref.position << ", "
            << ref.normal << ", "
            << ref.tangent << ", "
            << ref.uv0 << ", "
            << ref.color
            << "],\n"
        ;
    }
    out << "]";
    return out.str();
}

[[nodiscard]] AString WriteSkinInfluenceList(const UtilityVector<MeshSkinInfluence>& influences){
    AStringStream out;
    out.precision(9);
    out << "[\n";
    for(const MeshSkinInfluence& influence : influences){
        out
            << "    { \"joints\": ["
            << influence.joint[0u] << ", "
            << influence.joint[1u] << ", "
            << influence.joint[2u] << ", "
            << influence.joint[3u] << "], \"weights\": ["
        ;
        WriteFloat(out, influence.weight[0u]);
        out << ", ";
        WriteFloat(out, influence.weight[1u]);
        out << ", ";
        WriteFloat(out, influence.weight[2u]);
        out << ", ";
        WriteFloat(out, influence.weight[3u]);
        out << "] },\n";
    }
    out << "]";
    return out.str();
}

[[nodiscard]] bool AddReplacement(
    UtilityVector<TextReplacement>& replacements,
    const AString& source,
    const AStringView variableName,
    const AStringView fieldName,
    AString&& replacementText
){
    TextReplacement replacement;
    if(!FindListAssignmentRange(source, variableName, fieldName, replacement)){
        NWB_LOGGER_ERROR(NWB_TEXT("Failed to refresh NWB mesh: missing '{}.{}' assignment")
            , StringConvert(variableName)
            , StringConvert(fieldName)
        );
        return false;
    }

    replacement.text = Move(replacementText);
    replacements.push_back(Move(replacement));
    return true;
}

[[nodiscard]] bool AppendMeshReplacements(
    UtilityVector<TextReplacement>& replacements,
    const AString& source,
    const AStringView variableName,
    const SourceMeshStreams& before,
    const SourceMeshStreams& after
){
    if(before.positions.size() != after.positions.size() && !AddReplacement(replacements, source, variableName, "positions", WriteValueList(after.positions, [](AStringStream& out, const Vec3& value){ WriteVec3(out, value); })))
        return false;
    if(before.normals.size() != after.normals.size() && !AddReplacement(replacements, source, variableName, "normals", WriteValueList(after.normals, [](AStringStream& out, const Vec3& value){ WriteVec3(out, value); })))
        return false;
    if(before.tangents.size() != after.tangents.size() && !AddReplacement(replacements, source, variableName, "tangents", WriteValueList(after.tangents, [](AStringStream& out, const Vec4& value){ WriteVec4(out, value); })))
        return false;
    if(before.uv0.size() != after.uv0.size() && !AddReplacement(replacements, source, variableName, "uv0", WriteValueList(after.uv0, [](AStringStream& out, const Vec2& value){ WriteVec2(out, value); })))
        return false;
    if(before.colors.size() != after.colors.size() && !AddReplacement(replacements, source, variableName, "colors", WriteValueList(after.colors, [](AStringStream& out, const Vec4& value){ WriteVec4(out, value); })))
        return false;

    const bool componentRefsChanged =
        before.positions.size() != after.positions.size()
        || before.normals.size() != after.normals.size()
        || before.tangents.size() != after.tangents.size()
        || before.uv0.size() != after.uv0.size()
        || before.colors.size() != after.colors.size()
        || before.vertexRefs.size() != after.vertexRefs.size()
    ;
    if(componentRefsChanged && !AddReplacement(replacements, source, variableName, "vertex_refs", WriteVertexRefList(after.vertexRefs)))
        return false;
    if(before.vertexRefs.size() != after.vertexRefs.size() && !AddReplacement(replacements, source, variableName, "indices", WriteIndexList(after.indices)))
        return false;

    return true;
}

[[nodiscard]] bool ApplyTextReplacements(AString& inOutSource, UtilityVector<TextReplacement>& replacements){
    Sort(
        replacements.begin(),
        replacements.end(),
        [](const TextReplacement& lhs, const TextReplacement& rhs){
            return lhs.begin > rhs.begin;
        }
    );

    for(const TextReplacement& replacement : replacements){
        if(replacement.begin > replacement.end || replacement.end > inOutSource.size())
            return false;
        inOutSource.replace(replacement.begin, replacement.end - replacement.begin, replacement.text);
    }
    return true;
}

[[nodiscard]] bool PrefersCrlf(const AString& source){
    return source.find("\r\n") != AString::npos;
}

void NormalizeLineEndings(AString& inOutSource, const bool useCrlf){
    AString normalized;
    normalized.reserve(useCrlf ? inOutSource.size() + inOutSource.size() / 16u : inOutSource.size());

    for(usize i = 0u; i < inOutSource.size(); ++i){
        const char c = inOutSource[i];
        if(c == '\r'){
            if(i + 1u < inOutSource.size() && inOutSource[i + 1u] == '\n')
                ++i;
            if(useCrlf)
                normalized.append("\r\n", 2u);
            else
                normalized.push_back('\n');
            continue;
        }
        if(c == '\n'){
            if(useCrlf)
                normalized.append("\r\n", 2u);
            else
                normalized.push_back('\n');
            continue;
        }
        normalized.push_back(c);
    }

    inOutSource = Move(normalized);
}

[[nodiscard]] bool IsReferenceTo(const Core::Metascript::Value& value, const AStringView variableName){
    if(value.isReference()){
        const Core::Metascript::MStringView reference = value.asReference();
        return IsSameText(reference, variableName);
    }
    if(value.isString()){
        const Core::Metascript::MStringView text = value.asString();
        return IsSameText(text, variableName);
    }
    return false;
}

[[nodiscard]] const Core::Metascript::Value* FindSkinForMesh(
    const Core::Metascript::Document& doc,
    const AStringView meshVariableName,
    AString& outSkinVariableName
){
    const Core::Metascript::Value* result = nullptr;
    outSkinVariableName.clear();

    for(const Core::Metascript::Document::Declaration& declaration : doc.declarations()){
        if(!IsSameText(Core::Metascript::MStringView(declaration.type.data(), declaration.type.size()), "skin"))
            continue;

        const Core::Metascript::MStringView skinVariable(declaration.variable.data(), declaration.variable.size());
        const Core::Metascript::Value* skinAsset = doc.findVariable(skinVariable);
        if(!skinAsset || !skinAsset->isMap())
            continue;

        const Core::Metascript::Value* meshField = Core::Metascript::FindField(*skinAsset, "mesh");
        if(!meshField || !IsReferenceTo(*meshField, meshVariableName))
            continue;

        if(result){
            NWB_LOGGER_ERROR(NWB_TEXT("Failed to refresh NWB mesh: mesh '{}' has multiple skin assets"), StringConvert(meshVariableName));
            return nullptr;
        }

        result = skinAsset;
        outSkinVariableName = ToAString(skinVariable);
    }

    return result;
}

void AccumulateReport(SourceMeshCanonicalizeReport& total, const SourceMeshCanonicalizeReport& item){
    total.before.positions += item.before.positions;
    total.before.normals += item.before.normals;
    total.before.tangents += item.before.tangents;
    total.before.uv0 += item.before.uv0;
    total.before.colors += item.before.colors;
    total.before.skin += item.before.skin;
    total.before.vertexRefs += item.before.vertexRefs;
    total.before.indices += item.before.indices;

    total.after.positions += item.after.positions;
    total.after.normals += item.after.normals;
    total.after.tangents += item.after.tangents;
    total.after.uv0 += item.after.uv0;
    total.after.colors += item.after.colors;
    total.after.skin += item.after.skin;
    total.after.vertexRefs += item.after.vertexRefs;
    total.after.indices += item.after.indices;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


SourceMeshStreamCounts CountSourceMeshStreams(const SourceMeshStreams& mesh){
    SourceMeshStreamCounts counts;
    counts.positions = mesh.positions.size();
    counts.normals = mesh.normals.size();
    counts.tangents = mesh.tangents.size();
    counts.uv0 = mesh.uv0.size();
    counts.colors = mesh.colors.size();
    counts.skin = mesh.skin.size();
    counts.vertexRefs = mesh.vertexRefs.size();
    counts.indices = mesh.indices.size();
    return counts;
}

bool CanonicalizeSourceMeshStreams(SourceMeshStreams& mesh, Core::Alloc::ThreadPool& threadPool, SourceMeshCanonicalizeReport* const outReport){
    if(outReport)
        outReport->before = CountSourceMeshStreams(mesh);

    UtilityVector<u32> positionRemap;
    UtilityVector<u32> normalRemap;
    UtilityVector<u32> tangentRemap;
    UtilityVector<u32> uv0Remap;
    UtilityVector<u32> colorRemap;
    UtilityVector<u32> skinRemap;

    if(!__hidden_mesh_refresh::DeduplicateStream(mesh.positions, threadPool, positionRemap, "position"))
        return false;
    if(!__hidden_mesh_refresh::DeduplicateStream(mesh.normals, threadPool, normalRemap, "normal"))
        return false;
    if(!__hidden_mesh_refresh::DeduplicateStream(mesh.tangents, threadPool, tangentRemap, "tangent"))
        return false;
    if(!__hidden_mesh_refresh::DeduplicateStream(mesh.uv0, threadPool, uv0Remap, "uv0"))
        return false;
    if(!__hidden_mesh_refresh::DeduplicateStream(mesh.colors, threadPool, colorRemap, "color"))
        return false;
    if(!__hidden_mesh_refresh::DeduplicateStream(mesh.skin, threadPool, skinRemap, "skin"))
        return false;
    if(!__hidden_mesh_refresh::RemapComponentRefs(mesh, positionRemap, normalRemap, tangentRemap, uv0Remap, colorRemap, skinRemap))
        return false;

    if(outReport)
        outReport->after = CountSourceMeshStreams(mesh);
    return true;
}

bool RefreshNwbMeshAsset(const Path& inputPath, const Path& outputPath, Core::Alloc::ThreadPool& threadPool, SourceMeshCanonicalizeReport& outReport){
    outReport = SourceMeshCanonicalizeReport{};

    AString source;
    if(!__hidden_mesh_refresh::ReadMetascriptSource(inputPath, source))
        return false;
    const bool useCrlf = __hidden_mesh_refresh::PrefersCrlf(source);

    Core::Metascript::MetaArena metaArena(UtilityDetail::s_UtilityArena);
    Core::Metascript::Document doc(metaArena);
    if(!__hidden_mesh_refresh::ParseMetascriptDocument(inputPath, source, doc))
        return false;

    UtilityVector<__hidden_mesh_refresh::TextReplacement> replacements;
    bool sawMesh = false;

    for(const Core::Metascript::Document::Declaration& declaration : doc.declarations()){
        const Core::Metascript::MStringView typeName(declaration.type.data(), declaration.type.size());
        if(!__hidden_mesh_refresh::IsSameText(typeName, "mesh"))
            continue;

        sawMesh = true;

        const Core::Metascript::MStringView meshVariableView(declaration.variable.data(), declaration.variable.size());
        const AString meshVariableName = __hidden_mesh_refresh::ToAString(meshVariableView);
        const Core::Metascript::Value* meshValue = doc.findVariable(meshVariableView);
        if(!meshValue){
            NWB_LOGGER_ERROR(NWB_TEXT("Failed to refresh NWB mesh: missing mesh variable '{}'"), StringConvert(meshVariableName));
            return false;
        }

        SourceMeshStreams mesh;
        if(!__hidden_mesh_refresh::ParseMeshValue(inputPath, meshVariableName, *meshValue, mesh))
            return false;

        SourceMeshStreams before = mesh;
        SourceMeshCanonicalizeReport itemReport;

        AString skinVariableName;
        const Core::Metascript::Value* skinValue = __hidden_mesh_refresh::FindSkinForMesh(doc, meshVariableName, skinVariableName);
        if(!skinVariableName.empty()){
            UtilityVector<MeshSkinInfluence> skinInfluences;
            if(!skinValue || !__hidden_mesh_refresh::ParseSkinInfluences(inputPath, *skinValue, skinVariableName, skinInfluences))
                return false;
            if(!__hidden_mesh_refresh::CanonicalizeSkinnedMeshStreams(mesh, skinInfluences, threadPool, &itemReport))
                return false;
            if(!__hidden_mesh_refresh::AppendMeshReplacements(replacements, source, meshVariableName, before, mesh))
                return false;
            if(itemReport.before.skin != itemReport.after.skin && !__hidden_mesh_refresh::AddReplacement(
                replacements,
                source,
                skinVariableName,
                "influences",
                __hidden_mesh_refresh::WriteSkinInfluenceList(skinInfluences)
            )){
                return false;
            }
        }
        else{
            if(!CanonicalizeSourceMeshStreams(mesh, threadPool, &itemReport))
                return false;
            if(!__hidden_mesh_refresh::AppendMeshReplacements(replacements, source, meshVariableName, before, mesh))
                return false;
        }

        __hidden_mesh_refresh::AccumulateReport(outReport, itemReport);
    }

    if(!sawMesh){
        NWB_LOGGER_ERROR(NWB_TEXT("Failed to refresh NWB mesh: '{}' contains no mesh declarations"), PathToString<tchar>(inputPath));
        return false;
    }

    if(!__hidden_mesh_refresh::ApplyTextReplacements(source, replacements)){
        NWB_LOGGER_ERROR(NWB_TEXT("Failed to refresh NWB mesh: failed to apply text replacements for '{}'"), PathToString<tchar>(inputPath));
        return false;
    }
    __hidden_mesh_refresh::NormalizeLineEndings(source, useCrlf);

    return WriteTextFile(outputPath, AStringView(source.data(), source.size()));
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_FBX_TO_NWB_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

