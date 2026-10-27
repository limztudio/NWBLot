// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "preprocessor.h"

#include <global/text_utils.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_METASCRIPT_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace __hidden_metascript_preprocessor{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


constexpr u32 s_MaxIncludeDepth = 64u;
constexpr u32 s_MaxMacroExpansionPasses = 16u;

[[nodiscard]] constexpr MStringView TrimAsciiWhitespace(MStringView text)noexcept{
    usize start = 0u;
    while(start < text.size() && (text[start] == ' ' || text[start] == '\t' || text[start] == '\r'))
        ++start;
    usize end = text.size();
    while(end > start && (text[end - 1u] == ' ' || text[end - 1u] == '\t' || text[end - 1u] == '\r'))
        --end;
    return text.substr(start, end - start);
}

[[nodiscard]] constexpr MStringView StripLineComment(MStringView line)noexcept{
    bool inString = false;
    for(usize i = 0u; i < line.size(); ++i){
        if(line[i] == '"' && (i == 0u || line[i - 1u] != '\\'))
            inString = !inString;
        if(!inString && i + 1u < line.size() && line[i] == '/' && line[i + 1u] == '/')
            return line.substr(0u, i);
    }
    return line;
}

class ConditionEvaluator{
public:
    ConditionEvaluator(MStringView text, const Preprocessor::MacroMap& macros, MetaArena& arena, MVector<ParseError>& errors, const u32 line)noexcept
        : m_lexer(text)
        , m_macros(macros)
        , m_arena(arena)
        , m_errors(errors)
        , m_line(line)
    {
        advance();
    }


public:
    [[nodiscard]] bool evaluate(i64& outValue){
        outValue = parseLogicalOr();
        if(m_failed)
            return false;
        if(m_current.type != TokenType::EndOfFile){
            fail("unexpected token in preprocessor condition");
            return false;
        }
        return true;
    }


private:
    void advance()noexcept{
        m_current = m_lexer.next();
        if(m_current.type == TokenType::Error)
            fail(m_current.text);
    }

    void fail(MStringView msg){
        if(!m_failed){
            m_failed = true;
            m_errors.push_back(ParseError{m_line, 1u, MString(msg.data(), msg.size(), m_arena)});
        }
    }

    [[nodiscard]] i64 parseLogicalOr(){
        i64 left = parseLogicalAnd();
        while(!m_failed && m_current.type == TokenType::PipePipe){
            advance();
            const i64 right = parseLogicalAnd();
            left = (left != 0 || right != 0) ? 1 : 0;
        }
        return left;
    }

    [[nodiscard]] i64 parseLogicalAnd(){
        i64 left = parseBitwiseOr();
        while(!m_failed && m_current.type == TokenType::AmpersandAmpersand){
            advance();
            const i64 right = parseBitwiseOr();
            left = (left != 0 && right != 0) ? 1 : 0;
        }
        return left;
    }

    [[nodiscard]] i64 parseBitwiseOr(){
        i64 left = parseBitwiseXor();
        while(!m_failed && m_current.type == TokenType::Pipe){
            advance();
            const i64 right = parseBitwiseXor();
            left = left | right;
        }
        return left;
    }

    [[nodiscard]] i64 parseBitwiseXor(){
        i64 left = parseBitwiseAnd();
        while(!m_failed && m_current.type == TokenType::Caret){
            advance();
            const i64 right = parseBitwiseAnd();
            left = left ^ right;
        }
        return left;
    }

    [[nodiscard]] i64 parseBitwiseAnd(){
        i64 left = parseEquality();
        while(!m_failed && m_current.type == TokenType::Ampersand){
            advance();
            const i64 right = parseEquality();
            left = left & right;
        }
        return left;
    }

    [[nodiscard]] i64 parseEquality(){
        i64 left = parseRelational();
        while(!m_failed && (m_current.type == TokenType::EqualEqual || m_current.type == TokenType::BangEqual)){
            const TokenType::Enum op = m_current.type;
            advance();
            const i64 right = parseRelational();
            left = (op == TokenType::EqualEqual ? (left == right) : (left != right)) ? 1 : 0;
        }
        return left;
    }

