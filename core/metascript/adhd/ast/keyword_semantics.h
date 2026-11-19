// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#pragma once


#include "ast_common.h"


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_ADHD_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace ExternAttributeKind{
    enum Enum : u8{
        None = 0u,
#define NWB_ADHD_EXPORT_EXTERN_ATTRIBUTE(idName, attrName, duplicateErrorMessage, argErrorMessage) \
        idName,
#include "../export_keywords.h"
#undef NWB_ADHD_EXPORT_EXTERN_ATTRIBUTE
    };
};

namespace ExternTypeFlagKind{
    enum Enum : u8{
        None = 0u,
#define NWB_ADHD_EXPORT_EXTERN_TYPE_FLAG(idName, flagText) \
        idName,
#include "../export_keywords.h"
#undef NWB_ADHD_EXPORT_EXTERN_TYPE_FLAG
    };
};

namespace ExternCallableFlagKind{
    enum Enum : u8{
        None = 0u,
#define NWB_ADHD_EXPORT_EXTERN_CALLABLE_FLAG(idName, flagText) \
        idName,
#include "../export_keywords.h"
#undef NWB_ADHD_EXPORT_EXTERN_CALLABLE_FLAG
    };
};


struct ProhibitedKeywordEntry{
    MStringView keyword;
    MStringView errorMessage;
};


class KeywordSemantics final{
public:
#define NWB_ADHD_EXPORT_GRAMMAR_KEYWORD(idName, keywordText) \
    inline static constexpr MStringView s_##idName = keywordText;
#include "../export_keywords.h"
#undef NWB_ADHD_EXPORT_GRAMMAR_KEYWORD

#define NWB_ADHD_EXPORT_EXTERN_ATTRIBUTE(idName, attrName, duplicateErrorMessage, argErrorMessage) \
    inline static constexpr MStringView s_ExternAttr##idName = attrName;
#include "../export_keywords.h"
#undef NWB_ADHD_EXPORT_EXTERN_ATTRIBUTE


private:
    inline static constexpr auto s_ProhibitedTypeKeywords = Array{
    #define NWB_ADHD_EXPORT_PROHIBITED_TYPE_KEYWORD(keyword, errorMessage) \
        ProhibitedKeywordEntry{ keyword, errorMessage },
    #include "../export_keywords.h"
    #undef NWB_ADHD_EXPORT_PROHIBITED_TYPE_KEYWORD
    };

    inline static constexpr auto s_ProhibitedExprKeywords = Array{
    #define NWB_ADHD_EXPORT_PROHIBITED_EXPR_KEYWORD(keyword, errorMessage) \
        ProhibitedKeywordEntry{ keyword, errorMessage },
    #include "../export_keywords.h"
    #undef NWB_ADHD_EXPORT_PROHIBITED_EXPR_KEYWORD
    };

    inline static constexpr auto s_ProhibitedStmtKeywords = Array{
    #define NWB_ADHD_EXPORT_PROHIBITED_STMT_KEYWORD(keyword, errorMessage) \
        ProhibitedKeywordEntry{ keyword, errorMessage },
    #include "../export_keywords.h"
    #undef NWB_ADHD_EXPORT_PROHIBITED_STMT_KEYWORD
    };

    inline static constexpr auto s_ProhibitedTopLevelKeywords = Array{
    #define NWB_ADHD_EXPORT_PROHIBITED_TOP_LEVEL_KEYWORD(keyword, errorMessage) \
        ProhibitedKeywordEntry{ keyword, errorMessage },
    #include "../export_keywords.h"
    #undef NWB_ADHD_EXPORT_PROHIBITED_TOP_LEVEL_KEYWORD
    };

    inline static constexpr auto s_ProhibitedTemplateParamKeywords = Array{
    #define NWB_ADHD_EXPORT_PROHIBITED_TEMPLATE_PARAM_KEYWORD(keyword, errorMessage) \
        ProhibitedKeywordEntry{ keyword, errorMessage },
    #include "../export_keywords.h"
    #undef NWB_ADHD_EXPORT_PROHIBITED_TEMPLATE_PARAM_KEYWORD
    };

    inline static constexpr auto s_ProhibitedPostfixKeywords = Array{
    #define NWB_ADHD_EXPORT_PROHIBITED_POSTFIX_KEYWORD(keyword, errorMessage) \
        ProhibitedKeywordEntry{ keyword, errorMessage },
    #include "../export_keywords.h"
    #undef NWB_ADHD_EXPORT_PROHIBITED_POSTFIX_KEYWORD
    };

    template<usize N>
    [[nodiscard]] static constexpr MStringView LookupProhibitedKeywordError(const Array<ProhibitedKeywordEntry, N>& table, const MStringView text)noexcept{
        for(const ProhibitedKeywordEntry& entry : table){
            if(text == entry.keyword)
                return entry.errorMessage;
        }
        return {};
    }


public:
    [[nodiscard]] static constexpr ExternAttributeKind::Enum TryParseExternAttribute(const MStringView text, MStringView* outDuplicateError = nullptr, MStringView* outArgError = nullptr)noexcept{
    #define NWB_ADHD_EXPORT_EXTERN_ATTRIBUTE(idName, attrName, duplicateErrorMessage, argErrorMessage) \
        if(text == attrName){ \
            if(outDuplicateError) *outDuplicateError = duplicateErrorMessage; \
            if(outArgError) *outArgError = argErrorMessage; \
            return ExternAttributeKind::idName; \
        }
    #include "../export_keywords.h"
    #undef NWB_ADHD_EXPORT_EXTERN_ATTRIBUTE
        return ExternAttributeKind::None;
    }

