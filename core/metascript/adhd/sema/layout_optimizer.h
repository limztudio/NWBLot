// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#pragma once


#include "type_utils.h"


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_ADHD_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


class LayoutOptimizer final{
    friend class TemplateInstantiationCollector;


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


private:
    MetaArena& m_arena;
    const SymbolTable& m_symbols;
    const TranslationUnit* m_unit = nullptr;
};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_ADHD_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

