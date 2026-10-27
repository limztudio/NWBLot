// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#pragma once


#include "parser.h"

#include <core/alloc/scratch.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_METASCRIPT_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


// Token stream and parse error sink shared by the statement, expression, attribute, and adhd parsers.
class ParserCursor{
    friend class ExpressionParser;
    friend class StatementParser;


public:
    struct Checkpoint{
        Lexer lexer;
        Token current;
        usize errorCount = 0u;
    };


public:
    ParserCursor(MStringView source, MetaArena& arena, Alloc::ScratchArena& scratchArena, MVector<ParseError>& errors, bool extendedSyntax = false);


public:
    [[nodiscard]] Checkpoint save()const noexcept{ return Checkpoint{m_lexer, m_current, m_errors.size()}; }
    void restore(const Checkpoint& checkpoint)noexcept{
        m_lexer = checkpoint.lexer;
        m_current = checkpoint.current;
        while(m_errors.size() > checkpoint.errorCount)
            m_errors.pop_back();
    }

    [[nodiscard]] bool failed()const noexcept{ return !m_errors.empty(); }
    [[nodiscard]] const Token& current()const noexcept{ return m_current; }
    [[nodiscard]] MetaArena& arena()const noexcept{ return m_arena; }
    [[nodiscard]] Alloc::ScratchArena& scratchArena()const noexcept{ return m_scratchArena; }
    [[nodiscard]] MVector<ParseError>& errors()const noexcept{ return m_errors; }

    [[nodiscard]] bool check(const TokenType::Enum type)const noexcept{ return m_current.type == type; }
    [[nodiscard]] bool checkIdentifier(const MStringView text)const noexcept{ return m_current.type == TokenType::Identifier && m_current.text == text; }

    bool match(TokenType::Enum type);
    bool matchIdentifier(MStringView text);

    void advance();
    bool expect(TokenType::Enum type, MStringView expected);

    void error(MStringView message);
    void error(u32 line, u32 column, MStringView message);
    void errorExpected(MStringView expected);


private:
    Lexer m_lexer;
    MetaArena& m_arena;
    Alloc::ScratchArena& m_scratchArena;
    MVector<ParseError>& m_errors;

    Token m_current;
};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_METASCRIPT_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

