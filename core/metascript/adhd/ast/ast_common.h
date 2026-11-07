// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#pragma once


#include <core/metascript/attribute_parser.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#define NWB_ADHD_BEGIN NWB_METASCRIPT_BEGIN namespace Adhd{
#define NWB_ADHD_END }; NWB_METASCRIPT_END
#define NWB_ADHD_COMPILER_VERSION "1.0.0"


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_ADHD_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


struct SourceSpan{
    u32 line = 1;
    u32 column = 1;
};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace AccessSpecifier{
    enum Enum : u8{
        None,
#define NWB_ADHD_EXPORT_ACCESS_SPECIFIER(adhdKeyword, enumName, cppLabelPrefix, maybeUnusedOnField) \
        enumName,
#include "../export_keywords.h"
#undef NWB_ADHD_EXPORT_ACCESS_SPECIFIER
    };
};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace ParamPassingMode{
    enum Enum : u8{
        None,
#define NWB_ADHD_EXPORT_PARAM_PASSING_MODE(adhdKeyword, enumName) \
        enumName,
#include "../export_keywords.h"
#undef NWB_ADHD_EXPORT_PARAM_PASSING_MODE
    };
};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace ParallelCoreKind{
    enum Enum : u8{
#define NWB_ADHD_EXPORT_PARALLEL_CORE(adhdName, enumName, cppCostEnum, cppTargetEnum) \
        enumName,
#include "../export_keywords.h"
#undef NWB_ADHD_EXPORT_PARALLEL_CORE
    };
};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace ParallelPriorityKind{
    enum Enum : u8{
#define NWB_ADHD_EXPORT_PARALLEL_PRIORITY(adhdName, enumName, cppPriorityEnum) \
        enumName,
#include "../export_keywords.h"
#undef NWB_ADHD_EXPORT_PARALLEL_PRIORITY
    };
};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace DiagnosticKind{
    enum Enum : u8{
#define NWB_ADHD_EXPORT_DIAGNOSTIC_KIND(adhdKeyword, enumName, defaultMacroPrefix, essentialMacroPrefix, defaultMsgMacroPrefix, essentialMsgMacroPrefix) \
        enumName,
#include "../export_keywords.h"
#undef NWB_ADHD_EXPORT_DIAGNOSTIC_KIND
    };
};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


template<typename T>
[[nodiscard]] T* AllocateAstNode(MetaArena& arena){
    void* mem = arena.allocate(alignof(T), sizeof(T));
    return ::new(mem) T(arena);
}

[[nodiscard]] constexpr b8 HasAdhdExtension(const MStringView path)noexcept{
    return path.size() > 5u && path.substr(path.size() - 5u) == ".adhd";
}

[[nodiscard]] constexpr MStringView StripAdhdExtension(const MStringView path)noexcept{
    if(HasAdhdExtension(path))
        return path.substr(0u, path.size() - 5u);
    return path;
}

[[nodiscard]] constexpr MStringView ExtractAdhdFileStem(const MStringView path)noexcept{
    usize fileStart = GlobalFilesystemPathDetail::RootNameLength(path);
    for(usize i = fileStart; i < path.size(); ++i){
        if(GlobalFilesystemPathDetail::IsSeparator(path[i]))
            fileStart = i + 1u;
    }
    return StripAdhdExtension(path.substr(fileStart));
}

[[nodiscard]] constexpr AccessSpecifier::Enum ParseAccessSpecifierKeyword(const MStringView text)noexcept{
#define NWB_ADHD_EXPORT_ACCESS_SPECIFIER(adhdKeyword, enumName, cppLabelPrefix, maybeUnusedOnField) \
    if(text == adhdKeyword) return AccessSpecifier::enumName;
#include "../export_keywords.h"
#undef NWB_ADHD_EXPORT_ACCESS_SPECIFIER
    return AccessSpecifier::None;
}

