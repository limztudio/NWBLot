// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#pragma once


#include <logger/common.h>
#include <logger/server/crash_paths.adhd.h>
#include <logger/server/crash_retention.adhd.h>
#include <logger/server/crash_symbolicate.h>
#include <logger/server/crash_values.adhd.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_LOG_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


struct CrashIngestConfig{
    Path storageDirectory;
    CrashSymbolicationConfig symbolication;
    CrashRetentionConfig retention;


public:
    explicit CrashIngestConfig(LogArena& arena)noexcept
        : storageDirectory(arena)
        , symbolication(arena)
    {}
};

struct CrashIngestResult{
    LogString message;
    Core::Common::LogType::Enum type = Core::Common::LogType::EssentialInfo;
    b8 accepted = false;


public:
    explicit CrashIngestResult(LogArena& arena)noexcept
        : message(arena)
    {}
};


[[nodiscard]] CrashIngestResult ProcessCrashUpload(LogArena& arena, const Path& archivePath, const CrashIngestConfig& config);

template<typename ArchiveArenaT>
    requires (!IsSame_V<ArchiveArenaT, LogArena>)
[[nodiscard]] inline CrashIngestResult ProcessCrashUpload(LogArena& arena, const ::Path<ArchiveArenaT>& archivePath, const CrashIngestConfig& config){
    return ProcessCrashUpload(arena, Path(arena, archivePath), config);
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_LOG_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

