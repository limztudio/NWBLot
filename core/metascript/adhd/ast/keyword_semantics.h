// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#pragma once


#include "ast_common.h"


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_ADHD_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


class KeywordSemantics final{
public:
    [[nodiscard]] static constexpr AccessSpecifier::Enum ParseAccessSpecifierKeyword(const MStringView text)noexcept{
    #define NWB_ADHD_EXPORT_ACCESS_SPECIFIER(adhdKeyword, enumName, cppLabelPrefix, maybeUnusedOnField) \
        if(text == adhdKeyword) return AccessSpecifier::enumName;
    #include "../export_keywords.h"
    #undef NWB_ADHD_EXPORT_ACCESS_SPECIFIER
        return AccessSpecifier::None;
    }

    [[nodiscard]] static constexpr ParamPassingMode::Enum ParseParamPassingModeKeyword(const MStringView text)noexcept{
    #define NWB_ADHD_EXPORT_PARAM_PASSING_MODE(adhdKeyword, enumName) \
        if(text == adhdKeyword) return ParamPassingMode::enumName;
    #include "../export_keywords.h"
    #undef NWB_ADHD_EXPORT_PARAM_PASSING_MODE
        return ParamPassingMode::None;
    }

    [[nodiscard]] static constexpr b8 IsMethodQualifierKeyword(const MStringView text)noexcept{
    #define NWB_ADHD_EXPORT_METHOD_QUALIFIER(adhdKeyword, declField, duplicateErrorMessage) \
        if(text == adhdKeyword) return true;
    #include "../export_keywords.h"
    #undef NWB_ADHD_EXPORT_METHOD_QUALIFIER
        return false;
    }

    [[nodiscard]] static constexpr b8 IsNestedTypeKeyword(const MStringView text, const b8 classScopeOnly = false)noexcept{
    #define NWB_ADHD_EXPORT_NESTED_TYPE_KEYWORD(keyword, isClassNestedKeyword) \
        if((!classScopeOnly || isClassNestedKeyword) && text == keyword) return true;
    #include "../export_keywords.h"
    #undef NWB_ADHD_EXPORT_NESTED_TYPE_KEYWORD
        return false;
    }

    [[nodiscard]] static constexpr MStringView FindProhibitedTypeKeywordError(const MStringView text)noexcept{
    #define NWB_ADHD_EXPORT_PROHIBITED_TYPE_KEYWORD(keyword, errorMessage) \
        if(text == keyword) return errorMessage;
    #include "../export_keywords.h"
    #undef NWB_ADHD_EXPORT_PROHIBITED_TYPE_KEYWORD
        return {};
    }

    [[nodiscard]] static constexpr MStringView FindProhibitedExprKeywordError(const MStringView text)noexcept{
    #define NWB_ADHD_EXPORT_PROHIBITED_EXPR_KEYWORD(keyword, errorMessage) \
        if(text == keyword) return errorMessage;
    #include "../export_keywords.h"
    #undef NWB_ADHD_EXPORT_PROHIBITED_EXPR_KEYWORD
        return {};
    }

    [[nodiscard]] static constexpr MStringView FindProhibitedStmtKeywordError(const MStringView text)noexcept{
    #define NWB_ADHD_EXPORT_PROHIBITED_STMT_KEYWORD(keyword, errorMessage) \
        if(text == keyword) return errorMessage;
    #include "../export_keywords.h"
    #undef NWB_ADHD_EXPORT_PROHIBITED_STMT_KEYWORD
        return {};
    }

    [[nodiscard]] static constexpr MStringView FindProhibitedTopLevelKeywordError(const MStringView text)noexcept{
    #define NWB_ADHD_EXPORT_PROHIBITED_TOP_LEVEL_KEYWORD(keyword, errorMessage) \
        if(text == keyword) return errorMessage;
    #include "../export_keywords.h"
    #undef NWB_ADHD_EXPORT_PROHIBITED_TOP_LEVEL_KEYWORD
        return {};
    }