[[nodiscard]] constexpr ParamPassingMode::Enum ParseParamPassingModeKeyword(const MStringView text)noexcept{
#define NWB_ADHD_EXPORT_PARAM_PASSING_MODE(adhdKeyword, enumName) \
    if(text == adhdKeyword) return ParamPassingMode::enumName;
#include "../export_keywords.h"
#undef NWB_ADHD_EXPORT_PARAM_PASSING_MODE
    return ParamPassingMode::None;
}

[[nodiscard]] constexpr b8 IsMethodQualifierKeyword(const MStringView text)noexcept{
#define NWB_ADHD_EXPORT_METHOD_QUALIFIER(adhdKeyword, declField, duplicateErrorMessage) \
    if(text == adhdKeyword) return true;
#include "../export_keywords.h"
#undef NWB_ADHD_EXPORT_METHOD_QUALIFIER
    return false;
}

[[nodiscard]] constexpr b8 IsNestedTypeKeyword(const MStringView text, const b8 classScopeOnly = false)noexcept{
#define NWB_ADHD_EXPORT_NESTED_TYPE_KEYWORD(keyword, isClassNestedKeyword) \
    if((!classScopeOnly || isClassNestedKeyword) && text == keyword) return true;
#include "../export_keywords.h"
#undef NWB_ADHD_EXPORT_NESTED_TYPE_KEYWORD
    return false;
}

[[nodiscard]] constexpr MStringView FindProhibitedTypeKeywordError(const MStringView text)noexcept{
#define NWB_ADHD_EXPORT_PROHIBITED_TYPE_KEYWORD(keyword, errorMessage) \
    if(text == keyword) return errorMessage;
#include "../export_keywords.h"
#undef NWB_ADHD_EXPORT_PROHIBITED_TYPE_KEYWORD
    return {};
}

[[nodiscard]] constexpr MStringView FindProhibitedExprKeywordError(const MStringView text)noexcept{
#define NWB_ADHD_EXPORT_PROHIBITED_EXPR_KEYWORD(keyword, errorMessage) \
    if(text == keyword) return errorMessage;
#include "../export_keywords.h"
#undef NWB_ADHD_EXPORT_PROHIBITED_EXPR_KEYWORD
    return {};
}

[[nodiscard]] constexpr MStringView FindProhibitedStmtKeywordError(const MStringView text)noexcept{
#define NWB_ADHD_EXPORT_PROHIBITED_STMT_KEYWORD(keyword, errorMessage) \
    if(text == keyword) return errorMessage;
#include "../export_keywords.h"
#undef NWB_ADHD_EXPORT_PROHIBITED_STMT_KEYWORD
    return {};
}

[[nodiscard]] constexpr MStringView FindProhibitedTopLevelKeywordError(const MStringView text)noexcept{
#define NWB_ADHD_EXPORT_PROHIBITED_TOP_LEVEL_KEYWORD(keyword, errorMessage) \
    if(text == keyword) return errorMessage;
#include "../export_keywords.h"
#undef NWB_ADHD_EXPORT_PROHIBITED_TOP_LEVEL_KEYWORD
    return {};
}

[[nodiscard]] constexpr MStringView FindProhibitedMemberKeywordError(const MStringView text, const b8 isInterface)noexcept{
#define NWB_ADHD_EXPORT_PROHIBITED_MEMBER_KEYWORD(keyword, classErrorMessage, interfaceErrorMessage) \
    if(text == keyword) return isInterface ? MStringView(interfaceErrorMessage) : MStringView(classErrorMessage);
#include "../export_keywords.h"
#undef NWB_ADHD_EXPORT_PROHIBITED_MEMBER_KEYWORD
    return {};
}

