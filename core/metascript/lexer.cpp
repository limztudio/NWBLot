// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "lexer.h"
#include "literal_decoder.h"


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
                if(m_savedLineDepth < s_MaxSavedIncludeDepth)
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
    const usize start = m_current;
    const u32 startLine = m_line;
    const u32 startColumn = m_column;
    b8 isDouble = false;
    b8 isFloat = false;

    const auto isDecimalDigit = [](const MChar ch)noexcept -> b8{
        return IsAsciiDigit(ch);
    };
    const auto consumeSeparatedDigits = [&](auto isDigitFn)noexcept{
        while(!isAtEnd() && (isDigitFn(peek()) || (peek() == '\'' && isDigitFn(peekNext()))))
            advance();
    };
    const auto consumeInvalidSuffix = [&](const MStringView msg)noexcept -> Token{
        while(!isAtEnd() && (IsAsciiIdentifierChar(peek()) || peek() == '.'))
            advance();
        return makeErrorToken(msg, startLine, startColumn);
    };
    const auto makeNumberToken = [&](const TokenType::Enum type)noexcept -> Token{
        Token tok;
        tok.type = type;
        tok.text = m_source.substr(start, m_current - start);
        tok.line = startLine;
        tok.column = startColumn;
        return tok;
    };

    const auto finishRadixInteger = [&](auto isDigitFn, const MStringView invalidLitMsg, const MStringView invalidSuffixMsg)noexcept -> Token{
        advance();
        advance();
        if(isAtEnd() || !isDigitFn(peek()))
            return makeErrorToken(invalidLitMsg, startLine, startColumn);
        consumeSeparatedDigits(isDigitFn);
        if(!isAtEnd() && peek() == '\''){
            advance();
            return makeErrorToken(invalidLitMsg, startLine, startColumn);
        }
        while(!isAtEnd() && LiteralDecoder::IsIntegerLiteralSuffixChar(peek()))
            advance();
        if(!isAtEnd() && (IsAsciiIdentifierChar(peek()) || peek() == '.'))
            return consumeInvalidSuffix(invalidSuffixMsg);
        return makeNumberToken(TokenType::IntegerLiteral);
    };

    const auto consumeExponentAndTrailingSeparator = [&](Optional<Token>& outError)noexcept -> b8 {
        isDouble = true;
        advance();
        if(!isAtEnd() && (peek() == '+' || peek() == '-'))
            advance();
        if(isAtEnd() || !IsAsciiDigit(peek())){
            outError = makeErrorToken(MStringView("invalid exponent in number literal"), startLine, startColumn);
            return false;
        }
        consumeSeparatedDigits(isDecimalDigit);
        if(!isAtEnd() && peek() == '\''){
            advance();
            outError = makeErrorToken(MStringView("invalid digit separator in numeric literal"), startLine, startColumn);
            return false;
        }
        return true;
    };

    if(peek() == '0' && (peekNext() == 'x' || peekNext() == 'X')){
        advance();
        advance();
        b8 hasHexDigits = false;
        if(!isAtEnd() && IsHexDigit(peek())){
            hasHexDigits = true;
            consumeSeparatedDigits(IsHexDigit);
        }
        if(!isAtEnd() && peek() == '\''){
            advance();
            return makeErrorToken(MStringView("invalid hexadecimal integer literal"), startLine, startColumn);
        }
        if(!isAtEnd() && peek() == '.' && (hasHexDigits || IsHexDigit(peekNext()))){
            isDouble = true;
            advance();
            if(!isAtEnd() && IsHexDigit(peek())){
                hasHexDigits = true;
                consumeSeparatedDigits(IsHexDigit);
            }
            if(!isAtEnd() && peek() == '\''){
                advance();
                return makeErrorToken(MStringView("invalid suffix on hexadecimal integer literal"), startLine, startColumn);
            }
        }
        if(!hasHexDigits)
            return makeErrorToken(MStringView("invalid hexadecimal integer literal"), startLine, startColumn);
        if(!isAtEnd() && (peek() == 'p' || peek() == 'P')){
            Optional<Token> errorToken;
            if(!consumeExponentAndTrailingSeparator(errorToken))
                return *errorToken;
            if(!isAtEnd() && (peek() == 'f' || peek() == 'F')){
                isFloat = true;
                advance();
            }
            if(!isAtEnd() && (IsAsciiIdentifierChar(peek()) || peek() == '.'))
                return consumeInvalidSuffix(MStringView("invalid suffix on hexadecimal integer literal"));
            return makeNumberToken(isFloat ? TokenType::FloatLiteral : TokenType::DoubleLiteral);
        }
        if(isDouble)
            return consumeInvalidSuffix(MStringView("invalid suffix on hexadecimal integer literal"));
        while(!isAtEnd() && LiteralDecoder::IsIntegerLiteralSuffixChar(peek()))
            advance();
        if(!isAtEnd() && (IsAsciiIdentifierChar(peek()) || peek() == '.'))
            return consumeInvalidSuffix(MStringView("invalid suffix on hexadecimal integer literal"));
        return makeNumberToken(TokenType::IntegerLiteral);
    }

    if(peek() == '0' && (peekNext() == 'b' || peekNext() == 'B'))
        return finishRadixInteger(IsBinaryDigit, MStringView("invalid binary integer literal"), MStringView("invalid suffix on binary integer literal"));

    consumeSeparatedDigits(isDecimalDigit);

    if(!isAtEnd() && peek() == '.'){
        const MChar afterDot = peekNext();
        const b8 isTrailingFloatSuffix = (afterDot == 'f' || afterDot == 'F') && !IsAsciiIdentifierChar(peekAt(2u));
        const b8 isTrailingExponent = (afterDot == 'e' || afterDot == 'E') && (IsAsciiDigit(peekAt(2u)) || ((peekAt(2u) == '+' || peekAt(2u) == '-') && IsAsciiDigit(peekAt(3u))));
        const b8 isBareTrailingDot = afterDot != '.' && !IsAsciiIdentifierStart(afterDot);
        if(IsAsciiDigit(afterDot) || isTrailingFloatSuffix || isTrailingExponent || isBareTrailingDot){
            isDouble = true;
            advance();
            consumeSeparatedDigits(isDecimalDigit);
        }
    }

    if(!isAtEnd() && (peek() == 'e' || peek() == 'E')){
        Optional<Token> errorToken;
        if(!consumeExponentAndTrailingSeparator(errorToken))
            return *errorToken;
    }

    if(!isAtEnd() && peek() == '\''){
        advance();
        return makeErrorToken(MStringView("invalid digit separator in numeric literal"), startLine, startColumn);
    }

    if(isDouble && !isAtEnd() && (peek() == 'f' || peek() == 'F')){
        isFloat = true;
        advance();
    }
    else if(!isDouble){
        while(!isAtEnd() && LiteralDecoder::IsIntegerLiteralSuffixChar(peek()))
            advance();
    }

    if(!isAtEnd() && (IsAsciiIdentifierChar(peek()) || (isDouble && peek() == '.' && IsAsciiDigit(peekNext()))))
        return consumeInvalidSuffix(MStringView("invalid suffix on numeric literal"));

    return makeNumberToken(isFloat ? TokenType::FloatLiteral : (isDouble ? TokenType::DoubleLiteral : TokenType::IntegerLiteral));
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

            const MChar esc = advance();
            if(esc == 'x'){
                if(isAtEnd() || !IsHexDigit(peek()))
                    return makeErrorToken(MStringView("invalid hexadecimal escape sequence in string literal"), startLine, startColumn);
                while(!isAtEnd() && IsHexDigit(peek()))
                    advance();
            }
            else if(esc == 'u' || esc == 'U'){
                Token errTok;
                if(!consumeUnicodeEscape(esc, startLine, startColumn, MStringView("invalid unicode escape sequence in string literal"), MStringView("invalid unicode code point in string literal"), errTok))
                    return errTok;
            }
            continue;
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
            u64 hexVal = 0u;
            while(!isAtEnd() && IsHexDigit(peek())){
                if(hexVal > (Limit<u32>::s_Max >> 4u))
                    return makeErrorToken(MStringView("hexadecimal escape sequence out of range in character literal"), startLine, startColumn);
                hexVal = (hexVal << 4u) | static_cast<u64>(HexDigitValue(advance()));
            }
        }
        else if(esc == 'u' || esc == 'U'){
            Token errTok;
            if(!consumeUnicodeEscape(esc, startLine, startColumn, s_UnterminatedCharMessage, MStringView("invalid unicode code point in character literal"), errTok))
                return errTok;
        }
        else if(IsOctalDigit(esc)){
            for(usize oct = 0u; oct < 2u; ++oct){
                if(!isAtEnd() && IsOctalDigit(peek()))
                    advance();
                else
                    break;
            }
        }
        else if(!LiteralDecoder::TryDecodeSimpleEscapeChar(esc)){
            return makeErrorToken(MStringView("invalid escape sequence in character literal"), startLine, startColumn);
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

Token Lexer::makeErrorToken(MStringView message, const u32 line, const u32 column)noexcept{
    Token tok;
    tok.type = TokenType::Error;
    tok.text = message;
    tok.line = line;
    tok.column = column;
    return tok;
}

b8 Lexer::consumeUnicodeEscape(
    const MChar esc,
    const u32 startLine,
    const u32 startColumn,
    const MStringView invalidHexMsg,
    const MStringView invalidCodePointMsg,
    Token& outErrorToken
)noexcept{
    const usize expectedHex = (esc == 'u') ? 4u : 8u;
    u32 codePoint = 0u;
    for(usize h = 0u; h < expectedHex; ++h){
        if(isAtEnd() || !IsHexDigit(peek())){
            outErrorToken = makeErrorToken(invalidHexMsg, startLine, startColumn);
            return false;
        }
        const MChar hc = advance();
        codePoint = (codePoint << 4u) | HexDigitValue(hc);
    }
    if(!IsUnicodeScalarValue(codePoint)){
        outErrorToken = makeErrorToken(invalidCodePointMsg, startLine, startColumn);
        return false;
    }
    return true;
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

b8 Lexer::skipBalancedBraceBlock(usize& outBodyEndOffset, b8& outHasAngleBracket)noexcept{
    outHasAngleBracket = false;
    u32 depth = 1u;

    while(!isAtEnd()){
        const MChar c = peek();
        if(c == ' ' || c == '\t' || c == '\r' || c == '\n'){
            advance();
            continue;
        }
        if(c == '/' && (peekNext() == '/' || peekNext() == '*')){
            skipWhitespaceAndComments();
            if(m_hasPendingError)
                return false;
            continue;
        }
        if(IsAsciiIdentifierStart(c)){
            advance();
            while(!isAtEnd() && IsAsciiIdentifierChar(peek()))
                advance();
            continue;
        }
        if(IsAsciiDigit(c)){
            const Token numTok = readNumber();
            if(numTok.type == TokenType::Error){
                m_hasPendingError = true;
                m_errorLine = numTok.line;
                m_errorColumn = numTok.column;
                m_errorMessage = numTok.text;
                return false;
            }
            continue;
        }
        if(c == '"'){
            const Token strTok = readString();
            if(strTok.type == TokenType::Error){
                m_hasPendingError = true;
                m_errorLine = strTok.line;
                m_errorColumn = strTok.column;
                m_errorMessage = strTok.text;
                return false;
            }
            continue;
        }
        if(c == '\''){
            const Token chrTok = readCharLiteral();
            if(chrTok.type == TokenType::Error){
                m_hasPendingError = true;
                m_errorLine = chrTok.line;
                m_errorColumn = chrTok.column;
                m_errorMessage = chrTok.text;
                return false;
            }
            continue;
        }
        if(c == '<'){
            outHasAngleBracket = true;
            advance();
            continue;
        }
        if(c == '{'){
            ++depth;
            advance();
            continue;
        }
        if(c == '}'){
            --depth;
            advance();
            if(depth == 0u){
                outBodyEndOffset = m_current;
                return true;
            }
            continue;
        }
        advance();
    }

    return false;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_METASCRIPT_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

