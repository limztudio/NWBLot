// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "preprocessor.h"
#include "literal_decoder.h"
#include "preprocessor_macro_expander.h"


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_METASCRIPT_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace __hidden_metascript_preprocessor{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


constexpr i64 s_ShiftOperandMin = 0;
constexpr i64 s_I64ShiftOperandMax = 64;


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


class ConditionEvaluator final{
public:
    ConditionEvaluator(MStringView text, const Preprocessor::MacroMap& macros, MetaArena& arena, MVector<ParseError>& errors, const u32 line)
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
        outValue = parseTernary();
        if(m_failed)
            return false;
        if(m_current.type != TokenType::EndOfFile){
            fail("unexpected token in preprocessor condition");
            return false;
        }
        return true;
    }


private:
    void advance(){
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

    [[nodiscard]] i64 parseTernary(){
        const i64 cond = parseLogicalOr();
        if(!m_failed && m_current.type == TokenType::Question){
            advance();
            const i64 trueVal = parseTernary();
            if(m_current.type != TokenType::Colon){
                fail("expected ':' in preprocessor ternary expression");
                return 0;
            }
            advance();
            const i64 falseVal = parseTernary();
            return cond != 0 ? trueVal : falseVal;
        }
        return cond;
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
            if(right < s_ShiftOperandMin || right >= s_I64ShiftOperandMax){
                left = (op == TokenType::GreaterGreater && right >= s_I64ShiftOperandMax && left < 0) ? -1 : 0;
            }
            else if(op == TokenType::LessLess){
                left = static_cast<i64>(static_cast<u64>(left) << static_cast<u32>(right));
            }
            else{
                left = left >> static_cast<u32>(right);
            }
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
                left = (left == Limit<i64>::s_Min && right == -1) ? Limit<i64>::s_Min : (left / right);
            else
                left = (left == Limit<i64>::s_Min && right == -1) ? 0 : (left % right);
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
            const i64 val = parseTernary();
            if(m_current.type != TokenType::RightParen){
                fail("expected ')' in preprocessor condition");
                return 0;
            }
            advance();
            return val;
        }

        if(m_current.type == TokenType::IntegerLiteral){
            const MStringView text = m_current.text;
            advance();
            u64 parsed = 0u;
            if(!LiteralDecoder::TryParseUnsignedIntegerLiteral(text, parsed)){
                fail("invalid integer literal in preprocessor condition");
                return 0;
            }
            return static_cast<i64>(parsed);
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


b8 Preprocessor::evaluateCondition(const MStringView exprText, const u32 line, b8& outResult){
    using namespace __hidden_metascript_preprocessor;

    MString expanded(m_arena);
    PreprocessorMacroExpander macroExpander(m_arena, m_macros, m_errors);
    usize i = 0u;
    while(i < exprText.size()){
        if(exprText[i] == '"' || exprText[i] == '\''){
            i = AppendQuotedLiteral(exprText, i, expanded);
            continue;
        }
        if(IsAsciiDigit(exprText[i])){
            i = AppendPreprocessorNumber(exprText, i, false, expanded);
            continue;
        }
        if(IsAsciiIdentifierStart(exprText[i])){
            const usize idStart = i;
            while(i < exprText.size() && IsAsciiIdentifierChar(exprText[i]))
                ++i;
            const MStringView ident = exprText.substr(idStart, i - idStart);
            if(ident == "defined"){
                expanded.append(ident.data(), ident.size());
                i = PreprocessorMacroExpander::AppendDefinedOperand(exprText, i, expanded);
                continue;
            }
            usize afterIdent = i;
            while(afterIdent < exprText.size() && (exprText[afterIdent] == ' ' || exprText[afterIdent] == '\t'))
                ++afterIdent;
            if(afterIdent < exprText.size() && exprText[afterIdent] == '('){
                u32 parenDepth = 0u;
                usize scan = afterIdent;
                while(scan < exprText.size()){
                    const MChar ch = exprText[scan];
                    if(ch == '\'' && scan > 0u && (IsAsciiDigit(exprText[scan - 1u]) || IsAsciiIdentifierChar(exprText[scan - 1u]))){
                        ++scan;
                        continue;
                    }
                    if(ch == '"' || ch == '\''){
                        scan = SkipQuotedLiteral(exprText, scan);
                        continue;
                    }
                    if(ch == '(')
                        ++parenDepth;
                    else if(ch == ')'){
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
            if(!macroExpander.expand(macroTarget, line, piece))
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

