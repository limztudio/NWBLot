// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#pragma once


#include "../global.h"

#include "contract_semantics.h"


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

inline void CopyArenaChars(MChar* dst, const MStringView text)noexcept{
    if(!text.empty())
        FOUNDATION_MEMCPY(dst, text.size(), text.data(), text.size());
}

[[nodiscard]] constexpr b8 ContainsStringView(const Span<const MStringView> items, const MStringView target)noexcept{
    for(const MStringView item : items){
        if(item == target)
            return true;
    }
    return false;
}

[[nodiscard]] inline MStringView StripHeaderDelimiters(const MStringView rawText, b8* const outHadAngles = nullptr, b8* const outHadQuotes = nullptr)noexcept{
    if(outHadAngles)
        *outHadAngles = false;
    if(outHadQuotes)
        *outHadQuotes = false;
    const MStringView text = TrimView(rawText);
    if(text.size() >= 2u){
        if(text.front() == '<' && text.back() == '>'){
            if(outHadAngles)
                *outHadAngles = true;
            return text.substr(1u, text.size() - 2u);
        }
        if(text.front() == '"' && text.back() == '"'){
            if(outHadQuotes)
                *outHadQuotes = true;
            return text.substr(1u, text.size() - 2u);
        }
    }
    return text;
}

[[nodiscard]] inline MStringView InternAngleBracketHeader(MetaArena& arena, const MStringView innerPath){
    const usize totalLen = innerPath.size() + 2u;
    auto* buf = static_cast<MChar*>(arena.allocate(alignof(MChar), totalLen + 1u));
    buf[0] = '<';
    CopyArenaChars(buf + 1u, innerPath);
    buf[totalLen - 1u] = '>';
    buf[totalLen] = '\0';
    return MStringView(buf, totalLen);
}

[[nodiscard]] inline MStringView InternArenaString(MetaArena& arena, const MStringView text){
    if(text.empty())
        return {};
    auto* buf = static_cast<MChar*>(arena.allocate(alignof(MChar), text.size() + 1u));
    CopyArenaChars(buf, text);
    buf[text.size()] = '\0';
    return MStringView(buf, text.size());
}

[[nodiscard]] inline MStringView JoinScopedCppName(MetaArena& arena, const MStringView scope, const MStringView member){
    if(scope.empty())
        return InternArenaString(arena, member);
    if(member.empty())
        return InternArenaString(arena, scope);
    constexpr MStringView s_Sep = ContractSemantics::s_CppScopeSeparator;
    const usize totalLen = scope.size() + s_Sep.size() + member.size();
    auto* buf = static_cast<MChar*>(arena.allocate(alignof(MChar), totalLen + 1u));
    CopyArenaChars(buf, scope);
    CopyArenaChars(buf + scope.size(), s_Sep);
    CopyArenaChars(buf + scope.size() + s_Sep.size(), member);
    buf[totalLen] = '\0';
    return MStringView(buf, totalLen);
}

[[nodiscard]] constexpr b8 HasAdhdExtension(const MStringView path)noexcept{
    constexpr MStringView s_Ext = ContractSemantics::s_AdhdExtension;
    return path.size() > s_Ext.size() && path.ends_with(s_Ext);
}

[[nodiscard]] constexpr MStringView StripAdhdExtension(const MStringView path)noexcept{
    constexpr MStringView s_Ext = ContractSemantics::s_AdhdExtension;
    if(HasAdhdExtension(path))
        return path.substr(0u, path.size() - s_Ext.size());
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

