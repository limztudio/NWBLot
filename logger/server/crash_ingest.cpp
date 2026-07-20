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

[[nodiscard]] static Type::Enum AcceptedCrashLogType(const CrashPackageSummary& summary){
    if(summary.event == DiagnosticEventName::s_Assert)
        return Type::Assert;
    if(summary.event == DiagnosticEventName::s_Error)
        return Type::Error;
    if(summary.event == DiagnosticEventName::s_Fatal)
        return Type::Fatal;

    return Type::EssentialInfo;
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


[[nodiscard]] static bool ParseFileHeader(const AStringView line, AStringView& outRelativePath, usize& outFileSize){
    outRelativePath = AStringView();
    outFileSize = 0u;

    constexpr AStringView prefix(CrashNames::s_ArchiveFileHeaderPrefix);
    if(line.size() <= prefix.size() || AStringView(line.data(), prefix.size()) != prefix)
        return false;

    const AStringView body(line.data() + prefix.size(), line.size() - prefix.size());
    usize split = Limit<usize>::s_Max;
    for(usize i = body.size(); i > 0u; --i){
        if(body[i - 1u] == ' '){
            split = i - 1u;
            break;
        }
    }
    if(split == Limit<usize>::s_Max || split == 0u || split + 1u >= body.size())
        return false;

    const AStringView sizeText(body.data() + split + 1u, body.size() - split - 1u);
    u64 fileSize = 0u;
    if(!ParseU64(sizeText, fileSize) || fileSize > static_cast<u64>(Limit<usize>::s_Max))
        return false;

    outRelativePath = AStringView(body.data(), split);
    outFileSize = static_cast<usize>(fileSize);
    return IsSafeArchiveRelativePath(outRelativePath);
}

[[nodiscard]] static bool WriteExtractedFile(LogArena& arena, const Path& packageDirectory, const AStringView relativePath, const u8* bytes, const usize byteCount){
    const Path outputPath = packageDirectory / Path(arena, relativePath);
    const Path outputDirectory = outputPath.parent_path();
    ErrorCode error;
    if(!outputDirectory.empty()){
        if(!EnsureDirectories(outputDirectory, error))
            return false;
    }

    const BinaryByteView fileBytes{ bytes, byteCount };
    return WriteBinaryFile(outputPath, fileBytes);
}

[[nodiscard]] static bool ExtractCrashArchive(LogArena& arena, const Path& archivePath, const Path& packageDirectory, CrashText& outError){
    CrashBytes archiveBytes{arena};
    ErrorCode readError;
    if(!ReadBinaryFile(archivePath, archiveBytes, readError)){
        outError = "failed to read crash archive";
        return false;
    }

    usize cursor = 0u;
    AStringView line;
    if(!NextLfByteLine(archiveBytes, cursor, line) || line != CrashNames::s_ArchiveHeaderLine){
        outError = "invalid crash archive header";
        return false;
    }

    ErrorCode error;
    if(!EnsureEmptyDirectory(packageDirectory, error)){
        outError = "failed to create extracted crash package directory";
        return false;
    }

    usize fileCount = 0u;
    while(cursor < archiveBytes.size()){
        if(!NextLfByteLine(archiveBytes, cursor, line)){
            outError = "truncated crash archive file header";
            return false;
        }
        AStringView relativePath;
        usize fileSize = 0u;
        if(!ParseFileHeader(line, relativePath, fileSize)){
            outError = "malformed crash archive file header";
            return false;
        }
        if(cursor > archiveBytes.size() || fileSize > archiveBytes.size() - cursor){
            outError = "truncated crash archive file payload";
            return false;
        }

        if(!WriteExtractedFile(arena, packageDirectory, relativePath, archiveBytes.data() + cursor, fileSize)){
            outError = "failed to write extracted crash package file";
            return false;
        }
        cursor += fileSize;

        AStringView separator;
        AStringView endMarker;
        if(
            !NextLfByteLine(archiveBytes, cursor, separator)
            || !separator.empty()
            || !NextLfByteLine(archiveBytes, cursor, endMarker)
            || endMarker != CrashNames::s_ArchiveEntryEndLine
        ){
            outError = "malformed crash archive file footer";
            return false;
        }

        ++fileCount;
    }

    if(fileCount == 0u){
        outError = "crash archive contained no files";
        return false;
    }

    return true;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


[[nodiscard]] static bool FindGeneratedJsonValueCursor(LogArena& arena, const AStringView manifest, const AStringView key, usize& outCursor){
    CrashText needle{arena};
    needle.reserve(key.size() + s_GeneratedJsonNeedleReserveSlack);
    needle += '"';
    needle += key;
    needle += "\": ";

    const usize cursor = manifest.find(AStringView(needle.data(), needle.size()));
    if(cursor == AStringView::npos)
        return false;
    outCursor = cursor + needle.size();
    return true;
}

[[nodiscard]] static bool FindGeneratedJsonStringValue(LogArena& arena, const AStringView manifest, const AStringView key, CrashText& outValue){
    outValue.clear();

    usize cursor = 0u;
    if(!FindGeneratedJsonValueCursor(arena, manifest, key, cursor))
        return false;
    if(cursor >= manifest.size() || manifest[cursor] != '"')
        return false;
    ++cursor;

    CrashText parsedValue{arena};
    for(; cursor < manifest.size(); ++cursor){
        const char ch = manifest[cursor];
        if(ch == '"'){
            outValue = Move(parsedValue);
            return true;
        }

        if(ch != '\\'){
            parsedValue.push_back(ch);
            continue;
        }

        ++cursor;
        if(cursor >= manifest.size())
            return false;

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
            return false;
        }
    }

    return false;
}

[[nodiscard]] static bool FindGeneratedJsonUnsignedValue(LogArena& arena, const AStringView manifest, const AStringView key, u64& outValue){
    outValue = 0u;

    usize cursor = 0u;
    if(!FindGeneratedJsonValueCursor(arena, manifest, key, cursor))
        return false;

    const usize begin = cursor;
    while(cursor < manifest.size() && manifest[cursor] >= '0' && manifest[cursor] <= '9')
        ++cursor;
    if(cursor == begin)
        return false;

    return ParseU64(AStringView(manifest.data() + begin, cursor - begin), outValue);
}

[[nodiscard]] static bool FindGeneratedJsonBoolValue(LogArena& arena, const AStringView manifest, const AStringView key, bool& outValue){
    outValue = false;

    usize cursor = 0u;
    if(!FindGeneratedJsonValueCursor(arena, manifest, key, cursor))
        return false;

    constexpr AStringView s_TrueText("true");
    constexpr AStringView s_FalseText("false");
    if(cursor + s_TrueText.size() <= manifest.size() && AStringView(manifest.data() + cursor, s_TrueText.size()) == s_TrueText){
        outValue = true;
        return true;
    }
    if(cursor + s_FalseText.size() <= manifest.size() && AStringView(manifest.data() + cursor, s_FalseText.size()) == s_FalseText){
        outValue = false;
        return true;
    }
    return false;
}

[[nodiscard]] static bool ValidateManifest(LogArena& arena, const Path& packageDirectory, CrashPackageSummary& outSummary, CrashText& outError){
    CrashText manifest{arena};
    if(!ReadTextFile(packageDirectory / CrashNames::s_ManifestFileName, manifest)){
        outError = "missing ";
        outError += CrashNames::s_ManifestFileName;
        return false;
    }

    const AStringView manifestText(manifest.data(), manifest.size());
    CrashText manifestFormat{arena};
    const auto requireString = [&arena, manifestText](const AStringView key){
        CrashText value{arena};
        return FindGeneratedJsonStringValue(arena, manifestText, key, value);
    };
    const auto requireUnsigned = [&arena, manifestText](const AStringView key){
        u64 value = 0u;
        return FindGeneratedJsonUnsignedValue(arena, manifestText, key, value);
    };
    const auto requireBool = [&arena, manifestText](const AStringView key){
        bool value = false;
        return FindGeneratedJsonBoolValue(arena, manifestText, key, value);
    };
    if(
        !FindGeneratedJsonStringValue(arena, manifestText, CrashNames::s_ManifestFormatKey, manifestFormat)
        || !FindGeneratedJsonStringValue(arena, manifestText, CrashNames::s_ManifestCrashIdKey, outSummary.crashId)
        || !requireString(CrashNames::s_ManifestApplicationKey)
        || !requireString(CrashNames::s_ManifestVersionKey)
        || !requireString(CrashNames::s_ManifestBuildIdKey)
        || !requireString(CrashNames::s_ManifestAbiKey)
        || !FindGeneratedJsonStringValue(arena, manifestText, CrashNames::s_ManifestPlatformKey, outSummary.platform)
        || !FindGeneratedJsonStringValue(arena, manifestText, CrashNames::s_ManifestReasonKindKey, outSummary.reasonKind)
        || !FindGeneratedJsonUnsignedValue(arena, manifestText, CrashNames::s_ManifestReasonCodeKey, outSummary.reasonCode)
        || !requireUnsigned(CrashNames::s_ManifestProcessIdKey)
        || !FindGeneratedJsonUnsignedValue(arena, manifestText, CrashNames::s_ManifestThreadIdKey, outSummary.threadId)
        || !requireBool(CrashNames::s_ManifestHasExceptionContextKey)
        || !requireUnsigned(CrashNames::s_ManifestFaultAddressKey)
        || !requireUnsigned(CrashNames::s_ManifestInstructionPointerKey)
        || !requireUnsigned(CrashNames::s_ManifestStackPointerKey)
        || !requireUnsigned(CrashNames::s_ManifestFramePointerKey)
        || !FindGeneratedJsonStringValue(arena, manifestText, CrashNames::s_ManifestEventKey, outSummary.event)
        || !requireString(CrashNames::s_ManifestTriggerCategoryKey)
        || !FindGeneratedJsonStringValue(arena, manifestText, CrashNames::s_ManifestTriggerExpressionKey, outSummary.triggerExpression)
        || !FindGeneratedJsonStringValue(arena, manifestText, CrashNames::s_ManifestTriggerMessageKey, outSummary.triggerMessage)
        || !FindGeneratedJsonStringValue(arena, manifestText, CrashNames::s_ManifestTriggerFileKey, outSummary.triggerFile)
        || !FindGeneratedJsonUnsignedValue(arena, manifestText, CrashNames::s_ManifestTriggerLineKey, outSummary.triggerLine)
        || !requireString(CrashNames::s_ManifestDumpDetailModeKey)
        || !FindGeneratedJsonStringValue(arena, manifestText, CrashNames::s_ManifestArtifactStrategyKey, outSummary.artifactStrategy)
        || !requireString(CrashNames::s_ManifestHandlerLifetimeKey)
    ){
        outError = CrashNames::s_ManifestFileName;
        outError += " is missing required fields";
        return false;
    }

    if(manifestFormat != CrashNames::s_ManifestFormatValue){
        outError = CrashNames::s_ManifestFileName;
        outError += " has unsupported crash package format";
        return false;
    }
    if(outSummary.crashId.empty() || outSummary.platform.empty() || outSummary.reasonKind.empty() || outSummary.artifactStrategy.empty() || outSummary.event.empty()){
        outError = CrashNames::s_ManifestFileName;
        outError += " contains empty required fields";
        return false;
    }

    return true;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


[[nodiscard]] static CrashIngestResult RejectCrashUpload(
    LogArena& arena,
    const Path& archivePath,
    const Path& packageDirectory,
    const CrashIngestConfig& config,
    const AStringView reason
){
    ErrorCode removeError;
    if(!RemoveAllIfExists(packageDirectory, removeError))
        NWB_LOGGER_WARNING(NWB_TEXT("Failed to remove rejected crash package directory"));

    Path invalidPath(arena);
    if(!::MovePathToDirectory(archivePath, CrashInvalidDirectory(arena, config.storageDirectory), invalidPath))
        NWB_LOGGER_WARNING(NWB_TEXT("Failed to move rejected crash archive to invalid directory"));
    ApplyRetention(arena, config);

    CrashIngestResult result(arena);
    result.type = Type::Error;
    result.message = StringFormat(
        arena,
        NWB_TEXT("Crash upload rejected: {}; raw='{}'"),
        StringConvert(reason),
        PathToString<tchar>(invalidPath.empty() ? archivePath : invalidPath)
    );
    return result;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


CrashIngestResult ProcessCrashUpload(LogArena& arena, const Path& archivePath, const CrashIngestConfig& config){
    namespace Ingest = __hidden_logger_crash_ingest;

    CrashIngestResult result(arena);
    Ingest::CrashText error(arena);
    const Path packageDirectory = CrashExtractedPackageDirectory(arena, config.storageDirectory, archivePath);

    if(!Ingest::ExtractCrashArchive(arena, archivePath, packageDirectory, error)){
        return Ingest::RejectCrashUpload(arena, archivePath, packageDirectory, config, AStringView(error.data(), error.size()));
    }

    CrashPackageSummary summary(arena);
    if(!Ingest::ValidateManifest(arena, packageDirectory, summary, error)){
        return Ingest::RejectCrashUpload(arena, archivePath, packageDirectory, config, AStringView(error.data(), error.size()));
    }

    CrashReportText symbolicationReport(arena);
    try{
        symbolicationReport = BuildCrashSymbolicationReport(arena, packageDirectory, summary, config.symbolication);
        // Failing to persist the report on disk is an infrastructure error, not a malformed crash: keep the valid,
        // already-decoded package instead of quarantining it as invalid, and still return the in-memory report.
        if(!WriteTextFile(packageDirectory / s_ServerSymbolicationFileName, AStringView(symbolicationReport.data(), symbolicationReport.size())))
            NWB_LOGGER_WARNING(NWB_TEXT("Failed to persist server crash symbolication report; retaining the package and returning the in-memory report"));
    }
    catch(const GeneralException& e){
        return Ingest::RejectCrashUpload(arena, archivePath, packageDirectory, config, AStringView(e.what()));
    }
    catch(...){
        return Ingest::RejectCrashUpload(arena, archivePath, packageDirectory, config, AStringView("unknown ingest exception"));
    }

    Path rawPath(arena);
    const bool rawArchived = ::MovePathToDirectory(archivePath, CrashRawDirectory(arena, config.storageDirectory), rawPath);
    if(!rawArchived){
        ErrorCode removeError;
        if(!RemoveFile(archivePath, removeError))
            NWB_LOGGER_WARNING(NWB_TEXT("Failed to remove crash archive that could not be retained"));
    }
    Ingest::ApplyRetention(arena, config);

    result.accepted = true;
    result.type = rawArchived
        ? Ingest::AcceptedCrashLogType(summary)
        : Type::Warning
    ;
    Ingest::AppendAcceptedIngestDetails(arena, symbolicationReport, rawArchived ? rawPath : archivePath, rawArchived);
    result.message = StringConvert(arena, AStringView(symbolicationReport.data(), symbolicationReport.size()));
    return result;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_LOG_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

