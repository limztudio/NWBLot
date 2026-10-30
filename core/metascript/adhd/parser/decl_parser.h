// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#pragma once


#include "stmt_parser.h"

#include <core/metascript/preprocessor.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_ADHD_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


class DeclParser final{
private:
    [[nodiscard]] static b8 ValidateMemberOperatorArity(MStringView opName, usize paramCount, MStringView& outErrorMsg)noexcept;


public:
    explicit DeclParser(ParserCursor& cursor, const MStringView activeSource = {}, const Preprocessor* preprocessor = nullptr)noexcept
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
    [[nodiscard]] b8 isCurrentTokenImported()const noexcept;
    [[nodiscard]] b8 parseTopLevelDeclaration(TranslationUnit& outUnit);
    [[nodiscard]] TypeAliasDecl* parseTypeAlias(SourceSpan span, Span<const ParsedAttribute> attributes);
    [[nodiscard]] EnumDecl* parseEnumDecl(SourceSpan span, Span<const ParsedAttribute> attributes);
    [[nodiscard]] InterfaceDecl* parseInterfaceDecl(SourceSpan span, Span<const ParsedAttribute> attributes);
    [[nodiscard]] ClassDecl* parseClassDecl(SourceSpan span, Span<const ParsedAttribute> attributes);
    void parseClassMethodQualifiers(MethodDecl& method);
    [[nodiscard]] b8 parseClassMemberCallableBody(MethodDecl& method, MStringView missingTerminatorMessage);
    [[nodiscard]] b8 parseDestructorEmptyParams(MStringView missingLeftParenMessage);
    [[nodiscard]] b8 parseConstructorParamsAndRejectInitList(MVector<ParamDecl>& outParams);
    [[nodiscard]] b8 parseMethodParamsAndValidateArity(MethodDecl& method, SourceSpan span);
    [[nodiscard]] b8 parseClassMethodOrOperatorTail(ClassDecl& classDecl, AccessSpecifier::Enum access, TypeRef* returnType, MStringView name, b8 isOperator, MVector<TemplateParamDecl>&& templateParams, Span<const ParsedAttribute> attributes, SourceSpan span, MStringView missingTerminatorMessage);
    [[nodiscard]] b8 parseClassMember(ClassDecl& classDecl);
    [[nodiscard]] b8 parseCallableOwnerOrTemplateHeader(b8 failOnInvalidTemplateParams, MVector<MStringView>& outOwnerTemplateParams, MVector<TemplateParamDecl>& outTemplateParams);
    [[nodiscard]] MethodDecl* parseFreeOrOutOfLineCallable(SourceSpan span, Span<const ParsedAttribute> attributes);
    [[nodiscard]] b8 applyCallableAttributes(Span<const ParsedAttribute> attributes, MethodDecl& decl);
    [[nodiscard]] b8 parseOperatorName(MStringView& outOpName);


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

