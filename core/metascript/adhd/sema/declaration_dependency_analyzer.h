// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#pragma once


#include "type_utils.h"


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_ADHD_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


class DeclarationDependencyAnalyzer final{
public:
    DeclarationDependencyAnalyzer(MetaArena& arena, const SymbolTable& symbols)noexcept : m_arena(arena), m_symbols(symbols){}


public:
    [[nodiscard]] b8 typeRefReferencesSymbol(const TypeRef& type, MStringView targetName, u32 depth = 0u)const noexcept;
    [[nodiscard]] b8 typeRefReferencesSymbolByValue(const TypeRef& type, MStringView targetName, u32 depth = 0u)const noexcept;
    [[nodiscard]] b8 instantiationDependsOnClassByValue(const ClassTemplateInstantiation& inst, MStringView targetClassName)const noexcept;
    void sortTypeAliasesTopologically(Span<const TypeAliasDecl* const> aliases, MVector<const TypeAliasDecl*>& outSorted)const;
    void sortInterfacesTopologically(Span<const InterfaceDecl* const> interfaces, MVector<const InterfaceDecl*>& outSorted)const;
    void sortClassesTopologically(Span<const ClassDecl* const> classes, MVector<const ClassDecl*>& outSorted)const;
    void collectExternalForwardDeclClasses(const TypeRef& type, Span<const TemplateParamDecl> outerTemplateParams, Span<const TemplateParamDecl> innerTemplateParams, b8 inRefOrAssetContext, u32 depth, MVector<MStringView>& outNames)const;


private:
    [[nodiscard]] b8 classDependsOnClassByValue(const ClassDecl& cls, MStringView targetClassName, u32 depth = 0u)const noexcept;


private:
    MetaArena& m_arena;
    const SymbolTable& m_symbols;
};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_ADHD_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

