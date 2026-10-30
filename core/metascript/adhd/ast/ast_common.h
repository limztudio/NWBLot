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
        Public,
        Private,
        Protected,
    };
};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace ParamPassingMode{
    enum Enum : u8{
        None,
        In,
        Mut,
    };
};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace ParallelCoreKind{
    enum Enum : u8{
        Any,
        Heavy,
        Light,
        MainThread,
    };
};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace ParallelPriorityKind{
    enum Enum : u8{
        Normal,
        Critical,
        Background,
    };
};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace DiagnosticKind{
    enum Enum : u8{
        Assert,
        Info,
        Warning,
        Error,
    };
};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


template<typename T>
[[nodiscard]] T* AllocateAstNode(MetaArena& arena){
    void* mem = arena.allocate(alignof(T), sizeof(T));
    return ::new(mem) T(arena);
}

inline void AppendAdhdUnsigned(MString& out, usize value){
    char buf[32];
    usize idx = 0u;
    if(value == 0u){
        out.push_back('0');
        return;
    }
    while(value > 0u && idx < sizeof(buf)){
        buf[idx++] = static_cast<char>('0' + (value % 10u));
        value /= 10u;
    }
    while(idx > 0u)
        out.push_back(buf[--idx]);
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
    usize fileStart = 0u;
    for(usize i = 0u; i < path.size(); ++i){
        if(path[i] == '/' || path[i] == '\\')
            fileStart = i + 1u;
    }
    return StripAdhdExtension(path.substr(fileStart));
}

