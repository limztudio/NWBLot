// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#pragma once


#include "type_utils.h"


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_ADHD_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


class LayoutOptimizer{
public:
    LayoutOptimizer(MetaArena& arena, const SymbolTable& symbols)noexcept
        : m_arena(arena)
        , m_symbols(symbols)
    {}


public:
    [[nodiscard]] bool optimizeTranslationUnit(TranslationUnit& unit, MVector<ParseError>& errors);
    void resolveTypeSizeAndAlign(const TypeRef& type, usize& outSize, usize& outAlign, u32 depth = 0u)const noexcept;


private:
    [[nodiscard]] bool computeClassLayout(ClassDecl& classDecl, MVector<ParseError>& errors);
    void collectAllTemplateInstantiations(TranslationUnit& unit);
    void collectTemplateInstantiationsInStmt(const Stmt& stmt, TranslationUnit& unit);
    void collectTemplateInstantiationsInExpr(const Expr& expr, TranslationUnit& unit);
    void collectTemplateInstantiationsInType(const TypeRef& type, TranslationUnit& unit, u32 depth = 0u);
    void instantiateClassTemplate(const ClassDecl& templateDecl, const MVector<TypeArg>& templateArgs, TranslationUnit& unit);
    static void sortAndPackFields(
        MVector<FieldDecl*>& orderedFields,
        bool isSerialize,
        bool hasVtablePointer,
        usize& outTotalSize,
        usize& outTotalAlign
    )noexcept;


private:
    MetaArena& m_arena;
    const SymbolTable& m_symbols;
    const TranslationUnit* m_unit = nullptr;
    bool m_collectingImported = false;
};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_ADHD_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

