// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#pragma once


#include "report.h"

#include <core/telemetry/codec.h>
#include <global/filesystem/operations.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_LOG_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


inline constexpr const char* s_TelemetryStorageDirectoryName = "telemetry";
inline constexpr const char* s_TelemetryRawDirectoryName = "raw";
inline constexpr const char* s_TelemetryReportDirectoryName = "reports";
inline constexpr const char* s_TelemetryRawStreamFileExtension = ".nwbs";
inline constexpr const char* s_TelemetryJsonFileExtension = ".json";
inline constexpr const char* s_TelemetryPerfCsvFileExtension = ".perf.csv";

struct TelemetryIngestConfig{
    explicit TelemetryIngestConfig(LogArena& arena)
        : storageDirectory(arena)
    {}

    Path storageDirectory;
};

struct TelemetryIngestResult{
    explicit TelemetryIngestResult(LogArena& arena)
        : message(arena)
        , rawPath(arena)
        , jsonPath(arena)
        , perfCsvPath(arena)
    {}

    LogString message;
    Path rawPath;
    Path jsonPath;
    Path perfCsvPath;
    Type::Enum type = Type::Info;
    Telemetry::DecodeResult decode;
    ArchiveReportSummary summary;
    bool storedRaw = false;
    bool wroteJson = false;
    bool wrotePerfCsv = false;

    [[nodiscard]] bool ok()const{
        return storedRaw && decode.ok() && wroteJson && wrotePerfCsv && summary.parseFailureCount == 0u;
    }
};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


[[nodiscard]] Path TelemetryDefaultRootDirectory(LogArena& arena);
[[nodiscard]] Path TelemetryStorageDirectory(LogArena& arena, const Path& configuredStorageDirectory);
[[nodiscard]] Path TelemetryRawDirectory(LogArena& arena, const Path& configuredStorageDirectory);
[[nodiscard]] Path TelemetryReportDirectory(LogArena& arena, const Path& configuredStorageDirectory);
[[nodiscard]] TelemetryIngestResult ProcessTelemetryUpload(
    LogArena& arena,
    const void* bytes,
    usize byteCount,
    const TelemetryIngestConfig& config
);


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_LOG_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