    [[nodiscard]] i64 parseRelational(){
        i64 left = parseShift();
        while(!m_failed && (m_current.type == TokenType::Less || m_current.type == TokenType::LessEqual || m_current.type == TokenType::Greater || m_current.type == TokenType::GreaterEqual)){
            const TokenType::Enum op = m_current.type;
            advance();
            const i64 right = parseShift();
            if(op == TokenType::Less)
                left = left < right ? 1 : 0;
            else if(op == TokenType::LessEqual)
                left = left <= right ? 1 : 0;
            else if(op == TokenType::Greater)
                left = left > right ? 1 : 0;
            else
                left = left >= right ? 1 : 0;
        }
        return left;
    }

    [[nodiscard]] i64 parseShift(){
        i64 left = parseAdditive();
        while(!m_failed && (m_current.type == TokenType::LessLess || m_current.type == TokenType::GreaterGreater)){
            const TokenType::Enum op = m_current.type;
            advance();
            const i64 right = parseAdditive();
            left = op == TokenType::LessLess ? (left << right) : (left >> right);
        }
        return left;
    }

    [[nodiscard]] i64 parseAdditive(){
        i64 left = parseMultiplicative();
        while(!m_failed && (m_current.type == TokenType::Plus || m_current.type == TokenType::Minus)){
            const TokenType::Enum op = m_current.type;
            advance();
            const i64 right = parseMultiplicative();
            left = op == TokenType::Plus ? (left + right) : (left - right);
        }
        return left;
    }

    [[nodiscard]] i64 parseMultiplicative(){
        i64 left = parseUnary();
        while(!m_failed && (m_current.type == TokenType::Star || m_current.type == TokenType::Slash || m_current.type == TokenType::Percent)){
            const TokenType::Enum op = m_current.type;
            advance();
            const i64 right = parseUnary();
            if(op != TokenType::Star && right == 0){
                fail("division by zero in preprocessor condition");
                return 0;
            }
            if(op == TokenType::Star)
                left = left * right;
            else if(op == TokenType::Slash)
                left = left / right;
            else
                left = left % right;
        }
        return left;
    }

    [[nodiscard]] i64 parseUnary(){
        if(m_current.type == TokenType::Plus){
            advance();
            return parseUnary();
        }
        if(m_current.type == TokenType::Minus){
            advance();
            return -parseUnary();
        }
        if(m_current.type == TokenType::Bang){
            advance();
            return parseUnary() == 0 ? 1 : 0;
        }
        if(m_current.type == TokenType::Tilde){
            advance();
            return ~parseUnary();
        }
        return parsePrimary();
    }

    [[nodiscard]] i64 parsePrimary(){
        if(m_current.type == TokenType::LeftParen){
            advance();
            const i64 val = parseLogicalOr();
            if(m_current.type != TokenType::RightParen){
                fail("expected ')' in preprocessor condition");
                return 0;
            }
            advance();
            return val;
        }

        if(m_current.type == TokenType::IntegerLiteral){
            MStringView text = m_current.text;
            while(!text.empty() && (text.back() == 'u' || text.back() == 'U' || text.back() == 'l' || text.back() == 'L'))
                text.remove_suffix(1u);
            advance();
            if(text.size() > 2u && text[0] == '0' && (text[1] == 'x' || text[1] == 'X')){
                u64 val = 0u;
                for(usize i = 2u; i < text.size(); ++i){
                    const MChar ch = text[i];
                    const u32 digit = IsAsciiDigit(ch) ? static_cast<u32>(ch - '0') : static_cast<u32>((ch | 0x20) - 'a' + 10);
                    val = (val << 4u) | digit;
                }
                return static_cast<i64>(val);
            }
            if(text.size() > 2u && text[0] == '0' && (text[1] == 'b' || text[1] == 'B')){
                u64 val = 0u;
                for(usize i = 2u; i < text.size(); ++i)
                    val = (val << 1u) | static_cast<u64>(text[i] - '0');
                return static_cast<i64>(val);
            }
            const auto parsed = ParseI64(text);
            if(!parsed){
                fail("invalid integer literal in preprocessor condition");
                return 0;
            }
            return *parsed;
        }

        if(m_current.type == TokenType::Identifier){
            const MStringView ident = m_current.text;
            advance();
            if(ident == "true")
                return 1;
            if(ident == "false")
                return 0;
            if(ident == "defined"){
                bool hasParen = false;
                if(m_current.type == TokenType::LeftParen){
                    hasParen = true;
                    advance();
                }
                if(m_current.type != TokenType::Identifier){
                    fail("expected macro name after 'defined'");
                    return 0;
                }
                const MStringView macroName = m_current.text;
                advance();
                if(hasParen){
                    if(m_current.type != TokenType::RightParen){
                        fail("expected ')' after defined(NAME)");
                        return 0;
                    }
                    advance();
                }
                return m_macros.find(macroName) != m_macros.end() ? 1 : 0;
            }
            return 0;
        }

        fail("expected expression in preprocessor condition");
        return 0;
    }


private:
    Lexer m_lexer;
    const Preprocessor::MacroMap& m_macros;
    MetaArena& m_arena;
    MVector<ParseError>& m_errors;
    Token m_current;
    u32 m_line = 1;
    bool m_failed = false;
};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


