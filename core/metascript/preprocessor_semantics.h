// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#pragma once


#include "global.h"

#include <global/source_text.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_METASCRIPT_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace PreprocessorDirectiveKind{
    enum Enum : u8{
        Unknown = 0u,
#define NWB_META_EXPORT_PREPROCESSOR_DIRECTIVE(enumName, directiveText) \
        enumName,
#include "export_preprocessor.h"
#undef NWB_META_EXPORT_PREPROCESSOR_DIRECTIVE
    };
};


class PreprocessorSemantics final{
public:
    inline static constexpr MStringView s_DefinedOperator = "defined";
    inline static constexpr MStringView s_VariadicArgsMacro = "__VA_ARGS__";
    inline static constexpr MStringView s_VariadicEllipsis = "...";
    inline static constexpr MStringView s_DefaultDefinedMacroBody = "1";
    inline static constexpr MStringView s_RootFileKey = "<root>";
    inline static constexpr MStringView s_DebugConfigMacro = "NWB_DEBUG";
    inline static constexpr MStringView s_OptimizeConfigMacro = "NWB_OPTIMIZE";
    inline static constexpr MStringView s_FinalConfigMacro = "NWB_FINAL";


public:
    [[nodiscard]] static constexpr PreprocessorDirectiveKind::Enum ParseDirectiveKind(const MStringView text)noexcept{
#define NWB_META_EXPORT_PREPROCESSOR_DIRECTIVE(enumName, directiveText) \
        if(text == directiveText) return PreprocessorDirectiveKind::enumName;
#include "export_preprocessor.h"
#undef NWB_META_EXPORT_PREPROCESSOR_DIRECTIVE
        return PreprocessorDirectiveKind::Unknown;
    }

    [[nodiscard]] static constexpr b8 TryParseBoolLiteral(const MStringView text, i64& outValue)noexcept{
#define NWB_META_EXPORT_PREPROCESSOR_BOOL_LITERAL(keywordText, intValue) \
        if(text == keywordText){ outValue = intValue; return true; }
#include "export_preprocessor.h"
#undef NWB_META_EXPORT_PREPROCESSOR_BOOL_LITERAL
        return false;
    }

    [[nodiscard]] static constexpr b8 IsRawBraceBlockKeyword(const MStringView text)noexcept{
#define NWB_META_EXPORT_PREPROCESSOR_RAW_BRACE_KEYWORD(keywordText) \
        if(text == keywordText) return true;
#include "export_preprocessor.h"
#undef NWB_META_EXPORT_PREPROCESSOR_RAW_BRACE_KEYWORD
        return false;
    }

    [[nodiscard]] static constexpr b8 IsValidRawStringEncodingPrefix(const MStringView prefix)noexcept{
        if(prefix.empty())
            return true;
#define NWB_META_EXPORT_PREPROCESSOR_RAW_STRING_PREFIX(prefixText) \
        if(prefix == prefixText) return true;
#include "export_preprocessor.h"
#undef NWB_META_EXPORT_PREPROCESSOR_RAW_STRING_PREFIX
        return false;
    }

    [[nodiscard]] static constexpr b8 IsAllowedIncludePath(const MStringView path, const b8 allowNwbInclude)noexcept{
#define NWB_META_EXPORT_PREPROCESSOR_INCLUDE_EXTENSION(extText, requiresAllowNwbFlag) \
        if(!(requiresAllowNwbFlag) || allowNwbInclude){ \
            constexpr MStringView s_Ext = extText; \
            if(path.size() > s_Ext.size() && path.ends_with(s_Ext)) \
                return true; \
        }
#include "export_preprocessor.h"
#undef NWB_META_EXPORT_PREPROCESSOR_INCLUDE_EXTENSION
        return false;
    }

    [[nodiscard]] static constexpr usize SkipDefinedOperand(const MStringView source, usize index)noexcept{
        while(index < source.size() && (source[index] == ' ' || source[index] == '\t'))
            ++index;
        if(index < source.size() && source[index] == '('){
            ++index;
            while(index < source.size() && source[index] != ')')
                ++index;
            if(index < source.size())
                ++index;
        }
        else if(index < source.size() && IsAsciiIdentifierStart(source[index])){
            while(index < source.size() && IsAsciiIdentifierChar(source[index]))
                ++index;
        }
        return index;
    }

