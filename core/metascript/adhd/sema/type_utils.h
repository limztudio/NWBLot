// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#pragma once


#include "symbol_table.h"


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_ADHD_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


[[nodiscard]] constexpr b8 IsIntegerScalarType(const MStringView typeName)noexcept{
#define NWB_ADHD_EXPORT_SCALAR_TYPE(adhdName, byteSize, byteAlign, isInteger, isFixedWidthInt, isSigned, isFloat, defaultLiteral) \
    if constexpr(isInteger){ \
        if(typeName == adhdName) return true; \
    }
#include "../export_types.h"
#undef NWB_ADHD_EXPORT_SCALAR_TYPE
    return false;
}

[[nodiscard]] constexpr b8 IsFloatScalarType(const MStringView typeName)noexcept{
#define NWB_ADHD_EXPORT_SCALAR_TYPE(adhdName, byteSize, byteAlign, isInteger, isFixedWidthInt, isSigned, isFloat, defaultLiteral) \
    if constexpr(isFloat){ \
        if(typeName == adhdName) return true; \
    }
#include "../export_types.h"
#undef NWB_ADHD_EXPORT_SCALAR_TYPE
    return false;
}

[[nodiscard]] constexpr MStringView MapTokenTypeToOperatorSymbol(const TokenType::Enum op)noexcept{
    switch(op){
#define NWB_ADHD_EXPORT_OPERATOR_TOKEN(tokenType, symbol, isOverloadable, isAssignment, isCompoundAssignment) \
    case tokenType: \
        if constexpr(tokenType != TokenType::Equal) return symbol; \
        break;
#include "../export_operators.h"
#undef NWB_ADHD_EXPORT_OPERATOR_TOKEN
    default:
        return {};
    }
    return {};
}

[[nodiscard]] b8 IsNonThreadSafeContainer(MStringView typeName)noexcept;
[[nodiscard]] b8 IsSingleTypeParamBuiltinGeneric(MStringView name)noexcept;
[[nodiscard]] b8 IsTwoTypeParamBuiltinGeneric(MStringView name)noexcept;
[[nodiscard]] b8 IsTypeAndCountBuiltinGeneric(MStringView name)noexcept;
[[nodiscard]] b8 CheckSwizzlePattern(MStringView text, u32 maxLanes, b8& outIsPattern, b8& outValidLanes)noexcept;

[[nodiscard]] b8 IsVoidTypeRef(const TypeRef* ref)noexcept;
[[nodiscard]] b8 AreTypeRefsEquivalentWithSubst(
    const TypeRef* actual,
    const TypeRef* pattern,
    const MVector<TemplateParamDecl>* ifaceParams,
    const MVector<TypeArg>* ifaceArgs,
    const SymbolTable& symbols
)noexcept;
[[nodiscard]] b8 AreTypeRefsEquivalent(const TypeRef* a, const TypeRef* b, const SymbolTable& symbols)noexcept;
[[nodiscard]] b8 AreTemplateArgListsEqual(const MVector<TypeArg>& a, const MVector<TypeArg>& b, const SymbolTable& symbols)noexcept;
[[nodiscard]] b8 DoParamListsMatch(const MVector<ParamDecl>& a, const MVector<ParamDecl>& b, const SymbolTable& symbols)noexcept;

[[nodiscard]] TypeRef* ExtractUnderlyingFuncType(TypeRef* type, const SymbolTable& symbols)noexcept;
[[nodiscard]] b8 HasConstValueTemplateParam(const ClassDecl* ownerClass, const MethodDecl* method, MStringView name)noexcept;
[[nodiscard]] b8 NormalizeConstantTemplateArg(
    TypeArg& arg,
    const ClassDecl* enclosingClass,
    const MethodDecl* enclosingMethod,
    const SymbolTable& symbols
)noexcept;

[[nodiscard]] usize ParseUnsignedConstant(MStringView text, const SymbolTable& symbols)noexcept;
[[nodiscard]] b8 IsKnownConcreteTypeName(MStringView name, const SymbolTable& symbols)noexcept;
[[nodiscard]] b8 IsConcreteTypeRef(const TypeRef& type, const SymbolTable& symbols)noexcept;
[[nodiscard]] b8 IsConcreteTemplateArgList(
    const MVector<TemplateParamDecl>& params,
    const MVector<TypeArg>& args,
    const SymbolTable& symbols
)noexcept;
[[nodiscard]] b8 TypeContainsArenaBackedContainer(const TypeRef& type, const SymbolTable& symbols, u32 depth = 0u)noexcept;
[[nodiscard]] i32 ScoreCallableParams(
    const SymbolTable& symbols,
    const MVector<ParamDecl>& params,
    const MVector<MStringView>* argTypeNames,
    const MVector<b8>* argIsMut
)noexcept;


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_ADHD_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