Preprocessor::Preprocessor(MetaArena& arena, Alloc::ScratchArena& scratchArena, MVector<ParseError>& errors, PreprocessorOptions options)
    : m_arena(arena)
    , m_scratchArena(scratchArena)
    , m_errors(errors)
    , m_options(options)
    , m_macros(0, MStringHash(), MStringEqual(), arena)
    , m_includedSet(0, MStringHash(), MStringEqual(), arena)
    , m_includedFiles(arena)
{
#if defined(NWB_DEBUG) && NWB_DEBUG
    defineObjectMacro("NWB_DEBUG", "1");
#endif
#if defined(NWB_OPTIMIZE) && NWB_OPTIMIZE
    defineObjectMacro("NWB_OPTIMIZE", "1");
#endif
#if defined(NWB_FINAL) && NWB_FINAL
    defineObjectMacro("NWB_FINAL", "1");
#endif
}


void Preprocessor::defineObjectMacro(const MStringView name, const MStringView body){
    PreprocessorMacro macro(m_arena);
    macro.name.assign(name.data(), name.size());
    macro.body.assign(body.data(), body.size());
    macro.isFunctionLike = false;
    MString key(name.data(), name.size(), m_arena);
    m_macros.insert_or_assign(Move(key), Move(macro));
}

void Preprocessor::undefineMacro(const MStringView name){
    const auto it = m_macros.find(name);
    if(it != m_macros.end())
        m_macros.erase(it);
}

bool Preprocessor::isMacroDefined(const MStringView name)const noexcept{
    return m_macros.find(name) != m_macros.end();
}

bool Preprocessor::process(const MStringView source, MString& outProcessed){
    outProcessed.clear();
    outProcessed.reserve(source.size());
    return processInternal(source, 0u, outProcessed);
}

