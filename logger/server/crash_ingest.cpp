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
using CrashBytes = Vector<u8, LogArena>;
namespace CrashNames = ::NWB::Core::Crash::PackageNames;

inline constexpr usize s_GeneratedJsonNeedleReserveSlack = 4u;

static void ApplyRetention(LogArena& arena, const CrashIngestConfig& config){
    if(!ApplyDirectoryRetention(
        arena,
        CrashExtractedDirectory(arena, config.storageDirectory),
        config.retention.maxExtractedPackages
    ))
        NWB_LOGGER_WARNING(NWB_TEXT("Failed to apply retention to extracted crash packages"));
    if(!ApplyDirectoryRetention(
        arena,
        CrashRawDirectory(arena, config.storageDirectory),
        config.retention.maxRawArchives
    ))
        NWB_LOGGER_WARNING(NWB_TEXT("Failed to apply retention to raw crash archives"));
    if(!ApplyDirectoryRetention(
        arena,
        CrashInvalidDirectory(arena, config.storageDirectory),
        config.retention.maxInvalidArchives
    ))
        NWB_LOGGER_WARNING(NWB_TEXT("Failed to apply retention to invalid crash archives"));
}

[[nodiscard]] static Core::Common::LogType::Enum AcceptedCrashLogType(const CrashPackageSummary& summary){
    if(summary.event == DiagnosticEventName::s_Assert)
        return Core::Common::LogType::Assert;
    if(summary.event == DiagnosticEventName::s_Error)
        return Core::Common::LogType::Error;
    if(summary.event == DiagnosticEventName::s_Fatal)
        return Core::Common::LogType::Fatal;

    return Core::Common::LogType::EssentialInfo;
}

