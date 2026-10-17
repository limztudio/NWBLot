// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "ingest.h"

#include <core/alloc/scratch.h>
#include <core/telemetry/codec.h>

#include <global/binary.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_LOG_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace __hidden_telemetry_ingest{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


using ScratchPath = ::Path<Core::Alloc::ScratchArena>;


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


inline constexpr int s_LocalTimeYearBase = 1900;
inline constexpr int s_LocalTimeMonthBase = 1;
inline constexpr StringView s_StorageDirectoryName = "telemetry";
inline constexpr StringView s_RawDirectoryName = "raw";
inline constexpr StringView s_ReportDirectoryName = "reports";
inline constexpr StringView s_RawStreamFileExtension = ".nwbs";
inline constexpr StringView s_JsonFileExtension = ".json";
inline constexpr StringView s_PerfCsvFileExtension = ".perf.csv";
inline constexpr StringView s_GraphFileExtension = ".graph.dot";
static Atomic<u64> s_TelemetryUploadCounter{ 1u };


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


[[nodiscard]] ScratchPath MakeTelemetryUploadStem(Core::Alloc::ScratchArena& arena){
    const auto timeResult = GetLocalTime();
    if(!timeResult)
        NWB_LOGGER_WARNING(NWB_TEXT("Failed to read local time for telemetry upload name"));
    const LocalTime localTime = timeResult ? *timeResult : LocalTime{};

    const u64 counter = s_TelemetryUploadCounter.fetch_add(1u, MemoryOrder::relaxed);
    const auto fileName = StringFormat(
        arena,
        NWB_TEXT("telemetry_{:04}{:02}{:02}_{:02}{:02}{:02}_{}"),
        localTime.tm_year + s_LocalTimeYearBase,
        localTime.tm_mon + s_LocalTimeMonthBase,
        localTime.tm_mday,
        localTime.tm_hour,
        localTime.tm_min,
        localTime.tm_sec,
        counter
    );

    return ScratchPath(arena, fileName);
}

void SetResultMessage(TelemetryIngestResult& result, AStringView message, const Core::Common::LogType::Enum type){
    result.message = StringConvert(result.message.get_allocator().arena(), message);
    result.type = type;
}

[[nodiscard]] bool StoreRawTelemetry(const Path& rawPath, const void* bytes, const usize byteCount){
    const BinaryByteView view{ static_cast<const u8*>(bytes), byteCount };
    return WriteBinaryFile(rawPath, view);
}

[[nodiscard]] ScratchPath DefaultRootDirectory(Core::Alloc::ScratchArena& arena){
    const auto executableDirectory = GetExecutableDirectory(arena);
    if(executableDirectory)
        return *executableDirectory / s_StorageDirectoryName;

    return ScratchPath(arena, s_StorageDirectoryName);
}

[[nodiscard]] ScratchPath StorageDirectory(Core::Alloc::ScratchArena& arena, const Path& configuredStorageDirectory){
    if(!configuredStorageDirectory.empty())
        return ScratchPath(arena, configuredStorageDirectory);

    return DefaultRootDirectory(arena);
}

[[nodiscard]] ScratchPath RawDirectory(Core::Alloc::ScratchArena& arena, const Path& configuredStorageDirectory){
    return StorageDirectory(arena, configuredStorageDirectory) / s_RawDirectoryName;
}

