// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "mesh_refresh_text.h"

#include <global/text_write.h>

#include <core/common/log.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_FBX_TO_NWB_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace MeshRefreshTextDetail{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


using TextWrite::WriteFloat;
using TextWrite::WriteVec2;
using TextWrite::WriteVec3;
using TextWrite::WriteVec4;
using TextWrite::s_OutputFloatPrecision;

static constexpr AStringView s_ListOpenText = "[\n";
static constexpr AStringView s_ListCloseText = "]";
static constexpr AStringView s_ListItemIndentText = "    ";
static constexpr AStringView s_ListItemSuffixText = ",\n";
static constexpr AStringView s_RowOpenText = "    [";
static constexpr AStringView s_RowItemSeparatorText = ", ";
static constexpr AStringView s_RowSuffixText = "],\n";
static constexpr AStringView s_SkinJointsOpenText = "    { \"joints\": [";
static constexpr AStringView s_SkinWeightsSeparatorText = "], \"weights\": [";
static constexpr AStringView s_SkinRowSuffixText = "] },\n";


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


[[nodiscard]] bool IsSameText(const Core::Metascript::MStringView lhs, const AStringView rhs){
    if(lhs.size() != rhs.size())
        return false;
    for(usize i = 0u; i < lhs.size(); ++i){
        if(lhs[i] != rhs[i])
            return false;
    }
    return true;
}

[[nodiscard]] usize SkipWhitespace(const AStringView text, usize offset){
    while(offset < text.size() && IsAsciiSpace(text[offset]))
        ++offset;
    return offset;
}

[[nodiscard]] bool HasIdentifierBoundary(const AStringView text, const usize begin, const usize end){
    const bool leftOk = begin == 0u || !IsAsciiIdentifierChar(text[begin - 1u]);
    const bool rightOk = end >= text.size() || !IsAsciiIdentifierChar(text[end]);
    return leftOk && rightOk;
}

[[nodiscard]] Expected<TextReplacement> FindListAssignmentRange(
    const AStringView source,
    const AStringView variableName,
    const AStringView fieldName
){
    TextReplacement outRange;
    AString pattern;
    pattern.reserve(variableName.size() + fieldName.size() + 1u);
    pattern.append(variableName.data(), variableName.size());
    pattern.push_back('.');
    pattern.append(fieldName.data(), fieldName.size());

    usize offset = 0u;
    while(offset < source.size()){
        const usize found = source.find(pattern, offset);
        if(found == AStringView::npos)
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
                return MakeUnexpected(Failure{});
            --depth;
            if(depth == 0u){
                outRange.begin = listBegin;
                outRange.end = cursor + 1u;
                return outRange;
            }
        }
        return MakeUnexpected(Failure{});
    }

    return MakeUnexpected(Failure{});
}

template<typename Value, typename WriteValue>
[[nodiscard]] AString WriteValueList(const UtilityVector<Value>& values, WriteValue&& writeValue){
    AStringStream out;
    out.precision(s_OutputFloatPrecision);
    out << s_ListOpenText;
    for(const Value& value : values){
        out << s_ListItemIndentText;
        writeValue(out, value);
        out << s_ListItemSuffixText;
    }
    out << s_ListCloseText;
    return out.str();
}

[[nodiscard]] AString WriteIndexList(const UtilityVector<u32>& indices){
    AStringStream out;
    out.precision(s_OutputFloatPrecision);
    out << s_ListOpenText;
    for(usize i = 0u; i < indices.size(); i += s_TriangleIndexCount)
        out << s_RowOpenText << indices[i] << s_RowItemSeparatorText << indices[i + 1u] << s_RowItemSeparatorText << indices[i + 2u] << s_RowSuffixText;
    out << s_ListCloseText;
    return out.str();
}

[[nodiscard]] AString WriteVertexRefList(const UtilityVector<SourceVertexRef>& refs){
    AStringStream out;
    out.precision(s_OutputFloatPrecision);
    out << s_ListOpenText;
    for(const SourceVertexRef& ref : refs){
        out
            << s_RowOpenText
            << ref.position << s_RowItemSeparatorText
            << ref.normal << s_RowItemSeparatorText
            << ref.tangent << s_RowItemSeparatorText
            << ref.uv0 << s_RowItemSeparatorText
            << ref.color
            << s_RowSuffixText
        ;
    }
    out << s_ListCloseText;
    return out.str();
}

