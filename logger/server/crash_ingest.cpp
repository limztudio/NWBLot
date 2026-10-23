// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "crash_ingest.h"
#include "crash_paths.h"

#include <core/crash/package_names.h>

#include <global/binary.h>
#include <global/filesystem/archive.h>
#include <global/filesystem/retention.h>
#include <global/text_utils.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_LOG_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace __hidden_logger_crash_ingest{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


using CrashText = CrashReportText;
using CrashBytes = Vector<u8, Core::Alloc::ScratchArena>;
namespace CrashNames = ::NWB::Core::Crash::PackageNames;

inline constexpr usize s_GeneratedJsonNeedleReserveSlack = 4u;
inline constexpr AStringView s_MalformedFileFooterDetail = "malformed crash archive file footer";
inline constexpr AStringView s_ReportLineEnd = "\n";
inline constexpr Name s_IngestScratchArenaName("logger/server/crash_ingest/scratch");

struct IngestFailure{
    AStringView detail;
    Optional<ErrorCode> code{};
};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


static void ApplyRetention(Core::Alloc::ScratchArena& arena, const CrashIngestConfig& config){
    const auto extracted = ApplyDirectoryRetention(arena, CrashExtractedDirectory(arena, config.storageDirectory), config.retention.maxExtractedPackages);
    if(!extracted)
        NWB_LOGGER_WARNING(NWB_TEXT("Failed to apply retention to extracted crash packages: {}"), StringConvert(extracted.error().message()));
    const auto raw = ApplyDirectoryRetention(arena, CrashRawDirectory(arena, config.storageDirectory), config.retention.maxRawArchives);
    if(!raw)
        NWB_LOGGER_WARNING(NWB_TEXT("Failed to apply retention to raw crash archives: {}"), StringConvert(raw.error().message()));
    const auto invalid = ApplyDirectoryRetention(arena, CrashInvalidDirectory(arena, config.storageDirectory), config.retention.maxInvalidArchives);
    if(!invalid)
        NWB_LOGGER_WARNING(NWB_TEXT("Failed to apply retention to invalid crash archives: {}"), StringConvert(invalid.error().message()));
}

[[nodiscard]] static Core::Common::LogType::Enum AcceptedCrashLogType(const CrashPackageSummary& summary)noexcept{
    const AStringView event(summary.event.data(), summary.event.size());
    if(event == DiagnosticEventName::s_Assert)
        return Core::Common::LogType::Assert;
    if(event == DiagnosticEventName::s_Error)
        return Core::Common::LogType::Error;
    if(event == DiagnosticEventName::s_Fatal)
        return Core::Common::LogType::Fatal;

    return Core::Common::LogType::EssentialInfo;
}

