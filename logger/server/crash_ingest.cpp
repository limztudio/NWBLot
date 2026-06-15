// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "crash_ingest.h"
#include "crash_paths.h"

#include <core/crash/package_names.h>

#include <global/filesystem/directory_iterator.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_LOG_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace __hidden_logger_crash_ingest{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


using CrashText = CrashReportText;
using CrashBytes = Vector<u8, LogArena>;
namespace CrashNames = ::NWB::Core::Crash::PackageNames;

inline constexpr u8 s_MinSafeArchivePathCharacter = 0x20u;
inline constexpr usize s_GeneratedJsonStringNeedleReserveSlack = 5u;
inline constexpr usize s_GeneratedJsonUnsignedNeedleReserveSlack = 4u;

struct ByteView{
    using value_type = u8;

    const u8* bytes = nullptr;
    usize byteCount = 0u;

    [[nodiscard]] bool empty()const{ return byteCount == 0u; }
    [[nodiscard]] usize size()const{ return byteCount; }
    [[nodiscard]] const u8* data()const{ return bytes; }
};

static void ApplyRetentionToDirectory(LogArena& arena, const Path& directory, const usize maxEntries){
    if(maxEntries == 0u)
        return;

    ErrorCode error;
    const bool exists = IsDirectory(directory, error);
    if(error || !exists)
        return;

    Vector<Path, LogArena> entries{arena};
    DirectoryIterator directoryIt(directory, error);
    if(error)
        return;

    for(const auto& entry : directoryIt)
        entries.emplace_back(arena, entry.path());

    if(entries.size() <= maxEntries)
        return;

    Sort(entries.begin(), entries.end());

    const usize removeCount = entries.size() - maxEntries;
    for(usize i = 0u; i < removeCount; ++i){
        error.clear();
        static_cast<void>(RemoveAllIfExists(entries[i], error));
    }
}

