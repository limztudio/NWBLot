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


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


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
    out.precision(s_OutputFloatPrecision);
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
    out.precision(s_OutputFloatPrecision);
    out << "[\n";
    for(usize i = 0u; i < indices.size(); i += s_TriangleIndexCount)
        out << "    [" << indices[i] << ", " << indices[i + 1u] << ", " << indices[i + 2u] << "],\n";
    out << "]";
    return out.str();
}

[[nodiscard]] AString WriteVertexRefList(const UtilityVector<SourceVertexRef>& refs){
    AStringStream out;
    out.precision(s_OutputFloatPrecision);
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
    out.precision(s_OutputFloatPrecision);
    out << "[\n";
    for(const MeshSkinInfluence& influence : influences){
        out << "    { \"joints\": [";
        for(usize i = 0u; i < s_MeshSkinInfluenceCount; ++i){
            if(i != 0u)
                out << ", ";
            out << influence.joint[i];
        }
        out << "], \"weights\": [";
        for(usize i = 0u; i < s_MeshSkinInfluenceCount; ++i){
            if(i != 0u)
                out << ", ";
            WriteFloat(out, influence.weight.raw[i]);
        }
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

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_FBX_TO_NWB_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

