// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#pragma once


#include "report.h"

#include <core/telemetry/codec.h>

#include <global/filesystem/operations.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_LOG_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


struct TelemetryIngestConfig{
    Path storageDirectory;

    explicit TelemetryIngestConfig(LogArena& arena)
        : storageDirectory(arena)
    {}
};

struct TelemetryIngestResult{
    LogString message;
    Path rawPath;
    Path jsonPath;
    Path perfCsvPath;
    Path graphPath;
    Core::Common::LogType::Enum type = Core::Common::LogType::Info;
    bool storedRaw = false;
    bool wroteJson = false;
    bool wrotePerfCsv = false;
    bool wroteGraph = false;
    Telemetry::DecodeResult decode;
    TelemetryReportSummary summary;

    explicit TelemetryIngestResult(LogArena& arena)
        : message(arena)
        , rawPath(arena)
        , jsonPath(arena)
        , perfCsvPath(arena)
        , graphPath(arena)
    {}

    [[nodiscard]] bool ok()const noexcept{
        return storedRaw && decode.ok() && wroteJson && wrotePerfCsv && summary.parseFailureCount == 0u;
    }
};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


[[nodiscard]] TelemetryIngestResult ProcessTelemetryUpload(LogArena& arena, const void* bytes, usize byteCount, const TelemetryIngestConfig& config);


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_LOG_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