bool Preprocessor::processInternal(const MStringView source, const u32 depth, MString& outProcessed){
    using namespace __hidden_metascript_preprocessor;

    if(depth > s_MaxIncludeDepth){
        m_errors.push_back(ParseError{1u, 1u, MString("maximum #include nesting depth exceeded", m_arena)});
        return false;
    }

    Vector<ConditionalFrame, Alloc::ScratchArena> condStack{m_scratchArena};
    condStack.reserve(8u);

    bool inNativeKeywordPendingBrace = false;
    u32 nativeBraceDepth = 0u;
    u32 currentLine = 1u;
    usize pos = 0u;

    while(pos < source.size()){
        usize lineEnd = pos;
        while(lineEnd < source.size() && source[lineEnd] != '\n')
            ++lineEnd;

        MStringView rawLine = source.substr(pos, lineEnd - pos);
        if(!rawLine.empty() && rawLine.back() == '\r')
            rawLine.remove_suffix(1u);

        const bool isActive = condStack.empty() || condStack.back().currentActive;
        const MStringView trimmed = TrimAsciiWhitespace(rawLine);

        if(!trimmed.empty() && trimmed[0] == '#'){
            const MStringView afterHash = TrimAsciiWhitespace(trimmed.substr(1u));
            const bool isNativeInclude = m_options.preserveNativeIncludeDirectives && (nativeBraceDepth > 0u) && afterHash.starts_with("include");
            if(isNativeInclude){
                if(isActive)
                    outProcessed.append(rawLine.data(), rawLine.size());
                outProcessed.push_back('\n');
            }
            else{
                if(!handleDirective(afterHash, currentLine, depth, condStack, outProcessed))
                    return false;
                outProcessed.push_back('\n');
            }
        }
        else if(isActive){
            MString expandedLine(m_arena);
            if(!expandLineMacros(rawLine, currentLine, expandedLine))
                return false;

            Lexer scanLexer(expandedLine);
            for(;;){
                const Token tok = scanLexer.next();
                if(tok.type == TokenType::EndOfFile || tok.type == TokenType::Error)
                    break;
                if(tok.type == TokenType::Identifier && tok.text == "native" && nativeBraceDepth == 0u)
                    inNativeKeywordPendingBrace = true;
                else if(tok.type == TokenType::LeftBrace){
                    if(inNativeKeywordPendingBrace || nativeBraceDepth > 0u){
                        ++nativeBraceDepth;
                        inNativeKeywordPendingBrace = false;
                    }
                }
                else if(tok.type == TokenType::RightBrace){
                    if(nativeBraceDepth > 0u)
                        --nativeBraceDepth;
                }
            }

            outProcessed.append(expandedLine.data(), expandedLine.size());
            outProcessed.push_back('\n');
        }
        else{
            outProcessed.push_back('\n');
        }

        pos = (lineEnd < source.size()) ? (lineEnd + 1u) : lineEnd;
        ++currentLine;
    }

    if(!condStack.empty()){
        m_errors.push_back(ParseError{condStack.back().line, 1u, MString("unterminated preprocessor conditional (#if/#ifdef/#ifndef without #endif)", m_arena)});
        return false;
    }

    return true;
}

bool Preprocessor::handleDirective(const MStringView directiveLine, const u32 line, const u32 depth, Vector<ConditionalFrame, Alloc::ScratchArena>& condStack, MString& outProcessed){
    using namespace __hidden_metascript_preprocessor;

    const MStringView cleanLine = TrimAsciiWhitespace(StripLineComment(directiveLine));
    usize cmdEnd = 0u;
    while(cmdEnd < cleanLine.size() && IsAsciiIdentifierChar(cleanLine[cmdEnd]))
        ++cmdEnd;

    const MStringView cmd = cleanLine.substr(0u, cmdEnd);
    const MStringView rest = TrimAsciiWhitespace(cleanLine.substr(cmdEnd));
    const bool parentActive = condStack.empty() || condStack.back().currentActive;

    if(cmd == "ifdef" || cmd == "ifndef"){
        ConditionalFrame frame;
        frame.line = line;
        frame.parentActive = parentActive;
        if(!parentActive){
            frame.branchTaken = true;
            frame.currentActive = false;
            condStack.push_back(frame);
            return true;
        }
        if(rest.empty()){
            m_errors.push_back(ParseError{line, 1u, MString("expected macro name after #ifdef/#ifndef", m_arena)});
            return false;
        }
        usize nameEnd = 0u;
        while(nameEnd < rest.size() && IsAsciiIdentifierChar(rest[nameEnd]))
            ++nameEnd;
        const MStringView macroName = rest.substr(0u, nameEnd);
        const bool defined = isMacroDefined(macroName);
        frame.currentActive = (cmd == "ifdef") ? defined : !defined;
        frame.branchTaken = frame.currentActive;
        condStack.push_back(frame);
        return true;
    }

    if(cmd == "if"){
        ConditionalFrame frame;
        frame.line = line;
        frame.parentActive = parentActive;
        if(!parentActive){
            frame.branchTaken = true;
            frame.currentActive = false;
            condStack.push_back(frame);
            return true;
        }
        bool condResult = false;
        if(!evaluateCondition(rest, line, condResult))
            return false;
        frame.currentActive = condResult;
        frame.branchTaken = condResult;
        condStack.push_back(frame);
        return true;
    }

    if(cmd == "elif"){
        if(condStack.empty() || condStack.back().seenElse){
            m_errors.push_back(ParseError{line, 1u, MString("unexpected #elif", m_arena)});
            return false;
        }
        ConditionalFrame& frame = condStack.back();
        if(!frame.parentActive || frame.branchTaken){
            frame.currentActive = false;
            return true;
        }
        bool condResult = false;
        if(!evaluateCondition(rest, line, condResult))
            return false;
        frame.currentActive = condResult;
        frame.branchTaken = condResult;
        return true;
    }

    if(cmd == "else"){
        if(condStack.empty() || condStack.back().seenElse){
            m_errors.push_back(ParseError{line, 1u, MString("unexpected #else", m_arena)});
            return false;
        }
        ConditionalFrame& frame = condStack.back();
        frame.seenElse = true;
        frame.currentActive = frame.parentActive && !frame.branchTaken;
        frame.branchTaken = true;
        return true;
    }

    if(cmd == "endif"){
        if(condStack.empty()){
            m_errors.push_back(ParseError{line, 1u, MString("unexpected #endif without matching #if", m_arena)});
            return false;
        }
        condStack.pop_back();
        return true;
    }

    if(!parentActive)
        return true;

    if(cmd == "define")
        return handleDefine(rest, line);
    if(cmd == "undef")
        return handleUndef(rest, line);
    if(cmd == "include")
        return handleInclude(rest, line, depth, outProcessed);

    m_errors.push_back(ParseError{line, 1u, MString("unknown preprocessor directive", m_arena)});
    return false;
}