[[nodiscard]] constexpr MStringView FindProhibitedTemplateParamKeywordError(const MStringView text)noexcept{
#define NWB_ADHD_EXPORT_PROHIBITED_TEMPLATE_PARAM_KEYWORD(keyword, errorMessage) \
    if(text == keyword) return errorMessage;
#include "../export_keywords.h"
#undef NWB_ADHD_EXPORT_PROHIBITED_TEMPLATE_PARAM_KEYWORD
    return {};
}

[[nodiscard]] constexpr b8 TryParseParallelCoreKind(const MStringView text, ParallelCoreKind::Enum& outCore)noexcept{
#define NWB_ADHD_EXPORT_PARALLEL_CORE(adhdName, enumName, cppCostEnum, cppTargetEnum) \
    if(text == adhdName){ outCore = ParallelCoreKind::enumName; return true; }
#include "../export_keywords.h"
#undef NWB_ADHD_EXPORT_PARALLEL_CORE
    return false;
}

[[nodiscard]] constexpr b8 TryParseParallelPriorityKind(const MStringView text, ParallelPriorityKind::Enum& outPriority)noexcept{
#define NWB_ADHD_EXPORT_PARALLEL_PRIORITY(adhdName, enumName, cppPriorityEnum) \
    if(text == adhdName){ outPriority = ParallelPriorityKind::enumName; return true; }
#include "../export_keywords.h"
#undef NWB_ADHD_EXPORT_PARALLEL_PRIORITY
    return false;
}

[[nodiscard]] constexpr b8 TryParseDiagnosticKind(const MStringView text, DiagnosticKind::Enum& outKind)noexcept{
#define NWB_ADHD_EXPORT_DIAGNOSTIC_KIND(adhdKeyword, enumName, defaultMacroPrefix, essentialMacroPrefix, defaultMsgMacroPrefix, essentialMsgMacroPrefix) \
    if(text == adhdKeyword){ outKind = DiagnosticKind::enumName; return true; }
#include "../export_keywords.h"
#undef NWB_ADHD_EXPORT_DIAGNOSTIC_KIND
    return false;
}

struct BuiltinScalarTypeInfo{
    MStringView adhdName;
    u32 byteSize;
    u32 byteAlign;
    b8 isInteger;
    b8 isFixedWidthInt;
    b8 isSigned;
    b8 isFloat;
};

inline constexpr Array s_BuiltinScalarTypes = {
#define NWB_ADHD_EXPORT_SCALAR_TYPE(adhdName, cppType, byteSize, byteAlign, isInteger, isFixedWidthInt, isSigned, isFloat) \
    BuiltinScalarTypeInfo{ adhdName, byteSize, byteAlign, isInteger, isFixedWidthInt, isSigned, isFloat },
#include "../export_types.h"
#undef NWB_ADHD_EXPORT_SCALAR_TYPE
};

[[nodiscard]] constexpr const BuiltinScalarTypeInfo* FindBuiltinScalarType(const MStringView name)noexcept{
    for(const BuiltinScalarTypeInfo& info : s_BuiltinScalarTypes){
        if(info.adhdName == name)
            return &info;
    }
    return nullptr;
}

[[nodiscard]] constexpr b8 IsScalarType(const MStringView name)noexcept{
    return FindBuiltinScalarType(name) != nullptr;
}

[[nodiscard]] constexpr b8 IsIntegerScalarType(const MStringView name)noexcept{
    const BuiltinScalarTypeInfo* info = FindBuiltinScalarType(name);
    return info && info->isInteger;
}

[[nodiscard]] constexpr b8 IsFloatScalarType(const MStringView name)noexcept{
    const BuiltinScalarTypeInfo* info = FindBuiltinScalarType(name);
    return info && info->isFloat;
}

[[nodiscard]] constexpr b8 IsNumericScalarType(const MStringView name)noexcept{
    const BuiltinScalarTypeInfo* info = FindBuiltinScalarType(name);
    return info && (info->isInteger || info->isFloat);
}

