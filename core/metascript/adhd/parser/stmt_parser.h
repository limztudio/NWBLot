// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#pragma once


#include "expr_parser.h"


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_ADHD_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


class StmtParser{
public:
    StmtParser(ParserCursor& cursor, TypeParser& typeParser, ExprParser& exprParser)noexcept
        : m_cursor(cursor)
        , m_typeParser(typeParser)
        , m_exprParser(exprParser)
    {}


public:
    [[nodiscard]] b8 parseBlockStatements(MVector<Stmt*>& outStatements);
    [[nodiscard]] Stmt* parseStatement();
    [[nodiscard]] Stmt* parseConstDeclaration(SourceSpan span);


private:
    [[nodiscard]] b8 rejectStatementAttributes(Span<const ParsedAttribute> attributes, SourceSpan span, MStringView message);
    [[nodiscard]] Stmt* parseSimpleKeywordStatement(StmtKind::Enum kind, SourceSpan span, Span<const ParsedAttribute> attributes, MStringView rejectAttrMsg, MStringView missingSemiMsg);
    [[nodiscard]] b8 parseScopedBodyStatements(MVector<Stmt*>& outStatements);
    [[nodiscard]] Stmt* parseIfStatement(SourceSpan span);
    [[nodiscard]] Stmt* parseSwitchStatement(SourceSpan span);
    [[nodiscard]] Stmt* parseForOrParallelFor(SourceSpan span, b8 isParallel, Span<const ParsedAttribute> attributes);
    [[nodiscard]] Stmt* parseWhileStatement(SourceSpan span);
    [[nodiscard]] Stmt* parseDoWhileStatement(SourceSpan span);
    [[nodiscard]] Stmt* parseReturnStatement(SourceSpan span);
    [[nodiscard]] Stmt* parseParallelStatement(SourceSpan span, Span<const ParsedAttribute> attributes);
    [[nodiscard]] Stmt* parseNativeStatement(SourceSpan span);
    [[nodiscard]] Stmt* parseDiagnosticStatement(SourceSpan span, DiagnosticKind::Enum kind, Span<const ParsedAttribute> attributes);
    [[nodiscard]] Stmt* parseVarDeclWithType(TypeRef* varType, SourceSpan span, b8 isConst);
    [[nodiscard]] b8 applySchedulingAttributes(Span<const ParsedAttribute> attributes, ParallelCoreKind::Enum& outCore, ParallelPriorityKind::Enum& outPriority);


private:
    ParserCursor& m_cursor;
    TypeParser& m_typeParser;
    ExprParser& m_exprParser;
};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_ADHD_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

