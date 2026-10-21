// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "expression_parser.h"

#include "arena_names.h"

#include <global/simd/vector_arithmetic.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_METASCRIPT_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace __hidden_metascript_expression_parser{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


constexpr usize s_InitialReferencePathSegmentCount = 4u;


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


Value ExpressionParser::parseExpression(){
    return parseAdditive();
}

bool ExpressionParser::parseReferencePath(ScratchPath& path){
    while(m_cursor.m_current.type == TokenType::Dot){
        m_cursor.advance();
        if(m_cursor.m_current.type != TokenType::Identifier){
            m_cursor.errorExpected("expected identifier after '.'");
            return false;
        }
        path.push_back(m_cursor.m_current.text);
        m_cursor.advance();
    }
    return true;
}


template<typename ParseNext, typename ApplyOperation>
Value ExpressionParser::parseBinaryExpression(ParseNext&& parseNext, const TokenType::Enum lhsOp, const TokenType::Enum rhsOp, ApplyOperation&& applyOperation){
    Value left = parseNext();
    if(m_cursor.failed())
        return left;

    while(m_cursor.m_current.type == lhsOp || m_cursor.m_current.type == rhsOp){
        const auto op = m_cursor.m_current.type;
        const u32 opLine = m_cursor.m_current.line;
        const u32 opColumn = m_cursor.m_current.column;
        m_cursor.advance();
        Value right = parseNext();
        if(m_cursor.failed())
            return Value(m_arena);

        if(!m_validator.validateBinaryOperation(op, left, right, opLine, opColumn))
            return Value(m_arena);

        left = applyOperation(op, left, right);
    }

    return left;
}

Value ExpressionParser::parseAdditive(){
    return parseBinaryExpression(
        [&](){ return parseMultiplicative(); },
        TokenType::Plus, TokenType::Minus, [](const TokenType::Enum op, const Value& left, const Value& right){
            return op == TokenType::Plus ? left + right : left - right;
        }
    );
}

Value ExpressionParser::parseMultiplicative(){
    return parseBinaryExpression(
        [&](){ return parseUnary(); },
        TokenType::Star, TokenType::Slash, [](const TokenType::Enum op, const Value& left, const Value& right)noexcept{
            return op == TokenType::Star ? left * right : left / right;
        }
    );
}

Value ExpressionParser::parseUnary(){
    if(m_cursor.m_current.type == TokenType::Minus){
        m_cursor.advance();
        Value val = parseUnary();
        if(m_cursor.failed())
            return Value(m_arena);

        if(val.isInteger()){
            if(NegateOverflows<i64>(val.asInteger())){
                m_cursor.error(MetascriptArenaScope::s_IntegerOverflowText);
                return Value(m_arena);
            }
            return Value(-val.asInteger(), m_arena);
        }
        if(val.isDouble())
            return Value(VectorDoubleGetX(VectorDoubleNegate(VectorDoubleReplicate(val.asDouble()))), m_arena);

        m_cursor.error("unary '-' requires numeric operand");
        return Value(m_arena);
    }

    return parsePrimary();
}

Value ExpressionParser::parsePrimary(){
    switch(m_cursor.m_current.type){
    case TokenType::IntegerLiteral:{
        const auto text = m_cursor.m_current.text;
        m_cursor.advance();

        const auto result = ParseI64FromChars(text);
        if(!result){
            m_cursor.error("invalid integer literal");
            return Value(m_arena);
        }
        return Value(*result, m_arena);
    }
    case TokenType::DoubleLiteral:{
        const auto text = m_cursor.m_current.text;
        m_cursor.advance();

        const auto result = ParseF64FromChars(text);
        if(!result){
            m_cursor.error("invalid double literal");
            return Value(m_arena);
        }
        return Value(*result, m_arena);
    }
    case TokenType::StringLiteral:{
        const auto text = m_cursor.m_current.text;
        m_cursor.advance();
        return Value(text, m_arena);
    }
    case TokenType::Identifier:{
        const auto name = m_cursor.m_current.text;
        m_cursor.advance();

        ScratchPath path{m_scratchArena};
        path.reserve(__hidden_metascript_expression_parser::s_InitialReferencePathSegmentCount);
        path.push_back(name);
        if(!parseReferencePath(path))
            return Value(m_arena);

        return makeReference(path);
    }
    case TokenType::LeftBracket:
        return parseListLiteral(TokenType::RightBracket);
    case TokenType::LeftBrace:
        return parseBraceExpression();
    case TokenType::LeftParen:{
        m_cursor.advance();
        Value val = parseExpression();
        if(m_cursor.failed())
            return Value(m_arena);
        if(!m_cursor.expect(TokenType::RightParen, "expected ')' after expression"))
            return Value(m_arena);
        return val;
    }
    default:
        break;
    }

    m_cursor.errorExpected("expected expression");
    return Value(m_arena);
}


