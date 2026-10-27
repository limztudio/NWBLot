// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#pragma once


#include "type_parser.h"


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_ADHD_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


class StmtParser;


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


class ExprParser{
public:
    ExprParser(ParserCursor& cursor, TypeParser& typeParser)noexcept
        : m_cursor(cursor)
        , m_typeParser(typeParser)
    {}


public:
    void setStmtParser(StmtParser* stmtParser)noexcept{ m_stmtParser = stmtParser; }

    [[nodiscard]] Expr* parseExpression();
    [[nodiscard]] bool parseArgumentList(MVector<Expr*>& outArgs);


private:
    [[nodiscard]] Expr* parseAssignment();
    [[nodiscard]] Expr* parseLogicalOr();
    [[nodiscard]] Expr* parseLogicalAnd();
    [[nodiscard]] Expr* parseBitwiseOr();
    [[nodiscard]] Expr* parseBitwiseXor();
    [[nodiscard]] Expr* parseBitwiseAnd();
    [[nodiscard]] Expr* parseEquality();
    [[nodiscard]] Expr* parseRelational();
    [[nodiscard]] Expr* parseShift();
    [[nodiscard]] Expr* parseAdditive();
    [[nodiscard]] Expr* parseMultiplicative();
    [[nodiscard]] Expr* parseUnary();
    [[nodiscard]] Expr* parsePostfix();
    [[nodiscard]] Expr* parsePrimary();
    [[nodiscard]] Expr* parseLambda(SourceSpan span);


private:
    ParserCursor& m_cursor;
    TypeParser& m_typeParser;
    StmtParser* m_stmtParser = nullptr;
};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_ADHD_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