[[nodiscard]] AString WriteSkinInfluenceList(const UtilityVector<MeshSkinInfluence>& influences){
    AStringStream out;
    out.precision(s_OutputFloatPrecision);
    out << s_ListOpenText;
    for(const MeshSkinInfluence& influence : influences){
        out << s_SkinJointsOpenText;
        for(usize i = 0u; i < s_MeshSkinInfluenceCount; ++i){
            if(i != 0u)
                out << s_RowItemSeparatorText;
            out << influence.joint[i];
        }
        out << s_SkinWeightsSeparatorText;
        for(usize i = 0u; i < s_MeshSkinInfluenceCount; ++i){
            if(i != 0u)
                out << s_RowItemSeparatorText;
            WriteFloat(out, influence.weight.raw[i]);
        }
        out << s_SkinRowSuffixText;
    }
    out << s_ListCloseText;
    return out.str();
}

[[nodiscard]] bool AddReplacement(
    UtilityVector<TextReplacement>& replacements,
    const AStringView source,
    const AStringView variableName,
    const AStringView fieldName,
    AString&& replacementText
){
    auto replacementResult = FindListAssignmentRange(source, variableName, fieldName);
    if(!replacementResult){
        NWB_LOGGER_ERROR(NWB_TEXT("Failed to refresh NWB mesh: missing '{}.{}' assignment")
            , StringConvert(variableName)
            , StringConvert(fieldName)
        );
        return false;
    }

    TextReplacement& replacement = *replacementResult;
    replacement.text = Move(replacementText);
    replacements.push_back(Move(replacement));
    return true;
}

[[nodiscard]] bool AppendMeshReplacements(
    UtilityVector<TextReplacement>& replacements,
    const AStringView source,
    const AStringView variableName,
    const SourceMeshStreams& before,
    const SourceMeshStreams& after
){
    if(before.positions.size() != after.positions.size() && !AddReplacement(replacements, source, variableName, s_PositionsStreamLabel, WriteValueList(after.positions, [](AStringStream& out, const Vec3& value){ WriteVec3(out, value); })))
        return false;
    if(before.normals.size() != after.normals.size() && !AddReplacement(replacements, source, variableName, s_NormalsStreamLabel, WriteValueList(after.normals, [](AStringStream& out, const Vec3& value){ WriteVec3(out, value); })))
        return false;
    if(before.tangents.size() != after.tangents.size() && !AddReplacement(replacements, source, variableName, s_TangentsStreamLabel, WriteValueList(after.tangents, [](AStringStream& out, const Vec4& value){ WriteVec4(out, value); })))
        return false;
    if(before.uv0.size() != after.uv0.size() && !AddReplacement(replacements, source, variableName, s_Uv0StreamLabel, WriteValueList(after.uv0, [](AStringStream& out, const Vec2& value){ WriteVec2(out, value); })))
        return false;
    if(before.colors.size() != after.colors.size() && !AddReplacement(replacements, source, variableName, s_ColorsStreamLabel, WriteValueList(after.colors, [](AStringStream& out, const Vec4& value){ WriteVec4(out, value); })))
        return false;

    const bool componentRefsChanged =
        before.positions.size() != after.positions.size()
        || before.normals.size() != after.normals.size()
        || before.tangents.size() != after.tangents.size()
        || before.uv0.size() != after.uv0.size()
        || before.colors.size() != after.colors.size()
        || before.vertexRefs.size() != after.vertexRefs.size()
    ;
    if(componentRefsChanged && !AddReplacement(replacements, source, variableName, s_VertexRefsStreamLabel, WriteVertexRefList(after.vertexRefs)))
        return false;
    if(before.vertexRefs.size() != after.vertexRefs.size() && !AddReplacement(replacements, source, variableName, s_IndicesStreamLabel, WriteIndexList(after.indices)))
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

[[nodiscard]] Expected<SkinReference> FindSkinForMesh(
    const Core::Metascript::Document& doc,
    const AStringView meshVariableName
){
    SkinReference result;

    for(const Core::Metascript::Document::Declaration& declaration : doc.declarations()){
        if(!IsSameText(Core::Metascript::MStringView(declaration.type.data(), declaration.type.size()), s_SkinAssetTypeText))
            continue;

        const Core::Metascript::MStringView skinVariable(declaration.variable.data(), declaration.variable.size());
        const Core::Metascript::Value* skinAsset = doc.findVariable(skinVariable);
        if(!skinAsset || !skinAsset->isMap())
            continue;

        const Core::Metascript::Value* meshField = Core::Metascript::FindField(*skinAsset, s_MeshAssetTypeText);
        if(!meshField || !IsReferenceTo(*meshField, meshVariableName))
            continue;

        if(result.value){
            NWB_LOGGER_ERROR(NWB_TEXT("Failed to refresh NWB mesh: mesh '{}' has multiple skin assets"), StringConvert(meshVariableName));
            return MakeUnexpected(Failure{});
        }

        result.value = skinAsset;
        result.variableName = skinVariable;
    }

    return result;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_FBX_TO_NWB_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