[[nodiscard]] constexpr b8 IsIntegralOrBoolScalarType(const MStringView name)noexcept{
    const BuiltinScalarTypeInfo* info = FindBuiltinScalarType(name);
    return info && !info->isFloat;
}

[[nodiscard]] constexpr b8 IsBoolScalarType(const MStringView name)noexcept{
    const BuiltinScalarTypeInfo* info = FindBuiltinScalarType(name);
    return info && !info->isInteger && !info->isFloat;
}

[[nodiscard]] constexpr b8 TryParseBoolLiteral(const MStringView text, i64* outVal = nullptr)noexcept{
#define NWB_ADHD_EXPORT_BOOL_LITERAL(adhdKeyword, intValue) \
    if(text == adhdKeyword){ \
        if(outVal) *outVal = intValue; \
        return true; \
    }
#include "../export_keywords.h"
#undef NWB_ADHD_EXPORT_BOOL_LITERAL
    return false;
}

[[nodiscard]] constexpr b8 IsBoolLiteralText(const MStringView text)noexcept{
    return TryParseBoolLiteral(text);
}

[[nodiscard]] constexpr b8 TryParseEnumDeclKeyword(const MStringView text, b8* outIsFlag = nullptr)noexcept{
#define NWB_ADHD_EXPORT_ENUM_DECL_KEYWORD(adhdKeyword, isFlag) \
    if(text == adhdKeyword){ \
        if(outIsFlag) *outIsFlag = isFlag; \
        return true; \
    }
#include "../export_keywords.h"
#undef NWB_ADHD_EXPORT_ENUM_DECL_KEYWORD
    return false;
}

[[nodiscard]] constexpr b8 TryMapScalarLimitMember(const MStringView member, MStringView* outCppLimitSuffix = nullptr, b8* outIsMin = nullptr)noexcept{
#define NWB_ADHD_EXPORT_SCALAR_LIMIT(memberName, cppLimitSuffix, isMin) \
    if(member == memberName){ \
        if(outCppLimitSuffix) *outCppLimitSuffix = cppLimitSuffix; \
        if(outIsMin) *outIsMin = isMin; \
        return true; \
    }
#include "../export_types.h"
#undef NWB_ADHD_EXPORT_SCALAR_LIMIT
    return false;
}

[[nodiscard]] constexpr b8 ResolveFixedWidthIntScalar(const MStringView name, usize& outSize, usize& outAlign, b8* outIsSigned = nullptr)noexcept{
    const BuiltinScalarTypeInfo* info = FindBuiltinScalarType(name);
    if(!info || !info->isFixedWidthInt)
        return false;
    outSize = info->byteSize;
    outAlign = info->byteAlign;
    if(outIsSigned)
        *outIsSigned = info->isSigned;
    return true;
}