    [[nodiscard]] static constexpr b8 TryParseExternTypeFlag(const MStringView text, ExternTypeFlagKind::Enum& outFlag)noexcept{
    #define NWB_ADHD_EXPORT_EXTERN_TYPE_FLAG(idName, flagText) \
        if(text == flagText){ outFlag = ExternTypeFlagKind::idName; return true; }
    #include "../export_keywords.h"
    #undef NWB_ADHD_EXPORT_EXTERN_TYPE_FLAG
        return false;
    }

    [[nodiscard]] static constexpr b8 TryParseExternCallableFlag(const MStringView text, ExternCallableFlagKind::Enum& outFlag)noexcept{
    #define NWB_ADHD_EXPORT_EXTERN_CALLABLE_FLAG(idName, flagText) \
        if(text == flagText){ outFlag = ExternCallableFlagKind::idName; return true; }
    #include "../export_keywords.h"
    #undef NWB_ADHD_EXPORT_EXTERN_CALLABLE_FLAG
        return false;
    }

    [[nodiscard]] static constexpr b8 IsThisKeyword(const MStringView text)noexcept{
        return text == s_This;
    }

    [[nodiscard]] static constexpr b8 IsParallelWaitKeyword(const MStringView text)noexcept{
        return text == s_Wait;
    }

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
        return LookupProhibitedKeywordError(s_ProhibitedTypeKeywords, text);
    }

    [[nodiscard]] static constexpr MStringView FindProhibitedExprKeywordError(const MStringView text)noexcept{
        return LookupProhibitedKeywordError(s_ProhibitedExprKeywords, text);
    }

    [[nodiscard]] static constexpr MStringView FindProhibitedStmtKeywordError(const MStringView text)noexcept{
        return LookupProhibitedKeywordError(s_ProhibitedStmtKeywords, text);
    }

    [[nodiscard]] static constexpr MStringView FindProhibitedTopLevelKeywordError(const MStringView text)noexcept{
        return LookupProhibitedKeywordError(s_ProhibitedTopLevelKeywords, text);
    }

    [[nodiscard]] static constexpr MStringView FindProhibitedMemberKeywordError(const MStringView text, const b8 isInterface)noexcept{
    #define NWB_ADHD_EXPORT_PROHIBITED_MEMBER_KEYWORD(keyword, classErrorMessage, interfaceErrorMessage) \
        if(text == keyword) return isInterface ? MStringView(interfaceErrorMessage) : MStringView(classErrorMessage);
    #include "../export_keywords.h"
    #undef NWB_ADHD_EXPORT_PROHIBITED_MEMBER_KEYWORD
        return {};
    }

    [[nodiscard]] static constexpr MStringView FindProhibitedTemplateParamKeywordError(const MStringView text)noexcept{
        return LookupProhibitedKeywordError(s_ProhibitedTemplateParamKeywords, text);
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

    [[nodiscard]] static constexpr MStringView EnumDeclKeyword(const b8 isFlag)noexcept{
    #define NWB_ADHD_EXPORT_ENUM_DECL_KEYWORD(adhdKeyword, entryIsFlag) \
        if(isFlag == (entryIsFlag)) return adhdKeyword;
    #include "../export_keywords.h"
    #undef NWB_ADHD_EXPORT_ENUM_DECL_KEYWORD
        return {};
    }

    [[nodiscard]] static constexpr MStringView ParamPassingModeKeyword(const ParamPassingMode::Enum mode)noexcept{
    #define NWB_ADHD_EXPORT_PARAM_PASSING_MODE(adhdKeyword, enumName) \
        if(mode == ParamPassingMode::enumName) return adhdKeyword;
    #include "../export_keywords.h"
    #undef NWB_ADHD_EXPORT_PARAM_PASSING_MODE
        return {};
    }

    [[nodiscard]] static constexpr MStringView FindProhibitedPostfixKeywordError(const MStringView text)noexcept{
        return LookupProhibitedKeywordError(s_ProhibitedPostfixKeywords, text);
    }

    [[nodiscard]] static constexpr b8 IsEnumSentryReflected(const MStringView text)noexcept{
    #define NWB_ADHD_EXPORT_ENUM_SENTRY(sentryName, isReflected, enumErrorMessage, flagErrorMessage) \
        if(!(isReflected) && text == sentryName) return false;
    #include "../export_keywords.h"
    #undef NWB_ADHD_EXPORT_ENUM_SENTRY
        return true;
    }

    [[nodiscard]] static constexpr MStringView FindForbiddenEnumSentryError(const MStringView text, const b8 isFlag)noexcept{
    #define NWB_ADHD_EXPORT_ENUM_SENTRY(sentryName, isReflected, enumErrorMessage, flagErrorMessage) \
        if(text == sentryName && !MStringView(enumErrorMessage).empty()) \
            return isFlag ? MStringView(flagErrorMessage) : MStringView(enumErrorMessage);
    #include "../export_keywords.h"
    #undef NWB_ADHD_EXPORT_ENUM_SENTRY
        return {};
    }

    [[nodiscard]] static constexpr b8 IsUnresolvedArenaMemberName(const MStringView text)noexcept{
    #define NWB_ADHD_EXPORT_UNRESOLVED_ARENA_MEMBER(identName) \
        if(text == identName) return true;
    #include "../export_keywords.h"
    #undef NWB_ADHD_EXPORT_UNRESOLVED_ARENA_MEMBER
        return false;
    }

    [[nodiscard]] static constexpr b8 IsUnresolvedArenaVarName(const MStringView text)noexcept{
    #define NWB_ADHD_EXPORT_UNRESOLVED_ARENA_VAR(identName) \
        if(text == identName) return true;
    #include "../export_keywords.h"
    #undef NWB_ADHD_EXPORT_UNRESOLVED_ARENA_VAR
        return false;
    }
};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_ADHD_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

