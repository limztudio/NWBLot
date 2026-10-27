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
    [[nodiscard]] bool parseBlockStatements(MVector<Stmt*>& outStatements);
    [[nodiscard]] Stmt* parseStatement();
    [[nodiscard]] Stmt* parseConstDeclaration(SourceSpan span);
    [[nodiscard]] bool parseRawNativeBlock(MStringView& outCode);


private:
    [[nodiscard]] bool parseScopedBodyStatements(MVector<Stmt*>& outStatements);
    [[nodiscard]] Stmt* parseIfStatement(SourceSpan span);
    [[nodiscard]] Stmt* parseSwitchStatement(SourceSpan span);
    [[nodiscard]] Stmt* parseForOrParallelFor(SourceSpan span, bool isParallel, const MVector<ParsedAttribute>& attributes);
    [[nodiscard]] Stmt* parseWhileStatement(SourceSpan span);
    [[nodiscard]] Stmt* parseDoWhileStatement(SourceSpan span);
    [[nodiscard]] Stmt* parseReturnStatement(SourceSpan span);
    [[nodiscard]] Stmt* parseParallelStatement(SourceSpan span, const MVector<ParsedAttribute>& attributes);
    [[nodiscard]] Stmt* parseNativeStatement(SourceSpan span);
    [[nodiscard]] Stmt* parseDiagnosticStatement(SourceSpan span, DiagnosticKind::Enum kind, const MVector<ParsedAttribute>& attributes);
    [[nodiscard]] Stmt* parseVarDeclAfterType(TypeRef* varType, SourceSpan span);
    [[nodiscard]] bool applySchedulingAttributes(const MVector<ParsedAttribute>& attributes, ParallelCoreKind::Enum& outCore, ParallelPriorityKind::Enum& outPriority);


private:
    ParserCursor& m_cursor;
    TypeParser& m_typeParser;
    ExprParser& m_exprParser;
};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_ADHD_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