[[nodiscard]] constexpr b8 IsBuiltinTypeName(const MStringView name)noexcept{
    if(name == "void" || name == "Func" || IsScalarType(name))
        return true;
#define NWB_ADHD_EXPORT_SIMD_VECTOR_TYPE(adhdName, cppExportName, lanes, altCtorArity, byteSize, byteAlign, xyzScalarType, wScalarType, isFloatReg, isDoubleReg, isIntReg, isSignedInt, isMixed, regCppType, loadCppFunc, storeCppFunc) \
    if(name == adhdName) return true;
#define NWB_ADHD_EXPORT_SIMD_MATRIX_TYPE(adhdName, cppExportName, elemCount, byteSize, byteAlign, elemScalarType, rowVectorType, isDoubleReg, regCppType, loadCppFunc, storeCppFunc) \
    if(name == adhdName) return true;
#define NWB_ADHD_EXPORT_UNALIGNED_VECTOR_TYPE(adhdName, cppExportName, lanes, altCtorArity, byteSize, byteAlign, laneScalarType, isHalfFloat) \
    if(name == adhdName) return true;
#define NWB_ADHD_EXPORT_OBJECT_TYPE(adhdName, cppExportName, byteSize, byteAlign, isArenaBacked, isSelfDefaulted, isSmallValue, isBorrowView, isBinarySerializable, isSingleValueCtor, emitsBraceFieldInit, isStringLike, isStringSwitch, isNonThreadSafeContainer, isNwbMapKey, requiresTCharAdaptation) \
    if(name == adhdName) return true;
#define NWB_ADHD_EXPORT_GENERIC_TYPE(adhdName, cppExportName, paramKind, inlineLayoutKind, byteSize, byteAlign, isArenaBacked, isSelfDefaulted, isSmallValue, isBorrowView, isBinarySerializable, isSingleValueCtor, emitsBraceFieldInit, isNonThreadSafeContainer, isNonRandomAccessContainer, isIndexableSequence, reflectedLinearKind, isReflectedMap, isAssetHandle) \
    if(name == adhdName) return true;
#include "../export_types.h"
#undef NWB_ADHD_EXPORT_GENERIC_TYPE
#undef NWB_ADHD_EXPORT_OBJECT_TYPE
#undef NWB_ADHD_EXPORT_UNALIGNED_VECTOR_TYPE
#undef NWB_ADHD_EXPORT_SIMD_MATRIX_TYPE
#undef NWB_ADHD_EXPORT_SIMD_VECTOR_TYPE
    return false;
}

[[nodiscard]] constexpr b8 IsOverloadableOperatorToken(const TokenType::Enum op)noexcept{
#define NWB_ADHD_EXPORT_OPERATOR_TOKEN(tokenType, symbol, isOverloadable, isAssignment, returnsBool, isBitwiseFlagOp, isPrefixUnary, isIncDec, binaryPrecedence, minMemberParams, maxMemberParams, prohibitsUnaryOverload) \
    if constexpr(isOverloadable){ \
        if(op == tokenType) return true; \
    }
#include "../export_operators.h"
#undef NWB_ADHD_EXPORT_OPERATOR_TOKEN
    return false;
}

[[nodiscard]] constexpr b8 IsPrefixUnaryOperatorToken(const TokenType::Enum op)noexcept{
#define NWB_ADHD_EXPORT_OPERATOR_TOKEN(tokenType, symbol, isOverloadable, isAssignment, returnsBool, isBitwiseFlagOp, isPrefixUnary, isIncDec, binaryPrecedence, minMemberParams, maxMemberParams, prohibitsUnaryOverload) \
    if constexpr(isPrefixUnary){ \
        if(op == tokenType) return true; \
    }
#include "../export_operators.h"
#undef NWB_ADHD_EXPORT_OPERATOR_TOKEN
    return false;
}

[[nodiscard]] constexpr b8 IsIncrementOrDecrementOperatorToken(const TokenType::Enum op)noexcept{
#define NWB_ADHD_EXPORT_OPERATOR_TOKEN(tokenType, symbol, isOverloadable, isAssignment, returnsBool, isBitwiseFlagOp, isPrefixUnary, isIncDec, binaryPrecedence, minMemberParams, maxMemberParams, prohibitsUnaryOverload) \
    if constexpr(isIncDec){ \
        if(op == tokenType) return true; \
    }
#include "../export_operators.h"
#undef NWB_ADHD_EXPORT_OPERATOR_TOKEN
    return false;
}

[[nodiscard]] constexpr u8 BinaryOperatorPrecedence(const TokenType::Enum op)noexcept{
#define NWB_ADHD_EXPORT_OPERATOR_TOKEN(tokenType, symbol, isOverloadable, isAssignment, returnsBool, isBitwiseFlagOp, isPrefixUnary, isIncDec, binaryPrecedence, minMemberParams, maxMemberParams, prohibitsUnaryOverload) \
    if constexpr(binaryPrecedence > 0u){ \
        if(op == tokenType) return binaryPrecedence; \
    }
#include "../export_operators.h"
#undef NWB_ADHD_EXPORT_OPERATOR_TOKEN
    return 0u;
}

