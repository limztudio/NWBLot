// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "preprocessor.h"

#include <global/text_utils.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_METASCRIPT_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace __hidden_metascript_preprocessor{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


constexpr u32 s_MaxMacroExpansionPasses = 16u;


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


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
    [[nodiscard]] b8 evaluate(i64& outValue){
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
                b8 hasParen = false;
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
    b8 m_failed = false;
};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


b8 Preprocessor::expandLineMacros(const MStringView lineText, const u32 line, MString& outExpanded){
    using namespace __hidden_metascript_preprocessor;

    MString current(lineText.data(), lineText.size(), m_arena);

    for(u32 pass = 0u; pass < s_MaxMacroExpansionPasses; ++pass){
        b8 anyReplaced = false;
        MString next(m_arena);
        next.reserve(current.size());

        usize i = 0u;
        while(i < current.size()){
            if(current[i] == '"' || current[i] == '\''){
                const MChar quote = current[i];
                next.push_back(current[i++]);
                while(i < current.size() && current[i] != quote){
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

            if(i + 1u < current.size() && current[i] == '/' && current[i + 1u] == '*'){
                const usize closeComment = MStringView(current.data(), current.size()).find("*/", i + 2u);
                if(closeComment == MStringView::npos){
                    next.append(current.data() + i, current.size() - i);
                    break;
                }
                next.append(current.data() + i, (closeComment + 2u) - i);
                i = closeComment + 2u;
                continue;
            }

            if(IsAsciiDigit(current[i])){
                while(i < current.size()){
                    const MChar c = current[i];
                    if((c == 'e' || c == 'E' || c == 'p' || c == 'P') && i + 1u < current.size() && (current[i + 1u] == '+' || current[i + 1u] == '-')){
                        next.push_back(current[i++]);
                        next.push_back(current[i++]);
                        continue;
                    }
                    if(IsAsciiDigit(c) || IsAsciiIdentifierChar(c) || c == '.'){
                        next.push_back(current[i++]);
                        continue;
                    }
                    break;
                }
                continue;
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
                    if(ch == '"' || ch == '\''){
                        const MChar quote = ch;
                        ++argScan;
                        while(argScan < current.size() && current[argScan] != quote){
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
                            const MStringView argText = TrimView(MStringView(current.data() + argStart, argScan - argStart));
                            if(!argText.empty() || !macro.parameters.empty())
                                args.push_back(MString(argText.data(), argText.size(), m_arena));
                            ++argScan;
                            break;
                        }
                    }
                    else if(ch == ',' && parenDepth == 1u){
                        const MStringView argText = TrimView(MStringView(current.data() + argStart, argScan - argStart));
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
                        b8 matchedParam = false;
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

b8 Preprocessor::evaluateCondition(const MStringView exprText, const u32 line, b8& outResult){
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
            usize afterIdent = i;
            while(afterIdent < exprText.size() && (exprText[afterIdent] == ' ' || exprText[afterIdent] == '\t'))
                ++afterIdent;
            if(afterIdent < exprText.size() && exprText[afterIdent] == '('){
                u32 parenDepth = 0u;
                usize scan = afterIdent;
                while(scan < exprText.size()){
                    if(exprText[scan] == '(')
                        ++parenDepth;
                    else if(exprText[scan] == ')'){
                        if(--parenDepth == 0u){
                            ++scan;
                            break;
                        }
                    }
                    ++scan;
                }
                if(parenDepth == 0u)
                    i = scan;
            }
            const MStringView macroTarget = exprText.substr(idStart, i - idStart);
            MString piece(m_arena);
            if(!expandLineMacros(macroTarget, line, piece))
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


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_METASCRIPT_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
