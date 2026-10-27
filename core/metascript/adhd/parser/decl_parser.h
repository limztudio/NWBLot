// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#pragma once


#include "stmt_parser.h"


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_ADHD_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


class DeclParser{
public:
    explicit DeclParser(ParserCursor& cursor)noexcept
        : m_cursor(cursor)
        , m_typeParser(cursor)
        , m_exprParser(cursor, m_typeParser)
        , m_stmtParser(cursor, m_typeParser, m_exprParser)
    {
        m_exprParser.setStmtParser(&m_stmtParser);
    }


public:
    [[nodiscard]] bool parseTranslationUnit(TranslationUnit& outUnit);


private:
    [[nodiscard]] bool parseTopLevelDeclaration(TranslationUnit& outUnit);
    [[nodiscard]] TypeAliasDecl* parseTypeAlias(SourceSpan span, const MVector<ParsedAttribute>& attributes);
    [[nodiscard]] EnumDecl* parseEnumDecl(SourceSpan span, const MVector<ParsedAttribute>& attributes);
    [[nodiscard]] InterfaceDecl* parseInterfaceDecl(SourceSpan span, const MVector<ParsedAttribute>& attributes);
    [[nodiscard]] ClassDecl* parseClassDecl(SourceSpan span, const MVector<ParsedAttribute>& attributes);
    [[nodiscard]] bool parseClassMember(ClassDecl& classDecl);
    [[nodiscard]] MethodDecl* parseFreeOrOutOfLineCallable(SourceSpan span, const MVector<ParsedAttribute>& attributes);
    [[nodiscard]] bool parseParamList(MVector<ParamDecl>& outParams);
    [[nodiscard]] bool parseOperatorName(MStringView& outOpName);


private:
    ParserCursor& m_cursor;
    TypeParser m_typeParser;
    ExprParser m_exprParser;
    StmtParser m_stmtParser;
};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_ADHD_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

