// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "crash_ingest.h"

#include "crash_symbolicate.h"


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_LOG_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace __hidden_logger_crash_ingest{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


using CrashText = CrashReportText;
using CrashBytes = Vector<u8, LogArena>;

struct ByteView{
    using value_type = u8;

    const u8* bytes = nullptr;
    usize byteCount = 0u;

    [[nodiscard]] bool empty()const{ return byteCount == 0u; }
    [[nodiscard]] usize size()const{ return byteCount; }
    [[nodiscard]] const u8* data()const{ return bytes; }
};

[[nodiscard]] static Path CrashRootDirectory(LogArena& arena){
    Path executableDirectory(arena);
    if(GetExecutableDirectory(executableDirectory))
        return executableDirectory / "crashes";

    return Path(arena, "crashes");
}

[[nodiscard]] static Path RawCrashDirectory(LogArena& arena){
    return CrashRootDirectory(arena) / "raw";
}

[[nodiscard]] static Path InvalidCrashDirectory(LogArena& arena){
    return CrashRootDirectory(arena) / "invalid";
}

[[nodiscard]] static Path ExtractedCrashDirectory(LogArena& arena){
    return CrashRootDirectory(arena) / "packages";
}

[[nodiscard]] static Path ExtractedPackageDirectory(LogArena& arena, const Path& archivePath){
    return ExtractedCrashDirectory(arena) / archivePath.stem();
}

[[nodiscard]] static bool MovePathToDirectory(const Path& sourcePath, const Path& destinationDirectory, Path& outPath){
    ErrorCode error;
    static_cast<void>(EnsureDirectories(destinationDirectory, error));
    if(error)
        return false;

    outPath = destinationDirectory / sourcePath.filename();
    error.clear();
    static_cast<void>(RemoveAllIfExists(outPath, error));
    if(error)
        return false;

    error.clear();
    static_cast<void>(RenamePath(sourcePath, outPath, error));
    return !error;
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
        if(!atEnd && (static_cast<unsigned char>(ch) < 0x20u || ch == ':'))
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

    constexpr AStringView prefix("FILE ");
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
    if(!ReadArchiveLine(archiveBytes, cursor, line) || line != "NWBCRASHPKG 1"){
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
        if(!ReadArchiveLine(archiveBytes, cursor, separator) || !separator.empty() || !ReadArchiveLine(archiveBytes, cursor, endMarker) || endMarker != "END"){
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
    needle.reserve(key.size() + 5u);
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
    needle.reserve(key.size() + 4u);
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

[[nodiscard]] static bool ValidateManifest(LogArena& arena, const Path& packageDirectory, CrashPackageSummary& outSummary, CrashText& outError){
    CrashText manifest{arena};
    if(!ReadTextFile(packageDirectory / "manifest.json", manifest)){
        outError = "missing manifest.json";
        return false;
    }

    const AStringView manifestText(manifest.data(), manifest.size());
    if(
        !FindGeneratedJsonStringValue(arena, manifestText, "format", outSummary.format)
        || !FindGeneratedJsonStringValue(arena, manifestText, "crash_id", outSummary.crashId)
        || !FindGeneratedJsonStringValue(arena, manifestText, "platform", outSummary.platform)
        || !FindGeneratedJsonStringValue(arena, manifestText, "reason_kind", outSummary.reasonKind)
        || !FindGeneratedJsonStringValue(arena, manifestText, "artifact_strategy", outSummary.artifactStrategy)
        || !FindGeneratedJsonUnsignedValue(arena, manifestText, "thread_id", outSummary.threadId)
    ){
        outError = "manifest.json is missing required v1 fields";
        return false;
    }

    if(outSummary.format != "nwb-crash-package-v1"){
        outError = "manifest.json has unsupported crash package format";
        return false;
    }
    if(outSummary.crashId.empty() || outSummary.platform.empty() || outSummary.reasonKind.empty() || outSummary.artifactStrategy.empty()){
        outError = "manifest.json contains empty required v1 fields";
        return false;
    }

    return true;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


CrashIngestResult ProcessCrashUpload(LogArena& arena, const Path& archivePath){
    namespace Ingest = __hidden_logger_crash_ingest;

    CrashIngestResult result(arena);
    Ingest::CrashText error(arena);
    const Path packageDirectory = Ingest::ExtractedPackageDirectory(arena, archivePath);

    if(!Ingest::ExtractCrashArchive(arena, archivePath, packageDirectory, error)){
        ErrorCode removeError;
        static_cast<void>(RemoveAllIfExists(packageDirectory, removeError));

        Path invalidPath(arena);
        static_cast<void>(Ingest::MovePathToDirectory(archivePath, Ingest::InvalidCrashDirectory(arena), invalidPath));
        result.type = Type::Error;
        result.message = StringFormat(
            arena,
            NWB_TEXT("Crash upload rejected: {}; raw='{}'"),
            StringConvert(error),
            PathToString<tchar>(invalidPath.empty() ? archivePath : invalidPath)
        );
        return result;
    }

    CrashPackageSummary summary(arena);
    if(!Ingest::ValidateManifest(arena, packageDirectory, summary, error)){
        ErrorCode removeError;
        static_cast<void>(RemoveAllIfExists(packageDirectory, removeError));

        Path invalidPath(arena);
        static_cast<void>(Ingest::MovePathToDirectory(archivePath, Ingest::InvalidCrashDirectory(arena), invalidPath));
        result.type = Type::Error;
        result.message = StringFormat(
            arena,
            NWB_TEXT("Crash upload rejected: {}; raw='{}'"),
            StringConvert(error),
            PathToString<tchar>(invalidPath.empty() ? archivePath : invalidPath)
        );
        return result;
    }

    const CrashReportText symbolicationReport = BuildCrashSymbolicationReport(arena, packageDirectory, summary);
    static_cast<void>(WriteTextFile(packageDirectory / "server_symbolication.txt", AStringView(symbolicationReport.data(), symbolicationReport.size())));

    Path rawPath(arena);
    static_cast<void>(Ingest::MovePathToDirectory(archivePath, Ingest::RawCrashDirectory(arena), rawPath));

    result.accepted = true;
    result.type = Type::EssentialInfo;
    result.message = StringFormat(
        arena,
        NWB_TEXT("Crash package '{}' ingested: platform='{}' reason='{}' package='{}' raw='{}'"),
        StringConvert(summary.crashId),
        StringConvert(summary.platform),
        StringConvert(summary.reasonKind),
        PathToString<tchar>(packageDirectory),
        PathToString<tchar>(rawPath.empty() ? archivePath : rawPath)
    );
    return result;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_LOG_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