static void AppendAcceptedIngestDetails(const CrashScratchPath& rawPath, const bool rawArchived, Core::Alloc::ScratchArena& arena, CrashText& outReport){
    if(!outReport.empty() && outReport.back() != '\n')
        outReport += s_ReportLineEnd;

    outReport += "\ningest:\n";
    outReport += "ingest_status=accepted\nraw_archive=";
    outReport += PathToString<char>(arena, rawPath);
    outReport += s_ReportLineEnd;
    if(!rawArchived)
        outReport += "warning=raw upload archive could not be retained\n";
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


struct ArchiveFileHeader{
    AStringView relativePath;
    usize fileSize = 0u;
};

[[nodiscard]] static Expected<ArchiveFileHeader> ParseFileHeader(const AStringView line)noexcept{
    constexpr AStringView prefix(CrashNames::s_ArchiveFileHeaderPrefix);
    if(line.size() <= prefix.size() || AStringView(line.data(), prefix.size()) != prefix)
        return MakeUnexpected(Failure{});

    const AStringView body(line.data() + prefix.size(), line.size() - prefix.size());
    usize split = Limit<usize>::s_Max;
    for(usize i = body.size(); i > 0u; --i){
        if(body[i - 1u] == ' '){
            split = i - 1u;
            break;
        }
    }
    if(split == Limit<usize>::s_Max || split == 0u || split + 1u >= body.size())
        return MakeUnexpected(Failure{});

    const AStringView sizeText(body.data() + split + 1u, body.size() - split - 1u);
    const auto fileSize = ParseU64(sizeText);
    if(!fileSize || *fileSize > static_cast<u64>(Limit<usize>::s_Max))
        return MakeUnexpected(Failure{});

    const AStringView relativePath(body.data(), split);
    if(!IsSafeArchiveRelativePath(relativePath))
        return MakeUnexpected(Failure{});
    return ArchiveFileHeader{ relativePath, static_cast<usize>(*fileSize) };
}

[[nodiscard]] static Expected<void, ErrorCode> WriteExtractedFile(Core::Alloc::ScratchArena& scratchArena, const CrashScratchPath& packageDirectory, const AStringView relativePath, const u8* bytes, const usize byteCount){
    const ::Path<Core::Alloc::ScratchArena> outputPath = ::Path<Core::Alloc::ScratchArena>(scratchArena, packageDirectory) / relativePath;
    const auto outputDirectory = outputPath.parentPath();
    if(!outputDirectory.empty()){
        const auto ensured = EnsureDirectories(outputDirectory);
        if(!ensured)
            return MakeUnexpected(ensured.error());
    }

    if(!GlobalFilesystemDetail::CanRepresentStreamSize(static_cast<u64>(byteCount)))
        return MakeUnexpected(MakeErrorCode(ErrorCondition::value_too_large));
    const BinaryByteView fileBytes{ bytes, byteCount };
    if(!WriteBinaryFile(outputPath, fileBytes))
        return MakeUnexpected(MakeErrorCode(ErrorCondition::io_error));
    return {};
}

[[nodiscard]] static Expected<void, IngestFailure> ExtractCrashArchive(Core::Alloc::ScratchArena& scratchArena, const CrashScratchPath& archivePath, const CrashScratchPath& packageDirectory){
    CrashBytes archiveBytes{scratchArena};
    const auto read = ReadBinaryFile(archivePath, archiveBytes);
    if(!read)
        return MakeUnexpected(IngestFailure{ .detail = "failed to read crash archive", .code = read.error(), });

    usize cursor = 0u;
    auto line = NextLfByteLine(archiveBytes, cursor);
    if(!line || *line != CrashNames::s_ArchiveHeaderLine){
        return MakeUnexpected(IngestFailure{ .detail = "invalid crash archive header", });
    }

    const auto emptied = EnsureEmptyDirectory(packageDirectory);
    if(!emptied)
        return MakeUnexpected(IngestFailure{ .detail = "failed to create extracted crash package directory", .code = emptied.error(), });

    usize fileCount = 0u;
    while(cursor < archiveBytes.size()){
        line = NextLfByteLine(archiveBytes, cursor);
        if(!line){
            return MakeUnexpected(IngestFailure{ .detail = "truncated crash archive file header", });
        }
        const auto header = ParseFileHeader(*line);
        if(!header){
            return MakeUnexpected(IngestFailure{ .detail = "malformed crash archive file header", });
        }
        const usize fileSize = header->fileSize;
        if(cursor > archiveBytes.size() || fileSize > archiveBytes.size() - cursor){
            return MakeUnexpected(IngestFailure{ .detail = "truncated crash archive file payload", });
        }

        const auto written = WriteExtractedFile(scratchArena, packageDirectory, header->relativePath, archiveBytes.data() + cursor, fileSize);
        if(!written)
            return MakeUnexpected(IngestFailure{ .detail = "failed to write extracted crash package file", .code = written.error(), });
        cursor += fileSize;

        const auto separator = NextLfByteLine(archiveBytes, cursor);
        if(!separator || !separator->empty()){
            return MakeUnexpected(IngestFailure{ .detail = s_MalformedFileFooterDetail, });
        }
        const auto endMarker = NextLfByteLine(archiveBytes, cursor);
        if(!endMarker || *endMarker != CrashNames::s_ArchiveEntryEndLine){
            return MakeUnexpected(IngestFailure{ .detail = s_MalformedFileFooterDetail, });
        }

        ++fileCount;
    }

    if(fileCount == 0u){
        return MakeUnexpected(IngestFailure{ .detail = "crash archive contained no files", });
    }

    return {};
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


[[nodiscard]] static Expected<usize> FindGeneratedJsonValueCursor(Core::Alloc::ScratchArena& scratchArena, const AStringView manifest, const AStringView key){
    AString<Core::Alloc::ScratchArena> needle{scratchArena};
    needle.reserve(key.size() + s_GeneratedJsonNeedleReserveSlack);
    needle += '"';
    needle += key;
    needle += "\": ";

    const usize cursor = manifest.find(AStringView(needle.data(), needle.size()));
    if(cursor == AStringView::npos)
        return MakeUnexpected(Failure{});
    return cursor + needle.size();
}

template<typename ArenaT>
[[nodiscard]] static Expected<AString<ArenaT>> FindGeneratedJsonStringValue(ArenaT& arena, Core::Alloc::ScratchArena& scratchArena, const AStringView manifest, const AStringView key){
    const auto valueCursor = FindGeneratedJsonValueCursor(scratchArena, manifest, key);
    if(!valueCursor)
        return MakeUnexpected(Failure{});
    usize cursor = *valueCursor;
    if(cursor >= manifest.size() || manifest[cursor] != '"')
        return MakeUnexpected(Failure{});
    ++cursor;

    usize reserveEnd = cursor;
    for(; reserveEnd < manifest.size(); ++reserveEnd){
        if(manifest[reserveEnd] == '\\' && reserveEnd + 1u < manifest.size()){
            ++reserveEnd;
            continue;
        }
        if(manifest[reserveEnd] == '"')
            break;
    }
    AString<ArenaT> parsedValue{arena};
    parsedValue.reserve(reserveEnd - cursor);
    for(; cursor < manifest.size(); ++cursor){
        const char ch = manifest[cursor];
        if(ch == '"'){
            return parsedValue;
        }

        if(ch != '\\'){
            parsedValue.push_back(ch);
            continue;
        }

        ++cursor;
        if(cursor >= manifest.size())
            return MakeUnexpected(Failure{});

        switch(manifest[cursor]){
        case '"':
        case '\\':
            parsedValue.push_back(manifest[cursor]);
            break;
        case 'n':
            parsedValue.push_back('\n');
            break;
        case 'r':
            parsedValue.push_back('\r');
            break;
        case 't':
            parsedValue.push_back('\t');
            break;
        default:
            return MakeUnexpected(Failure{});
        }
    }

    return MakeUnexpected(Failure{});
}

[[nodiscard]] static Expected<u64> FindGeneratedJsonUnsignedValue(Core::Alloc::ScratchArena& scratchArena, const AStringView manifest, const AStringView key){
    const auto valueCursor = FindGeneratedJsonValueCursor(scratchArena, manifest, key);
    if(!valueCursor)
        return MakeUnexpected(Failure{});
    usize cursor = *valueCursor;

    const usize begin = cursor;
    while(cursor < manifest.size() && manifest[cursor] >= '0' && manifest[cursor] <= '9')
        ++cursor;
    if(cursor == begin)
        return MakeUnexpected(Failure{});

    return ParseU64(AStringView(manifest.data() + begin, cursor - begin));
}

[[nodiscard]] static Expected<bool> FindGeneratedJsonBoolValue(Core::Alloc::ScratchArena& scratchArena, const AStringView manifest, const AStringView key){
    const auto valueCursor = FindGeneratedJsonValueCursor(scratchArena, manifest, key);
    if(!valueCursor)
        return MakeUnexpected(Failure{});
    usize cursor = *valueCursor;

    constexpr AStringView s_TrueText("true");
    constexpr AStringView s_FalseText("false");
    if(cursor + s_TrueText.size() <= manifest.size() && AStringView(manifest.data() + cursor, s_TrueText.size()) == s_TrueText){
        return true;
    }
    if(cursor + s_FalseText.size() <= manifest.size() && AStringView(manifest.data() + cursor, s_FalseText.size()) == s_FalseText){
        return false;
    }
    return MakeUnexpected(Failure{});
}

[[nodiscard]] static Expected<CrashPackageSummary, IngestFailure> ValidateManifest(LogArena& arena, Core::Alloc::ScratchArena& scratchArena, const CrashScratchPath& packageDirectory){
    CrashPackageSummary outSummary(arena);
    AString<Core::Alloc::ScratchArena> manifest{scratchArena};
    const auto read = ReadTextFile(packageDirectory / CrashNames::s_ManifestFileName, manifest);
    if(!read)
        return MakeUnexpected(IngestFailure{ .detail = "missing manifest.json", .code = read.error(), });

    const AStringView manifestText(manifest.data(), manifest.size());
    const auto manifestFormat = FindGeneratedJsonStringValue(scratchArena, scratchArena, manifestText, CrashNames::s_ManifestFormatKey);
    const auto requireString = [&scratchArena, manifestText](const AStringView key){
        return FindGeneratedJsonStringValue(scratchArena, scratchArena, manifestText, key);
    };
    const auto requireUnsigned = [&scratchArena, manifestText](const AStringView key){
        return FindGeneratedJsonUnsignedValue(scratchArena, manifestText, key);
    };
    const auto requireBool = [&scratchArena, manifestText](const AStringView key){
        return FindGeneratedJsonBoolValue(scratchArena, manifestText, key);
    };
    auto crashId = FindGeneratedJsonStringValue(arena, scratchArena, manifestText, CrashNames::s_ManifestCrashIdKey);
    auto platform = FindGeneratedJsonStringValue(arena, scratchArena, manifestText, CrashNames::s_ManifestPlatformKey);
    auto reasonKind = FindGeneratedJsonStringValue(arena, scratchArena, manifestText, CrashNames::s_ManifestReasonKindKey);
    auto reasonCode = FindGeneratedJsonUnsignedValue(scratchArena, manifestText, CrashNames::s_ManifestReasonCodeKey);
    auto threadId = FindGeneratedJsonUnsignedValue(scratchArena, manifestText, CrashNames::s_ManifestThreadIdKey);
    auto event = FindGeneratedJsonStringValue(arena, scratchArena, manifestText, CrashNames::s_ManifestEventKey);
    auto triggerExpression = FindGeneratedJsonStringValue(arena, scratchArena, manifestText, CrashNames::s_ManifestTriggerExpressionKey);
    auto triggerMessage = FindGeneratedJsonStringValue(arena, scratchArena, manifestText, CrashNames::s_ManifestTriggerMessageKey);
    auto triggerFile = FindGeneratedJsonStringValue(arena, scratchArena, manifestText, CrashNames::s_ManifestTriggerFileKey);
    auto triggerLine = FindGeneratedJsonUnsignedValue(scratchArena, manifestText, CrashNames::s_ManifestTriggerLineKey);
    auto artifactStrategy = FindGeneratedJsonStringValue(arena, scratchArena, manifestText, CrashNames::s_ManifestArtifactStrategyKey);
    if(
        !manifestFormat
        || !crashId
        || !requireString(CrashNames::s_ManifestApplicationKey)
        || !requireString(CrashNames::s_ManifestVersionKey)
        || !requireString(CrashNames::s_ManifestBuildIdKey)
        || !requireString(CrashNames::s_ManifestAbiKey)
        || !platform
        || !reasonKind
        || !reasonCode
        || !requireUnsigned(CrashNames::s_ManifestProcessIdKey)
        || !threadId
        || !requireBool(CrashNames::s_ManifestHasExceptionContextKey)
        || !requireUnsigned(CrashNames::s_ManifestFaultAddressKey)
        || !requireUnsigned(CrashNames::s_ManifestInstructionPointerKey)
        || !requireUnsigned(CrashNames::s_ManifestStackPointerKey)
        || !requireUnsigned(CrashNames::s_ManifestFramePointerKey)
        || !event
        || !requireString(CrashNames::s_ManifestTriggerCategoryKey)
        || !triggerExpression
        || !triggerMessage
        || !triggerFile
        || !triggerLine
        || !requireString(CrashNames::s_ManifestDumpDetailModeKey)
        || !artifactStrategy
        || !requireString(CrashNames::s_ManifestHandlerLifetimeKey)
    ){
        return MakeUnexpected(IngestFailure{ .detail = "manifest.json is missing required fields", });
    }

    outSummary.crashId = Move(*crashId);
    outSummary.platform = Move(*platform);
    outSummary.reasonKind = Move(*reasonKind);
    outSummary.reasonCode = *reasonCode;
    outSummary.threadId = *threadId;
    outSummary.event = Move(*event);
    outSummary.triggerExpression = Move(*triggerExpression);
    outSummary.triggerMessage = Move(*triggerMessage);
    outSummary.triggerFile = Move(*triggerFile);
    outSummary.triggerLine = *triggerLine;
    outSummary.artifactStrategy = Move(*artifactStrategy);

    if(*manifestFormat != CrashNames::s_ManifestFormatValue){
        return MakeUnexpected(IngestFailure{ .detail = "manifest.json has unsupported crash package format", });
    }
    if(outSummary.crashId.empty() || outSummary.platform.empty() || outSummary.reasonKind.empty() || outSummary.artifactStrategy.empty() || outSummary.event.empty()){
        return MakeUnexpected(IngestFailure{ .detail = "manifest.json contains empty required fields", });
    }

    return outSummary;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


[[nodiscard]] static CrashIngestResult RejectCrashUpload(LogArena& arena, Core::Alloc::ScratchArena& scratchArena, const CrashScratchPath& archivePath, const CrashScratchPath& packageDirectory, const CrashIngestConfig& config, const IngestFailure& failure){
    if(!RemoveAllIfExists(packageDirectory))
        NWB_LOGGER_WARNING(NWB_TEXT("Failed to remove rejected crash package directory"));

    const auto invalidPath = ::MovePathToDirectory(archivePath, CrashInvalidDirectory(scratchArena, config.storageDirectory));
    if(!invalidPath)
        NWB_LOGGER_WARNING(NWB_TEXT("Failed to move rejected crash archive to invalid directory"));
    ApplyRetention(scratchArena, config);

    CrashIngestResult result(arena);
    result.type = Core::Common::LogType::Error;
    result.message = StringFormat(arena, NWB_TEXT("Crash upload rejected: {}; raw='{}'"), StringConvert(failure.detail), PathToString<tchar>(invalidPath ? *invalidPath : archivePath));
    if(failure.code)
        result.message += StringFormat(arena, NWB_TEXT("; native_category='{}'; native_code={}"), StringConvert(failure.code->category().name()), failure.code->value());
    return result;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


CrashIngestResult ProcessCrashUpload(LogArena& arena, const Path& archivePath, const CrashIngestConfig& config){
    namespace Ingest = __hidden_logger_crash_ingest;

    Core::Alloc::ScratchArena scratchArena(Ingest::s_IngestScratchArenaName);
    CrashIngestResult result(arena);
    const CrashScratchPath operationArchivePath(scratchArena, archivePath);
    const CrashScratchPath packageDirectory = CrashExtractedPackageDirectory(scratchArena, config.storageDirectory, operationArchivePath);

    const auto extracted = Ingest::ExtractCrashArchive(scratchArena, operationArchivePath, packageDirectory);
    if(!extracted)
        return Ingest::RejectCrashUpload(arena, scratchArena, operationArchivePath, packageDirectory, config, extracted.error());

    auto summaryResult = Ingest::ValidateManifest(arena, scratchArena, packageDirectory);
    if(!summaryResult)
        return Ingest::RejectCrashUpload(arena, scratchArena, operationArchivePath, packageDirectory, config, summaryResult.error());
    const CrashPackageSummary& summary = *summaryResult;

    CrashReportText symbolicationReport = BuildCrashSymbolicationReport(arena, packageDirectory, summary, config.symbolication, scratchArena);
    // Report-write failure retains the valid package and in-memory report; it is not a malformed crash.
    if(!WriteTextFile(packageDirectory / s_ServerSymbolicationFileName, AStringView(symbolicationReport.data(), symbolicationReport.size())))
        NWB_LOGGER_WARNING(NWB_TEXT("Failed to persist server crash symbolication report; retaining the package and returning the in-memory report"));

    const auto rawPath = ::MovePathToDirectory(operationArchivePath, CrashRawDirectory(scratchArena, config.storageDirectory));
    const bool rawArchived = rawPath.has_value();
    if(!rawArchived){
        if(const auto removed = RemoveFile(archivePath); !removed || !*removed)
            NWB_LOGGER_WARNING(NWB_TEXT("Failed to remove crash archive that could not be retained"));
    }
    Ingest::ApplyRetention(scratchArena, config);

    result.accepted = true;
    result.type = rawArchived ? Ingest::AcceptedCrashLogType(summary) : Core::Common::LogType::Warning;
    Ingest::AppendAcceptedIngestDetails(rawArchived ? *rawPath : operationArchivePath, rawArchived, scratchArena, symbolicationReport);
    result.message = StringConvert(arena, AStringView(symbolicationReport.data(), symbolicationReport.size()));
    return result;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_LOG_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

