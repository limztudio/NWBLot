// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#pragma once


#include <logger/common.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_LOG_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


using CrashReportText = AString<LogArena>;

struct CrashSymbolicationConfig{
    Path symbolStoreDirectory;

    explicit CrashSymbolicationConfig(LogArena& arena)
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

    explicit CrashPackageSummary(LogArena& arena)
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


[[nodiscard]] CrashReportText BuildCrashSymbolicationReport(LogArena& arena, const Path& packageDirectory, const CrashPackageSummary& summary, const CrashSymbolicationConfig& config);


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_LOG_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

