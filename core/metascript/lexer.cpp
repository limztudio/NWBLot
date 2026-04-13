// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "lexer.h"


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_METASCRIPT_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace __hidden_metascript_lexer{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


[[nodiscard]] inline bool isDigit(MChar c){ return c >= '0' && c <= '9'; }
[[nodiscard]] inline bool isAlpha(MChar c){ return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || c == '_'; }
[[nodiscard]] inline bool isAlphaNumeric(MChar c){ return isAlpha(c) || isDigit(c); }


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


Lexer::Lexer(MStringView source)
    : m_source(source)
{}


Token Lexer::next(){
    using namespace __hidden_metascript_lexer;

    skipWhitespaceAndComments();

    if(m_hasPendingError){
        Token tok;
        tok.type = TokenType::Error;
        tok.text = m_errorMessage;
        tok.line = m_errorLine;
        tok.column = m_errorColumn;

        m_hasPendingError = false;
        return tok;
    }

    if(isAtEnd())
        return makeToken(TokenType::EndOfFile, 0);

    const MChar c = peek();

    if(isAlpha(c))
        return readIdentifier();

    if(isDigit(c))
        return readNumber();

    if(c == '"')
        return readString();

    switch(c){
    case '+':
        if(peekNext() == '=')
            return makeToken(TokenType::PlusEqual, 2);
        return makeToken(TokenType::Plus, 1);
    case '-':
        if(peekNext() == '=')
            return makeToken(TokenType::MinusEqual, 2);
        return makeToken(TokenType::Minus, 1);
    case '*':
        if(peekNext() == '=')
            return makeToken(TokenType::StarEqual, 2);
        return makeToken(TokenType::Star, 1);
    case '/':
        if(peekNext() == '=')
            return makeToken(TokenType::SlashEqual, 2);
        return makeToken(TokenType::Slash, 1);
    case '=':
        return makeToken(TokenType::Equal, 1);
    case ';':
        return makeToken(TokenType::Semicolon, 1);
    case '.':
        return makeToken(TokenType::Dot, 1);
    case ',':
        return makeToken(TokenType::Comma, 1);
    case ':':
        return makeToken(TokenType::Colon, 1);
    case '[':
        return makeToken(TokenType::LeftBracket, 1);
    case ']':
        return makeToken(TokenType::RightBracket, 1);
    case '{':
        return makeToken(TokenType::LeftBrace, 1);
    case '}':
        return makeToken(TokenType::RightBrace, 1);
    case '(':
        return makeToken(TokenType::LeftParen, 1);
    case ')':
        return makeToken(TokenType::RightParen, 1);
    default:
        break;
    }

    {
        Token tok = makeErrorToken("unexpected character");
        advance();
        return tok;
    }
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


void Lexer::skipWhitespaceAndComments(){
    for(;;){
        if(isAtEnd())
            return;

        const MChar c = peek();
        if(c == ' ' || c == '\t' || c == '\r'){
            advance();
            continue;
        }
        if(c == '\n'){
            advance();
            continue;
        }

        if(c == '/' && peekNext() == '/'){
            while(!isAtEnd() && peek() != '\n')
                advance();
            continue;
        }

        if(c == '/' && peekNext() == '*'){
            const u32 commentLine = m_line;
            const u32 commentColumn = m_column;
            advance();
            advance();

            bool terminated = false;
            while(!isAtEnd()){
                if(peek() == '*' && peekNext() == '/'){
                    advance();
                    advance();
                    terminated = true;
                    break;
                }
                advance();
            }

            if(!terminated){
                m_hasPendingError = true;
                m_errorLine = commentLine;
                m_errorColumn = commentColumn;
                m_errorMessage = MStringView("unterminated block comment");
                return;
            }

            continue;
        }

        return;
    }
}

Token Lexer::readIdentifier(){
    using namespace __hidden_metascript_lexer;

    const usize start = m_current;
    const u32 startLine = m_line;
    const u32 startColumn = m_column;

    while(!isAtEnd() && isAlphaNumeric(peek()))
        advance();

    Token tok;
    tok.type = TokenType::Identifier;
    tok.text = m_source.substr(start, m_current - start);
    tok.line = startLine;
    tok.column = startColumn;
    return tok;
}

Token Lexer::readNumber(){
    using namespace __hidden_metascript_lexer;

    const usize start = m_current;
    const u32 startLine = m_line;
    const u32 startColumn = m_column;
    bool isDouble = false;

    while(!isAtEnd() && isDigit(peek()))
        advance();

    if(!isAtEnd() && peek() == '.'){
        const MChar afterDot = peekNext();
        if(afterDot >= '0' && afterDot <= '9'){
            isDouble = true;
            advance();
            while(!isAtEnd() && isDigit(peek()))
                advance();
        }
    }

    Token tok;
    tok.type = isDouble ? TokenType::DoubleLiteral : TokenType::IntegerLiteral;
    tok.text = m_source.substr(start, m_current - start);
    tok.line = startLine;
    tok.column = startColumn;
    return tok;
}

Token Lexer::readString(){
    const u32 startLine = m_line;
    const u32 startColumn = m_column;

    advance();

    const usize contentStart = m_current;

    while(!isAtEnd() && peek() != '"'){
        if(peek() == '\n' || peek() == '\r'){
            return makeErrorToken(MStringView("newline in string literal"), startLine, startColumn);
        }

        if(peek() == '\\'){
            advance();

            if(isAtEnd()){
                return makeErrorToken(MStringView("unterminated string literal"), startLine, startColumn);
            }
            if(peek() == '\n' || peek() == '\r'){
                return makeErrorToken(MStringView("newline in string literal"), startLine, startColumn);
            }
        }

        advance();
    }

    const usize contentEnd = m_current;

    if(isAtEnd()){
        Token tok;
        tok.type = TokenType::Error;
        tok.text = MStringView("unterminated string literal");
        tok.line = startLine;
        tok.column = startColumn;
        return tok;
    }

    advance();

    Token tok;
    tok.type = TokenType::StringLiteral;
    tok.text = m_source.substr(contentStart, contentEnd - contentStart);
    tok.line = startLine;
    tok.column = startColumn;
    return tok;
}

Token Lexer::makeToken(TokenType::Enum type, usize length){
    Token tok;
    tok.type = type;
    tok.text = m_source.substr(m_current, length);
    tok.line = m_line;
    tok.column = m_column;

    for(usize i = 0; i < length; ++i)
        advance();

    return tok;
}

Token Lexer::makeErrorToken(MStringView message){
    return makeErrorToken(message, m_line, m_column);
}

Token Lexer::makeErrorToken(MStringView message, const u32 line, const u32 column){
    Token tok;
    tok.type = TokenType::Error;
    tok.text = message;
    tok.line = line;
    tok.column = column;
    return tok;
}

MChar Lexer::peek()const{
    if(isAtEnd())
        return '\0';
    return m_source[m_current];
}

MChar Lexer::peekNext()const{
    if(m_current + 1 >= m_source.size())
        return '\0';
    return m_source[m_current + 1];
}

MChar Lexer::advance(){
    if(isAtEnd())
        return '\0';

    const MChar c = m_source[m_current];
    ++m_current;

    if(c == '\n'){
        ++m_line;
        m_column = 1;
    }
    else
        ++m_column;

    return c;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_METASCRIPT_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

