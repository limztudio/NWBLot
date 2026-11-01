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


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


[[nodiscard]] static b8 IsReferenceTo(const Core::Metascript::Value& value, const AStringView variableName)noexcept{
    return value.isReference() && value.asReference() == variableName;
}

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
        writeValue(value, out);
        out << s_ListItemSuffixText;
    }
    out << s_ListCloseText;
    return Move(out).str();
}

[[nodiscard]] static ScratchString WriteIndexList(const Span<const u32> indices, Core::Alloc::ScratchArena& scratchArena){
    ScratchStringStream out(s_FileOpenWrite, scratchArena);
    out.precision(s_OutputFloatPrecision);
    out << s_ListOpenText;
    for(usize i = 0u; i < indices.size(); i += Impl::s_MeshletTriangleIndexCount)
        out << s_RowOpenText << indices[i] << s_RowItemSeparatorText << indices[i + 1u] << s_RowItemSeparatorText << indices[i + 2u] << s_RowSuffixText;
    out << s_ListCloseText;
    return Move(out).str();
}

[[nodiscard]] static ScratchString WriteVertexRefList(const Span<const SourceVertexRef> refs, Core::Alloc::ScratchArena& scratchArena){
    ScratchStringStream out(s_FileOpenWrite, scratchArena);
    out.precision(s_OutputFloatPrecision);
    out << s_ListOpenText;
    for(const SourceVertexRef& ref : refs)
        out << s_RowOpenText << ref.position << s_RowItemSeparatorText << ref.normal << s_RowItemSeparatorText << ref.tangent << s_RowItemSeparatorText << ref.uv0 << s_RowItemSeparatorText << ref.color << s_RowSuffixText;
    out << s_ListCloseText;
    return Move(out).str();
}

[[nodiscard]] static b8 AddReplacement(const AStringView source, const AStringView variableName, const AStringView fieldName, ScratchString&& replacementText, TextReplacements& inOutReplacements){
    auto replacementResult = FindListAssignmentRange(source, variableName, fieldName);
    if(!replacementResult){
        NWB_LOGGER_ERROR(NWB_TEXT("Failed to refresh NWB mesh: missing or ambiguous direct list assignment '{}.{}'"), ConvertString(variableName), ConvertString(fieldName));
        return false;
    }

    const AssignmentRange& range = *replacementResult;
    inOutReplacements.push_back(TextReplacement{ range.begin, range.end, Move(replacementText) });
    return true;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


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

[[nodiscard]] b8 AppendMeshReplacements(const AStringView source, const AStringView variableName, const SourceMeshStreamCounts& before, const SourceMeshStreams& after, Core::Alloc::ScratchArena& scratchArena, TextReplacements& inOutReplacements){
    if(before.positions != after.positions.size() && !AddReplacement(source, variableName, Impl::s_MeshPositionsFieldNameView, WriteValueList<Vec3>(after.positions, WriteVec3<ScratchStringStream>, scratchArena), inOutReplacements))
        return false;
    if(before.normals != after.normals.size() && !AddReplacement(source, variableName, Impl::s_MeshNormalsFieldNameView, WriteValueList<Vec3>(after.normals, WriteVec3<ScratchStringStream>, scratchArena), inOutReplacements))
        return false;
    if(before.tangents != after.tangents.size() && !AddReplacement(source, variableName, Impl::s_MeshTangentsFieldNameView, WriteValueList<Vec4>(after.tangents, WriteVec4<ScratchStringStream>, scratchArena), inOutReplacements))
        return false;
    if(before.uv0 != after.uv0.size() && !AddReplacement(source, variableName, Impl::s_MeshUv0FieldNameView, WriteValueList<Vec2>(after.uv0, WriteVec2<ScratchStringStream>, scratchArena), inOutReplacements))
        return false;
    if(before.colors != after.colors.size() && !AddReplacement(source, variableName, Impl::s_MeshColorsFieldNameView, WriteValueList<Vec4>(after.colors, WriteVec4<ScratchStringStream>, scratchArena), inOutReplacements))
        return false;

    const b8 componentRefsChanged =
        before.positions != after.positions.size()
        || before.normals != after.normals.size()
        || before.tangents != after.tangents.size()
        || before.uv0 != after.uv0.size()
        || before.colors != after.colors.size()
        || before.vertexRefs != after.vertexRefs.size()
    ;
    if(componentRefsChanged && !AddReplacement(source, variableName, Impl::s_MeshVertexRefsFieldNameView, WriteVertexRefList(after.vertexRefs, scratchArena), inOutReplacements))
        return false;
    if(before.vertexRefs != after.vertexRefs.size() && !AddReplacement(source, variableName, Impl::s_MeshIndicesFieldNameView, WriteIndexList(after.indices, scratchArena), inOutReplacements))
        return false;

    return true;
}

void ApplyTextReplacements(const Span<TextReplacement> inOutReplacements, ScratchString& inOutSource){
    Sort(inOutReplacements.begin(), inOutReplacements.end(), [](const TextReplacement& lhs, const TextReplacement& rhs)noexcept{
        return lhs.begin > rhs.begin;
    });

    usize capacity = inOutSource.size();
    for(const TextReplacement& replacement : inOutReplacements){
        NWB_ASSERT(replacement.begin <= replacement.end && replacement.end <= inOutSource.size());
        const usize oldSize = replacement.end - replacement.begin;
        if(replacement.text.size() > oldSize)
            capacity = AddSize(capacity, replacement.text.size() - oldSize);
    }
    inOutSource.reserve(capacity);
    for(const TextReplacement& replacement : inOutReplacements)
        inOutSource.replace(replacement.begin, replacement.end - replacement.begin, replacement.text);
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_FBX_TO_NWB_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

