// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#pragma once


#include "stmt_parser.h"

#include <core/metascript/preprocessor.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_ADHD_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


class DeclParser final{
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
    [[nodiscard]] b8 computeCurrentTokenOffset(usize& outOffset)const noexcept;
    [[nodiscard]] b8 isCurrentTokenImported()const noexcept;
    [[nodiscard]] MStringView currentTokenImportedNamespace()const noexcept;
    [[nodiscard]] b8 tryParseExternAttribute(const ParsedAttribute& attr, MStringView& outCppExportName, MStringView& outRequiredHeader, b8 isExtern, b8& outMatched);
    [[nodiscard]] b8 tryParseExternClassAttribute(const ParsedAttribute& attr, ClassDecl& outClass, b8 isExtern, b8& inOutSeenFlags, b8& outMatched);
    [[nodiscard]] b8 tryParseExternCallableAttribute(const ParsedAttribute& attr, MethodDecl& outMethod, b8& inOutSeenFlags, b8& outMatched);
    [[nodiscard]] b8 parseRegistration(TranslationUnit& unit, const MVector<ParsedAttribute>& attributes, b8 isExtern, b8 isImported);
    [[nodiscard]] b8 parseTopLevelDeclaration(TranslationUnit& outUnit);
    [[nodiscard]] TypeAliasDecl* parseTypeAlias(SourceSpan span, Span<const ParsedAttribute> attributes, b8 isExtern = false);
    [[nodiscard]] EnumDecl* parseEnumDecl(SourceSpan span, Span<const ParsedAttribute> attributes, b8 isFlag, b8 isExtern = false);
    [[nodiscard]] InterfaceDecl* parseInterfaceDecl(SourceSpan span, Span<const ParsedAttribute> attributes, b8 isExtern = false);
    [[nodiscard]] ClassDecl* parseClassDecl(SourceSpan span, Span<const ParsedAttribute> attributes, b8 isExtern = false, b8 isImported = false);
    void parseClassMethodQualifiers(MethodDecl& method);
    [[nodiscard]] b8 parseClassMemberCallableBody(const ClassDecl& ownerClass, MethodDecl& method, MStringView missingTerminatorMessage);
    [[nodiscard]] b8 parseDestructorEmptyParams(MStringView missingLeftParenMessage);
    [[nodiscard]] b8 parseConstructorParamsAndRejectInitList(MethodDecl& method);
    [[nodiscard]] b8 parseMethodParamsAndValidateArity(MethodDecl& method, SourceSpan span);
    [[nodiscard]] b8 parseColonTypeList(MVector<TypeRef*>& outTypes, MStringView expectedTypeMsg, b8 rejectAccessSpecifiers);
    [[nodiscard]] b8 parseMethodOrOperatorName(MStringView& outName, b8& outIsOperator, MStringView expectedNameMsg);
    [[nodiscard]] b8 initializeDestructorDecl(
        MethodDecl& method,
        MStringView ownerName,
        MStringView missingLeftParenMsg,
        MVector<MStringView>* ownerTemplateParams = nullptr,
        MVector<TemplateParamDecl>* ownerTemplateParamDecls = nullptr
    );
    [[nodiscard]] b8 initializeConstructorDecl(
        MethodDecl& method,
        MStringView ownerName,
        MVector<MStringView>* ownerTemplateParams = nullptr,
        MVector<TemplateParamDecl>* ownerTemplateParamDecls = nullptr
    );
    [[nodiscard]] b8 parseClassMethodOrOperatorTail(
        ClassDecl& classDecl,
        AccessSpecifier::Enum access,
        b8 isStatic,
        b8 isExtern,
        TypeRef* returnType,
        MStringView name,
        b8 isOperator,
        MVector<TemplateParamDecl>&& templateParams,
        Span<const ParsedAttribute> attributes,
        SourceSpan span,
        MStringView missingTerminatorMessage
    );
    [[nodiscard]] b8 parseClassMember(ClassDecl& classDecl);
    [[nodiscard]] b8 parseCallableOwnerOrTemplateHeader(b8 failOnInvalidTemplateParams, MVector<MStringView>& outOwnerTemplateParams, MVector<TemplateParamDecl>& outOwnerTemplateParamDecls, MVector<TemplateParamDecl>& outTemplateParams);
    [[nodiscard]] MethodDecl* parseFreeOrOutOfLineCallable(SourceSpan span, Span<const ParsedAttribute> attributes, b8 isExtern = false, b8 isImported = false);
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

