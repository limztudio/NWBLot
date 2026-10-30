// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "lexer.h"


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_METASCRIPT_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace __hidden_metascript_lexer{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


inline constexpr MStringView s_NewlineInStringMessage = MStringView("newline in string literal");
inline constexpr MStringView s_UnterminatedStringMessage = MStringView("unterminated string literal");
inline constexpr MStringView s_EmptyCharMessage = MStringView("empty character literal");
inline constexpr MStringView s_NewlineInCharMessage = MStringView("newline in character literal");
inline constexpr MStringView s_UnterminatedCharMessage = MStringView("unterminated character literal");
constexpr usize s_CompoundAssignmentTokenLength = 2u;
constexpr usize s_ThreeCharTokenLength = 3u;


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


Lexer::Lexer(MStringView source)noexcept
    : m_source(source)
{}


Token Lexer::next()noexcept{
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

    if(IsAsciiIdentifierStart(c))
        return readIdentifier();

    if(IsAsciiDigit(c))
        return readNumber();

    if(c == '"')
        return readString();

    if(c == '\'')
        return readCharLiteral();

    switch(c){
    case '+':
        if(peekNext() == '=')
            return makeToken(TokenType::PlusEqual, s_CompoundAssignmentTokenLength);
        if(peekNext() == '+')
            return makeToken(TokenType::PlusPlus, s_CompoundAssignmentTokenLength);
        return makeToken(TokenType::Plus, 1);
    case '-':
        if(peekNext() == '=')
            return makeToken(TokenType::MinusEqual, s_CompoundAssignmentTokenLength);
        if(peekNext() == '-')
            return makeToken(TokenType::MinusMinus, s_CompoundAssignmentTokenLength);
        if(peekNext() == '>')
            return makeToken(TokenType::Arrow, s_CompoundAssignmentTokenLength);
        return makeToken(TokenType::Minus, 1);
    case '*':
        if(peekNext() == '=')
            return makeToken(TokenType::StarEqual, s_CompoundAssignmentTokenLength);
        return makeToken(TokenType::Star, 1);
    case '/':
        if(peekNext() == '=')
            return makeToken(TokenType::SlashEqual, s_CompoundAssignmentTokenLength);
        return makeToken(TokenType::Slash, 1);
    case '%':
        if(peekNext() == '=')
            return makeToken(TokenType::PercentEqual, s_CompoundAssignmentTokenLength);
        return makeToken(TokenType::Percent, 1);
    case '=':
        if(peekNext() == '=')
            return makeToken(TokenType::EqualEqual, s_CompoundAssignmentTokenLength);
        return makeToken(TokenType::Equal, 1);
    case '!':
        if(peekNext() == '=')
            return makeToken(TokenType::BangEqual, s_CompoundAssignmentTokenLength);
        return makeToken(TokenType::Bang, 1);
    case '<':
        if(peekNext() == '<'){
            if(peekAt(2u) == '=')
                return makeToken(TokenType::LessLessEqual, s_ThreeCharTokenLength);
            return makeToken(TokenType::LessLess, s_CompoundAssignmentTokenLength);
        }
        if(peekNext() == '=')
            return makeToken(TokenType::LessEqual, s_CompoundAssignmentTokenLength);
        return makeToken(TokenType::Less, 1);
    case '>':
        if(peekNext() == '>'){
            if(peekAt(2u) == '=')
                return makeToken(TokenType::GreaterGreaterEqual, s_ThreeCharTokenLength);
            return makeToken(TokenType::GreaterGreater, s_CompoundAssignmentTokenLength);
        }
        if(peekNext() == '=')
            return makeToken(TokenType::GreaterEqual, s_CompoundAssignmentTokenLength);
        return makeToken(TokenType::Greater, 1);
    case '&':
        if(peekNext() == '&')
            return makeToken(TokenType::AmpersandAmpersand, s_CompoundAssignmentTokenLength);
        if(peekNext() == '=')
            return makeToken(TokenType::AmpersandEqual, s_CompoundAssignmentTokenLength);
        return makeToken(TokenType::Ampersand, 1);
    case '|':
        if(peekNext() == '|')
            return makeToken(TokenType::PipePipe, s_CompoundAssignmentTokenLength);
        if(peekNext() == '=')
            return makeToken(TokenType::PipeEqual, s_CompoundAssignmentTokenLength);
        return makeToken(TokenType::Pipe, 1);
    case '^':
        if(peekNext() == '=')
            return makeToken(TokenType::CaretEqual, s_CompoundAssignmentTokenLength);
        return makeToken(TokenType::Caret, 1);
    case '~':
        return makeToken(TokenType::Tilde, 1);
    case '?':
        return makeToken(TokenType::Question, 1);
    case '#':
        return makeToken(TokenType::Hash, 1);
    case ';':
        return makeToken(TokenType::Semicolon, 1);
    case '.':
        return makeToken(TokenType::Dot, 1);
    case ',':
        return makeToken(TokenType::Comma, 1);
    case ':':
        if(peekNext() == ':')
            return makeToken(TokenType::ColonColon, s_CompoundAssignmentTokenLength);
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

b8 Lexer::consumeBalancedBracesRaw(MStringView& outInnerCode)noexcept{
    const usize startOffset = m_current;
    u32 braceDepth = 1u;

    while(!isAtEnd()){
        const MChar c = peek();
        if(c == '/' && peekNext() == '/'){
            while(!isAtEnd() && peek() != '\n')
                advance();
            continue;
        }
        if(c == '/' && peekNext() == '*'){
            advance();
            advance();
            while(!isAtEnd()){
                if(peek() == '*' && peekNext() == '/'){
                    advance();
                    advance();
                    break;
                }
                advance();
            }
            continue;
        }
        if(c == '\'' && m_current > startOffset && IsHexDigit(m_source[m_current - 1u])){
            advance();
            continue;
        }
        if(c == '"' && m_current > startOffset && m_source[m_current - 1u] == 'R'){
            advance();
            const usize delimStart = m_current;
            while(!isAtEnd() && peek() != '(' && peek() != '"' && peek() != '\n')
                advance();
            if(!isAtEnd() && peek() == '('){
                const MStringView delim = m_source.substr(delimStart, m_current - delimStart);
                advance();
                while(!isAtEnd()){
                    if(peek() == ')' && m_current + 1u + delim.size() < m_source.size()
                        && m_source.substr(m_current + 1u, delim.size()) == delim
                        && m_source[m_current + 1u + delim.size()] == '"')
                    {
                        for(usize skip = 0u; skip < delim.size() + 2u; ++skip)
                            advance();
                        break;
                    }
                    advance();
                }
                continue;
            }
        }
        if(c == '"' || c == '\''){
            const MChar quote = advance();
            while(!isAtEnd() && peek() != quote){
                if(peek() == '\\'){
                    advance();
                    if(isAtEnd())
                        break;
                }
                advance();
            }
            if(!isAtEnd())
                advance();
            continue;
        }
        if(c == '{'){
            ++braceDepth;
            advance();
            continue;
        }
        if(c == '}'){
            --braceDepth;
            if(braceDepth == 0u){
                const usize endOffset = m_current;
                advance();
                outInnerCode = m_source.substr(startOffset, endOffset - startOffset);
                return true;
            }
            advance();
            continue;
        }
        advance();
    }

    return false;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


void Lexer::skipWhitespaceAndComments()noexcept{
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
            const usize commentStart = m_current;
            const u32 commentLine = m_line;
            const u32 commentColumn = m_column;
            advance();
            advance();

            b8 terminated = false;
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

            const MStringView commentText = m_source.substr(commentStart, m_current - commentStart);
            if(commentText == s_IncludePushMarker){
                if(m_savedLineDepth < 64u)
                    m_savedLines[m_savedLineDepth++] = m_line;
                m_line = 1u;
                m_column = 1u;
            }
            else if(commentText == s_IncludePopMarker){
                if(m_savedLineDepth > 0u)
                    m_line = m_savedLines[--m_savedLineDepth];
            }

            continue;
        }

        return;
    }
}

Token Lexer::readIdentifier()noexcept{
    using namespace __hidden_metascript_lexer;

    const usize start = m_current;
    const u32 startLine = m_line;
    const u32 startColumn = m_column;

    while(!isAtEnd() && IsAsciiIdentifierChar(peek()))
        advance();

    Token tok;
    tok.type = TokenType::Identifier;
    tok.text = m_source.substr(start, m_current - start);
    tok.line = startLine;
    tok.column = startColumn;
    return tok;
}

Token Lexer::readNumber()noexcept{
    using namespace __hidden_metascript_lexer;

    const usize start = m_current;
    const u32 startLine = m_line;
    const u32 startColumn = m_column;
    b8 isDouble = false;
    b8 isFloat = false;

    const auto finishRadixInteger = [&](auto isDigitFn, const MStringView invalidLitMsg, const MStringView invalidSuffixMsg)noexcept -> Token{
        advance();
        advance();
        if(isAtEnd() || !isDigitFn(peek()))
            return makeErrorToken(invalidLitMsg, startLine, startColumn);
        while(!isAtEnd() && isDigitFn(peek()))
            advance();
        while(!isAtEnd() && (peek() == 'u' || peek() == 'U' || peek() == 'l' || peek() == 'L'))
            advance();
        if(!isAtEnd() && (IsAsciiIdentifierChar(peek()) || peek() == '.')){
            while(!isAtEnd() && (IsAsciiIdentifierChar(peek()) || peek() == '.'))
                advance();
            return makeErrorToken(invalidSuffixMsg, startLine, startColumn);
        }
        Token tok;
        tok.type = TokenType::IntegerLiteral;
        tok.text = m_source.substr(start, m_current - start);
        tok.line = startLine;
        tok.column = startColumn;
        return tok;
    };

    if(peek() == '0' && (peekNext() == 'x' || peekNext() == 'X')){
        return finishRadixInteger(
            IsHexDigit,
            MStringView("invalid hexadecimal integer literal"),
            MStringView("invalid suffix on hexadecimal integer literal")
        );
    }

    if(peek() == '0' && (peekNext() == 'b' || peekNext() == 'B')){
        return finishRadixInteger(
            IsBinaryDigit,
            MStringView("invalid binary integer literal"),
            MStringView("invalid suffix on binary integer literal")
        );
    }

    while(!isAtEnd() && IsAsciiDigit(peek()))
        advance();

    if(!isAtEnd() && peek() == '.'){
        const MChar afterDot = peekNext();
        if(IsAsciiDigit(afterDot)){
            isDouble = true;
            advance();
            while(!isAtEnd() && IsAsciiDigit(peek()))
                advance();
        }
    }

    if(!isAtEnd() && (peek() == 'e' || peek() == 'E')){
        isDouble = true;
        advance();
        if(!isAtEnd() && (peek() == '+' || peek() == '-'))
            advance();
        if(isAtEnd() || !IsAsciiDigit(peek()))
            return makeErrorToken(MStringView("invalid exponent in number literal"), startLine, startColumn);
        while(!isAtEnd() && IsAsciiDigit(peek()))
            advance();
    }

    if(isDouble && !isAtEnd() && (peek() == 'f' || peek() == 'F')){
        isFloat = true;
        advance();
    }
    else if(!isDouble){
        while(!isAtEnd() && (peek() == 'u' || peek() == 'U' || peek() == 'l' || peek() == 'L'))
            advance();
    }

    if(!isAtEnd() && (IsAsciiIdentifierChar(peek()) || (isDouble && peek() == '.' && IsAsciiDigit(peekNext())))){
        while(!isAtEnd() && (IsAsciiIdentifierChar(peek()) || peek() == '.'))
            advance();
        return makeErrorToken(MStringView("invalid suffix on numeric literal"), startLine, startColumn);
    }

    Token tok;
    tok.type = isFloat ? TokenType::FloatLiteral : (isDouble ? TokenType::DoubleLiteral : TokenType::IntegerLiteral);
    tok.text = m_source.substr(start, m_current - start);
    tok.line = startLine;
    tok.column = startColumn;
    return tok;
}

Token Lexer::readString()noexcept{
    const u32 startLine = m_line;
    const u32 startColumn = m_column;

    advance();

    const usize contentStart = m_current;

    while(!isAtEnd() && peek() != '"'){
        if(peek() == '\n' || peek() == '\r')
            return makeErrorToken(__hidden_metascript_lexer::s_NewlineInStringMessage, startLine, startColumn);

        if(peek() == '\\'){
            advance();

            if(isAtEnd())
                return makeErrorToken(__hidden_metascript_lexer::s_UnterminatedStringMessage, startLine, startColumn);
            if(peek() == '\n' || peek() == '\r')
                return makeErrorToken(__hidden_metascript_lexer::s_NewlineInStringMessage, startLine, startColumn);
        }

        advance();
    }

    const usize contentEnd = m_current;

    if(isAtEnd())
        return makeErrorToken(__hidden_metascript_lexer::s_UnterminatedStringMessage, startLine, startColumn);

    advance();

    Token tok;
    tok.type = TokenType::StringLiteral;
    tok.text = m_source.substr(contentStart, contentEnd - contentStart);
    tok.line = startLine;
    tok.column = startColumn;
    return tok;
}

Token Lexer::readCharLiteral()noexcept{
    using namespace __hidden_metascript_lexer;

    const usize start = m_current;
    const u32 startLine = m_line;
    const u32 startColumn = m_column;

    advance();

    if(isAtEnd())
        return makeErrorToken(s_UnterminatedCharMessage, startLine, startColumn);
    if(peek() == '\'')
        return makeErrorToken(s_EmptyCharMessage, startLine, startColumn);
    if(peek() == '\n' || peek() == '\r')
        return makeErrorToken(s_NewlineInCharMessage, startLine, startColumn);

    if(peek() == '\\'){
        advance();
        if(isAtEnd())
            return makeErrorToken(s_UnterminatedCharMessage, startLine, startColumn);
        if(peek() == '\n' || peek() == '\r')
            return makeErrorToken(s_NewlineInCharMessage, startLine, startColumn);
        const MChar esc = advance();
        if(esc == 'x' || esc == 'X'){
            if(isAtEnd() || !IsHexDigit(peek()))
                return makeErrorToken(s_UnterminatedCharMessage, startLine, startColumn);
            while(!isAtEnd() && IsHexDigit(peek()))
                advance();
        }
        else if(esc == 'u' || esc == 'U'){
            const usize expectedHex = (esc == 'u') ? 4u : 8u;
            u32 codePoint = 0u;
            for(usize h = 0u; h < expectedHex; ++h){
                if(isAtEnd() || !IsHexDigit(peek()))
                    return makeErrorToken(s_UnterminatedCharMessage, startLine, startColumn);
                const MChar hc = advance();
                codePoint = (codePoint << 4u) | HexDigitValue(hc);
            }
            if(codePoint > 0x10FFFFu || (codePoint >= 0xD800u && codePoint <= 0xDFFFu))
                return makeErrorToken(MStringView("invalid unicode code point in character literal"), startLine, startColumn);
        }
        else if(esc >= '0' && esc <= '7'){
            for(usize oct = 0u; oct < 2u; ++oct){
                if(!isAtEnd() && peek() >= '0' && peek() <= '7')
                    advance();
                else
                    break;
            }
        }
    }
    else{
        advance();
    }

    if(isAtEnd() || peek() != '\'')
        return makeErrorToken(s_UnterminatedCharMessage, startLine, startColumn);

    advance();

    Token tok;
    tok.type = TokenType::IntegerLiteral;
    tok.text = m_source.substr(start, m_current - start);
    tok.line = startLine;
    tok.column = startColumn;
    return tok;
}

Token Lexer::makeToken(TokenType::Enum type, usize length)noexcept{
    Token tok;
    tok.type = type;
    tok.text = m_source.substr(m_current, length);
    tok.line = m_line;
    tok.column = m_column;

    for(usize i = 0u; i < length; ++i)
        advance();

    return tok;
}

Token Lexer::makeErrorToken(MStringView message)noexcept{
    return makeErrorToken(message, m_line, m_column);
}

Token Lexer::makeErrorToken(MStringView message, const u32 line, const u32 column)noexcept{
    Token tok;
    tok.type = TokenType::Error;
    tok.text = message;
    tok.line = line;
    tok.column = column;
    return tok;
}

MChar Lexer::peek()const noexcept{
    return peekAt(0u);
}

MChar Lexer::peekNext()const noexcept{
    return peekAt(1u);
}

MChar Lexer::peekAt(const usize offset)const noexcept{
    if(m_current + offset >= m_source.size())
        return '\0';
    return m_source[m_current + offset];
}

MChar Lexer::advance()noexcept{
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