static void AppendAcceptedIngestDetails(LogArena& arena, CrashText& outReport, const Path& rawPath, const bool rawArchived){
    if(!outReport.empty() && outReport.back() != '\n')
        outReport += "\n";

    outReport += "\ningest:\n";
    outReport += "ingest_status=accepted\nraw_archive=";
    outReport += PathToString<char>(arena, rawPath);
    outReport += "\n";
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

[[nodiscard]] static bool WriteExtractedFile(LogArena& arena, const Path& packageDirectory, const AStringView relativePath, const u8* bytes, const usize byteCount){
    const Path outputPath = packageDirectory / Path(arena, relativePath);
    const Path outputDirectory = outputPath.parentPath();
    if(!outputDirectory.empty()){
        if(!EnsureDirectories(outputDirectory))
            return false;
    }

    const BinaryByteView fileBytes{ bytes, byteCount };
    return WriteBinaryFile(outputPath, fileBytes);
}

[[nodiscard]] static Expected<void, AStringView> ExtractCrashArchive(LogArena& arena, const Path& archivePath, const Path& packageDirectory){
    CrashBytes archiveBytes{arena};
    if(!ReadBinaryFile(archivePath, archiveBytes)){
        return MakeUnexpected(AStringView("failed to read crash archive"));
    }

    usize cursor = 0u;
    auto line = NextLfByteLine(archiveBytes, cursor);
    if(!line || *line != CrashNames::s_ArchiveHeaderLine){
        return MakeUnexpected(AStringView("invalid crash archive header"));
    }

    if(!EnsureEmptyDirectory(packageDirectory)){
        return MakeUnexpected(AStringView("failed to create extracted crash package directory"));
    }

    usize fileCount = 0u;
    while(cursor < archiveBytes.size()){
        line = NextLfByteLine(archiveBytes, cursor);
        if(!line){
            return MakeUnexpected(AStringView("truncated crash archive file header"));
        }
        const auto header = ParseFileHeader(*line);
        if(!header){
            return MakeUnexpected(AStringView("malformed crash archive file header"));
        }
        const usize fileSize = header->fileSize;
        if(cursor > archiveBytes.size() || fileSize > archiveBytes.size() - cursor){
            return MakeUnexpected(AStringView("truncated crash archive file payload"));
        }

        if(!WriteExtractedFile(arena, packageDirectory, header->relativePath, archiveBytes.data() + cursor, fileSize)){
            return MakeUnexpected(AStringView("failed to write extracted crash package file"));
        }
        cursor += fileSize;

        const auto separator = NextLfByteLine(archiveBytes, cursor);
        if(!separator || !separator->empty()){
            return MakeUnexpected(AStringView("malformed crash archive file footer"));
        }
        const auto endMarker = NextLfByteLine(archiveBytes, cursor);
        if(!endMarker || *endMarker != CrashNames::s_ArchiveEntryEndLine){
            return MakeUnexpected(AStringView("malformed crash archive file footer"));
        }

        ++fileCount;
    }

    if(fileCount == 0u){
        return MakeUnexpected(AStringView("crash archive contained no files"));
    }

    return {};
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


[[nodiscard]] static Expected<usize> FindGeneratedJsonValueCursor(LogArena& arena, const AStringView manifest, const AStringView key){
    CrashText needle{arena};
    needle.reserve(key.size() + s_GeneratedJsonNeedleReserveSlack);
    needle += '"';
    needle += key;
    needle += "\": ";

    const usize cursor = manifest.find(AStringView(needle.data(), needle.size()));
    if(cursor == AStringView::npos)
        return MakeUnexpected(Failure{});
    return cursor + needle.size();
}

[[nodiscard]] static Expected<CrashText> FindGeneratedJsonStringValue(LogArena& arena, const AStringView manifest, const AStringView key){
    const auto valueCursor = FindGeneratedJsonValueCursor(arena, manifest, key);
    if(!valueCursor)
        return MakeUnexpected(Failure{});
    usize cursor = *valueCursor;
    if(cursor >= manifest.size() || manifest[cursor] != '"')
        return MakeUnexpected(Failure{});
    ++cursor;

    CrashText parsedValue{arena};
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

[[nodiscard]] static Expected<u64> FindGeneratedJsonUnsignedValue(LogArena& arena, const AStringView manifest, const AStringView key){
    const auto valueCursor = FindGeneratedJsonValueCursor(arena, manifest, key);
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

[[nodiscard]] static Expected<bool> FindGeneratedJsonBoolValue(LogArena& arena, const AStringView manifest, const AStringView key){
    const auto valueCursor = FindGeneratedJsonValueCursor(arena, manifest, key);
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

[[nodiscard]] static Expected<CrashPackageSummary, AStringView> ValidateManifest(LogArena& arena, const Path& packageDirectory){
    CrashPackageSummary outSummary(arena);
    CrashText manifest{arena};
    if(!ReadTextFile(packageDirectory / CrashNames::s_ManifestFileName, manifest)){
        return MakeUnexpected(AStringView("missing manifest.json"));
    }

    const AStringView manifestText(manifest.data(), manifest.size());
    const auto manifestFormat = FindGeneratedJsonStringValue(arena, manifestText, CrashNames::s_ManifestFormatKey);
    const auto requireString = [&arena, manifestText](const AStringView key){
        return FindGeneratedJsonStringValue(arena, manifestText, key);
    };
    const auto requireUnsigned = [&arena, manifestText](const AStringView key){
        return FindGeneratedJsonUnsignedValue(arena, manifestText, key);
    };
    const auto requireBool = [&arena, manifestText](const AStringView key){
        return FindGeneratedJsonBoolValue(arena, manifestText, key);
    };
    auto crashId = FindGeneratedJsonStringValue(arena, manifestText, CrashNames::s_ManifestCrashIdKey);
    auto platform = FindGeneratedJsonStringValue(arena, manifestText, CrashNames::s_ManifestPlatformKey);
    auto reasonKind = FindGeneratedJsonStringValue(arena, manifestText, CrashNames::s_ManifestReasonKindKey);
    auto reasonCode = FindGeneratedJsonUnsignedValue(arena, manifestText, CrashNames::s_ManifestReasonCodeKey);
    auto threadId = FindGeneratedJsonUnsignedValue(arena, manifestText, CrashNames::s_ManifestThreadIdKey);
    auto event = FindGeneratedJsonStringValue(arena, manifestText, CrashNames::s_ManifestEventKey);
    auto triggerExpression = FindGeneratedJsonStringValue(arena, manifestText, CrashNames::s_ManifestTriggerExpressionKey);
    auto triggerMessage = FindGeneratedJsonStringValue(arena, manifestText, CrashNames::s_ManifestTriggerMessageKey);
    auto triggerFile = FindGeneratedJsonStringValue(arena, manifestText, CrashNames::s_ManifestTriggerFileKey);
    auto triggerLine = FindGeneratedJsonUnsignedValue(arena, manifestText, CrashNames::s_ManifestTriggerLineKey);
    auto artifactStrategy = FindGeneratedJsonStringValue(arena, manifestText, CrashNames::s_ManifestArtifactStrategyKey);
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
        return MakeUnexpected(AStringView("manifest.json is missing required fields"));
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
        return MakeUnexpected(AStringView("manifest.json has unsupported crash package format"));
    }
    if(outSummary.crashId.empty() || outSummary.platform.empty() || outSummary.reasonKind.empty() || outSummary.artifactStrategy.empty() || outSummary.event.empty()){
        return MakeUnexpected(AStringView("manifest.json contains empty required fields"));
    }

    return outSummary;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


[[nodiscard]] static CrashIngestResult RejectCrashUpload(
    LogArena& arena,
    const Path& archivePath,
    const Path& packageDirectory,
    const CrashIngestConfig& config,
    const AStringView reason
){
    if(!RemoveAllIfExists(packageDirectory))
        NWB_LOGGER_WARNING(NWB_TEXT("Failed to remove rejected crash package directory"));

    const auto invalidPath = ::MovePathToDirectory(archivePath, CrashInvalidDirectory(arena, config.storageDirectory));
    if(!invalidPath)
        NWB_LOGGER_WARNING(NWB_TEXT("Failed to move rejected crash archive to invalid directory"));
    ApplyRetention(arena, config);

    CrashIngestResult result(arena);
    result.type = Core::Common::LogType::Error;
    result.message = StringFormat(
        arena,
        NWB_TEXT("Crash upload rejected: {}; raw='{}'"),
        StringConvert(reason),
        PathToString<tchar>(invalidPath ? *invalidPath : archivePath)
    );
    return result;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


CrashIngestResult ProcessCrashUpload(LogArena& arena, const Path& archivePath, const CrashIngestConfig& config){
    namespace Ingest = __hidden_logger_crash_ingest;

    CrashIngestResult result(arena);
    const Path packageDirectory = CrashExtractedPackageDirectory(arena, config.storageDirectory, archivePath);

    const auto extracted = Ingest::ExtractCrashArchive(arena, archivePath, packageDirectory);
    if(!extracted)
        return Ingest::RejectCrashUpload(arena, archivePath, packageDirectory, config, extracted.error());

    auto summaryResult = Ingest::ValidateManifest(arena, packageDirectory);
    if(!summaryResult)
        return Ingest::RejectCrashUpload(arena, archivePath, packageDirectory, config, summaryResult.error());
    const CrashPackageSummary& summary = *summaryResult;

    CrashReportText symbolicationReport = BuildCrashSymbolicationReport(arena, packageDirectory, summary, config.symbolication);
    // Report-write failure retains the valid package and in-memory report; it is not a malformed crash.
    if(!WriteTextFile(packageDirectory / s_ServerSymbolicationFileName, AStringView(symbolicationReport.data(), symbolicationReport.size())))
        NWB_LOGGER_WARNING(NWB_TEXT("Failed to persist server crash symbolication report; retaining the package and returning the in-memory report"));

    const auto rawPath = ::MovePathToDirectory(archivePath, CrashRawDirectory(arena, config.storageDirectory));
    const bool rawArchived = rawPath.has_value();
    if(!rawArchived){
        if(const auto removed = RemoveFile(archivePath); !removed || !*removed)
            NWB_LOGGER_WARNING(NWB_TEXT("Failed to remove crash archive that could not be retained"));
    }
    Ingest::ApplyRetention(arena, config);

    result.accepted = true;
    result.type = rawArchived
        ? Ingest::AcceptedCrashLogType(summary)
        : Core::Common::LogType::Warning
    ;
    Ingest::AppendAcceptedIngestDetails(arena, symbolicationReport, rawArchived ? *rawPath : archivePath, rawArchived);
    result.message = StringConvert(arena, AStringView(symbolicationReport.data(), symbolicationReport.size()));
    return result;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_LOG_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