bool Preprocessor::handleInclude(const MStringView rest, const u32 line, const u32 depth, MString& outProcessed){
    if(rest.size() < 3u){
        m_errors.push_back(ParseError{line, 1u, MString("invalid #include syntax", m_arena)});
        return false;
    }

    const MChar openChar = rest[0];
    const MChar closeChar = (openChar == '"') ? '"' : ((openChar == '<') ? '>' : '\0');
    if(closeChar == '\0'){
        m_errors.push_back(ParseError{line, 1u, MString("expected '\"' or '<' after #include", m_arena)});
        return false;
    }

    const usize closePos = rest.find(closeChar, 1u);
    if(closePos == MStringView::npos || closePos <= 1u){
        m_errors.push_back(ParseError{line, 1u, MString("unterminated #include path", m_arena)});
        return false;
    }

    const MStringView includePath = rest.substr(1u, closePos - 1u);
    if(m_includedSet.find(includePath) != m_includedSet.end())
        return true;
    if(!m_options.includeResolver){
        m_errors.push_back(ParseError{line, 1u, MString("#include is not available without an include resolver", m_arena)});
        return false;
    }

    MString resolvedKey(m_arena);
    MString content(m_arena);
    if(!m_options.includeResolver->resolveInclude(includePath, openChar == '<', m_arena, resolvedKey, content)){
        m_errors.push_back(ParseError{line, 1u, MString("failed to resolve #include file", m_arena)});
        return false;
    }

    if(m_includedSet.find(resolvedKey) != m_includedSet.end())
        return true;

    m_includedSet.emplace(MString(includePath.data(), includePath.size(), m_arena), true);
    m_includedSet.emplace(MString(resolvedKey.data(), resolvedKey.size(), m_arena), true);
    m_includedFiles.push_back(MString( includePath.data(), includePath.size(), m_arena));

    if(m_options.expandIncludedFilesInline)
        return processInternal(content, depth + 1u, outProcessed);

    return true;
}

