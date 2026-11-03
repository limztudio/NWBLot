// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "ingest.h"
#include "file_publication.h"

#include <logger/common.h>

#include <core/alloc/scratch.h>

#include <global/binary.h>
#include <global/process.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_LOG_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace __hidden_telemetry_ingest{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


using ScratchPath = ::Path<Core::Alloc::ScratchArena>;


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


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
        NWB_TEXT("telemetry_{:04}{:02}{:02}_{:02}{:02}{:02}_{}_{}"),
        localTime.tm_year + s_LocalTimeYearBase,
        localTime.tm_mon + s_LocalTimeMonthBase,
        localTime.tm_mday,
        localTime.tm_hour,
        localTime.tm_min,
        localTime.tm_sec,
        CurrentProcessId(),
        counter
    );

    return ScratchPath(arena, fileName);
}

void SetResultMessage(TelemetryIngestResult& result, AStringView message, const Core::Common::LogType::Enum type){
    result.message = ConvertString(result.message.get_allocator().arena(), message);
    result.type = type;
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
    const auto storageDirectory = ResolveStorageDirectory(scratchArena, config.storageDirectory, __hidden_telemetry_ingest::s_StorageDirectoryName);
    const auto rawDirectory = storageDirectory / __hidden_telemetry_ingest::s_RawDirectoryName;
    const auto reportDirectory = storageDirectory / __hidden_telemetry_ingest::s_ReportDirectoryName;
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

    const auto rawPublication = PublishNewTelemetryFile(result.rawPath, BinaryByteView{ static_cast<const u8*>(bytes), byteCount });
    result.storedRaw = rawPublication.published;
    if(rawPublication.error && !rawPublication.published){
        result.message = StringFormat(arena, NWB_TEXT("Telemetry raw publication failed: raw='{}' staging_retained={} error={}"), PathToString<tchar>(result.rawPath), rawPublication.stagingRetained, rawPublication.error.value());
        result.type = Core::Common::LogType::Error;
        return result;
    }
    result.rawCleanupError = rawPublication.error;
    result.rawStagingRetained = rawPublication.stagingRetained;

    Core::Telemetry::Recorder recorder(arena);
    const auto decoded = Core::Telemetry::DecodeEventStream(bytes, byteCount, arena, recorder);
    result.decode = decoded ? Core::Telemetry::DecodeResult{ *decoded, Core::Telemetry::DecodeStatus::Ok } : decoded.error();
    if(!result.decode.ok()){
        result.message = StringFormat(arena, NWB_TEXT("Telemetry upload stored but decode failed: raw='{}' bytes_read={} status={}"), PathToString<tchar>(result.rawPath), result.decode.bytesRead, static_cast<u32>(result.decode.status));
        result.type = Core::Common::LogType::Error;
        return result;
    }

    auto report = BuildTelemetryReport(recorder.view(), arena, scratchArena);
    if(!report){
        result.message = StringFormat(arena, NWB_TEXT("Telemetry upload stored but report build failed: raw='{}'"), PathToString<tchar>(result.rawPath));
        result.type = Core::Common::LogType::Error;
        return result;
    }

    result.summary = report->summary;
    const auto jsonPublication = PublishNewTelemetryFile(result.jsonPath, BinaryByteView{ reinterpret_cast<const u8*>(report->json.data()), report->json.size() });
    const auto csvPublication = PublishNewTelemetryFile(result.perfCsvPath, BinaryByteView{ reinterpret_cast<const u8*>(report->perfCsv.data()), report->perfCsv.size() });
    TelemetryFilePublicationResult graphPublication;
    if(!report->graph.empty())
        graphPublication = PublishNewTelemetryFile(result.graphPath, BinaryByteView{ reinterpret_cast<const u8*>(report->graph.data()), report->graph.size() });
    result.wroteJson = jsonPublication.published;
    result.wrotePerfCsv = csvPublication.published;
    result.wroteGraph = graphPublication.published;
    if(jsonPublication.error || csvPublication.error || graphPublication.error){
        result.message = StringFormat(arena, NWB_TEXT("Telemetry report publication failed: raw='{}' json='{}' error={} csv='{}' error={} graph='{}' error={}"), PathToString<tchar>(result.rawPath), PathToString<tchar>(result.jsonPath), jsonPublication.error.value(), PathToString<tchar>(result.perfCsvPath), csvPublication.error.value(), PathToString<tchar>(result.graphPath), graphPublication.error.value());
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

