// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#pragma once


#include "type_utils.h"


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_ADHD_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


[[nodiscard]] b8 TypeRefReferencesSymbol(const TypeRef& type, MStringView targetName, u32 depth = 0u)noexcept;
[[nodiscard]] b8 TypeRefReferencesSymbolByValue(
    const TypeRef& type,
    MStringView targetName,
    const SymbolTable& symbols,
    u32 depth = 0u
)noexcept;
[[nodiscard]] b8 InstantiationDependsOnClassByValue(
    const ClassTemplateInstantiation& inst,
    MStringView targetClassName,
    const SymbolTable& symbols
)noexcept;
void SortTypeAliasesTopologically(
    Span<const TypeAliasDecl* const> aliases,
    MetaArena& arena,
    MVector<const TypeAliasDecl*>& outSorted
);
void SortInterfacesTopologically(
    Span<const InterfaceDecl* const> interfaces,
    const SymbolTable& symbols,
    MetaArena& arena,
    MVector<const InterfaceDecl*>& outSorted
);
void SortClassesTopologically(
    Span<const ClassDecl* const> classes,
    const SymbolTable& symbols,
    MetaArena& arena,
    MVector<const ClassDecl*>& outSorted
);
void CollectExternalForwardDeclClasses(
    const TypeRef& type,
    const SymbolTable& symbols,
    Span<const TemplateParamDecl> outerTemplateParams,
    Span<const TemplateParamDecl> innerTemplateParams,
    MVector<MStringView>& outNames,
    b8 inRefOrAssetContext = false,
    u32 depth = 0u
);


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


class LayoutOptimizer{
private:
    static void sortAndPackFields(
        MVector<FieldDecl*>& orderedFields,
        b8 isSerialize,
        b8 hasVtablePointer,
        usize& outTotalSize,
        usize& outTotalAlign
    )noexcept;


public:
    LayoutOptimizer(MetaArena& arena, const SymbolTable& symbols)noexcept
        : m_arena(arena)
        , m_symbols(symbols)
    {}


public:
    [[nodiscard]] b8 optimizeTranslationUnit(TranslationUnit& unit, MVector<ParseError>& errors);
    void resolveTypeSizeAndAlign(const TypeRef& type, usize& outSize, usize& outAlign, u32 depth = 0u)const noexcept;


private:
    void markFieldArenaUsage(FieldDecl& field, const TypeRef& fieldType, ClassDecl& classDecl)const noexcept;
    [[nodiscard]] b8 computeClassLayout(ClassDecl& classDecl, MVector<ParseError>& errors);
    void finalizeClassFieldLayouts(
        MVector<FieldDecl*>& orderedFields,
        b8 isSerialize,
        b8 hasVtablePointer,
        usize& outTotalSize,
        usize& outTotalAlign
    )const noexcept;
    void collectAllTemplateInstantiations(TranslationUnit& unit);
    void collectTemplateInstantiationsInCallable(const MethodDecl& callable, TranslationUnit& unit);
    void collectTemplateInstantiationsInStmt(const Stmt& stmt, TranslationUnit& unit);
    void collectTemplateInstantiationsInExpr(const Expr& expr, TranslationUnit& unit);
    void collectTemplateInstantiationsInType(const TypeRef& type, TranslationUnit& unit, u32 depth = 0u);
    void instantiateClassTemplate(const ClassDecl& templateDecl, Span<const TypeArg> templateArgs, TranslationUnit& unit);


private:
    MetaArena& m_arena;
    const SymbolTable& m_symbols;
    const TranslationUnit* m_unit = nullptr;
    b8 m_collectingImported = false;
};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_ADHD_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

