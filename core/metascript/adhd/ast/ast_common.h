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