    [[nodiscard]] static constexpr MStringView FindProhibitedMemberKeywordError(const MStringView text, const b8 isInterface)noexcept{
    #define NWB_ADHD_EXPORT_PROHIBITED_MEMBER_KEYWORD(keyword, classErrorMessage, interfaceErrorMessage) \
        if(text == keyword) return isInterface ? MStringView(interfaceErrorMessage) : MStringView(classErrorMessage);
    #include "../export_keywords.h"
    #undef NWB_ADHD_EXPORT_PROHIBITED_MEMBER_KEYWORD
        return {};
    }

    [[nodiscard]] static constexpr MStringView FindProhibitedTemplateParamKeywordError(const MStringView text)noexcept{
    #define NWB_ADHD_EXPORT_PROHIBITED_TEMPLATE_PARAM_KEYWORD(keyword, errorMessage) \
        if(text == keyword) return errorMessage;
    #include "../export_keywords.h"
    #undef NWB_ADHD_EXPORT_PROHIBITED_TEMPLATE_PARAM_KEYWORD
        return {};
    }

    [[nodiscard]] static constexpr b8 TryParseParallelCoreKind(const MStringView text, ParallelCoreKind::Enum& outCore)noexcept{
    #define NWB_ADHD_EXPORT_PARALLEL_CORE(adhdName, enumName, cppCostEnum, cppTargetEnum) \
        if(text == adhdName){ outCore = ParallelCoreKind::enumName; return true; }
    #include "../export_keywords.h"
    #undef NWB_ADHD_EXPORT_PARALLEL_CORE
        return false;
    }

    [[nodiscard]] static constexpr b8 TryParseParallelPriorityKind(const MStringView text, ParallelPriorityKind::Enum& outPriority)noexcept{
    #define NWB_ADHD_EXPORT_PARALLEL_PRIORITY(adhdName, enumName, cppPriorityEnum) \
        if(text == adhdName){ outPriority = ParallelPriorityKind::enumName; return true; }
    #include "../export_keywords.h"
    #undef NWB_ADHD_EXPORT_PARALLEL_PRIORITY
        return false;
    }

    [[nodiscard]] static constexpr b8 TryParseDiagnosticKind(const MStringView text, DiagnosticKind::Enum& outKind)noexcept{
    #define NWB_ADHD_EXPORT_DIAGNOSTIC_KIND(adhdKeyword, enumName, defaultMacroPrefix, essentialMacroPrefix, defaultMsgMacroPrefix, essentialMsgMacroPrefix) \
        if(text == adhdKeyword){ outKind = DiagnosticKind::enumName; return true; }
    #include "../export_keywords.h"
    #undef NWB_ADHD_EXPORT_DIAGNOSTIC_KIND
        return false;
    }

    [[nodiscard]] static constexpr b8 TryParseBoolLiteral(const MStringView text, i64* outVal = nullptr)noexcept{
    #define NWB_ADHD_EXPORT_BOOL_LITERAL(adhdKeyword, intValue) \
        if(text == adhdKeyword){ \
            if(outVal) *outVal = intValue; \
            return true; \
        }
    #include "../export_keywords.h"
    #undef NWB_ADHD_EXPORT_BOOL_LITERAL
        return false;
    }

    [[nodiscard]] static constexpr b8 IsBoolLiteralText(const MStringView text)noexcept{
        return TryParseBoolLiteral(text);
    }

    [[nodiscard]] static constexpr MStringView FindProhibitedNativeAttributeError(const MStringView text)noexcept{
    #define NWB_ADHD_EXPORT_PROHIBITED_NATIVE_ATTRIBUTE(attrName, errorMessage) \
        if(text == attrName) return errorMessage;
    #include "../export_keywords.h"
    #undef NWB_ADHD_EXPORT_PROHIBITED_NATIVE_ATTRIBUTE
        return {};
    }

    [[nodiscard]] static constexpr b8 TryParseEnumDeclKeyword(const MStringView text, b8* outIsFlag = nullptr)noexcept{
    #define NWB_ADHD_EXPORT_ENUM_DECL_KEYWORD(adhdKeyword, isFlag) \
        if(text == adhdKeyword){ \
            if(outIsFlag) *outIsFlag = isFlag; \
            return true; \
        }
    #include "../export_keywords.h"
    #undef NWB_ADHD_EXPORT_ENUM_DECL_KEYWORD
        return false;
    }
};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_ADHD_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

