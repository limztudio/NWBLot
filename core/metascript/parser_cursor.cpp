// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "parser_cursor.h"


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_METASCRIPT_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace __hidden_metascript_parser_cursor{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


constexpr AStringView s_ExpectedTokenFoundSeparator = ", found ";
constexpr usize s_ExpectedTokenFoundSeparatorLength = s_ExpectedTokenFoundSeparator.size();


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


ParserCursor::ParserCursor(MStringView source, MetaArena& arena, Alloc::ScratchArena& scratchArena, MVector<ParseError>& errors)
    : m_lexer(source)
    , m_arena(arena)
    , m_scratchArena(scratchArena)
    , m_errors(errors)
{
    advance();
}


void ParserCursor::advance(){
    m_current = m_lexer.next();

    if(m_current.type == TokenType::Error)
        error(m_current.text);
}

bool ParserCursor::expect(TokenType::Enum type, MStringView expected){
    if(m_current.type == type){
        advance();
        return true;
    }
    errorExpected(expected);
    return false;
}

void ParserCursor::error(MStringView message){
    error(m_current.line, m_current.column, message);
}

void ParserCursor::error(u32 line, u32 column, MStringView message){
    m_errors.push_back(ParseError{line, column, MString(message.data(), message.size(), m_arena)});
}

void ParserCursor::errorExpected(MStringView expected){
    using namespace __hidden_metascript_parser_cursor;

    const auto desc = tokenDescription();
    ScratchString msg{m_scratchArena};
    if(expected.size() <= Limit<usize>::s_Max - s_ExpectedTokenFoundSeparatorLength && desc.size() <= Limit<usize>::s_Max - expected.size() - s_ExpectedTokenFoundSeparatorLength)
        msg.reserve(expected.size() + s_ExpectedTokenFoundSeparatorLength + desc.size());
    msg.append(expected.data(), expected.size());
    msg.append(s_ExpectedTokenFoundSeparator);
    msg.append(desc.data(), desc.size());
    error(m_current.line, m_current.column, MStringView(msg.data(), msg.size()));
}


ParserCursor::ScratchString ParserCursor::tokenDescription(){
    switch(m_current.type){
    case TokenType::Identifier:{
        ScratchString d("identifier '", m_scratchArena);
        if(m_current.text.size() <= Limit<usize>::s_Max - d.size() - 1u)
            d.reserve(d.size() + m_current.text.size() + 1u);
        d.append(m_current.text.data(), m_current.text.size());
        d.push_back('\'');
        return d;
    }
    case TokenType::IntegerLiteral:
    case TokenType::DoubleLiteral:{
        ScratchString d("number '", m_scratchArena);
        if(m_current.text.size() <= Limit<usize>::s_Max - d.size() - 1u)
            d.reserve(d.size() + m_current.text.size() + 1u);
        d.append(m_current.text.data(), m_current.text.size());
        d.push_back('\'');
        return d;
    }
    case TokenType::StringLiteral: return ScratchString("string literal", m_scratchArena);
    case TokenType::Plus: return ScratchString("'+'", m_scratchArena);
    case TokenType::Minus: return ScratchString("'-'", m_scratchArena);
    case TokenType::Star: return ScratchString("'*'", m_scratchArena);
    case TokenType::Slash: return ScratchString("'/'", m_scratchArena);
    case TokenType::PlusEqual: return ScratchString("'+='", m_scratchArena);
    case TokenType::MinusEqual: return ScratchString("'-='", m_scratchArena);
    case TokenType::StarEqual: return ScratchString("'*='", m_scratchArena);
    case TokenType::SlashEqual: return ScratchString("'/='", m_scratchArena);
    case TokenType::Equal: return ScratchString("'='", m_scratchArena);
    case TokenType::Semicolon: return ScratchString("';'", m_scratchArena);
    case TokenType::Dot: return ScratchString("'.'", m_scratchArena);
    case TokenType::Comma: return ScratchString("','", m_scratchArena);
    case TokenType::Colon: return ScratchString("':'", m_scratchArena);
    case TokenType::LeftBracket: return ScratchString("'['", m_scratchArena);
    case TokenType::RightBracket: return ScratchString("']'", m_scratchArena);
    case TokenType::LeftBrace: return ScratchString("'{'", m_scratchArena);
    case TokenType::RightBrace: return ScratchString("'}'", m_scratchArena);
    case TokenType::LeftParen: return ScratchString("'('", m_scratchArena);
    case TokenType::RightParen: return ScratchString("')'", m_scratchArena);
    case TokenType::EndOfFile: return ScratchString("end of file", m_scratchArena);
    case TokenType::Error: return ScratchString("error", m_scratchArena);
    default: return ScratchString("unknown token", m_scratchArena);
    }
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_METASCRIPT_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