Value ExpressionParser::parseListLiteral(TokenType::Enum closeToken){
    m_cursor.advance();

    Value list(m_arena);
    list.makeList();

    if(m_cursor.m_current.type == closeToken){
        m_cursor.advance();
        return list;
    }

    for(;;){
        Value elem = parseExpression();
        if(m_cursor.failed())
            return Value(m_arena);
        list.append(Move(elem));

        if(m_cursor.m_current.type == closeToken){
            m_cursor.advance();
            return list;
        }

        if(!m_cursor.expect(TokenType::Comma, "expected ',' or closing delimiter in list"))
            return Value(m_arena);
        if(m_cursor.m_current.type == closeToken){
            m_cursor.advance();
            return list;
        }
    }
}

Value ExpressionParser::parseBraceExpression(){
    m_cursor.advance();

    if(m_cursor.m_current.type == TokenType::RightBrace){
        m_cursor.advance();
        Value list(m_arena);
        list.makeList();
        return list;
    }

    Value first = parseExpression();
    if(m_cursor.failed())
        return Value(m_arena);

    if(m_cursor.m_current.type == TokenType::Colon)
        return parseMapLiteralContinuation(Move(first));

    Value list(m_arena);
    list.makeList();
    list.append(Move(first));

    if(m_cursor.m_current.type == TokenType::RightBrace){
        m_cursor.advance();
        return list;
    }

    if(!m_cursor.expect(TokenType::Comma, "expected ',' or '}' in list"))
        return Value(m_arena);
    if(m_cursor.m_current.type == TokenType::RightBrace){
        m_cursor.advance();
        return list;
    }

    while(m_cursor.m_current.type != TokenType::RightBrace){
        Value elem = parseExpression();
        if(m_cursor.failed())
            return Value(m_arena);
        list.append(Move(elem));

        if(m_cursor.m_current.type == TokenType::RightBrace)
            break;

        if(!m_cursor.expect(TokenType::Comma, "expected ',' or '}' in list"))
            return Value(m_arena);
        if(m_cursor.m_current.type == TokenType::RightBrace)
            break;
    }

    m_cursor.advance();
    return list;
}

Value ExpressionParser::parseMapLiteralContinuation(Value&& firstKey){
    m_cursor.advance();

    if(!firstKey.isString()){
        m_cursor.error("map keys must be strings");
        return Value(m_arena);
    }

    Value map(m_arena);
    map.makeMap();

    Value firstValue = parseExpression();
    if(m_cursor.failed())
        return Value(m_arena);

    map.field(firstKey.asString()) = Move(firstValue);

    if(m_cursor.m_current.type == TokenType::RightBrace){
        m_cursor.advance();
        return map;
    }

    if(!m_cursor.expect(TokenType::Comma, "expected ',' or '}' in map"))
        return Value(m_arena);
    if(m_cursor.m_current.type == TokenType::RightBrace){
        m_cursor.advance();
        return map;
    }

    while(m_cursor.m_current.type != TokenType::RightBrace){
        Value key = parseExpression();
        if(m_cursor.failed())
            return Value(m_arena);

        if(!key.isString()){
            m_cursor.error("map keys must be strings");
            return Value(m_arena);
        }

        if(!m_cursor.expect(TokenType::Colon, "expected ':' after map key"))
            return Value(m_arena);

        Value val = parseExpression();
        if(m_cursor.failed())
            return Value(m_arena);

        map.field(key.asString()) = Move(val);

        if(m_cursor.m_current.type == TokenType::RightBrace)
            break;

        if(!m_cursor.expect(TokenType::Comma, "expected ',' or '}' in map"))
            return Value(m_arena);
        if(m_cursor.m_current.type == TokenType::RightBrace)
            break;
    }

    m_cursor.advance();
    return map;
}


Value ExpressionParser::makeReference(const ScratchPath& path){
    NWB_ASSERT(!path.empty());

    if(m_variables.find(path[0]) == m_variables.end()){
        m_cursor.error("references must target a declared variable");
        return Value(m_arena);
    }

    usize textSize = path.size() - 1u;
    for(const MStringView segment : path)
        textSize += segment.size();

    ScratchString text(m_scratchArena);
    text.reserve(textSize);
    for(usize i = 0u; i < path.size(); ++i){
        if(i != 0u)
            text.push_back('.');
        text.append(path[i].data(), path[i].size());
    }

    return Value::Reference(MStringView(text.data(), text.size()), m_arena);
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_METASCRIPT_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

