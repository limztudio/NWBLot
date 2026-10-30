// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#pragma once


#include "type_utils.h"


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_ADHD_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


[[nodiscard]] b8 TypeRefReferencesSymbol(const TypeRef& type, MStringView targetName, u32 depth = 0u)noexcept;
[[nodiscard]] b8 TypeRefReferencesSymbolByValue(const TypeRef& type, MStringView targetName, const SymbolTable& symbols, u32 depth = 0u)noexcept;
[[nodiscard]] b8 InstantiationDependsOnClassByValue(const ClassTemplateInstantiation& inst, MStringView targetClassName, const SymbolTable& symbols)noexcept;
void SortTypeAliasesTopologically(Span<const TypeAliasDecl* const> aliases, MetaArena& arena, MVector<const TypeAliasDecl*>& outSorted);
void SortInterfacesTopologically(Span<const InterfaceDecl* const> interfaces, const SymbolTable& symbols, MetaArena& arena, MVector<const InterfaceDecl*>& outSorted);
void SortClassesTopologically(Span<const ClassDecl* const> classes, const SymbolTable& symbols, MetaArena& arena, MVector<const ClassDecl*>& outSorted);
void CollectExternalForwardDeclClasses(const TypeRef& type, const SymbolTable& symbols, Span<const TemplateParamDecl> outerTemplateParams, Span<const TemplateParamDecl> innerTemplateParams, b8 inRefOrAssetContext, u32 depth, MVector<MStringView>& outNames);
inline void CollectExternalForwardDeclClasses(const TypeRef& type, const SymbolTable& symbols, const Span<const TemplateParamDecl> outerTemplateParams, const Span<const TemplateParamDecl> innerTemplateParams, MVector<MStringView>& outNames){ CollectExternalForwardDeclClasses(type, symbols, outerTemplateParams, innerTemplateParams, false, 0u, outNames); }


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


class LayoutOptimizer final{
private:
    static void SortAndPackFields(b8 isSerialize, b8 packSerialize, b8 hasVtablePointer, MVector<FieldDecl*>& orderedFields, usize& outTotalSize, usize& outTotalAlign)noexcept;


public:
    LayoutOptimizer(MetaArena& arena, const SymbolTable& symbols)noexcept
        : m_arena(arena)
        , m_symbols(symbols)
    {}


public:
    [[nodiscard]] b8 optimizeTranslationUnit(TranslationUnit& unit, MVector<ParseError>& errors);
    void resolveTypeSizeAndAlign(const TypeRef& type, u32 depth, usize& outSize, usize& outAlign)const noexcept;
    void resolveTypeSizeAndAlign(const TypeRef& type, usize& outSize, usize& outAlign)const noexcept{ resolveTypeSizeAndAlign(type, 0u, outSize, outAlign); }


private:
    void markFieldArenaUsage(const TypeRef& fieldType, FieldDecl& field, ClassDecl& classDecl)const noexcept;
    [[nodiscard]] b8 computeClassLayout(ClassDecl& classDecl, MVector<ParseError>& errors);
    void finalizeClassFieldLayouts(b8 isSerialize, b8 hasVtablePointer, MVector<FieldDecl*>& orderedFields, usize& outTotalSize, usize& outTotalAlign)const noexcept;
    void collectAllTemplateInstantiations(TranslationUnit& unit);
    void collectTemplateInstantiationsInCallable(const MethodDecl& callable, TranslationUnit& unit);
    void collectTemplateInstantiationsInStmt(const Stmt& stmt, TranslationUnit& unit);
    void collectTemplateInstantiationsInExpr(const Expr& expr, TranslationUnit& unit);
    void collectTemplateInstantiationsInType(const TypeRef& type, u32 depth, TranslationUnit& unit);
    void collectTemplateInstantiationsInType(const TypeRef& type, TranslationUnit& unit){ collectTemplateInstantiationsInType(type, 0u, unit); }
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

