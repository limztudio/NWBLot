// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#pragma once


#include "crash_paths.h"

#include <logger/common.h>

#include <core/alloc/scratch.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_LOG_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


using CrashReportText = AString<LogArena>;

struct CrashSymbolicationConfig{
    Path symbolStoreDirectory;


public:
    explicit CrashSymbolicationConfig(LogArena& arena)noexcept
        : symbolStoreDirectory(arena)
    {}
};

struct CrashPackageSummary{
    CrashReportText crashId;
    CrashReportText platform;
    CrashReportText reasonKind;
    CrashReportText artifactStrategy;
    CrashReportText event;
    CrashReportText triggerExpression;
    CrashReportText triggerMessage;
    CrashReportText triggerFile;
    u64 reasonCode = 0u;
    u64 threadId = 0u;
    u64 triggerLine = 0u;


public:
    explicit CrashPackageSummary(LogArena& arena)noexcept
        : crashId(arena)
        , platform(arena)
        , reasonKind(arena)
        , artifactStrategy(arena)
        , event(arena)
        , triggerExpression(arena)
        , triggerMessage(arena)
        , triggerFile(arena)
    {}
};


[[nodiscard]] CrashReportText BuildCrashSymbolicationReport(LogArena& arena, const CrashScratchPath& packageDirectory, const CrashPackageSummary& summary, const CrashSymbolicationConfig& config, Core::Alloc::ScratchArena& scratchArena);


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_LOG_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