[[nodiscard]] constexpr AccessSpecifier::Enum ParseAccessSpecifierKeyword(const MStringView text)noexcept{
#define NWB_ADHD_EXPORT_ACCESS_SPECIFIER(adhdKeyword, astEnum) \
    if(text == adhdKeyword) return astEnum;
#include "../export_keywords.h"
#undef NWB_ADHD_EXPORT_ACCESS_SPECIFIER
    return AccessSpecifier::None;
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

[[nodiscard]] constexpr b8 TryParseParallelCoreKind(const MStringView text, ParallelCoreKind::Enum& outCore)noexcept{
#define NWB_ADHD_EXPORT_PARALLEL_CORE(adhdName, astEnum, cppCostEnum, cppTargetEnum) \
    if(text == adhdName){ outCore = astEnum; return true; }
#include "../export_keywords.h"
#undef NWB_ADHD_EXPORT_PARALLEL_CORE
    return false;
}

[[nodiscard]] constexpr b8 TryParseParallelPriorityKind(const MStringView text, ParallelPriorityKind::Enum& outPriority)noexcept{
#define NWB_ADHD_EXPORT_PARALLEL_PRIORITY(adhdName, astEnum, cppPriorityEnum) \
    if(text == adhdName){ outPriority = astEnum; return true; }
#include "../export_keywords.h"
#undef NWB_ADHD_EXPORT_PARALLEL_PRIORITY
    return false;
}

[[nodiscard]] constexpr b8 TryParseDiagnosticKind(const MStringView text, DiagnosticKind::Enum& outKind)noexcept{
#define NWB_ADHD_EXPORT_DIAGNOSTIC_KIND(adhdKeyword, astEnum, defaultMacroPrefix, essentialMacroPrefix, defaultMsgMacroPrefix, essentialMsgMacroPrefix) \
    if(text == adhdKeyword){ outKind = astEnum; return true; }
#include "../export_keywords.h"
#undef NWB_ADHD_EXPORT_DIAGNOSTIC_KIND
    return false;
}

[[nodiscard]] constexpr b8 IsScalarType(const MStringView name)noexcept{
#define NWB_ADHD_EXPORT_SCALAR_TYPE(adhdName, byteSize, byteAlign, isInteger, isFixedWidthInt, isSigned, isFloat) \
    if(name == adhdName) return true;
#include "../export_types.h"
#undef NWB_ADHD_EXPORT_SCALAR_TYPE
    return false;
}

[[nodiscard]] constexpr b8 IsIntegerScalarType(const MStringView name)noexcept{
#define NWB_ADHD_EXPORT_SCALAR_TYPE(adhdName, byteSize, byteAlign, isInteger, isFixedWidthInt, isSigned, isFloat) \
    if constexpr(isInteger){ \
        if(name == adhdName) return true; \
    }
#include "../export_types.h"
#undef NWB_ADHD_EXPORT_SCALAR_TYPE
    return false;
}

[[nodiscard]] constexpr b8 IsFloatScalarType(const MStringView name)noexcept{
#define NWB_ADHD_EXPORT_SCALAR_TYPE(adhdName, byteSize, byteAlign, isInteger, isFixedWidthInt, isSigned, isFloat) \
    if constexpr(isFloat){ \
        if(name == adhdName) return true; \
    }
#include "../export_types.h"
#undef NWB_ADHD_EXPORT_SCALAR_TYPE
    return false;
}

[[nodiscard]] constexpr b8 ResolveFixedWidthIntScalar(const MStringView name, usize& outSize, usize& outAlign, b8* outIsSigned = nullptr)noexcept{
#define NWB_ADHD_EXPORT_SCALAR_TYPE(adhdName, byteSize, byteAlign, isInteger, isFixedWidthInt, isSigned, isFloat) \
    if constexpr(isFixedWidthInt){ \
        if(name == adhdName){ \
            outSize = byteSize; \
            outAlign = byteAlign; \
            if(outIsSigned) *outIsSigned = isSigned; \
            return true; \
        } \
    }
#include "../export_types.h"
#undef NWB_ADHD_EXPORT_SCALAR_TYPE
    return false;
}

[[nodiscard]] constexpr b8 IsBuiltinTypeName(const MStringView name)noexcept{
    if(name == "void" || name == "Func")
        return true;
#define NWB_ADHD_EXPORT_SCALAR_TYPE(adhdName, byteSize, byteAlign, isInteger, isFixedWidthInt, isSigned, isFloat) \
    if(name == adhdName) return true;
#define NWB_ADHD_EXPORT_SIMD_VECTOR_TYPE(adhdName, cppExportName, lanes, byteSize, byteAlign, xyzScalarType, wScalarType, isFloatReg, isDoubleReg, isIntReg, isSignedInt, isMixed, regCppType, loadCppFunc, storeCppFunc) \
    if(name == adhdName) return true;
#define NWB_ADHD_EXPORT_SIMD_MATRIX_TYPE(adhdName, cppExportName, elemCount, byteSize, byteAlign, elemScalarType, isDoubleReg, regCppType, loadCppFunc, storeCppFunc) \
    if(name == adhdName) return true;
#define NWB_ADHD_EXPORT_UNALIGNED_VECTOR_TYPE(adhdName, cppExportName, lanes, byteSize, byteAlign, laneScalarType) \
    if(name == adhdName) return true;
#define NWB_ADHD_EXPORT_OBJECT_TYPE(adhdName, cppExportName, byteSize, byteAlign, isArenaBacked, isSelfDefaulted, isSmallValue, isStringSwitch, isNonThreadSafeContainer, isNwbMapKey) \
    if(name == adhdName) return true;
#define NWB_ADHD_EXPORT_GENERIC_TYPE(adhdName, cppExportName, paramKind, byteSize, byteAlign, isArenaBacked, isSelfDefaulted, isSmallValue, isNonThreadSafeContainer, isNonRandomAccessContainer, isIndexableSequence, isReflectedLinear, isReflectedMap) \
    if(name == adhdName) return true;
#include "../export_types.h"
#undef NWB_ADHD_EXPORT_GENERIC_TYPE
#undef NWB_ADHD_EXPORT_OBJECT_TYPE
#undef NWB_ADHD_EXPORT_UNALIGNED_VECTOR_TYPE
#undef NWB_ADHD_EXPORT_SIMD_MATRIX_TYPE
#undef NWB_ADHD_EXPORT_SIMD_VECTOR_TYPE
#undef NWB_ADHD_EXPORT_SCALAR_TYPE
    return false;
}

[[nodiscard]] constexpr b8 IsOverloadableOperatorToken(const TokenType::Enum op)noexcept{
#define NWB_ADHD_EXPORT_OPERATOR_TOKEN(tokenType, symbol, isOverloadable, isAssignment) \
    if constexpr(isOverloadable){ \
        if(op == tokenType) return true; \
    }
#include "../export_operators.h"
#undef NWB_ADHD_EXPORT_OPERATOR_TOKEN
    return false;
}

[[nodiscard]] constexpr b8 IsCompoundOrSimpleAssignment(const TokenType::Enum op)noexcept{
#define NWB_ADHD_EXPORT_OPERATOR_TOKEN(tokenType, symbol, isOverloadable, isAssignment) \
    if constexpr(isAssignment){ \
        if(op == tokenType) return true; \
    }
#include "../export_operators.h"
#undef NWB_ADHD_EXPORT_OPERATOR_TOKEN
    return false;
}

[[nodiscard]] constexpr MStringView MapTokenTypeToOperatorSymbol(const TokenType::Enum op)noexcept{
#define NWB_ADHD_EXPORT_OPERATOR_TOKEN(tokenType, symbol, isOverloadable, isAssignment) \
    if(op == tokenType) return symbol;
#include "../export_operators.h"
#undef NWB_ADHD_EXPORT_OPERATOR_TOKEN
    return {};
}

[[nodiscard]] constexpr b8 TryMapSwizzleCharToLane(const MChar c, u32& outLane, b8* outIsRgba = nullptr)noexcept{
#define NWB_ADHD_EXPORT_SWIZZLE_COMPONENT(xyzwChar, rgbaChar, laneIndex) \
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