bool Preprocessor::handleDefine(const MStringView rest, const u32 line){
    using namespace __hidden_metascript_preprocessor;

    if(rest.empty() || !IsAsciiIdentifierStart(rest[0])){
        m_errors.push_back(ParseError{line, 1u, MString("expected macro name after #define", m_arena)});
        return false;
    }

    usize nameEnd = 0u;
    while(nameEnd < rest.size() && IsAsciiIdentifierChar(rest[nameEnd]))
        ++nameEnd;

    const MStringView name = rest.substr(0u, nameEnd);
    PreprocessorMacro macro(m_arena);
    macro.name.assign(name.data(), name.size());

    if(nameEnd < rest.size() && rest[nameEnd] == '('){
        macro.isFunctionLike = true;
        const usize closeParen = rest.find(')', nameEnd + 1u);
        if(closeParen == MStringView::npos){
            m_errors.push_back(ParseError{line, 1u, MString("unterminated parameter list in function-like #define", m_arena)});
            return false;
        }
        const MStringView paramList = TrimAsciiWhitespace(rest.substr(nameEnd + 1u, closeParen - (nameEnd + 1u)));
        if(!paramList.empty()){
            usize pStart = 0u;
            while(pStart < paramList.size()){
                usize comma = paramList.find(',', pStart);
                if(comma == MStringView::npos)
                    comma = paramList.size();
                const MStringView paramName = TrimAsciiWhitespace(paramList.substr(pStart, comma - pStart));
                if(paramName.empty() || !IsAsciiIdentifierStart(paramName[0])){
                    m_errors.push_back(ParseError{line, 1u, MString("invalid parameter name in function-like #define", m_arena)});
                    return false;
                }
                macro.parameters.push_back(MString(paramName.data(), paramName.size(), m_arena));
                pStart = comma + 1u;
            }
        }
        const MStringView body = TrimAsciiWhitespace(rest.substr(closeParen + 1u));
        macro.body.assign(body.data(), body.size());
    }
    else{
        const MStringView body = TrimAsciiWhitespace(rest.substr(nameEnd));
        macro.body.assign(body.data(), body.size());
    }

    MString key(name.data(), name.size(), m_arena);
    m_macros.insert_or_assign(Move(key), Move(macro));
    return true;
}

bool Preprocessor::handleUndef(const MStringView rest, const u32 line){
    if(rest.empty() || !IsAsciiIdentifierStart(rest[0])){
        m_errors.push_back(ParseError{line, 1u, MString("expected macro name after #undef", m_arena)});
        return false;
    }
    usize nameEnd = 0u;
    while(nameEnd < rest.size() && IsAsciiIdentifierChar(rest[nameEnd]))
        ++nameEnd;
    undefineMacro(rest.substr(0u, nameEnd));
    return true;
}

bool Preprocessor::evaluateCondition(const MStringView exprText, const u32 line, bool& outResult){
    using namespace __hidden_metascript_preprocessor;

    MString expanded(m_arena);
    usize i = 0u;
    while(i < exprText.size()){
        if(IsAsciiIdentifierStart(exprText[i])){
            const usize idStart = i;
            while(i < exprText.size() && IsAsciiIdentifierChar(exprText[i]))
                ++i;
            const MStringView ident = exprText.substr(idStart, i - idStart);
            if(ident == "defined"){
                expanded.append(ident.data(), ident.size());
                while(i < exprText.size() && (exprText[i] == ' ' || exprText[i] == '\t'))
                    expanded.push_back(exprText[i++]);
                if(i < exprText.size() && exprText[i] == '('){
                    expanded.push_back(exprText[i++]);
                    while(i < exprText.size() && exprText[i] != ')')
                        expanded.push_back(exprText[i++]);
                    if(i < exprText.size())
                        expanded.push_back(exprText[i++]);
                }
                else if(i < exprText.size() && IsAsciiIdentifierStart(exprText[i])){
                    while(i < exprText.size() && IsAsciiIdentifierChar(exprText[i]))
                        expanded.push_back(exprText[i++]);
                }
                continue;
            }
            MString piece(m_arena);
            if(!expandLineMacros(ident, line, piece))
                return false;
            expanded.append(piece.data(), piece.size());
        }
        else{
            expanded.push_back(exprText[i++]);
        }
    }

    ConditionEvaluator evaluator(expanded, m_macros, m_arena, m_errors, line);
    i64 value = 0;
    if(!evaluator.evaluate(value))
        return false;
    outResult = (value != 0);
    return true;
}

