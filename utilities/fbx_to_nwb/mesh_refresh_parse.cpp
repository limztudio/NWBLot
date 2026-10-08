// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "mesh_refresh_parse.h"

#include <core/common/log.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_FBX_TO_NWB_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace MeshRefreshParseDetail{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


static constexpr AStringView s_MeshMetaKind = "Mesh";
static constexpr AStringView s_PositionsField = "positions";
static constexpr AStringView s_NormalsField = "normals";
static constexpr AStringView s_TangentsField = "tangents";
static constexpr AStringView s_Uv0Field = "uv0";
static constexpr AStringView s_ColorsField = "colors";
static constexpr AStringView s_VertexRefsField = "vertex_refs";
static constexpr AStringView s_IndicesField = "indices";
static constexpr AStringView s_PositionField = "position";
static constexpr AStringView s_NormalField = "normal";
static constexpr AStringView s_TangentField = "tangent";
static constexpr AStringView s_ColorField = "color";


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


[[nodiscard]] Expected<f32> ParseFiniteF32(
    const Path& nwbFilePath,
    const Core::Metascript::Value& value,
    const AStringView label
){
    if(!value.isNumeric()){
        NWB_LOGGER_ERROR(NWB_TEXT("{} meta '{}': '{}' must contain only numeric values")
            , StringConvert(s_MeshMetaKind)
            , PathToString<tchar>(nwbFilePath)
            , StringConvert(label)
        );
        return MakeUnexpected(Failure{});
    }

    const f64 numericValue = value.toDouble();
    if(!IsFinite(numericValue) || numericValue < static_cast<f64>(s_MinF32) || numericValue > static_cast<f64>(s_MaxF32)){
        NWB_LOGGER_ERROR(NWB_TEXT("{} meta '{}': '{}' contains a non-finite or out-of-range f32 value")
            , StringConvert(s_MeshMetaKind)
            , PathToString<tchar>(nwbFilePath)
            , StringConvert(label)
        );
        return MakeUnexpected(Failure{});
    }

    return static_cast<f32>(numericValue);
}

[[nodiscard]] Expected<u32> ParseU32(
    const Path& nwbFilePath,
    const Core::Metascript::Value& value,
    const AStringView label
){
    if(!value.isNumeric()){
        NWB_LOGGER_ERROR(NWB_TEXT("{} meta '{}': '{}' must contain only integer values")
            , StringConvert(s_MeshMetaKind)
            , PathToString<tchar>(nwbFilePath)
            , StringConvert(label)
        );
        return MakeUnexpected(Failure{});
    }

    const f64 numericValue = value.toDouble();
    if(!IsFinite(numericValue) || numericValue < 0.0 || numericValue != Floor(numericValue) || numericValue > static_cast<f64>(s_MaxU32)){
        NWB_LOGGER_ERROR(NWB_TEXT("{} meta '{}': '{}' contains a non-integer, negative, or out-of-range u32 value")
            , StringConvert(s_MeshMetaKind)
            , PathToString<tchar>(nwbFilePath)
            , StringConvert(label)
        );
        return MakeUnexpected(Failure{});
    }

    return static_cast<u32>(numericValue);
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
[[nodiscard]] Expected<UtilityVector<ElementT>> ParseFloatListField(
    const Path& nwbFilePath,
    const Core::Metascript::Value& asset,
    const AStringView fieldName
){
    UtilityVector<ElementT> outValues;

    const Core::Metascript::Value* field = FindRequiredListField(nwbFilePath, asset, fieldName);
    if(!field)
        return MakeUnexpected(Failure{});

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
            return MakeUnexpected(Failure{});
        }

        f32 tuple[ComponentCount] = {};
        const auto& components = value.asList();
        for(usize componentIndex = 0u; componentIndex < ComponentCount; ++componentIndex){
            AStringStream label;
            label << fieldName << "[" << i << "][" << componentIndex << "]";
            const auto component = ParseFiniteF32(nwbFilePath, components[componentIndex], label.str());
            if(!component)
                return MakeUnexpected(Failure{});
            tuple[componentIndex] = *component;
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
        return MakeUnexpected(Failure{});
    }
    return outValues;
}

