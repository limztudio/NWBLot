// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#pragma once


#include "stmt_parser.h"

#include <core/metascript/preprocessor.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_ADHD_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


class DeclParser{
public:
    explicit DeclParser(
        ParserCursor& cursor,
        const MStringView activeSource = {},
        const Preprocessor* preprocessor = nullptr
    )noexcept
        : m_cursor(cursor)
        , m_typeParser(cursor)
        , m_exprParser(cursor, m_typeParser)
        , m_stmtParser(cursor, m_typeParser, m_exprParser)
        , m_activeSource(activeSource)
        , m_preprocessor(preprocessor)
    {
        m_exprParser.setStmtParser(&m_stmtParser);
    }


public:
    [[nodiscard]] b8 parseTranslationUnit(TranslationUnit& outUnit);


private:
    [[nodiscard]] b8 parseTopLevelDeclaration(TranslationUnit& outUnit);
    [[nodiscard]] TypeAliasDecl* parseTypeAlias(SourceSpan span, const MVector<ParsedAttribute>& attributes);
    [[nodiscard]] EnumDecl* parseEnumDecl(SourceSpan span, const MVector<ParsedAttribute>& attributes);
    [[nodiscard]] InterfaceDecl* parseInterfaceDecl(SourceSpan span, const MVector<ParsedAttribute>& attributes);
    [[nodiscard]] ClassDecl* parseClassDecl(SourceSpan span, const MVector<ParsedAttribute>& attributes);
    [[nodiscard]] b8 parseClassMember(ClassDecl& classDecl);
    void parseClassMethodQualifiers(MethodDecl& method)noexcept;
    [[nodiscard]] b8 parseClassMemberCallableBody(MethodDecl& method, MStringView missingTerminatorMessage);
    [[nodiscard]] MethodDecl* parseFreeOrOutOfLineCallable(SourceSpan span, const MVector<ParsedAttribute>& attributes);
    [[nodiscard]] b8 parseCallableOwnerOrTemplateHeader(MVector<MStringView>& outOwnerTemplateParams, MVector<TemplateParamDecl>& outTemplateParams, b8 failOnInvalidTemplateParams);
    [[nodiscard]] b8 applyCallableAttributes(const MVector<ParsedAttribute>& attributes, SourceSpan span, MethodDecl& decl);
    [[nodiscard]] b8 parseOperatorName(MStringView& outOpName);
    [[nodiscard]] static b8 validateMemberOperatorArity(MStringView opName, usize paramCount, MStringView& outErrorMsg)noexcept;
    [[nodiscard]] b8 isCurrentTokenImported()const noexcept;


private:
    ParserCursor& m_cursor;
    TypeParser m_typeParser;
    ExprParser m_exprParser;
    StmtParser m_stmtParser;
    MStringView m_activeSource;
    const Preprocessor* m_preprocessor = nullptr;
};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_ADHD_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