[[nodiscard]] constexpr MStringView FindProhibitedUnaryOperatorError(const TokenType::Enum op)noexcept{
#define NWB_ADHD_EXPORT_PROHIBITED_UNARY_OP(tokenType, errorMessage) \
    if(op == tokenType) return errorMessage;
#include "../export_operators.h"
#undef NWB_ADHD_EXPORT_PROHIBITED_UNARY_OP
    return {};
}

[[nodiscard]] constexpr MStringView FindProhibitedOperatorOverloadError(const TokenType::Enum op)noexcept{
#define NWB_ADHD_EXPORT_PROHIBITED_OPERATOR_OVERLOAD(tokenType, errorMessage) \
    if(op == tokenType) return errorMessage;
#include "../export_operators.h"
#undef NWB_ADHD_EXPORT_PROHIBITED_OPERATOR_OVERLOAD
    return {};
}

[[nodiscard]] constexpr b8 TryGetMemberOperatorArityRules(const MStringView opSym, usize& outMinParams, usize& outMaxParams, b8& outProhibitsUnary)noexcept{
    if(opSym == "[]"){
        outMinParams = 1u;
        outMaxParams = 1u;
        outProhibitsUnary = false;
        return true;
    }
#define NWB_ADHD_EXPORT_OPERATOR_TOKEN(tokenType, symbol, isOverloadable, isAssignment, returnsBool, isBitwiseFlagOp, isPrefixUnary, isIncDec, binaryPrecedence, minMemberParams, maxMemberParams, prohibitsUnaryOverload) \
    if constexpr(isOverloadable){ \
        if(opSym == symbol){ \
            outMinParams = minMemberParams; \
            outMaxParams = maxMemberParams; \
            outProhibitsUnary = prohibitsUnaryOverload; \
            return true; \
        } \
    }
#include "../export_operators.h"
#undef NWB_ADHD_EXPORT_OPERATOR_TOKEN
    return false;
}

[[nodiscard]] constexpr b8 IsCompoundOrSimpleAssignment(const TokenType::Enum op)noexcept{
#define NWB_ADHD_EXPORT_OPERATOR_TOKEN(tokenType, symbol, isOverloadable, isAssignment, returnsBool, isBitwiseFlagOp, isPrefixUnary, isIncDec, binaryPrecedence, minMemberParams, maxMemberParams, prohibitsUnaryOverload) \
    if constexpr(isAssignment){ \
        if(op == tokenType) return true; \
    }
#include "../export_operators.h"
#undef NWB_ADHD_EXPORT_OPERATOR_TOKEN
    return false;
}

[[nodiscard]] constexpr b8 IsBoolReturnOperatorToken(const TokenType::Enum op)noexcept{
#define NWB_ADHD_EXPORT_OPERATOR_TOKEN(tokenType, symbol, isOverloadable, isAssignment, returnsBool, isBitwiseFlagOp, isPrefixUnary, isIncDec, binaryPrecedence, minMemberParams, maxMemberParams, prohibitsUnaryOverload) \
    if constexpr(returnsBool){ \
        if(op == tokenType) return true; \
    }
#include "../export_operators.h"
#undef NWB_ADHD_EXPORT_OPERATOR_TOKEN
    return false;
}

[[nodiscard]] constexpr b8 IsBoolReturnOperatorSymbol(const MStringView sym)noexcept{
#define NWB_ADHD_EXPORT_OPERATOR_TOKEN(tokenType, symbol, isOverloadable, isAssignment, returnsBool, isBitwiseFlagOp, isPrefixUnary, isIncDec, binaryPrecedence, minMemberParams, maxMemberParams, prohibitsUnaryOverload) \
    if constexpr(returnsBool){ \
        if(sym == symbol) return true; \
    }
#include "../export_operators.h"
#undef NWB_ADHD_EXPORT_OPERATOR_TOKEN
    return false;
}

