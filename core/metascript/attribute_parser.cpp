// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "attribute_parser.h"


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_METASCRIPT_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


bool AttributeParser::parseSingleBracketList(MVector<ParsedAttribute>& outAttributes){
    if(!m_cursor.check(TokenType::LeftBracket))
        return true;

    m_cursor.advance();

    if(m_cursor.check(TokenType::RightBracket)){
        m_cursor.errorExpected("expected attribute name inside '[...]'");
        return false;
    }

    for(;;){
        if(!parseAttributeItem(outAttributes))
            return false;

        if(m_cursor.check(TokenType::RightBracket))
            break;

        if(!m_cursor.expect(TokenType::Comma, "expected ',' or ']' in attribute list"))
            return false;
    }

    if(!m_cursor.expect(TokenType::RightBracket, "expected ']' after attribute list"))
        return false;

    if(m_cursor.check(TokenType::LeftBracket)){
        m_cursor.error("stacked '[...]' attribute blocks are prohibited; combine attributes with ',' inside a single '[...]'");
        return false;
    }

    return true;
}

bool AttributeParser::parseAttributeItem(MVector<ParsedAttribute>& outAttributes){
    if(!m_cursor.check(TokenType::Identifier)){
        m_cursor.errorExpected("expected attribute name");
        return false;
    }

    ParsedAttribute attr(m_cursor.arena());
    attr.name = m_cursor.current().text;
    attr.line = m_cursor.current().line;
    attr.column = m_cursor.current().column;
    m_cursor.advance();

    if(m_cursor.match(TokenType::LeftParen)){
        if(!m_cursor.check(TokenType::RightParen)){
            for(;;){
                const Token& tok = m_cursor.current();
                if(tok.type != TokenType::StringLiteral && tok.type != TokenType::Identifier && tok.type != TokenType::IntegerLiteral){
                    m_cursor.errorExpected("expected attribute argument (string, identifier, or integer)");
                    return false;
                }
                attr.arguments.push_back(ParsedAttributeArg{tok.text, tok.type, tok.line, tok.column});
                m_cursor.advance();

                if(m_cursor.check(TokenType::RightParen))
                    break;
                if(!m_cursor.expect(TokenType::Comma, "expected ',' or ')' in attribute arguments"))
                    return false;
            }
        }

        if(!m_cursor.expect(TokenType::RightParen, "expected ')' after attribute arguments"))
            return false;
    }

    outAttributes.push_back(Move(attr));
    return true;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_METASCRIPT_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

