// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#pragma once


#include "symbol_table.h"


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_ADHD_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


[[nodiscard]] b8 IsValidScopedEnumTemplateArg(
    MStringView text,
    const SymbolTable& symbols,
    const EnumDecl** outEnum = nullptr,
    MStringView* outEntryName = nullptr
)noexcept;
[[nodiscard]] b8 TryEvaluateEnumEntryConstant(
    const EnumDecl& ed,
    MStringView targetEntry,
    const SymbolTable& symbols,
    i64& outVal,
    u32 depth = 0u
)noexcept;
[[nodiscard]] b8 TryEvaluateExprConstant(
    const Expr* expr,
    const SymbolTable& symbols,
    i64& outVal,
    u32 depth = 0u,
    const EnumDecl* contextEnum = nullptr
)noexcept;
[[nodiscard]] b8 TryEvaluateTextConstant(
    MStringView text,
    const SymbolTable& symbols,
    i64& outVal,
    u32 depth = 0u,
    const EnumDecl* contextEnum = nullptr
)noexcept;
[[nodiscard]] b8 IsConcreteConstTemplateArgText(MStringView text, const SymbolTable& symbols)noexcept;
[[nodiscard]] b8 AreConstantTemplateArgsEquivalent(MStringView a, MStringView b, const SymbolTable& symbols)noexcept;
[[nodiscard]] b8 HasConstValueTemplateParam(const ClassDecl* ownerClass, const MethodDecl* method, MStringView name)noexcept;
[[nodiscard]] b8 NormalizeConstantTemplateArg(
    TypeArg& arg,
    const ClassDecl* enclosingClass,
    const MethodDecl* enclosingMethod,
    const SymbolTable& symbols
)noexcept;
[[nodiscard]] usize ParseUnsignedConstant(MStringView text, const SymbolTable& symbols)noexcept;


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_ADHD_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