[[nodiscard]] Expected<UtilityVector<SourceVertexRef>> ParseVertexRefs(
    const Path& nwbFilePath,
    const Core::Metascript::Value& asset
){
    UtilityVector<SourceVertexRef> outVertexRefs;

    const Core::Metascript::Value* field = FindRequiredListField(nwbFilePath, asset, s_VertexRefsField);
    if(!field)
        return MakeUnexpected(Failure{});

    const auto& list = field->asList();
    outVertexRefs.reserve(list.size());
    for(usize i = 0u; i < list.size(); ++i){
        const Core::Metascript::Value& value = list[i];
        if(!value.isList() || value.asList().size() != s_AuthoredVertexRefComponentCount){
            NWB_LOGGER_ERROR(NWB_TEXT("{} meta '{}': 'vertex_refs[{}]' must contain {} integer stream indices")
                , StringConvert(s_MeshMetaKind)
                , PathToString<tchar>(nwbFilePath)
                , i
                , s_AuthoredVertexRefComponentCount
            );
            return MakeUnexpected(Failure{});
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
        for(usize componentIndex = 0u; componentIndex < s_AuthoredVertexRefComponentCount; ++componentIndex){
            AStringStream label;
            label << "vertex_refs[" << i << "][" << componentIndex << "]";
            const auto component = ParseU32(nwbFilePath, components[componentIndex], label.str());
            if(!component)
                return MakeUnexpected(Failure{});
            *componentValues[componentIndex] = *component;
        }

        outVertexRefs.push_back(ref);
    }

    if(outVertexRefs.empty()){
        NWB_LOGGER_ERROR(NWB_TEXT("{} meta '{}': 'vertex_refs' must not be empty")
            , StringConvert(s_MeshMetaKind)
            , PathToString<tchar>(nwbFilePath)
        );
        return MakeUnexpected(Failure{});
    }
    return outVertexRefs;
}

[[nodiscard]] Expected<UtilityVector<MeshSkinInfluence>> ParseSkinInfluences(
    const Path& nwbFilePath,
    const Core::Metascript::Value& skinAsset,
    const AStringView skinVariableName
){
    UtilityVector<MeshSkinInfluence> outInfluences;

    const Core::Metascript::Value* field = FindRequiredListField(nwbFilePath, skinAsset, "influences");
    if(!field)
        return MakeUnexpected(Failure{});

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
            return MakeUnexpected(Failure{});
        }

        const Core::Metascript::Value* jointsValue = Core::Metascript::FindField(influenceValue, "joints");
        const Core::Metascript::Value* weightsValue = Core::Metascript::FindField(influenceValue, "weights");
        if(!jointsValue || !jointsValue->isList() || jointsValue->asList().size() != s_MeshSkinInfluenceCount){
            NWB_LOGGER_ERROR(NWB_TEXT("{} meta '{}': '{}.influences[{}].joints' must contain {} integers")
                , StringConvert(s_MeshMetaKind)
                , PathToString<tchar>(nwbFilePath)
                , StringConvert(skinVariableName)
                , i
                , s_MeshSkinInfluenceCount
            );
            return MakeUnexpected(Failure{});
        }
        if(!weightsValue || !weightsValue->isList() || weightsValue->asList().size() != s_MeshSkinInfluenceCount){
            NWB_LOGGER_ERROR(NWB_TEXT("{} meta '{}': '{}.influences[{}].weights' must contain {} numeric values")
                , StringConvert(s_MeshMetaKind)
                , PathToString<tchar>(nwbFilePath)
                , StringConvert(skinVariableName)
                , i
                , s_MeshSkinInfluenceCount
            );
            return MakeUnexpected(Failure{});
        }

        MeshSkinInfluence influence;
        for(usize componentIndex = 0u; componentIndex < s_MeshSkinInfluenceCount; ++componentIndex){
            AStringStream jointLabel;
            jointLabel << skinVariableName << ".influences[" << i << "].joints[" << componentIndex << "]";
            const auto jointIndex = ParseU32(nwbFilePath, jointsValue->asList()[componentIndex], jointLabel.str());
            if(!jointIndex)
                return MakeUnexpected(Failure{});
            if(*jointIndex > static_cast<u32>(Limit<u16>::s_Max)){
                NWB_LOGGER_ERROR(NWB_TEXT("{} meta '{}': '{}' contains an out-of-range joint index")
                    , StringConvert(s_MeshMetaKind)
                    , PathToString<tchar>(nwbFilePath)
                    , StringConvert(jointLabel.str())
                );
                return MakeUnexpected(Failure{});
            }

            AStringStream weightLabel;
            weightLabel << skinVariableName << ".influences[" << i << "].weights[" << componentIndex << "]";
            influence.joint[componentIndex] = static_cast<u16>(*jointIndex);
            const auto weight = ParseFiniteF32(nwbFilePath, weightsValue->asList()[componentIndex], weightLabel.str());
            if(!weight)
                return MakeUnexpected(Failure{});
            influence.weight.raw[componentIndex] = *weight;
        }

        outInfluences.push_back(influence);
    }

    if(outInfluences.empty()){
        NWB_LOGGER_ERROR(NWB_TEXT("{} meta '{}': '{}.influences' must not be empty")
            , StringConvert(s_MeshMetaKind)
            , PathToString<tchar>(nwbFilePath)
            , StringConvert(skinVariableName)
        );
        return MakeUnexpected(Failure{});
    }
    return outInfluences;
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

    const auto index = ParseU32(nwbFilePath, value, label);
    if(!index)
        return false;
    outIndices.push_back(*index);
    return true;
}