[[nodiscard]] ScratchPath ReportDirectory(Core::Alloc::ScratchArena& arena, const Path& configuredStorageDirectory){
    return StorageDirectory(arena, configuredStorageDirectory) / s_ReportDirectoryName;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


TelemetryIngestResult ProcessTelemetryUpload(LogArena& arena, const void* const bytes, const usize byteCount, const TelemetryIngestConfig& config){
    TelemetryIngestResult result(arena);
    if(!bytes || byteCount == 0u){
        __hidden_telemetry_ingest::SetResultMessage(result, "Telemetry upload is empty", Core::Common::LogType::Error);
        return result;
    }

    Core::Alloc::ScratchArena scratchArena(Name("logger/telemetry/report"));
    const auto rawDirectory = __hidden_telemetry_ingest::RawDirectory(scratchArena, config.storageDirectory);
    const auto reportDirectory = __hidden_telemetry_ingest::ReportDirectory(scratchArena, config.storageDirectory);
    if(!EnsureDirectories(rawDirectory) || !EnsureDirectories(reportDirectory)){
        __hidden_telemetry_ingest::SetResultMessage(result, "Telemetry upload could not create storage directories", Core::Common::LogType::Error);
        return result;
    }

    const auto uploadStem = __hidden_telemetry_ingest::MakeTelemetryUploadStem(scratchArena);
    result.rawPath = rawDirectory / uploadStem;
    result.rawPath.replaceExtension(__hidden_telemetry_ingest::s_RawStreamFileExtension);
    result.jsonPath = reportDirectory / uploadStem;
    result.jsonPath.replaceExtension(__hidden_telemetry_ingest::s_JsonFileExtension);
    result.perfCsvPath = reportDirectory / uploadStem;
    result.perfCsvPath.replaceExtension(__hidden_telemetry_ingest::s_PerfCsvFileExtension);
    result.graphPath = reportDirectory / uploadStem;
    result.graphPath.replaceExtension(__hidden_telemetry_ingest::s_GraphFileExtension);

    result.storedRaw = __hidden_telemetry_ingest::StoreRawTelemetry(result.rawPath, bytes, byteCount);
    if(!result.storedRaw){
        __hidden_telemetry_ingest::SetResultMessage(result, "Telemetry upload could not store raw stream", Core::Common::LogType::Error);
        return result;
    }

    Telemetry::Recorder recorder(arena);
    const auto decoded = Telemetry::DecodeEventStream(arena, bytes, byteCount, recorder);
    result.decode = decoded ? Telemetry::DecodeResult{ *decoded, Telemetry::DecodeStatus::Ok } : decoded.error();
    if(!result.decode.ok()){
        result.message = StringFormat(arena, NWB_TEXT("Telemetry upload stored but decode failed: raw='{}' bytes_read={} status={}"), PathToString<tchar>(result.rawPath), result.decode.bytesRead, static_cast<u32>(result.decode.status));
        result.type = Core::Common::LogType::Error;
        return result;
    }

    auto report = BuildTelemetryReport(arena, recorder.view(), scratchArena);
    if(!report){
        result.message = StringFormat(arena, NWB_TEXT("Telemetry upload stored but report build failed: raw='{}'"), PathToString<tchar>(result.rawPath));
        result.type = Core::Common::LogType::Error;
        return result;
    }

    result.summary = report->summary;
    result.wroteJson = WriteTextFile(result.jsonPath, AStringView(report->json.data(), report->json.size()));
    result.wrotePerfCsv = WriteTextFile(result.perfCsvPath, AStringView(report->perfCsv.data(), report->perfCsv.size()));
    if(!report->graph.empty())
        result.wroteGraph = WriteTextFile(result.graphPath, AStringView(report->graph.data(), report->graph.size()));
    if(!result.wroteJson || !result.wrotePerfCsv){
        result.message = StringFormat(arena, NWB_TEXT("Telemetry upload decoded but report write failed: raw='{}' json='{}' csv='{}'"), PathToString<tchar>(result.rawPath), PathToString<tchar>(result.jsonPath), PathToString<tchar>(result.perfCsvPath));
        result.type = Core::Common::LogType::Error;
        return result;
    }

    result.message = StringFormat(
        arena,
        NWB_TEXT("Telemetry upload processed: events={} parse_failures={} raw='{}' json='{}' csv='{}'"),
        result.summary.eventCount,
        result.summary.parseFailureCount,
        PathToString<tchar>(result.rawPath),
        PathToString<tchar>(result.jsonPath),
        PathToString<tchar>(result.perfCsvPath)
    );
    result.type = result.summary.parseFailureCount == 0u ? Core::Common::LogType::EssentialInfo : Core::Common::LogType::Warning;
    return result;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_LOG_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

