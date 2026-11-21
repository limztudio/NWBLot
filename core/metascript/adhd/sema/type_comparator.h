// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#pragma once


#include "type_utils.h"


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_ADHD_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


class TypeComparator final{
public:
    explicit TypeComparator(const SymbolTable& symbols)noexcept
        : m_symbols(symbols)
    {}

    [[nodiscard]] b8 areEquivalentWithSubst(const TypeRef* actual, const TypeRef* pattern, Span<const TemplateParamDecl> ifaceParams, Span<const TypeArg> ifaceArgs, b8 ignoreTopLevelRefMode = false)const noexcept;
    [[nodiscard]] b8 areEquivalent(const TypeRef* a, const TypeRef* b)const noexcept{ return areEquivalentWithSubst(a, b, {}, {}, false); }
    [[nodiscard]] b8 areEquivalentIgnoringRefMode(const TypeRef* a, const TypeRef* b)const noexcept{ return areEquivalentWithSubst(a, b, {}, {}, true); }
    [[nodiscard]] b8 hasEquivalentInPrefix(Span<const TypeRef* const> types, const TypeRef* target)const noexcept;
    [[nodiscard]] b8 areTemplateArgListsEqual(Span<const TypeArg> a, Span<const TypeArg> b)const noexcept;
    [[nodiscard]] b8 doTemplateParamHeaderMatch(const TemplateParamDecl& a, const TemplateParamDecl& b, b8 checkName)const noexcept;
    [[nodiscard]] b8 doTemplateParamBoundsMatch(const TemplateParamDecl& a, const TemplateParamDecl& b)const noexcept;
    [[nodiscard]] b8 doParamListsMatchWithSubst(Span<const ParamDecl> actual, Span<const ParamDecl> pattern, Span<const TemplateParamDecl> ifaceParams, Span<const TypeArg> ifaceArgs, usize* outMismatchIndex = nullptr)const noexcept;
    [[nodiscard]] b8 doParamListsMatch(Span<const ParamDecl> a, Span<const ParamDecl> b)const noexcept{ return doParamListsMatchWithSubst(a, b, {}, {}); }


private:
    const SymbolTable& m_symbols;
};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_ADHD_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