bool Preprocessor::expandLineMacros(const MStringView lineText, const u32 line, MString& outExpanded){
    using namespace __hidden_metascript_preprocessor;

    MString current(lineText.data(), lineText.size(), m_arena);

    for(u32 pass = 0u; pass < s_MaxMacroExpansionPasses; ++pass){
        bool anyReplaced = false;
        MString next(m_arena);
        next.reserve(current.size());

        usize i = 0u;
        while(i < current.size()){
            if(current[i] == '"'){
                next.push_back(current[i++]);
                while(i < current.size() && current[i] != '"'){
                    if(current[i] == '\\' && i + 1u < current.size())
                        next.push_back(current[i++]);
                    next.push_back(current[i++]);
                }
                if(i < current.size())
                    next.push_back(current[i++]);
                continue;
            }

            if(i + 1u < current.size() && current[i] == '/' && current[i + 1u] == '/'){
                next.append(current.data() + i, current.size() - i);
                break;
            }

            if(IsAsciiIdentifierStart(current[i])){
                const usize idStart = i;
                while(i < current.size() && IsAsciiIdentifierChar(current[i]))
                    ++i;
                const MStringView ident(current.data() + idStart, i - idStart);
                const auto it = m_macros.find(ident);
                if(it == m_macros.end()){
                    next.append(ident.data(), ident.size());
                    continue;
                }

                const PreprocessorMacro& macro = it.value();
                if(!macro.isFunctionLike){
                    next.append(macro.body.data(), macro.body.size());
                    anyReplaced = true;
                    continue;
                }

                usize lookahead = i;
                while(lookahead < current.size() && (current[lookahead] == ' ' || current[lookahead] == '\t'))
                    ++lookahead;
                if(lookahead >= current.size() || current[lookahead] != '('){
                    next.append(ident.data(), ident.size());
                    continue;
                }

                usize argScan = lookahead + 1u;
                u32 parenDepth = 1u;
                MVector<MString> args(m_arena);
                usize argStart = argScan;
                while(argScan < current.size() && parenDepth > 0u){
                    const MChar ch = current[argScan];
                    if(ch == '"'){
                        ++argScan;
                        while(argScan < current.size() && current[argScan] != '"'){
                            if(current[argScan] == '\\' && argScan + 1u < current.size())
                                ++argScan;
                            ++argScan;
                        }
                    }
                    else if(ch == '(')
                        ++parenDepth;
                    else if(ch == ')'){
                        --parenDepth;
                        if(parenDepth == 0u){
                            const MStringView argText = TrimAsciiWhitespace(MStringView(current.data() + argStart, argScan - argStart));
                            if(!argText.empty() || !macro.parameters.empty())
                                args.push_back(MString(argText.data(), argText.size(), m_arena));
                            ++argScan;
                            break;
                        }
                    }
                    else if(ch == ',' && parenDepth == 1u){
                        const MStringView argText = TrimAsciiWhitespace(MStringView(current.data() + argStart, argScan - argStart));
                        args.push_back(MString(argText.data(), argText.size(), m_arena));
                        argStart = argScan + 1u;
                    }
                    ++argScan;
                }

                if(parenDepth != 0u){
                    m_errors.push_back(ParseError{line, 1u, MString("unterminated function-like macro invocation", m_arena)});
                    return false;
                }
                if(args.size() != macro.parameters.size()){
                    m_errors.push_back(ParseError{line, 1u, MString("function-like macro argument count mismatch", m_arena)});
                    return false;
                }

                i = argScan;
                usize b = 0u;
                while(b < macro.body.size()){
                    if(IsAsciiIdentifierStart(macro.body[b])){
                        const usize pStart = b;
                        while(b < macro.body.size() && IsAsciiIdentifierChar(macro.body[b]))
                            ++b;
                        const MStringView tokenName(macro.body.data() + pStart, b - pStart);
                        bool matchedParam = false;
                        for(usize pIdx = 0u; pIdx < macro.parameters.size(); ++pIdx){
                            if(MStringView(macro.parameters[pIdx]) == tokenName){
                                next.append(args[pIdx].data(), args[pIdx].size());
                                matchedParam = true;
                                break;
                            }
                        }
                        if(!matchedParam)
                            next.append(tokenName.data(), tokenName.size());
                    }
                    else{
                        next.push_back(macro.body[b++]);
                    }
                }
                anyReplaced = true;
            }
            else{
                next.push_back(current[i++]);
            }
        }

        current = Move(next);
        if(!anyReplaced)
            break;
    }

    outExpanded = Move(current);
    return true;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_METASCRIPT_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