[[nodiscard]] Expected<UtilityVector<u32>> ParseIndices(
    const Path& nwbFilePath,
    const Core::Metascript::Value& asset
){
    UtilityVector<u32> outIndices;

    const Core::Metascript::Value* field = FindRequiredListField(nwbFilePath, asset, s_IndicesField);
    if(!field)
        return MakeUnexpected(Failure{});
    if(!FillIndicesRecursive(nwbFilePath, *field, s_IndicesField, outIndices))
        return MakeUnexpected(Failure{});
    if(outIndices.empty() || (outIndices.size() % s_TriangleIndexCount) != 0u){
        NWB_LOGGER_ERROR(NWB_TEXT("{} meta '{}': 'indices' must contain whole triangles")
            , StringConvert(s_MeshMetaKind)
            , PathToString<tchar>(nwbFilePath)
        );
        return MakeUnexpected(Failure{});
    }
    return outIndices;
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
        if(!ValidateStreamIndex(nwbFilePath, s_PositionField, ref.position, mesh.positions.size()))
            return false;
        if(!ValidateStreamIndex(nwbFilePath, s_NormalField, ref.normal, mesh.normals.size()))
            return false;
        if(!ValidateStreamIndex(nwbFilePath, s_TangentField, ref.tangent, mesh.tangents.size()))
            return false;
        if(!ValidateStreamIndex(nwbFilePath, s_Uv0Field, ref.uv0, mesh.uv0.size()))
            return false;
        if(!ValidateStreamIndex(nwbFilePath, s_ColorField, ref.color, mesh.colors.size()))
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
    return fieldName == Core::Metascript::MStringView(s_PositionsField.data(), s_PositionsField.size())
        || fieldName == Core::Metascript::MStringView(s_NormalsField.data(), s_NormalsField.size())
        || fieldName == Core::Metascript::MStringView(s_TangentsField.data(), s_TangentsField.size())
        || fieldName == Core::Metascript::MStringView(s_Uv0Field.data(), s_Uv0Field.size())
        || fieldName == Core::Metascript::MStringView(s_ColorsField.data(), s_ColorsField.size())
        || fieldName == Core::Metascript::MStringView(s_VertexRefsField.data(), s_VertexRefsField.size())
        || fieldName == Core::Metascript::MStringView(s_IndicesField.data(), s_IndicesField.size())
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

[[nodiscard]] Expected<SourceMeshStreams> ParseMeshValue(const Path& nwbFilePath, const AStringView meshVariableName, const Core::Metascript::Value& asset){
    if(!ValidateMeshAssetFields(nwbFilePath, meshVariableName, asset))
        return MakeUnexpected(Failure{});
    SourceMeshStreams mesh;
    auto positions = ParseFloatListField<Vec3, 3u>(nwbFilePath, asset, s_PositionsField);
    if(!positions)
        return MakeUnexpected(positions.error());
    mesh.positions = Move(*positions);
    auto normals = ParseFloatListField<Vec3, 3u>(nwbFilePath, asset, s_NormalsField);
    if(!normals)
        return MakeUnexpected(normals.error());
    mesh.normals = Move(*normals);
    auto tangents = ParseFloatListField<Vec4, 4u>(nwbFilePath, asset, s_TangentsField);
    if(!tangents)
        return MakeUnexpected(tangents.error());
    mesh.tangents = Move(*tangents);
    auto uv0 = ParseFloatListField<Vec2, 2u>(nwbFilePath, asset, s_Uv0Field);
    if(!uv0)
        return MakeUnexpected(uv0.error());
    mesh.uv0 = Move(*uv0);
    auto colors = ParseFloatListField<Vec4, 4u>(nwbFilePath, asset, s_ColorsField);
    if(!colors)
        return MakeUnexpected(colors.error());
    mesh.colors = Move(*colors);
    auto vertexRefs = ParseVertexRefs(nwbFilePath, asset);
    if(!vertexRefs)
        return MakeUnexpected(vertexRefs.error());
    mesh.vertexRefs = Move(*vertexRefs);
    auto indices = ParseIndices(nwbFilePath, asset);
    if(!indices)
        return MakeUnexpected(indices.error());
    mesh.indices = Move(*indices);
    if(!ValidateMesh(nwbFilePath, mesh))
        return MakeUnexpected(Failure{});
    return mesh;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_FBX_TO_NWB_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

