// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#pragma once


#include "type_utils.h"


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_ADHD_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


class LayoutOptimizer;


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


class TemplateInstantiationCollector final{
public:
    TemplateInstantiationCollector(MetaArena& arena, const SymbolTable& symbols, LayoutOptimizer& optimizer)noexcept
        : m_arena(arena)
        , m_symbols(symbols)
        , m_optimizer(optimizer)
    {}


public:
    void collectAllTemplateInstantiations(TranslationUnit& unit);


private:
    void collectTemplateInstantiationsInCallable(const MethodDecl& callable, TranslationUnit& unit);
    void collectTemplateInstantiationsInStmt(const Stmt& stmt, TranslationUnit& unit);
    void collectTemplateInstantiationsInExpr(const Expr& expr, TranslationUnit& unit);
    void collectSubstitutedTemplateInstantiationsInStmt(const Stmt& stmt, Span<const TemplateParamDecl> params, Span<const TypeArg> args, TranslationUnit& unit);
    void collectSubstitutedTemplateInstantiationsInExpr(const Expr& expr, Span<const TemplateParamDecl> params, Span<const TypeArg> args, TranslationUnit& unit);
    void collectTemplateInstantiationsInType(const TypeRef& type, u32 depth, TranslationUnit& unit);
    void collectTemplateInstantiationsInType(const TypeRef& type, TranslationUnit& unit){ collectTemplateInstantiationsInType(type, 0u, unit); }
    void instantiateClassTemplate(const ClassDecl& templateDecl, Span<const TypeArg> templateArgs, TranslationUnit& unit);


private:
    MetaArena& m_arena;
    const SymbolTable& m_symbols;
    LayoutOptimizer& m_optimizer;
    b8 m_collectingImported = false;
};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_ADHD_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

