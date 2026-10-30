// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "mesh_refresh_text.h"

#include "text_write.h"

#include <core/common/log.h>
#include <core/metascript/lexer.h>

#include <global/allocation_size.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_FBX_TO_NWB_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace MeshRefreshTextDetail{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


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


struct AssignmentRange{
    usize begin = 0u;
    usize end = 0u;
};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


[[nodiscard]] static Expected<AssignmentRange> FindListAssignmentRange(const AStringView source, const AStringView variableName, const AStringView fieldName)noexcept{
    AssignmentRange outRange;
    Core::Metascript::Lexer lexer(source);
    Core::Metascript::Token token = lexer.next();
    b8 statementBegin = true;
    b8 foundAssignment = false;
    while(token.type != Core::Metascript::TokenType::EndOfFile){
        if(token.type == Core::Metascript::TokenType::Error)
            return MakeUnexpected(Failure{});

        if(statementBegin && token.type == Core::Metascript::TokenType::Identifier && token.text == variableName){
            token = lexer.next();
            if(token.type == Core::Metascript::TokenType::Dot){
                token = lexer.next();
                if(token.type == Core::Metascript::TokenType::Identifier && token.text == fieldName){
                    token = lexer.next();
                    const b8 assignment = token.type == Core::Metascript::TokenType::Equal || token.type == Core::Metascript::TokenType::PlusEqual || token.type == Core::Metascript::TokenType::MinusEqual
                        || token.type == Core::Metascript::TokenType::StarEqual || token.type == Core::Metascript::TokenType::SlashEqual;
                    if(assignment){
                        // Parsed values can combine assignments, but rewriting needs one authoritative literal list.
                        if(foundAssignment || token.type != Core::Metascript::TokenType::Equal)
                            return MakeUnexpected(Failure{});
                        foundAssignment = true;
                        token = lexer.next();
                        if(token.type != Core::Metascript::TokenType::LeftBracket)
                            return MakeUnexpected(Failure{});

                        outRange.begin = static_cast<usize>(token.text.data() - source.data());
                        usize depth = 1u;
                        while(depth != 0u){
                            token = lexer.next();
                            if(token.type == Core::Metascript::TokenType::Error || token.type == Core::Metascript::TokenType::EndOfFile)
                                return MakeUnexpected(Failure{});
                            if(token.type == Core::Metascript::TokenType::LeftBracket)
                                ++depth;
                            else if(token.type == Core::Metascript::TokenType::RightBracket)
                                --depth;
                        }
                        outRange.end = static_cast<usize>(token.text.data() - source.data()) + token.text.size();
                        token = lexer.next();
                        if(token.type != Core::Metascript::TokenType::Semicolon)
                            return MakeUnexpected(Failure{});
                    }
                }
            }
        }

        statementBegin = token.type == Core::Metascript::TokenType::Semicolon;
        token = lexer.next();
    }
    if(!foundAssignment)
        return MakeUnexpected(Failure{});
    return outRange;
}

template<typename Value, typename WriteValue>
[[nodiscard]] static ScratchString WriteValueList(const Span<const Value> values, WriteValue&& writeValue, Core::Alloc::ScratchArena& scratchArena){
    ScratchStringStream out(s_FileOpenWrite, scratchArena);
    out.precision(s_OutputFloatPrecision);
    out << s_ListOpenText;
    for(const Value& value : values){
        out << s_ListItemIndentText;
        writeValue(out, value);
        out << s_ListItemSuffixText;
    }
    out << s_ListCloseText;
    return Move(out).str();
}

[[nodiscard]] static ScratchString WriteIndexList(const Span<const u32> indices, Core::Alloc::ScratchArena& scratchArena){
    ScratchStringStream out(s_FileOpenWrite, scratchArena);
    out.precision(s_OutputFloatPrecision);
    out << s_ListOpenText;
    for(usize i = 0u; i < indices.size(); i += s_TriangleIndexCount)
        out << s_RowOpenText << indices[i] << s_RowItemSeparatorText << indices[i + 1u] << s_RowItemSeparatorText << indices[i + 2u] << s_RowSuffixText;
    out << s_ListCloseText;
    return Move(out).str();
}

[[nodiscard]] static ScratchString WriteVertexRefList(const Span<const SourceVertexRef> refs, Core::Alloc::ScratchArena& scratchArena){
    ScratchStringStream out(s_FileOpenWrite, scratchArena);
    out.precision(s_OutputFloatPrecision);
    out << s_ListOpenText;
    for(const SourceVertexRef& ref : refs){
        out << s_RowOpenText << ref.position << s_RowItemSeparatorText << ref.normal << s_RowItemSeparatorText << ref.tangent << s_RowItemSeparatorText << ref.uv0 << s_RowItemSeparatorText << ref.color << s_RowSuffixText;
    }
    out << s_ListCloseText;
    return Move(out).str();
}

