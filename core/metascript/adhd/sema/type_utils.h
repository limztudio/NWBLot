// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#pragma once


#include "symbol_table.h"


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_ADHD_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


[[nodiscard]] constexpr b8 IsIntegerScalarType(const MStringView typeName)noexcept{
    return typeName == "i8" || typeName == "u8"
        || typeName == "i16" || typeName == "u16"
        || typeName == "i32" || typeName == "u32"
        || typeName == "i64" || typeName == "u64"
        || typeName == "isize" || typeName == "usize";
}

[[nodiscard]] constexpr b8 IsFloatScalarType(const MStringView typeName)noexcept{
    return typeName == "f32" || typeName == "f64";
}

[[nodiscard]] constexpr MStringView MapTokenTypeToOperatorSymbol(const TokenType::Enum op)noexcept{
    switch(op){
    case TokenType::Plus: return "+";
    case TokenType::Minus: return "-";
    case TokenType::Star: return "*";
    case TokenType::Slash: return "/";
    case TokenType::Percent: return "%";
    case TokenType::Ampersand: return "&";
    case TokenType::Pipe: return "|";
    case TokenType::Caret: return "^";
    case TokenType::Tilde: return "~";
    case TokenType::Bang: return "!";
    case TokenType::LessLess: return "<<";
    case TokenType::GreaterGreater: return ">>";
    case TokenType::EqualEqual: return "==";
    case TokenType::BangEqual: return "!=";
    case TokenType::Less: return "<";
    case TokenType::LessEqual: return "<=";
    case TokenType::Greater: return ">";
    case TokenType::GreaterEqual: return ">=";
    case TokenType::PlusEqual: return "+=";
    case TokenType::MinusEqual: return "-=";
    case TokenType::StarEqual: return "*=";
    case TokenType::SlashEqual: return "/=";
    case TokenType::PercentEqual: return "%=";
    case TokenType::AmpersandEqual: return "&=";
    case TokenType::PipeEqual: return "|=";
    case TokenType::CaretEqual: return "^=";
    case TokenType::LessLessEqual: return "<<=";
    case TokenType::GreaterGreaterEqual: return ">>=";
    case TokenType::PlusPlus: return "++";
    case TokenType::MinusMinus: return "--";
    default: return {};
    }
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


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_ADHD_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

