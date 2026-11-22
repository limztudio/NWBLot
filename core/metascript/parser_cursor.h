// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#pragma once


#include "parser.h"

#include <core/alloc/scratch.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_METASCRIPT_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


class ParserCursor final{
    friend class ExpressionParser;
    friend class StatementParser;


public:
    struct Checkpoint{
        Lexer lexer;
        Token current;
        usize errorCount = 0u;
    };

    class SuppressErrorsScope final : NoCopy{
    public:
        explicit SuppressErrorsScope(ParserCursor& cursor)noexcept
            : m_cursor(cursor)
            , m_prevSuppress(cursor.m_suppressErrors)
        {
            m_cursor.m_suppressErrors = true;
        }
        ~SuppressErrorsScope()noexcept{
            m_cursor.m_suppressErrors = m_prevSuppress;
        }

    private:
        ParserCursor& m_cursor;
        b8 m_prevSuppress = false;
    };


public:
    ParserCursor(MStringView source, MetaArena& arena, Alloc::ScratchArena& scratchArena, MVector<ParseError>& errors);


public:
    [[nodiscard]] Checkpoint save()const noexcept{ return Checkpoint{m_lexer, m_current, m_errors.size()}; }
    void restore(const Checkpoint& checkpoint)noexcept;

    [[nodiscard]] b8 failed()const noexcept{ return !m_errors.empty(); }
    [[nodiscard]] const Token& current()const noexcept{ return m_current; }
    [[nodiscard]] Token peekNextToken()const noexcept{ Lexer copy = m_lexer; return copy.next(); }
    [[nodiscard]] MetaArena& arena()const noexcept{ return m_arena; }

    [[nodiscard]] b8 check(const TokenType::Enum type)const noexcept{ return m_current.type == type; }
    [[nodiscard]] b8 checkIdentifier(const MStringView text)const noexcept{ return m_current.type == TokenType::Identifier && m_current.text == text; }

    [[nodiscard]] b8 match(TokenType::Enum type);
    [[nodiscard]] b8 matchIdentifier(MStringView text);

    void advance();
    [[nodiscard]] b8 expect(TokenType::Enum type, MStringView expected);
    [[nodiscard]] b8 skipBalancedBraces(usize& outStartOffset, usize& outEndOffset, b8& outHasAngleBracket);

    void error(MStringView message){ error(m_current.line, m_current.column, message); }
    void error(u32 line, u32 column, MStringView message);
    void errorExpected(MStringView expected);


private:
    Lexer m_lexer;
    MetaArena& m_arena;
    Alloc::ScratchArena& m_scratchArena;
    MVector<ParseError>& m_errors;

    Token m_current;
    b8 m_suppressErrors = false;
};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_METASCRIPT_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

