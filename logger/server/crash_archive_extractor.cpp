// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "crash_archive_extractor.h"

#include <global/binary.h>
#include <global/filesystem/archive.h>
#include <global/text_utils.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_LOG_BEGIN


namespace __hidden_crash_archive_extractor{

using CrashBytes = Vector<u8, Core::Alloc::ScratchArena>;
inline constexpr AStringView s_MalformedFileFooterDetail = "malformed crash archive file footer";

struct ArchiveFileHeader{
    AStringView relativePath;
    usize fileSize = 0u;
};

[[nodiscard]] static Expected<ArchiveFileHeader> ParseFileHeader(const AStringView line)noexcept{
    constexpr AStringView prefix(Core::Crash::PackageNames::s_ArchiveFileHeaderPrefix);
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

[[nodiscard]] static Expected<void, ErrorCode> WriteExtractedFile(Core::Alloc::ScratchArena& scratchArena, const CrashScratchPath& packageDirectory, const AStringView relativePath, const BinaryByteView fileBytes){
    const ::Path<Core::Alloc::ScratchArena> outputPath = ::Path<Core::Alloc::ScratchArena>(scratchArena, packageDirectory) / relativePath;
    const auto outputDirectory = outputPath.parentPath();
    if(!outputDirectory.empty()){
        const auto ensured = EnsureDirectories(outputDirectory);
        if(!ensured)
            return MakeUnexpected(ensured.error());
    }

    if(!CanRepresentStreamSize(static_cast<u64>(fileBytes.size())))
        return MakeUnexpected(MakeErrorCode(ErrorCondition::value_too_large));
    if(!WriteBinaryFile(outputPath, fileBytes))
        return MakeUnexpected(MakeErrorCode(ErrorCondition::io_error));
    return {};
}


};


Expected<void, CrashIngestFailure> CrashArchiveExtractor::Extract(Core::Alloc::ScratchArena& scratchArena, const CrashScratchPath& archivePath, const CrashScratchPath& packageDirectory){
    using namespace __hidden_crash_archive_extractor;
    CrashBytes archiveBytes{scratchArena};
    const auto read = ReadBinaryFile(archivePath, archiveBytes);
    if(!read)
        return MakeUnexpected(CrashIngestFailure{ .detail = "failed to read crash archive", .code = read.error(), });

    usize cursor = 0u;
    auto line = NextLfByteLine(archiveBytes, cursor);
    if(!line || *line != Core::Crash::PackageNames::s_ArchiveHeaderLine){
        return MakeUnexpected(CrashIngestFailure{ .detail = "invalid crash archive header", });
    }

    const auto emptied = EnsureEmptyDirectory(packageDirectory);
    if(!emptied)
        return MakeUnexpected(CrashIngestFailure{ .detail = "failed to create extracted crash package directory", .code = emptied.error(), });

    usize fileCount = 0u;
    while(cursor < archiveBytes.size()){
        line = NextLfByteLine(archiveBytes, cursor);
        if(!line){
            return MakeUnexpected(CrashIngestFailure{ .detail = "truncated crash archive file header", });
        }
        const auto header = ParseFileHeader(*line);
        if(!header){
            return MakeUnexpected(CrashIngestFailure{ .detail = "malformed crash archive file header", });
        }
        const usize fileSize = header->fileSize;
        if(cursor > archiveBytes.size() || fileSize > archiveBytes.size() - cursor){
            return MakeUnexpected(CrashIngestFailure{ .detail = "truncated crash archive file payload", });
        }

        const auto written = WriteExtractedFile(scratchArena, packageDirectory, header->relativePath, BinaryByteView{ archiveBytes.data() + cursor, fileSize });
        if(!written)
            return MakeUnexpected(CrashIngestFailure{ .detail = "failed to write extracted crash package file", .code = written.error(), });
        cursor += fileSize;

        const auto separator = NextLfByteLine(archiveBytes, cursor);
        if(!separator || !separator->empty()){
            return MakeUnexpected(CrashIngestFailure{ .detail = s_MalformedFileFooterDetail, });
        }
        const auto endMarker = NextLfByteLine(archiveBytes, cursor);
        if(!endMarker || *endMarker != Core::Crash::PackageNames::s_ArchiveEntryEndLine){
            return MakeUnexpected(CrashIngestFailure{ .detail = s_MalformedFileFooterDetail, });
        }

        ++fileCount;
    }

    if(fileCount == 0u){
        return MakeUnexpected(CrashIngestFailure{ .detail = "crash archive contained no files", });
    }

    return {};
}


NWB_LOG_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