    [[nodiscard]] static constexpr usize SkipPreprocessorNumber(const MStringView text, usize index, const b8 allowSignedExponent)noexcept{
        while(index < text.size()){
            const MChar ch = text[index];
            if(allowSignedExponent && (ch == 'e' || ch == 'E' || ch == 'p' || ch == 'P') && index + 1u < text.size() && (text[index + 1u] == '+' || text[index + 1u] == '-')){
                index += 2u;
                continue;
            }
            if(ch == '\'' && index + 1u < text.size() && (IsAsciiDigit(text[index + 1u]) || IsAsciiIdentifierChar(text[index + 1u]))){
                index += 2u;
                continue;
            }
            if(IsAsciiDigit(ch) || IsAsciiIdentifierChar(ch) || ch == '.'){
                ++index;
                continue;
            }
            break;
        }
        return index;
    }

    [[nodiscard]] static constexpr b8 TryParseRawStringOpening(const MStringView text, const usize quoteIndex, MStringView& outDelimiter)noexcept{
        if(quoteIndex == 0u || quoteIndex >= text.size() || text[quoteIndex] != '"' || text[quoteIndex - 1u] != 'R')
            return false;

        usize prefixStart = quoteIndex - 1u;
        while(prefixStart > 0u && IsAsciiIdentifierChar(text[prefixStart - 1u]))
            --prefixStart;

        const MStringView prefix = text.substr(prefixStart, (quoteIndex - 1u) - prefixStart);
        if(!IsValidRawStringEncodingPrefix(prefix))
            return false;

        const usize delimStart = quoteIndex + 1u;
        usize cursor = delimStart;
        while(cursor < text.size() && (cursor - delimStart) <= 16u){
            const MChar c = text[cursor];
            if(c == '('){
                outDelimiter = text.substr(delimStart, cursor - delimStart);
                return true;
            }
            if(c == ')' || c == '\\' || c == '"' || c == ' ' || c == '\t' || c == '\v' || c == '\f' || c == '\r' || c == '\n')
                return false;
            ++cursor;
        }
        return false;
    }

    [[nodiscard]] static constexpr b8 FindRawStringClosing(const MStringView text, const usize searchStart, const MStringView delimiter, usize& outAfterCloseQuote)noexcept{
        const usize closeSeqLen = delimiter.size() + 2u;
        if(searchStart + closeSeqLen > text.size())
            return false;
        for(usize pos = searchStart; pos + closeSeqLen <= text.size(); ++pos){
            if(text[pos] == ')' && text[pos + 1u + delimiter.size()] == '"' && text.substr(pos + 1u, delimiter.size()) == delimiter){
                outAfterCloseQuote = pos + closeSeqLen;
                return true;
            }
        }
        return false;
    }

    [[nodiscard]] static constexpr b8 TrySkipRawStringLiteral(const MStringView text, const usize quoteIndex, usize& outNextIndex, b8& outUnterminated)noexcept{
        outUnterminated = false;
        MStringView rawDelim;
        if(!TryParseRawStringOpening(text, quoteIndex, rawDelim))
            return false;
        const usize bodyStart = quoteIndex + 1u + rawDelim.size() + 1u;
        if(!FindRawStringClosing(text, bodyStart, rawDelim, outNextIndex)){
            outNextIndex = text.size();
            outUnterminated = true;
        }
        return true;
    }

    [[nodiscard]] static b8 AdvancePreprocessorScanStep(const MStringView text, usize& index, const usize digitGuardStart, const b8 stopAtLineComment, b8& outHitTerminator)noexcept{
        outHitTerminator = false;
        const MChar ch = text[index];
        if(ch == '\'' && index > digitGuardStart && (IsAsciiDigit(text[index - 1u]) || IsAsciiIdentifierChar(text[index - 1u]))){
            ++index;
            return true;
        }
        if(ch == '"'){
            usize afterClose = 0u;
            b8 unterminated = false;
            if(TrySkipRawStringLiteral(text, index, afterClose, unterminated)){
                index = afterClose;
                outHitTerminator = unterminated;
                return true;
            }
        }
        if(ch == '"' || ch == '\''){
            index = SkipQuotedLiteral(text, index);
            return true;
        }
        if(stopAtLineComment && index + 1u < text.size() && ch == '/' && text[index + 1u] == '/'){
            outHitTerminator = true;
            return true;
        }
        if(index + 1u < text.size() && ch == '/' && text[index + 1u] == '*'){
            const usize closeComment = text.find("*/", index + 2u);
            if(closeComment == MStringView::npos)
                outHitTerminator = true;
            else
                index = closeComment + 2u;
            return true;
        }
        return false;
    }
};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_METASCRIPT_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

