// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "native_block_classifier.h"


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_ADHD_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace __hidden_adhd_native_block_classifier{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


[[nodiscard]] constexpr b8 IsIdentStart(const MChar ch)noexcept{
    return (ch >= 'a' && ch <= 'z') || (ch >= 'A' && ch <= 'Z') || ch == '_';
}

[[nodiscard]] constexpr b8 IsIdentChar(const MChar ch)noexcept{
    return IsIdentStart(ch) || (ch >= '0' && ch <= '9');
}

[[nodiscard]] constexpr b8 IsHorizontalWhitespace(const MChar ch)noexcept{
    return ch == ' ' || ch == '\t' || ch == '\r' || ch == '\v' || ch == '\f';
}

[[nodiscard]] constexpr b8 IsRawStringPrefix(const MStringView ident)noexcept{
    return ident == "R" || ident == "LR" || ident == "uR" || ident == "UR" || ident == "u8R";
}

[[nodiscard]] constexpr b8 IsQuotedLiteralPrefix(const MStringView ident)noexcept{
    return ident == "L" || ident == "u" || ident == "U" || ident == "u8";
}

[[nodiscard]] constexpr b8 IsInlineSpecifier(const MStringView tok)noexcept{
    return tok == "inline"
        || tok == "NWB_INLINE"
        || tok == "NWB_FORCE_INLINE"
        || tok == "__forceinline"
        || tok == "constexpr"
        || tok == "consteval"
        || tok == "template"
        || tok == "concept";
}

[[nodiscard]] constexpr b8 IsTypeDeclarationKeyword(const MStringView tok)noexcept{
    return tok == "class"
        || tok == "struct"
        || tok == "interface"
        || tok == "union"
        || tok == "enum";
}

[[nodiscard]] constexpr b8 IsNonParamParenPrefix(const MStringView tok)noexcept{
    return tok == "alignas"
        || tok == "decltype"
        || tok == "noexcept"
        || tok == "sizeof"
        || tok == "static_assert"
        || tok == "__attribute__"
        || tok == "requires";
}

[[nodiscard]] constexpr b8 IsNonVariableDeclKeyword(const MStringView tok)noexcept{
    return tok == "using"
        || tok == "typedef"
        || tok == "static_assert"
        || tok == "operator";
}

[[nodiscard]] constexpr b8 IsSourceDefinitionMacro(const MStringView tok)noexcept{
    if(tok == "TEST" || tok == "TEST_F" || tok == "TEST_P" || tok == "TYPED_TEST" || tok == "TYPED_TEST_P")
        return true;
    return tok.starts_with("NWB_DEFINE_") && (tok.ends_with("_REGISTRAR") || tok.ends_with("_ENTRY_POINT"));
}

constexpr usize SkipQuotedLiteral(const MStringView code, usize index, const MChar quote)noexcept{
    const usize length = code.size();
    while(index < length){
        if(code[index] == '\\'){
            index = index + 2u <= length ? index + 2u : length;
        }
        else if(code[index] == quote){
            return index + 1u;
        }
        else{
            ++index;
        }
    }
    return length;
}

[[nodiscard]] MStringView NextCppToken(const MStringView code, usize& inOutPos, b8& inOutAtLineStart)noexcept{
    const usize length = code.size();
    usize i = inOutPos;
    b8 lineStart = inOutAtLineStart;

    while(i < length){
        const MChar ch = code[i];
        if(ch == '\n'){
            lineStart = true;
            ++i;
            continue;
        }
        if(IsHorizontalWhitespace(ch)){
            ++i;
            continue;
        }

        const MChar nxt = i + 1u < length ? code[i + 1u] : '\0';
        if(ch == '#' && lineStart){
            while(i < length){
                const usize nl = code.find('\n', i);
                if(nl == MStringView::npos){
                    i = length;
                    break;
                }
                usize trimmedEnd = nl;
                while(trimmedEnd > i && IsHorizontalWhitespace(code[trimmedEnd - 1u]))
                    --trimmedEnd;
                const b8 continued = trimmedEnd > i && code[trimmedEnd - 1u] == '\\';
                i = nl + 1u;
                if(!continued)
                    break;
            }
            lineStart = true;
            continue;
        }
        if(ch == '/' && nxt == '/'){
            const usize nl = code.find('\n', i + 2u);
            if(nl == MStringView::npos){
                inOutPos = length;
                inOutAtLineStart = false;
                return {};
            }
            i = nl + 1u;
            lineStart = true;
            continue;
        }
        if(ch == '/' && nxt == '*'){
            const usize endComment = code.find("*/", i + 2u);
            if(endComment == MStringView::npos){
                inOutPos = length;
                inOutAtLineStart = false;
                return {};
            }
            for(usize k = i + 2u; k < endComment; ++k){
                if(code[k] == '\n')
                    lineStart = true;
                else if(!IsHorizontalWhitespace(code[k]))
                    lineStart = false;
            }
            i = endComment + 2u;
            continue;
        }

        lineStart = false;

        if(IsIdentStart(ch)){
            const usize start = i;
            ++i;
            while(i < length && IsIdentChar(code[i]))
                ++i;
            const MStringView ident = code.substr(start, i - start);
            if(i < length && code[i] == '"' && IsRawStringPrefix(ident)){
                const usize openParen = code.find('(', i + 1u);
                if(openParen != MStringView::npos && (openParen - (i + 1u)) <= 16u){
                    const MStringView delim = code.substr(i + 1u, openParen - (i + 1u));
                    b8 validDelim = true;
                    for(const MChar dc : delim){
                        if(IsHorizontalWhitespace(dc) || dc == '\n' || dc == '\\' || dc == ')'){
                            validDelim = false;
                            break;
                        }
                    }
                    if(validDelim){
                        usize scan = openParen + 1u;
                        i = length;
                        while(scan < length){
                            const usize closeParen = code.find(')', scan);
                            if(closeParen == MStringView::npos)
                                break;
                            const usize afterDelim = closeParen + 1u + delim.size();
                            if(afterDelim < length && code.substr(closeParen + 1u, delim.size()) == delim && code[afterDelim] == '"'){
                                i = afterDelim + 1u;
                                break;
                            }
                            scan = closeParen + 1u;
                        }
                        inOutPos = i;
                        inOutAtLineStart = false;
                        return "\"\"";
                    }
                }
            }
            if(i < length && (code[i] == '"' || code[i] == '\'') && IsQuotedLiteralPrefix(ident)){
                inOutPos = SkipQuotedLiteral(code, i + 1u, code[i]);
                inOutAtLineStart = false;
                return "\"\"";
            }
            inOutPos = i;
            inOutAtLineStart = false;
            return ident;
        }

        if(ch == '"' || ch == '\''){
            inOutPos = SkipQuotedLiteral(code, i + 1u, ch);
            inOutAtLineStart = false;
            return "\"\"";
        }

        if((ch == ':' && nxt == ':')
            || (ch == '-' && nxt == '>')
            || ((ch == '<' || ch == '>' || ch == '=' || ch == '!') && nxt == '=')
            || (ch == '<' && nxt == '<')
            || (ch == '>' && nxt == '>')){
            inOutPos = i + 2u;
            inOutAtLineStart = false;
            return code.substr(i, 2u);
        }

        inOutPos = i + 1u;
        inOutAtLineStart = false;
        return code.substr(i, 1u);
    }

    inOutPos = length;
    inOutAtLineStart = lineStart;
    return {};
}

struct StatementScanState{
    u32 topTokCount = 0u;
    u32 angleDepth = 0u;
    MStringView firstTok{};
    MStringView secondTok{};
    MStringView prevTopTok{};
    b8 sawNamespace = false;
    b8 sawTypeKeyword = false;
    b8 sawParamParens = false;
    b8 sawEqual = false;
    b8 sawNonVarDeclKeyword = false;
    b8 hasInline = false;
    b8 hasStatic = false;
    b8 hasExtern = false;
    u32 lhsTokCount = 0u;
    b8 lhsHasInline = false;
    b8 lhsHasStatic = false;
    b8 lhsHasExtern = false;
    b8 lhsHasConst = false;
    b8 lhsHasVolatile = false;
    b8 lhsHasColonColon = false;
};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


b8 NativeBlockClassifier::HasCppSourceOdrDefinition(const MStringView code)noexcept{
    using namespace __hidden_adhd_native_block_classifier;

    usize pos = 0u;
    b8 atLineStart = true;
    u32 namespaceDepth = 0u;
    u32 innerDepth = 0u;
    u32 parenDepth = 0u;
    StatementScanState stmt{};

    for(;;){
        const MStringView tok = NextCppToken(code, pos, atLineStart);
        if(tok.empty())
            break;

        if(innerDepth > 0u){
            if(tok == "{"){
                ++innerDepth;
            }
            else if(tok == "}"){
                --innerDepth;
                if(innerDepth == 0u)
                    stmt = StatementScanState{};
            }
            continue;
        }

        if(tok == "("){
            if(parenDepth == 0u){
                if(IsSourceDefinitionMacro(stmt.prevTopTok))
                    return true;
                if(stmt.angleDepth == 0u && !IsNonParamParenPrefix(stmt.prevTopTok) && !stmt.sawEqual)
                    stmt.sawParamParens = true;
                ++stmt.topTokCount;
            }
            ++parenDepth;
            continue;
        }

        if(tok == ")"){
            if(parenDepth > 0u){
                --parenDepth;
                if(parenDepth == 0u){
                    ++stmt.topTokCount;
                    stmt.prevTopTok = ")";
                }
            }
            continue;
        }

        if(parenDepth > 0u)
            continue;

        if(tok == "<" && stmt.prevTopTok != "operator"){
            ++stmt.angleDepth;
            ++stmt.topTokCount;
            stmt.prevTopTok = tok;
            continue;
        }

        if(tok == ">" && stmt.angleDepth > 0u && stmt.prevTopTok != "operator"){
            --stmt.angleDepth;
            ++stmt.topTokCount;
            stmt.prevTopTok = tok;
            continue;
        }

        if(tok == "{"){
            const b8 isExternLinkageBlock = stmt.firstTok == "extern" && stmt.secondTok == "\"\"" && !stmt.sawParamParens && !stmt.sawEqual;
            if(stmt.sawNamespace || isExternLinkageBlock){
                ++namespaceDepth;
            }
            else if(stmt.sawTypeKeyword && !stmt.sawParamParens){
                innerDepth = 1u;
            }
            else if(stmt.sawParamParens && !stmt.sawEqual){
                if(!stmt.hasInline && !stmt.hasStatic)
                    return true;
                innerDepth = 1u;
            }
            else{
                if(stmt.topTokCount > 0u && !stmt.hasInline && !stmt.hasStatic && !stmt.hasExtern && !stmt.sawNonVarDeclKeyword && !stmt.sawNamespace)
                    return true;
                innerDepth = 1u;
            }
            stmt = StatementScanState{};
            continue;
        }

        if(tok == "}"){
            if(namespaceDepth > 0u)
                --namespaceDepth;
            stmt = StatementScanState{};
            continue;
        }

        if(tok == ";"){
            if(stmt.sawEqual && !stmt.sawNonVarDeclKeyword && !stmt.sawNamespace){
                if(stmt.lhsTokCount > 0u && !stmt.lhsHasInline && !(stmt.lhsHasStatic && !stmt.lhsHasColonColon) && !stmt.lhsHasExtern){
                    const b8 isInternalConst = stmt.lhsHasConst && !stmt.lhsHasColonColon && !stmt.lhsHasVolatile;
                    if(!isInternalConst)
                        return true;
                }
            }
            stmt = StatementScanState{};
            continue;
        }

        if(tok == "=" && stmt.angleDepth == 0u && stmt.prevTopTok != "operator"){
            if(!stmt.sawEqual){
                stmt.sawEqual = true;
                stmt.lhsTokCount = stmt.topTokCount;
                stmt.lhsHasInline = stmt.hasInline;
                stmt.lhsHasStatic = stmt.hasStatic;
                stmt.lhsHasExtern = stmt.hasExtern;
            }
            ++stmt.topTokCount;
            stmt.prevTopTok = tok;
            continue;
        }

        if(stmt.topTokCount == 0u)
            stmt.firstTok = tok;
        else if(stmt.topTokCount == 1u)
            stmt.secondTok = tok;
        ++stmt.topTokCount;

        if(tok == "namespace")
            stmt.sawNamespace = true;
        else if(IsTypeDeclarationKeyword(tok))
            stmt.sawTypeKeyword = true;
        else if(IsNonVariableDeclKeyword(tok))
            stmt.sawNonVarDeclKeyword = true;

        if(IsInlineSpecifier(tok))
            stmt.hasInline = true;
        else if(tok == "static")
            stmt.hasStatic = true;
        else if(tok == "extern")
            stmt.hasExtern = true;

        if(!stmt.sawEqual){
            if(tok == "const")
                stmt.lhsHasConst = true;
            else if(tok == "volatile")
                stmt.lhsHasVolatile = true;
            else if(tok == "::")
                stmt.lhsHasColonColon = true;
        }

        stmt.prevTopTok = tok;
    }

    return false;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_ADHD_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