[[nodiscard]] static b8 AddReplacement(TextReplacements& replacements, const AStringView source, const AStringView variableName, const AStringView fieldName, ScratchString&& replacementText){
    auto replacementResult = FindListAssignmentRange(source, variableName, fieldName);
    if(!replacementResult){
        NWB_LOGGER_ERROR(NWB_TEXT("Failed to refresh NWB mesh: missing or ambiguous direct list assignment '{}.{}'"), ConvertString(variableName), ConvertString(fieldName));
        return false;
    }

    const AssignmentRange& range = *replacementResult;
    replacements.push_back(TextReplacement{ range.begin, range.end, Move(replacementText) });
    return true;
}

[[nodiscard]] b8 AppendMeshReplacements(TextReplacements& replacements, const AStringView source, const AStringView variableName, const SourceMeshStreamCounts& before, const SourceMeshStreams& after, Core::Alloc::ScratchArena& scratchArena){
    if(before.positions != after.positions.size() && !AddReplacement(replacements, source, variableName, s_PositionsStreamLabel, WriteValueList<Vec3>(after.positions, [](ScratchStringStream& out, const Vec3& value){ WriteVec3(out, value); }, scratchArena)))
        return false;
    if(before.normals != after.normals.size() && !AddReplacement(replacements, source, variableName, s_NormalsStreamLabel, WriteValueList<Vec3>(after.normals, [](ScratchStringStream& out, const Vec3& value){ WriteVec3(out, value); }, scratchArena)))
        return false;
    if(before.tangents != after.tangents.size() && !AddReplacement(replacements, source, variableName, s_TangentsStreamLabel, WriteValueList<Vec4>(after.tangents, [](ScratchStringStream& out, const Vec4& value){ WriteVec4(out, value); }, scratchArena)))
        return false;
    if(before.uv0 != after.uv0.size() && !AddReplacement(replacements, source, variableName, s_Uv0StreamLabel, WriteValueList<Vec2>(after.uv0, [](ScratchStringStream& out, const Vec2& value){ WriteVec2(out, value); }, scratchArena)))
        return false;
    if(before.colors != after.colors.size() && !AddReplacement(replacements, source, variableName, s_ColorsStreamLabel, WriteValueList<Vec4>(after.colors, [](ScratchStringStream& out, const Vec4& value){ WriteVec4(out, value); }, scratchArena)))
        return false;

    const b8 componentRefsChanged =
        before.positions != after.positions.size()
        || before.normals != after.normals.size()
        || before.tangents != after.tangents.size()
        || before.uv0 != after.uv0.size()
        || before.colors != after.colors.size()
        || before.vertexRefs != after.vertexRefs.size()
    ;
    if(componentRefsChanged && !AddReplacement(replacements, source, variableName, s_VertexRefsStreamLabel, WriteVertexRefList(after.vertexRefs, scratchArena)))
        return false;
    if(before.vertexRefs != after.vertexRefs.size() && !AddReplacement(replacements, source, variableName, s_IndicesStreamLabel, WriteIndexList(after.indices, scratchArena)))
        return false;

    return true;
}

void ApplyTextReplacements(ScratchString& inOutSource, const Span<TextReplacement> replacements){
    Sort(replacements.begin(), replacements.end(), [](const TextReplacement& lhs, const TextReplacement& rhs)noexcept{
        return lhs.begin > rhs.begin;
    });

    usize capacity = inOutSource.size();
    for(const TextReplacement& replacement : replacements){
        NWB_ASSERT(replacement.begin <= replacement.end && replacement.end <= inOutSource.size());
        const usize oldSize = replacement.end - replacement.begin;
        if(replacement.text.size() > oldSize)
            capacity = AddSize(capacity, replacement.text.size() - oldSize);
    }
    inOutSource.reserve(capacity);
    for(const TextReplacement& replacement : replacements){
        inOutSource.replace(replacement.begin, replacement.end - replacement.begin, replacement.text);
    }
}

[[nodiscard]] static b8 IsReferenceTo(const Core::Metascript::Value& value, const AStringView variableName)noexcept{
    return value.isReference() && value.asReference() == variableName;
}

[[nodiscard]] Expected<SkinReference> FindSkinForMesh(const Core::Metascript::Document& doc, const AStringView meshVariableName){
    SkinReference result;

    for(const Core::Metascript::Document::Declaration& declaration : doc.declarations()){
        if(declaration.type != s_SkinAssetTypeText)
            continue;

        const Core::Metascript::MStringView skinVariable(declaration.variable);
        const Core::Metascript::Value* skinAsset = doc.findVariable(skinVariable);
        if(!skinAsset || !skinAsset->isMap())
            continue;

        const Core::Metascript::Value* meshField = skinAsset->findField(s_MeshAssetTypeText);
        if(!meshField || !IsReferenceTo(*meshField, meshVariableName))
            continue;

        if(result.value){
            NWB_LOGGER_ERROR(NWB_TEXT("Failed to refresh NWB mesh: mesh '{}' has multiple skin assets"), ConvertString(meshVariableName));
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