[[nodiscard]] constexpr b8 IsBitwiseFlagOperatorToken(const TokenType::Enum op)noexcept{
#define NWB_ADHD_EXPORT_OPERATOR_TOKEN(tokenType, symbol, isOverloadable, isAssignment, returnsBool, isBitwiseFlagOp, isPrefixUnary, isIncDec, binaryPrecedence, minMemberParams, maxMemberParams, prohibitsUnaryOverload) \
    if constexpr(isBitwiseFlagOp){ \
        if(op == tokenType) return true; \
    }
#include "../export_operators.h"
#undef NWB_ADHD_EXPORT_OPERATOR_TOKEN
    return false;
}

[[nodiscard]] constexpr b8 IsArithmeticOrShiftAssignOperatorToken(const TokenType::Enum op)noexcept{
#define NWB_ADHD_EXPORT_OPERATOR_TOKEN(tokenType, symbol, isOverloadable, isAssignment, returnsBool, isBitwiseFlagOp, isPrefixUnary, isIncDec, binaryPrecedence, minMemberParams, maxMemberParams, prohibitsUnaryOverload) \
    if constexpr(isAssignment && !isBitwiseFlagOp && tokenType != TokenType::Equal){ \
        if(op == tokenType) return true; \
    }
#include "../export_operators.h"
#undef NWB_ADHD_EXPORT_OPERATOR_TOKEN
    return false;
}

[[nodiscard]] constexpr MStringView MapTokenTypeToOperatorSymbol(const TokenType::Enum op)noexcept{
#define NWB_ADHD_EXPORT_OPERATOR_TOKEN(tokenType, symbol, isOverloadable, isAssignment, returnsBool, isBitwiseFlagOp, isPrefixUnary, isIncDec, binaryPrecedence, minMemberParams, maxMemberParams, prohibitsUnaryOverload) \
    if(op == tokenType) return symbol;
#include "../export_operators.h"
#undef NWB_ADHD_EXPORT_OPERATOR_TOKEN
    return {};
}

[[nodiscard]] constexpr TokenType::Enum MapOperatorSymbolToTokenType(const MStringView sym)noexcept{
#define NWB_ADHD_EXPORT_OPERATOR_TOKEN(tokenType, symbol, isOverloadable, isAssignment, returnsBool, isBitwiseFlagOp, isPrefixUnary, isIncDec, binaryPrecedence, minMemberParams, maxMemberParams, prohibitsUnaryOverload) \
    if(sym == symbol) return tokenType;
#include "../export_operators.h"
#undef NWB_ADHD_EXPORT_OPERATOR_TOKEN
    return TokenType::EndOfFile;
}

[[nodiscard]] constexpr b8 TryMapSwizzleCharToLane(const MChar c, u32& outLane, b8* outIsRgba = nullptr)noexcept{
#define NWB_ADHD_EXPORT_SWIZZLE_COMPONENT(xyzwChar, rgbaChar, laneIndex, cppLaneCallSuffix) \
    if(c == xyzwChar || c == rgbaChar){ \
        outLane = laneIndex; \
        if(outIsRgba) \
            *outIsRgba = (c == rgbaChar); \
        return true; \
    }
#include "../export_types.h"
#undef NWB_ADHD_EXPORT_SWIZZLE_COMPONENT
    return false;
}

inline constexpr MStringView s_ProhibitedCStyleArrayError = "C-style arrays ('T name[N]') are prohibited in adhd; use 'Array<T, N>' or 'Vector<T>'";

inline void ReportAdhdDiagnostic(MetaArena& arena, MVector<ParseError>& errors, const SourceSpan span, const MStringView message){
    errors.push_back(ParseError{span.line, span.column, MString(message.data(), message.size(), arena)});
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_ADHD_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