static void ApplyRetention(LogArena& arena, const CrashIngestConfig& config){
    ApplyRetentionToDirectory(arena, CrashExtractedDirectory(arena, config.storageDirectory), config.retention.maxExtractedPackages);
    ApplyRetentionToDirectory(arena, CrashRawDirectory(arena, config.storageDirectory), config.retention.maxRawArchives);
    ApplyRetentionToDirectory(arena, CrashInvalidDirectory(arena, config.storageDirectory), config.retention.maxInvalidArchives);
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


[[nodiscard]] static bool ReadArchiveLine(const CrashBytes& archiveBytes, usize& inOutCursor, AStringView& outLine){
    outLine = AStringView();
    if(inOutCursor >= archiveBytes.size())
        return false;

    const usize begin = inOutCursor;
    while(inOutCursor < archiveBytes.size() && archiveBytes[inOutCursor] != static_cast<u8>('\n'))
        ++inOutCursor;
    if(inOutCursor >= archiveBytes.size())
        return false;

    usize end = inOutCursor;
    ++inOutCursor;
    if(end > begin && archiveBytes[end - 1u] == static_cast<u8>('\r'))
        --end;

    outLine = AStringView(reinterpret_cast<const char*>(archiveBytes.data() + begin), end - begin);
    return true;
}

[[nodiscard]] static bool IsPathSeparator(const char ch)noexcept{
    return ch == '/' || ch == '\\';
}

[[nodiscard]] static bool IsSafeArchiveRelativePath(const AStringView pathText)noexcept{
    if(pathText.empty() || IsPathSeparator(pathText.front()))
        return false;

    usize segmentBegin = 0u;
    for(usize i = 0u; i <= pathText.size(); ++i){
        const bool atEnd = i == pathText.size();
        const char ch = atEnd ? '/' : pathText[i];
        if(!atEnd && (static_cast<unsigned char>(ch) < s_MinSafeArchivePathCharacter || ch == ':'))
            return false;

        if(!atEnd && !IsPathSeparator(ch))
            continue;

        const usize segmentLength = i - segmentBegin;
        if(segmentLength == 0u)
            return false;

        const AStringView segment(pathText.data() + segmentBegin, segmentLength);
        if(segment == "." || segment == "..")
            return false;

        segmentBegin = i + 1u;
    }

    return true;
}

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
        static_cast<void>(EnsureDirectories(outputDirectory, error));
        if(error)
            return false;
    }

    const ByteView fileBytes{ bytes, byteCount };
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
    if(!ReadArchiveLine(archiveBytes, cursor, line) || line != CrashNames::s_ArchiveHeaderLine){
        outError = "invalid crash archive header";
        return false;
    }

    ErrorCode error;
    static_cast<void>(EnsureEmptyDirectory(packageDirectory, error));
    if(error){
        outError = "failed to create extracted crash package directory";
        return false;
    }

    usize fileCount = 0u;
    while(cursor < archiveBytes.size()){
        if(!ReadArchiveLine(archiveBytes, cursor, line)){
            outError = "truncated crash archive file header";
            return false;
        }
        if(line.empty())
            continue;

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
        if(!ReadArchiveLine(archiveBytes, cursor, separator) || !separator.empty() || !ReadArchiveLine(archiveBytes, cursor, endMarker) || endMarker != CrashNames::s_ArchiveEntryEndLine){
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


[[nodiscard]] static bool FindGeneratedJsonStringValue(LogArena& arena, const AStringView manifest, const AStringView key, CrashText& outValue){
    outValue.clear();

    CrashText needle{arena};
    needle.reserve(key.size() + s_GeneratedJsonStringNeedleReserveSlack);
    needle += '"';
    needle += key;
    needle += "\": ";

    usize cursor = manifest.find(AStringView(needle.data(), needle.size()));
    if(cursor == AStringView::npos)
        return false;
    cursor += needle.size();
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

    CrashText needle{arena};
    needle.reserve(key.size() + s_GeneratedJsonUnsignedNeedleReserveSlack);
    needle += '"';
    needle += key;
    needle += "\": ";

    usize cursor = manifest.find(AStringView(needle.data(), needle.size()));
    if(cursor == AStringView::npos)
        return false;
    cursor += needle.size();

    const usize begin = cursor;
    while(cursor < manifest.size() && manifest[cursor] >= '0' && manifest[cursor] <= '9')
        ++cursor;
    if(cursor == begin)
        return false;

    return ParseU64(AStringView(manifest.data() + begin, cursor - begin), outValue);
}

[[nodiscard]] static bool FindGeneratedJsonBoolValue(LogArena& arena, const AStringView manifest, const AStringView key, bool& outValue){
    outValue = false;

    CrashText needle{arena};
    needle.reserve(key.size() + s_GeneratedJsonUnsignedNeedleReserveSlack);
    needle += '"';
    needle += key;
    needle += "\": ";

    usize cursor = manifest.find(AStringView(needle.data(), needle.size()));
    if(cursor == AStringView::npos)
        return false;
    cursor += needle.size();

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
    CrashText requiredText{arena};
    u64 requiredUnsigned = 0u;
    bool requiredBool = false;
    if(
        !FindGeneratedJsonStringValue(arena, manifestText, CrashNames::s_ManifestFormatKey, outSummary.format)
        || !FindGeneratedJsonStringValue(arena, manifestText, CrashNames::s_ManifestCrashIdKey, outSummary.crashId)
        || !FindGeneratedJsonStringValue(arena, manifestText, CrashNames::s_ManifestApplicationKey, requiredText)
        || !FindGeneratedJsonStringValue(arena, manifestText, CrashNames::s_ManifestVersionKey, requiredText)
        || !FindGeneratedJsonStringValue(arena, manifestText, CrashNames::s_ManifestBuildIdKey, requiredText)
        || !FindGeneratedJsonStringValue(arena, manifestText, CrashNames::s_ManifestAbiKey, requiredText)
        || !FindGeneratedJsonStringValue(arena, manifestText, CrashNames::s_ManifestPlatformKey, outSummary.platform)
        || !FindGeneratedJsonStringValue(arena, manifestText, CrashNames::s_ManifestReasonKindKey, outSummary.reasonKind)
        || !FindGeneratedJsonUnsignedValue(arena, manifestText, CrashNames::s_ManifestReasonCodeKey, outSummary.reasonCode)
        || !FindGeneratedJsonUnsignedValue(arena, manifestText, CrashNames::s_ManifestProcessIdKey, requiredUnsigned)
        || !FindGeneratedJsonUnsignedValue(arena, manifestText, CrashNames::s_ManifestThreadIdKey, outSummary.threadId)
        || !FindGeneratedJsonBoolValue(arena, manifestText, CrashNames::s_ManifestHasExceptionContextKey, requiredBool)
        || !FindGeneratedJsonUnsignedValue(arena, manifestText, CrashNames::s_ManifestFaultAddressKey, requiredUnsigned)
        || !FindGeneratedJsonUnsignedValue(arena, manifestText, CrashNames::s_ManifestInstructionPointerKey, requiredUnsigned)
        || !FindGeneratedJsonUnsignedValue(arena, manifestText, CrashNames::s_ManifestStackPointerKey, requiredUnsigned)
        || !FindGeneratedJsonUnsignedValue(arena, manifestText, CrashNames::s_ManifestFramePointerKey, requiredUnsigned)
        || !FindGeneratedJsonStringValue(arena, manifestText, CrashNames::s_ManifestEventKey, outSummary.event)
        || !FindGeneratedJsonStringValue(arena, manifestText, CrashNames::s_ManifestTriggerCategoryKey, requiredText)
        || !FindGeneratedJsonStringValue(arena, manifestText, CrashNames::s_ManifestTriggerExpressionKey, requiredText)
        || !FindGeneratedJsonStringValue(arena, manifestText, CrashNames::s_ManifestTriggerMessageKey, requiredText)
        || !FindGeneratedJsonStringValue(arena, manifestText, CrashNames::s_ManifestTriggerFileKey, requiredText)
        || !FindGeneratedJsonUnsignedValue(arena, manifestText, CrashNames::s_ManifestTriggerLineKey, requiredUnsigned)
        || !FindGeneratedJsonStringValue(arena, manifestText, CrashNames::s_ManifestDumpDetailModeKey, requiredText)
        || !FindGeneratedJsonBoolValue(arena, manifestText, CrashNames::s_ManifestGpuDumpsEnabledKey, requiredBool)
        || !FindGeneratedJsonStringValue(arena, manifestText, CrashNames::s_ManifestArtifactStrategyKey, outSummary.artifactStrategy)
        || !FindGeneratedJsonStringValue(arena, manifestText, CrashNames::s_ManifestHandlerLifetimeKey, requiredText)
    ){
        outError = CrashNames::s_ManifestFileName;
        outError += " is missing required fields";
        return false;
    }

    if(outSummary.format != CrashNames::s_ManifestFormatValue){
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
    static_cast<void>(RemoveAllIfExists(packageDirectory, removeError));

    Path invalidPath(arena);
    static_cast<void>(::MovePathToDirectory(archivePath, CrashInvalidDirectory(arena, config.storageDirectory), invalidPath));
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
        if(!WriteTextFile(packageDirectory / s_ServerSymbolicationFileName, AStringView(symbolicationReport.data(), symbolicationReport.size())))
            return Ingest::RejectCrashUpload(arena, archivePath, packageDirectory, config, AStringView("failed to write server symbolication report"));
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
        static_cast<void>(RemoveFile(archivePath, removeError));
    }
    Ingest::ApplyRetention(arena, config);

    result.accepted = true;
    result.type = rawArchived
        ? Type::EssentialInfo
        : Type::Warning
    ;
    if(rawArchived){
        result.message = StringFormat(
            arena,
            NWB_TEXT("Crash package '{}' ingested: platform='{}' reason='{}' package='{}' raw='{}'\n{}"),
            StringConvert(summary.crashId),
            StringConvert(summary.platform),
            StringConvert(summary.reasonKind),
            PathToString<tchar>(packageDirectory),
            PathToString<tchar>(rawPath),
            StringConvert(symbolicationReport)
        );
    }
    else{
        result.message = StringFormat(
            arena,
            NWB_TEXT("Crash package '{}' ingested but raw upload archive could not be retained: platform='{}' reason='{}' package='{}' raw='{}'\n{}"),
            StringConvert(summary.crashId),
            StringConvert(summary.platform),
            StringConvert(summary.reasonKind),
            PathToString<tchar>(packageDirectory),
            PathToString<tchar>(archivePath),
            StringConvert(symbolicationReport)
        );
    }
    return result;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_LOG_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

