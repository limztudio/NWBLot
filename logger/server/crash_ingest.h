// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#pragma once


#include <logger/common.h>

#include "crash_symbolicate.h"


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_LOG_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

inline constexpr usize s_DefaultMaxExtractedCrashPackages = 256u;
inline constexpr usize s_DefaultMaxRawCrashArchives = 256u;
inline constexpr usize s_DefaultMaxInvalidCrashArchives = 64u;


struct CrashRetentionConfig{
    usize maxExtractedPackages = s_DefaultMaxExtractedCrashPackages;
    usize maxRawArchives = s_DefaultMaxRawCrashArchives;
    usize maxInvalidArchives = s_DefaultMaxInvalidCrashArchives;
};

struct CrashIngestConfig{
    Path storageDirectory;
    CrashSymbolicationConfig symbolication;
    CrashRetentionConfig retention;

    explicit CrashIngestConfig(LogArena& arena)
        : storageDirectory(arena)
        , symbolication(arena)
    {}
};

struct CrashIngestResult{
    LogString message;
    Type::Enum type = Type::EssentialInfo;
    bool accepted = false;

    explicit CrashIngestResult(LogArena& arena)
        : message(arena)
    {}
};


[[nodiscard]] CrashIngestResult ProcessCrashUpload(LogArena& arena, const Path& archivePath, const CrashIngestConfig& config);


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_LOG_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

