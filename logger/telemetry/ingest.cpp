// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "ingest.h"

#include <core/telemetry/codec.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_LOG_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace __hidden_telemetry_ingest{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


inline constexpr int s_LocalTimeYearBase = 1900;
inline constexpr int s_LocalTimeMonthBase = 1;
inline constexpr const char* s_StorageDirectoryName = "telemetry";
inline constexpr const char* s_RawDirectoryName = "raw";
inline constexpr const char* s_ReportDirectoryName = "reports";
inline constexpr const char* s_RawStreamFileExtension = ".nwbs";
inline constexpr const char* s_JsonFileExtension = ".json";
inline constexpr const char* s_PerfCsvFileExtension = ".perf.csv";
static Atomic<u64> s_TelemetryUploadCounter{ 1u };

struct ByteView{
    const u8* bytes = nullptr;
    usize byteCount = 0u;

    [[nodiscard]] bool empty()const{ return byteCount == 0u; }
    [[nodiscard]] usize size()const{ return byteCount; }
    [[nodiscard]] const u8* data()const{ return bytes; }
};

[[nodiscard]] Path MakeTelemetryUploadStem(LogArena& arena){
    LocalTime localTime = {};
    if(!GetLocalTime(localTime))
        NWB_LOGGER_WARNING(NWB_TEXT("Failed to read local time for telemetry upload name"));

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

    return Path(arena, fileName);
}

void SetResultMessage(TelemetryIngestResult& result, AStringView message, const Type::Enum type){
    result.message = StringConvert(result.message.get_allocator().arena(), message);
    result.type = type;
}

[[nodiscard]] bool EnsureDirectory(const Path& directory){
    ErrorCode error;
    return EnsureDirectories(directory, error);
}

[[nodiscard]] bool StoreRawTelemetry(const Path& rawPath, const void* bytes, const usize byteCount){
    const ByteView view{ static_cast<const u8*>(bytes), byteCount };
    return WriteBinaryFile(rawPath, view);
}

[[nodiscard]] Path DefaultRootDirectory(LogArena& arena){
    Path executableDirectory(arena);
    if(GetExecutableDirectory(executableDirectory))
        return executableDirectory / s_StorageDirectoryName;

    return Path(arena, s_StorageDirectoryName);
}

[[nodiscard]] Path StorageDirectory(LogArena& arena, const Path& configuredStorageDirectory){
    if(!configuredStorageDirectory.empty())
        return Path(arena, configuredStorageDirectory);

    return DefaultRootDirectory(arena);
}

[[nodiscard]] Path RawDirectory(LogArena& arena, const Path& configuredStorageDirectory){
    return StorageDirectory(arena, configuredStorageDirectory) / s_RawDirectoryName;
}

[[nodiscard]] Path ReportDirectory(LogArena& arena, const Path& configuredStorageDirectory){
    return StorageDirectory(arena, configuredStorageDirectory) / s_ReportDirectoryName;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


TelemetryIngestResult ProcessTelemetryUpload(
    LogArena& arena,
    const void* const bytes,
    const usize byteCount,
    const TelemetryIngestConfig& config
){
    TelemetryIngestResult result(arena);
    if(!bytes || byteCount == 0u){
        __hidden_telemetry_ingest::SetResultMessage(result, "Telemetry upload is empty", Type::Error);
        return result;
    }

    const Path rawDirectory = __hidden_telemetry_ingest::RawDirectory(arena, config.storageDirectory);
    const Path reportDirectory = __hidden_telemetry_ingest::ReportDirectory(arena, config.storageDirectory);
    if(!__hidden_telemetry_ingest::EnsureDirectory(rawDirectory) || !__hidden_telemetry_ingest::EnsureDirectory(reportDirectory)){
        __hidden_telemetry_ingest::SetResultMessage(result, "Telemetry upload could not create storage directories", Type::Error);
        return result;
    }

    const Path uploadStem = __hidden_telemetry_ingest::MakeTelemetryUploadStem(arena);
    result.rawPath = rawDirectory / uploadStem;
    result.rawPath.replace_extension(__hidden_telemetry_ingest::s_RawStreamFileExtension);
    result.jsonPath = reportDirectory / uploadStem;
    result.jsonPath.replace_extension(__hidden_telemetry_ingest::s_JsonFileExtension);
    result.perfCsvPath = reportDirectory / uploadStem;
    result.perfCsvPath.replace_extension(__hidden_telemetry_ingest::s_PerfCsvFileExtension);

    result.storedRaw = __hidden_telemetry_ingest::StoreRawTelemetry(result.rawPath, bytes, byteCount);
    if(!result.storedRaw){
        __hidden_telemetry_ingest::SetResultMessage(result, "Telemetry upload could not store raw stream", Type::Error);
        return result;
    }

    Telemetry::Recorder recorder(arena);
    result.decode = Telemetry::DecodeEventStream(arena, bytes, byteCount, recorder);
    if(!result.decode.ok()){
        result.message = StringFormat(
            arena,
            NWB_TEXT("Telemetry upload stored but decode failed: raw='{}' bytes_read={} status={}"),
            PathToString<tchar>(result.rawPath),
            result.decode.bytesRead,
            static_cast<u32>(result.decode.status)
        );
        result.type = Type::Error;
        return result;
    }

    TelemetryReport report(arena);
    if(!BuildTelemetryReport(arena, recorder.view(), report)){
        result.message = StringFormat(arena, NWB_TEXT("Telemetry upload stored but report build failed: raw='{}'"), PathToString<tchar>(result.rawPath));
        result.type = Type::Error;
        return result;
    }

    result.summary = report.summary;
    result.wroteJson = WriteTextFile(result.jsonPath, AStringView(report.json.data(), report.json.size()));
    result.wrotePerfCsv = WriteTextFile(result.perfCsvPath, AStringView(report.perfCsv.data(), report.perfCsv.size()));
    if(!result.wroteJson || !result.wrotePerfCsv){
        result.message = StringFormat(
            arena,
            NWB_TEXT("Telemetry upload decoded but report write failed: raw='{}' json='{}' csv='{}'"),
            PathToString<tchar>(result.rawPath),
            PathToString<tchar>(result.jsonPath),
            PathToString<tchar>(result.perfCsvPath)
        );
        result.type = Type::Error;
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
    result.type = result.summary.parseFailureCount == 0u ? Type::EssentialInfo : Type::Warning;
    return result;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_LOG_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
