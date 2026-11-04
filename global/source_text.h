// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#pragma once


#include "basic_string.h"
#include "text_utils.h"
#include "type.h"


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


[[nodiscard]] inline b8 HasMatchingOuterParens(const AStringView text)noexcept{
    if(text.size() < 2u || text.front() != '(' || text.back() != ')')
        return false;
    u32 depth = 0u;
    for(usize i = 0u; i < text.size(); ++i){
        if(text[i] == '('){
            ++depth;
        }
        else if(text[i] == ')'){
            if(depth == 0u)
                return false;
            --depth;
            if(depth == 0u && i + 1u < text.size())
                return false;
        }
    }
    return depth == 0u;
}

[[nodiscard]] inline AStringView ReadFirstIdentifierAfterCComments(const AStringView text)noexcept{
    usize index = 0u;
    while(index < text.size()){
        const char ch = text[index];
        if(ch == ' ' || ch == '\t' || ch == '\r' || ch == '\n'){
            ++index;
            continue;
        }
        if(ch == '/' && index + 1u < text.size()){
            if(text[index + 1u] == '/'){
                index += 2u;
                while(index < text.size() && text[index] != '\n')
                    ++index;
                continue;
            }
            if(text[index + 1u] == '*'){
                index += 2u;
                while(index + 1u < text.size() && !(text[index] == '*' && text[index + 1u] == '/'))
                    ++index;
                if(index + 1u < text.size())
                    index += 2u;
                else
                    return {};
                continue;
            }
        }
        break;
    }

    return ExtractLeadingIdentifier(text.substr(index));
}

// The starting index must point to an opening quote.
[[nodiscard]] inline usize SkipQuotedLiteral(const AStringView text, usize index)noexcept{
    const char quote = text[index++];
    while(index < text.size() && text[index] != quote){
        if(text[index] == '\\' && index + 1u < text.size())
            ++index;
        ++index;
    }
    if(index < text.size())
        ++index;
    return index;
}

template<typename StringT>
inline usize AppendQuotedLiteral(const AStringView text, usize index, StringT& out)noexcept(noexcept(out.push_back(char{}))){
    const char quote = text[index];
    out.push_back(text[index++]);
    while(index < text.size() && text[index] != quote){
        if(text[index] == '\\' && index + 1u < text.size())
            out.push_back(text[index++]);
        out.push_back(text[index++]);
    }
    if(index < text.size())
        out.push_back(text[index++]);
    return index;
}

template<typename StringT>
inline void SpliceSourceLines(StringT& inOutSource)noexcept(noexcept(inOutSource.size()) && noexcept(inOutSource[usize{}] = char{}) && noexcept(inOutSource.resize(usize{}))){
    usize writeCursor = 0u;
    for(usize readCursor = 0u; readCursor < inOutSource.size(); ++readCursor){
        if(inOutSource[readCursor] == '\\' && readCursor + 1u < inOutSource.size()){
            if(inOutSource[readCursor + 1u] == '\n'){
                ++readCursor;
                continue;
            }
            if(inOutSource[readCursor + 1u] == '\r' && readCursor + 2u < inOutSource.size() && inOutSource[readCursor + 2u] == '\n'){
                readCursor += 2u;
                continue;
            }
        }
        inOutSource[writeCursor++] = inOutSource[readCursor];
    }
    inOutSource.resize(writeCursor);
}

template<typename StringT>
inline void MaskCStyleSourceComments(StringT& inOutSource)noexcept(noexcept(inOutSource.size()) && noexcept(inOutSource[usize{}] = char{})){
    b8 blockComment = false;
    b8 lineComment = false;
    b8 escaped = false;
    char quote = '\0';
    for(usize cursor = 0u; cursor < inOutSource.size(); ++cursor){
        char& ch = inOutSource[cursor];
        if(blockComment){
            if(ch == '*' && cursor + 1u < inOutSource.size() && inOutSource[cursor + 1u] == '/'){
                ch = ' ';
                inOutSource[++cursor] = ' ';
                blockComment = false;
            }
            else if(ch != '\r' && ch != '\n')
                ch = ' ';
            continue;
        }
        if(lineComment){
            if(ch == '\n')
                lineComment = false;
            else if(ch != '\r')
                ch = ' ';
            continue;
        }
        if(quote != '\0'){
            if(escaped)
                escaped = false;
            else if(ch == '\\')
                escaped = true;
            else if(ch == quote)
                quote = '\0';
            if(ch == '\r' || ch == '\n')
                ch = ' ';
            continue;
        }
        if(ch == '"' || ch == '\''){
            quote = ch;
            continue;
        }
        if(ch != '/' || cursor + 1u >= inOutSource.size())
            continue;
        const char next = inOutSource[cursor + 1u];
        if(next == '/' || next == '*'){
            ch = ' ';
            inOutSource[++cursor] = ' ';
            lineComment = next == '/';
            blockComment = next == '*';
        }
    }
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

