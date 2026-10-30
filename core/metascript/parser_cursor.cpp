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


struct TokenDescription{
    MStringView prefix;
    MStringView text;
    MStringView suffix;
};


[[nodiscard]] TokenDescription DescribeToken(const Token& token)noexcept{
    switch(token.type){
    case TokenType::Identifier: return { "identifier '", token.text, "'" };
    case TokenType::IntegerLiteral:
    case TokenType::DoubleLiteral:
    case TokenType::FloatLiteral: return { "number '", token.text, "'" };
    case TokenType::StringLiteral: return { "string literal", {}, {} };
    case TokenType::Plus: return { "'+'", {}, {} };
    case TokenType::Minus: return { "'-'", {}, {} };
    case TokenType::Star: return { "'*'", {}, {} };
    case TokenType::Slash: return { "'/'", {}, {} };
    case TokenType::Percent: return { "'%'", {}, {} };
    case TokenType::PlusEqual: return { "'+='", {}, {} };
    case TokenType::MinusEqual: return { "'-='", {}, {} };
    case TokenType::StarEqual: return { "'*='", {}, {} };
    case TokenType::SlashEqual: return { "'/='", {}, {} };
    case TokenType::PercentEqual: return { "'%='", {}, {} };
    case TokenType::PlusPlus: return { "'++'", {}, {} };
    case TokenType::MinusMinus: return { "'--'", {}, {} };
    case TokenType::Arrow: return { "'->'", {}, {} };
    case TokenType::Equal: return { "'='", {}, {} };
    case TokenType::EqualEqual: return { "'=='", {}, {} };
    case TokenType::BangEqual: return { "'!='", {}, {} };
    case TokenType::Less: return { "'<'", {}, {} };
    case TokenType::LessEqual: return { "'<='", {}, {} };
    case TokenType::Greater: return { "'>'", {}, {} };
    case TokenType::GreaterEqual: return { "'>='", {}, {} };
    case TokenType::Ampersand: return { "'&'", {}, {} };
    case TokenType::AmpersandEqual: return { "'&='", {}, {} };
    case TokenType::AmpersandAmpersand: return { "'&&'", {}, {} };
    case TokenType::Pipe: return { "'|'", {}, {} };
    case TokenType::PipeEqual: return { "'|='", {}, {} };
    case TokenType::PipePipe: return { "'||'", {}, {} };
    case TokenType::Caret: return { "'^'", {}, {} };
    case TokenType::CaretEqual: return { "'^='", {}, {} };
    case TokenType::Tilde: return { "'~'", {}, {} };
    case TokenType::Bang: return { "'!'", {}, {} };
    case TokenType::Question: return { "'?'", {}, {} };
    case TokenType::LessLess: return { "'<<'", {}, {} };
    case TokenType::LessLessEqual: return { "'<<='", {}, {} };
    case TokenType::GreaterGreater: return { "'>>'", {}, {} };
    case TokenType::GreaterGreaterEqual: return { "'>>='", {}, {} };
    case TokenType::Semicolon: return { "';'", {}, {} };
    case TokenType::Dot: return { "'.'", {}, {} };
    case TokenType::Comma: return { "','", {}, {} };
    case TokenType::Colon: return { "':'", {}, {} };
    case TokenType::ColonColon: return { "'::'", {}, {} };
    case TokenType::Hash: return { "'#'", {}, {} };
    case TokenType::LeftBracket: return { "'['", {}, {} };
    case TokenType::RightBracket: return { "']'", {}, {} };
    case TokenType::LeftBrace: return { "'{'", {}, {} };
    case TokenType::RightBrace: return { "'}'", {}, {} };
    case TokenType::LeftParen: return { "'('", {}, {} };
    case TokenType::RightParen: return { "')'", {}, {} };
    case TokenType::EndOfFile: return { "end of file", {}, {} };
    case TokenType::Error: return { "error", {}, {} };
    default: return { "unknown token", {}, {} };
    }
}


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


b8 ParserCursor::match(const TokenType::Enum type){
    if(m_current.type != type)
        return false;
    advance();
    return true;
}

b8 ParserCursor::matchIdentifier(const MStringView text){
    if(m_current.type != TokenType::Identifier || m_current.text != text)
        return false;
    advance();
    return true;
}

void ParserCursor::advance(){
    m_current = m_lexer.next();

    if(m_current.type == TokenType::Error)
        error(m_current.text);
}

b8 ParserCursor::expect(TokenType::Enum type, MStringView expected){
    if(m_current.type == type){
        advance();
        return true;
    }
    errorExpected(expected);
    return false;
}

b8 ParserCursor::consumeBalancedBracesRaw(MStringView& outInnerCode){
    if(m_current.type != TokenType::LeftBrace){
        errorExpected("expected '{' to start native block");
        return false;
    }
    if(!m_lexer.consumeBalancedBracesRaw(outInnerCode)){
        error("unterminated 'native' block; expected '}'");
        return false;
    }
    advance();
    return true;
}

void ParserCursor::error(u32 line, u32 column, MStringView message){
    m_errors.push_back(ParseError{line, column, MString(message.data(), message.size(), m_arena)});
}

void ParserCursor::errorExpected(MStringView expected){
    using namespace __hidden_metascript_parser_cursor;

    const TokenDescription found = DescribeToken(m_current);
    const usize foundSize = found.prefix.size() + found.text.size() + found.suffix.size();
    MString message(m_arena);
    if(foundSize >= found.text.size() && expected.size() <= Limit<usize>::s_Max - s_ExpectedTokenFoundSeparatorLength && foundSize <= Limit<usize>::s_Max - expected.size() - s_ExpectedTokenFoundSeparatorLength)
        message.reserve(expected.size() + s_ExpectedTokenFoundSeparatorLength + foundSize);
    message.append(expected.data(), expected.size());
    message.append(s_ExpectedTokenFoundSeparator);
    message.append(found.prefix.data(), found.prefix.size());
    message.append(found.text.data(), found.text.size());
    message.append(found.suffix.data(), found.suffix.size());
    m_errors.push_back(ParseError{m_current.line, m_current.column, Move(message)});
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_METASCRIPT_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

