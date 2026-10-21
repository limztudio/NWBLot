// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#pragma once


#include "operation_validator.h"


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_METASCRIPT_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


// Parses literal, reference, list, map and arithmetic expressions into values.
class ExpressionParser{
public:
    using ScratchPath = Vector<MStringView, Alloc::ScratchArena>;


private:
    using ScratchString = BasicString<MChar, Alloc::ScratchArena>;


public:
    ExpressionParser(ParserCursor& cursor, OperationValidator& validator, const MStringMap<Value>& variables)noexcept
        : m_cursor(cursor)
        , m_validator(validator)
        , m_arena(cursor.m_arena)
        , m_scratchArena(cursor.m_scratchArena)
        , m_variables(variables)
    {}


public:
    Value parseExpression();

    bool parseReferencePath(ScratchPath& path);


private:
    template<typename ParseNext, typename ApplyOperation>
    Value parseBinaryExpression(ParseNext&& parseNext, TokenType::Enum lhsOp, TokenType::Enum rhsOp, ApplyOperation&& applyOperation);
    Value parseAdditive();
    Value parseMultiplicative();
    Value parseUnary();
    Value parsePrimary();

    Value parseListLiteral(TokenType::Enum closeToken);
    Value parseBraceExpression();
    Value parseMapLiteralContinuation(Value&& firstKey);

    Value makeReference(const ScratchPath& path);


private:
    ParserCursor& m_cursor;
    OperationValidator& m_validator;
    MetaArena& m_arena;
    Alloc::ScratchArena& m_scratchArena;
    const MStringMap<Value>& m_variables;
};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_METASCRIPT_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

