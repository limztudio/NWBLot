// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "module.h"
#include "arena_names.h"


#include <core/common/log.h>
#include <core/alloc/core.h>
#include <core/alloc/scratch.h>
#include <global/binary.h>
#include <global/filesystem/volume_naming.h>
#include <global/limit.h>
#include <global/simplemath.h>

#include <cerrno>
#if defined(NWB_PLATFORM_WINDOWS)
#include <windows.h>
#else
#include <unistd.h>
#endif


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_FILESYSTEM_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace __hidden_filesystem{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


constexpr char s_StagedVolumeTokenPrefix[] = "volume_";
constexpr char s_StagedVolumeKeySeparator = '|';
constexpr usize s_StagedVolumeHashDigits = sizeof(u64) * 2u;


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


bool RemoveStagedDirectoryIfPresent(const Path& directoryPath, const AStringView operationName, const AStringView label){
    ErrorCode errorCode;

    if(!RemoveAllIfExists(directoryPath, errorCode)){
        NWB_LOGGER_ERROR(NWB_TEXT("{}: failed to remove {} '{}': {}")
            , StringConvert(operationName)
            , StringConvert(label)
            , PathToString<tchar>(directoryPath)
            , StringConvert(errorCode.message())
        );
        return false;
    }

    return true;
}

void CleanupStagedDirectoryBestEffort(const Path& directoryPath, const AStringView operationName, const AStringView label){
    ErrorCode errorCode;

    if(!RemoveAllIfExists(directoryPath, errorCode) && errorCode){
        NWB_LOGGER_WARNING(NWB_TEXT("{}: failed to remove {} '{}': {}")
            , StringConvert(operationName)
            , StringConvert(label)
            , PathToString<tchar>(directoryPath)
            , StringConvert(errorCode.message())
        );
    }
}

bool EnsureEmptyStagedDirectory(const Path& directoryPath, const AStringView operationName, const AStringView label){
    ErrorCode errorCode;

    if(!::EnsureEmptyDirectory(directoryPath, errorCode)){
        NWB_LOGGER_ERROR(NWB_TEXT("{}: failed to create {} '{}': {}")
            , StringConvert(operationName)
            , StringConvert(label)
            , PathToString<tchar>(directoryPath)
            , StringConvert(errorCode.message())
        );
        return false;
    }

    return true;
}

StagedDirectoryCleanupGuard::StagedDirectoryCleanupGuard(const Path& directoryPath, const AStringView operationName, const AStringView label)
    : m_directoryPath(directoryPath)
    , m_operationName(operationName)
    , m_label(label)
{}

StagedDirectoryCleanupGuard::~StagedDirectoryCleanupGuard(){
    if(m_active)
        CleanupStagedDirectoryBestEffort(
            m_directoryPath,
            m_operationName.view(),
            m_label.view()
        );
}

void StagedDirectoryCleanupGuard::dismiss(){
    m_active = false;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace __hidden_filesystem{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


static constexpr u64 s_VolumeDefaultMetadataBytes = 512ull * 1024ull;
static constexpr u64 s_VolumeMinMetadataBytes = 4ull * 1024ull;
static constexpr u64 s_VolumeMoveChunkBytes = 1024ull * 1024ull;
static constexpr usize s_ErrnoMessageBufferBytes = 256u;
static constexpr AStringView s_VolumePublishLogPrefix = "Filesystem volume publish";

using ::AddNoOverflow;
using ::CanRepresentU64;
using ::ValidVolumeName;


static constexpr char s_VolumeMagic[8] = { 'N', 'W', 'B', 'V', 'O', 'L', '1', '\0' };

struct VolumeHeaderDisk{
    char magic[8];
    u64 segmentSize;
    u64 metadataBytes;
    u64 fileCount;
    u64 indexBytes;
    u64 nextFreeOffset;
};

struct VolumeIndexEntryDisk{
    NameHash hash;
    u64 offset;
    u64 size;
};

static_assert(sizeof(VolumeHeaderDisk) == 48, "VolumeHeaderDisk size mismatch");
static_assert(sizeof(VolumeIndexEntryDisk) == 80, "VolumeIndexEntryDisk size mismatch");


static ACompactString FilesystemMutationFailureDetail(const ErrorCode& errorCode, const AStringView fallbackDetail){
    ACompactString detail;
    if(errorCode && detail.assign(errorCode.message()))
        return detail;

    if(!detail.assign(fallbackDetail)){
        if(!detail.assign("filesystem operation failed"))
            return {};
    }
    return detail;
}


static bool ComputeVolumeIndexBytes(const u64 fileCount, u64& outIndexBytes){
    outIndexBytes = 0;
    if(fileCount > Limit<u64>::s_Max / static_cast<u64>(sizeof(VolumeIndexEntryDisk)))
        return false;

    outIndexBytes = fileCount * static_cast<u64>(sizeof(VolumeIndexEntryDisk));
    return true;
}

static bool ComputeVolumeMetadataRequirement(const u64 fileCount, u64& outMetadataBytes){
    outMetadataBytes = 0;

    u64 indexBytes = 0;
    if(!ComputeVolumeIndexBytes(fileCount, indexBytes))
        return false;

    return AddNoOverflow(static_cast<u64>(sizeof(VolumeHeaderDisk)), indexBytes, outMetadataBytes);
}

static u64 DefaultMetadataBytes(const u64 segmentSize){
    u64 output = s_VolumeDefaultMetadataBytes;
    if(output >= segmentSize)
        output = segmentSize / 8;
    if(output < s_VolumeMinMetadataBytes)
        output = s_VolumeMinMetadataBytes;
    return output;
}

static bool ToStreamOff(const u64 value, GlobalFilesystemDetail::StreamOffset& out){
    if(!CanRepresentU64<GlobalFilesystemDetail::StreamOffset>(value))
        return false;
    out = static_cast<GlobalFilesystemDetail::StreamOffset>(value);
    return true;
}

static bool ToStreamSize(const u64 value, GlobalFilesystemDetail::StreamSize& out){
    if(!CanRepresentU64<GlobalFilesystemDetail::StreamSize>(value))
        return false;
    out = static_cast<GlobalFilesystemDetail::StreamSize>(value);
    return true;
}

static bool ResizeFile(const Path& path, const u64 byteCount, ErrorCode& outError){
#if defined(NWB_PLATFORM_WINDOWS)
    if(byteCount > static_cast<u64>(Limit<LONGLONG>::s_Max)){
        outError = std::make_error_code(std::errc::value_too_large);
        return false;
    }

    HANDLE file = CreateFile(
        path.c_str(),
        GENERIC_WRITE,
        FILE_SHARE_READ,
        nullptr,
        OPEN_EXISTING,
        FILE_ATTRIBUTE_NORMAL,
        nullptr
    );
    if(file == INVALID_HANDLE_VALUE){
        GlobalFilesystemDetail::SetLastSystemError(outError);
        return false;
    }

    LARGE_INTEGER offset{};
    offset.QuadPart = static_cast<LONGLONG>(byteCount);
    const bool seekSucceeded = SetFilePointerEx(file, offset, nullptr, FILE_BEGIN) != 0;
    const bool resizeSucceeded = seekSucceeded && SetEndOfFile(file) != 0;
    if(resizeSucceeded)
        GlobalFilesystemDetail::ClearError(outError);
    else
        GlobalFilesystemDetail::SetLastSystemError(outError);
    CloseHandle(file);
    return resizeSucceeded;
#else
    if(!CanRepresentU64<off_t>(byteCount)){
        outError = std::make_error_code(std::errc::value_too_large);
        return false;
    }

    if(::truncate(path.c_str(), static_cast<off_t>(byteCount)) == 0){
        GlobalFilesystemDetail::ClearError(outError);
        return true;
    }

    GlobalFilesystemDetail::SetLastSystemError(outError);
    return false;
#endif
}

static ACompactString LastErrnoMessage(){
    const i32 errorNumber = errno;
    if(errorNumber == 0)
        return ACompactString("none");

    char errorText[s_ErrnoMessageBufferBytes] = {};
    if(NWB_STRERROR(errorText, sizeof(errorText), errorNumber) != 0)
        return ACompactString("unknown");

    ACompactString output(errorText);
    output += " (";
    char numberText[TextDetail::s_DecimalTextBufferBytes] = {};
    output += FormatDecimal(errorNumber, numberText);
    output += ")";
    return output;
}

static void LogFailure(AStringView volumeName, AStringView operation, AStringView detail){
    NWB_LOGGER_WARNING(NWB_TEXT("Filesystem('{}'): {} failed: {}")
        , StringConvert(volumeName)
        , StringConvert(operation)
        , StringConvert(detail)
    );
}

static void LogFailureWithPath(AStringView volumeName, AStringView operation, const Path& path, AStringView detail){
    NWB_LOGGER_WARNING(NWB_TEXT("Filesystem('{}'): {} failed on '{}': {}")
        , StringConvert(volumeName)
        , StringConvert(operation)
        , StringConvert(path.string())
        , StringConvert(detail)
    );
}

static void LogFailureWithFsError(AStringView volumeName, AStringView operation, const Path& path, const ErrorCode& errorCode){
    NWB_LOGGER_WARNING(NWB_TEXT("Filesystem('{}'): {} failed on '{}': [{}] {}")
        , StringConvert(volumeName)
        , StringConvert(operation)
        , StringConvert(path.string())
        , errorCode.value()
        , StringConvert(errorCode.message())
    );
}

static bool ReadVolumeHeaderFromSegment(
    const AStringView volumeName,
    const Path& segmentPath,
    VolumeHeaderDisk& outHeader
){
    outHeader = {};

    GlobalFilesystemDetail::InputFileStream stream(
        segmentPath,
        GlobalFilesystemDetail::InputFileStream::binary
    );
    if(!stream.is_open()){
        LogFailureWithPath(volumeName, "mount:open_header", segmentPath, LastErrnoMessage());
        return false;
    }

    stream.read(
        reinterpret_cast<char*>(&outHeader),
        static_cast<GlobalFilesystemDetail::StreamSize>(sizeof(outHeader))
    );
    if(stream.good())
        return true;
    if(stream.eof() && stream.gcount() == static_cast<GlobalFilesystemDetail::StreamSize>(sizeof(outHeader)))
        return true;

    LogFailureWithPath(volumeName, "mount:read_header", segmentPath, LastErrnoMessage());
    return false;
}

template<typename SegmentPaths, typename ChunkFunc>
static bool ForEachSegmentChunk(
    const AStringView volumeName,
    const AStringView operation,
    const SegmentPaths& segmentPaths,
    const u64 segmentSize,
    const u64 offset,
    const u64 byteCount,
    ChunkFunc&& chunkFunc){
    if(segmentSize == 0){
        LogFailure(volumeName, operation, "segment size is zero");
        return false;
    }

    u64 endOffset = 0;
    if(!AddNoOverflow(offset, byteCount, endOffset)){
        LogFailure(volumeName, operation, "offset overflow");
        return false;
    }
    if(static_cast<u64>(segmentPaths.size()) > Limit<u64>::s_Max / segmentSize){
        LogFailure(volumeName, operation, "capacity overflow");
        return false;
    }
    const u64 capacityBytes = static_cast<u64>(segmentPaths.size()) * segmentSize;
    if(endOffset > capacityBytes){
        NWB_LOGGER_WARNING(NWB_TEXT("Filesystem('{}'): {} failed: range [{}..{}) exceeds capacity {}")
            , StringConvert(volumeName)
            , StringConvert(operation)
            , offset
            , endOffset
            , capacityBytes
        );
        return false;
    }

    u64 currentOffset = offset;
    u64 remainingBytes = byteCount;
    while(remainingBytes > 0){
        const usize segmentIndex = static_cast<usize>(currentOffset / segmentSize);
        const u64 segmentOffset = currentOffset % segmentSize;
        const u64 chunkBytes = Min(remainingBytes, segmentSize - segmentOffset);

        GlobalFilesystemDetail::StreamOffset streamOffset = 0;
        GlobalFilesystemDetail::StreamSize streamChunkSize = 0;
        if(!ToStreamOff(segmentOffset, streamOffset)){
            LogFailure(volumeName, operation, "segment offset cannot be represented as stream offset");
            return false;
        }
        if(!ToStreamSize(chunkBytes, streamChunkSize)){
            LogFailure(volumeName, operation, "chunk size cannot be represented as stream size");
            return false;
        }

        if(!chunkFunc(segmentIndex, streamOffset, streamChunkSize, chunkBytes))
            return false;

        currentOffset += chunkBytes;
        remainingBytes -= chunkBytes;
    }

    return true;
}

template<typename SegmentPaths, typename Stream, typename SeekStream, typename TransferStream>
static bool TransferSegmentChunk(
    const AStringView volumeName,
    const SegmentPaths& segmentPaths,
    const usize segmentIndex,
    const GlobalFilesystemDetail::StreamOffset streamOffset,
    const AStringView openOperation,
    const AStringView seekOperation,
    Stream& stream,
    SeekStream&& seekStream,
    TransferStream&& transferStream){
    if(!stream.is_open()){
        LogFailureWithPath(volumeName, openOperation, segmentPaths[segmentIndex], LastErrnoMessage());
        return false;
    }

    seekStream(stream, streamOffset);
    if(!stream.good()){
        LogFailureWithPath(volumeName, seekOperation, segmentPaths[segmentIndex], LastErrnoMessage());
        return false;
    }

    return transferStream(stream);
}

template<typename SegmentPaths, typename Bytes, typename TransferChunk>
static bool TransferVolumeBytes(
    const AStringView volumeName,
    const AStringView operation,
    const SegmentPaths& segmentPaths,
    const u64 segmentSize,
    const u64 offset,
    const u64 byteCount,
    Bytes& bytes,
    TransferChunk&& transferChunk
){
    return ForEachSegmentChunk(
        volumeName,
        operation,
        segmentPaths,
        segmentSize,
        offset,
        byteCount,
        [&](
            const usize segmentIndex,
            const GlobalFilesystemDetail::StreamOffset streamOffset,
            const GlobalFilesystemDetail::StreamSize streamChunkSize,
            const u64 chunkBytes
        ){
            return transferChunk(
                volumeName,
                segmentPaths,
                segmentIndex,
                streamOffset,
                streamChunkSize,
                chunkBytes,
                bytes
            );
        }
    );
}

template<typename SegmentPaths>
static bool ReadSegmentBytes(
    const AStringView volumeName,
    const SegmentPaths& segmentPaths,
    const usize segmentIndex,
    const GlobalFilesystemDetail::StreamOffset streamOffset,
    const GlobalFilesystemDetail::StreamSize streamChunkSize,
    const u64 chunkBytes,
    u8*& outputBytes){
    GlobalFilesystemDetail::InputFileStream stream(
        segmentPaths[segmentIndex],
        GlobalFilesystemDetail::InputFileStream::binary
    );
    return TransferSegmentChunk(
        volumeName,
        segmentPaths,
        segmentIndex,
        streamOffset,
        "readBytes:open",
        "readBytes:seek",
        stream,
        [](auto& stream, const GlobalFilesystemDetail::StreamOffset offset){ stream.seekg(offset); },
        [&](auto& stream){
            stream.read(reinterpret_cast<char*>(outputBytes), streamChunkSize);
            if(stream.gcount() != streamChunkSize){
                NWB_LOGGER_WARNING(NWB_TEXT("Filesystem('{}'): readBytes failed on '{}': requested {} bytes, received {} bytes, errno {}")
                    , StringConvert(volumeName)
                    , StringConvert(segmentPaths[segmentIndex].string())
                    , static_cast<i64>(streamChunkSize)
                    , static_cast<i64>(stream.gcount())
                    , StringConvert(LastErrnoMessage())
                );
                return false;
            }

            outputBytes += chunkBytes;
            return true;
        }
    );
}

template<typename SegmentPaths>
static bool WriteSegmentBytes(
    const AStringView volumeName,
    const SegmentPaths& segmentPaths,
    const usize segmentIndex,
    const GlobalFilesystemDetail::StreamOffset streamOffset,
    const GlobalFilesystemDetail::StreamSize streamChunkSize,
    const u64 chunkBytes,
    const u8*& inputBytes){
    GlobalFilesystemDetail::FileStream stream(
        segmentPaths[segmentIndex],
        GlobalFilesystemDetail::FileStream::binary
            | GlobalFilesystemDetail::FileStream::in
            | GlobalFilesystemDetail::FileStream::out
    );
    return TransferSegmentChunk(
        volumeName,
        segmentPaths,
        segmentIndex,
        streamOffset,
        "writeBytes:open",
        "writeBytes:seek",
        stream,
        [](auto& stream, const GlobalFilesystemDetail::StreamOffset offset){ stream.seekp(offset); },
        [&](auto& stream){
            stream.write(reinterpret_cast<const char*>(inputBytes), streamChunkSize);
            if(!stream.good()){
                NWB_LOGGER_WARNING(NWB_TEXT("Filesystem('{}'): writeBytes failed on '{}': attempted {} bytes, errno {}")
                    , StringConvert(volumeName)
                    , StringConvert(segmentPaths[segmentIndex].string())
                    , static_cast<i64>(streamChunkSize)
                    , StringConvert(LastErrnoMessage())
                );
                return false;
            }

            inputBytes += chunkBytes;
            return true;
        }
    );
}

struct ReadSegmentBytesOp{
    template<typename SegmentPaths>
    bool operator()(
        const AStringView volumeName,
        const SegmentPaths& segmentPaths,
        const usize segmentIndex,
        const GlobalFilesystemDetail::StreamOffset streamOffset,
        const GlobalFilesystemDetail::StreamSize streamChunkSize,
        const u64 chunkBytes,
        u8*& outputBytes
    )const{
        return ReadSegmentBytes(
            volumeName,
            segmentPaths,
            segmentIndex,
            streamOffset,
            streamChunkSize,
            chunkBytes,
            outputBytes
        );
    }
};

struct WriteSegmentBytesOp{
    template<typename SegmentPaths>
    bool operator()(
        const AStringView volumeName,
        const SegmentPaths& segmentPaths,
        const usize segmentIndex,
        const GlobalFilesystemDetail::StreamOffset streamOffset,
        const GlobalFilesystemDetail::StreamSize streamChunkSize,
        const u64 chunkBytes,
        const u8*& inputBytes
    )const{
        return WriteSegmentBytes(
            volumeName,
            segmentPaths,
            segmentIndex,
            streamOffset,
            streamChunkSize,
            chunkBytes,
            inputBytes
        );
    }
};

using StagedVolumePaths = StagedDirectoryPaths;

template<typename FileNameVector>
static bool RestoreVolumeSegments(const Path& fromDirectory, const Path& toDirectory, const FileNameVector& fileNames);

static StagedVolumePaths BuildStagedVolumePaths(const Path& outputDirectory, const AStringView volumeName){
    Core::Alloc::ScratchArena scratchArena(FilesystemArenaScope::s_StagedVolumePathsScratch);
    AString<Core::Alloc::ScratchArena> stageKey = PathToString<char>(scratchArena, outputDirectory);
    stageKey += s_StagedVolumeKeySeparator;
    stageKey += volumeName;

    AString<Core::Alloc::ScratchArena> stageToken{scratchArena};
    stageToken.reserve((sizeof(s_StagedVolumeTokenPrefix) - 1u) + s_StagedVolumeHashDigits);
    stageToken += s_StagedVolumeTokenPrefix;
    AppendHexU64<char, Core::Alloc::ScratchArena>(ComputeFnv64Text(AStringView(stageKey)), stageToken);
    return BuildStagedDirectoryPaths(scratchArena, outputDirectory, stageToken);
}

template<typename FileNameVector>
static bool MoveExistingVolumeSegments(const Path& fromDirectory, const Path& toDirectory, const AStringView volumeName, FileNameVector& outMovedFileNames){
    ErrorCode errorCode;

    outMovedFileNames.clear();

    const auto rollbackMovedFiles = [&]() -> void {
        if(outMovedFileNames.empty())
            return;

        if(!RestoreVolumeSegments(toDirectory, fromDirectory, outMovedFileNames)){
            NWB_LOGGER_WARNING(NWB_TEXT("Filesystem volume publish: failed to roll back existing output volume after backup failure"));
            return;
        }

        CleanupStagedDirectoryBestEffort(toDirectory, s_VolumePublishLogPrefix, "backup directory");
        outMovedFileNames.clear();
    };

    const bool sourceExists = FileExists(fromDirectory, errorCode);
    if(errorCode){
        NWB_LOGGER_ERROR(NWB_TEXT("Filesystem volume publish: failed to query output directory '{}': {}")
            , PathToString<tchar>(fromDirectory)
            , StringConvert(errorCode.message())
        );
        return false;
    }
    if(!sourceExists)
        return true;

    errorCode.clear();
    if(!IsDirectory(fromDirectory, errorCode)){
        if(errorCode){
            NWB_LOGGER_ERROR(NWB_TEXT("Filesystem volume publish: failed to inspect output directory '{}': {}")
                , PathToString<tchar>(fromDirectory)
                , StringConvert(errorCode.message())
            );
        }
        else{
            NWB_LOGGER_ERROR(NWB_TEXT("Filesystem volume publish: output path '{}' is not a directory")
                , PathToString<tchar>(fromDirectory)
            );
        }
        return false;
    }

    bool destinationCreated = false;
    const auto ensureDestination = [&]() -> bool {
        if(destinationCreated)
            return true;

        if(!EnsureDirectories(toDirectory, errorCode)){
            NWB_LOGGER_ERROR(NWB_TEXT("Filesystem volume publish: failed to create backup directory '{}': {}")
                , PathToString<tchar>(toDirectory)
                , StringConvert(errorCode.message())
            );
            return false;
        }

        destinationCreated = true;
        return true;
    };

    const auto moveSegmentToBackup = [&](const Path& currentPath) -> bool{
        if(!ensureDestination())
            return false;
        if(!RenamePath(currentPath, toDirectory / currentPath.filename(), errorCode)){
            NWB_LOGGER_ERROR(NWB_TEXT("Filesystem volume publish: failed to move existing segment '{}' to backup: {}")
                , PathToString<tchar>(currentPath)
                , StringConvert(errorCode.message())
            );
            rollbackMovedFiles();
            return false;
        }

        outMovedFileNames.push_back(currentPath.filename());
        return true;
    };

    for(usize segmentIndex = 0;; ++segmentIndex){
        const Path currentPath = fromDirectory / ::MakeVolumeSegmentFileName(volumeName, segmentIndex).c_str();
        const bool exists = FileExists(currentPath, errorCode);
        if(errorCode){
            NWB_LOGGER_ERROR(NWB_TEXT("Filesystem volume publish: failed to query volume segment '{}': {}")
                , PathToString<tchar>(currentPath)
                , StringConvert(errorCode.message())
            );
            rollbackMovedFiles();
            return false;
        }
        if(!exists)
            break;

        if(!moveSegmentToBackup(currentPath))
            return false;

        if(segmentIndex == Limit<usize>::s_Max){
            NWB_LOGGER_ERROR(NWB_TEXT("Filesystem volume publish: segment index overflow while backing up existing volume"));
            rollbackMovedFiles();
            return false;
        }
    }

    return true;
}

template<typename FileNameVector>
static bool RestoreVolumeSegments(const Path& fromDirectory, const Path& toDirectory, const FileNameVector& fileNames){
    ErrorCode errorCode;

    if(fileNames.empty())
        return true;
    if(!EnsureDirectories(toDirectory, errorCode)){
        NWB_LOGGER_WARNING(NWB_TEXT("Filesystem volume publish: failed to recreate output directory '{}' during rollback: {}")
            , PathToString<tchar>(toDirectory)
            , StringConvert(errorCode.message())
        );
        return false;
    }

    for(const Path& fileName : fileNames){
        const Path sourcePath = fromDirectory / fileName;
        const Path destinationPath = toDirectory / fileName;
        if(!RenamePath(sourcePath, destinationPath, errorCode)){
            NWB_LOGGER_WARNING(NWB_TEXT("Filesystem volume publish: failed to restore backup segment '{}' during rollback: {}")
                , PathToString<tchar>(sourcePath)
                , StringConvert(errorCode.message())
            );
            return false;
        }
    }

    return true;
}

static bool MoveStagedVolumeSegments(const Path& fromDirectory, const Path& toDirectory, const AStringView volumeName, const usize segmentCount, usize& outMovedCount){
    ErrorCode errorCode;

    outMovedCount = 0;

    if(segmentCount == 0){
        NWB_LOGGER_ERROR(NWB_TEXT("Filesystem volume publish: staged volume '{}' did not produce any segments"), StringConvert(volumeName));
        return false;
    }
    if(!EnsureDirectories(toDirectory, errorCode)){
        NWB_LOGGER_ERROR(NWB_TEXT("Filesystem volume publish: failed to create output directory '{}': {}")
            , PathToString<tchar>(toDirectory)
            , StringConvert(errorCode.message())
        );
        return false;
    }

    for(usize segmentIndex = 0; segmentIndex < segmentCount; ++segmentIndex){
        const Path fileName(fromDirectory.arena(), ::MakeVolumeSegmentFileName(volumeName, segmentIndex).c_str());
        const Path sourcePath = fromDirectory / fileName;
        const Path destinationPath = toDirectory / fileName;
        if(!RenamePath(sourcePath, destinationPath, errorCode)){
            NWB_LOGGER_ERROR(NWB_TEXT("Filesystem volume publish: failed to promote staged segment '{}' to '{}': {}")
                , PathToString<tchar>(sourcePath)
                , PathToString<tchar>(destinationPath)
                , StringConvert(errorCode.message())
            );
            return false;
        }

        ++outMovedCount;
    }

    return true;
}

static void RemovePromotedVolumeSegmentsBestEffort(const Path& outputDirectory, const AStringView volumeName, const usize segmentCount){
    ErrorCode errorCode;

    for(usize segmentIndex = 0; segmentIndex < segmentCount; ++segmentIndex){
        const Path segmentPath = outputDirectory / ::MakeVolumeSegmentFileName(volumeName, segmentIndex).c_str();
        errorCode.clear();
        if(!RemoveFile(segmentPath, errorCode)){
            NWB_LOGGER_WARNING(NWB_TEXT("Filesystem volume publish: failed to remove promoted segment '{}' after failed promotion: {}")
                , PathToString<tchar>(segmentPath)
                , StringConvert(FilesystemMutationFailureDetail(errorCode, "segment was not present"))
            );
        }
    }
}

static bool PromoteStagedVolume(const StagedVolumePaths& stagedPaths, const Path& outputDirectory, const AStringView volumeName, const usize segmentCount){
    Core::Alloc::ScratchArena scratchArena(FilesystemArenaScope::s_PromoteStagedVolumeScratch);
    Vector<Path, Core::Alloc::ScratchArena> movedBackupFiles{scratchArena};
    if(!MoveExistingVolumeSegments(outputDirectory, stagedPaths.backupDirectory, volumeName, movedBackupFiles))
        return false;

    usize movedStageSegmentCount = 0;
    if(!MoveStagedVolumeSegments(stagedPaths.stageDirectory, outputDirectory, volumeName, segmentCount, movedStageSegmentCount)){
        RemovePromotedVolumeSegmentsBestEffort(outputDirectory, volumeName, movedStageSegmentCount);
        if(RestoreVolumeSegments(stagedPaths.backupDirectory, outputDirectory, movedBackupFiles)){
            CleanupStagedDirectoryBestEffort(stagedPaths.backupDirectory, s_VolumePublishLogPrefix, "backup directory");
            CleanupStagedDirectoryBestEffort(stagedPaths.stageDirectory, s_VolumePublishLogPrefix, "stage directory");
        }
        return false;
    }

    CleanupStagedDirectoryBestEffort(stagedPaths.backupDirectory, s_VolumePublishLogPrefix, "backup directory");
    CleanupStagedDirectoryBestEffort(stagedPaths.stageDirectory, s_VolumePublishLogPrefix, "stage directory");

    return true;
}

static bool RemoveExistingVolumeSegments(const Path& outputDirectory, const AStringView volumeName){
    ErrorCode errorCode;

    const bool outputExists = FileExists(outputDirectory, errorCode);
    if(errorCode){
        NWB_LOGGER_ERROR(NWB_TEXT("Failed to query output directory '{}' : {}")
            , PathToString<tchar>(outputDirectory)
            , StringConvert(errorCode.message())
        );
        return false;
    }
    if(!outputExists)
        return true;

    errorCode.clear();
    if(!IsDirectory(outputDirectory, errorCode)){
        if(errorCode){
            NWB_LOGGER_ERROR(NWB_TEXT("Failed to inspect output directory '{}' : {}")
                , PathToString<tchar>(outputDirectory)
                , StringConvert(errorCode.message())
            );
        }
        else{
            NWB_LOGGER_ERROR(NWB_TEXT("Failed to remove old segments: output path '{}' is not a directory")
                , PathToString<tchar>(outputDirectory)
            );
        }
        return false;
    }

    for(usize segmentIndex = 0;; ++segmentIndex){
        const Path hashedPath = outputDirectory / ::MakeVolumeSegmentFileName(volumeName, segmentIndex).c_str();

        const bool exists = FileExists(hashedPath, errorCode);
        if(errorCode){
            NWB_LOGGER_ERROR(NWB_TEXT("Failed to query hashed segment '{}' : {}")
                , PathToString<tchar>(hashedPath)
                , StringConvert(errorCode.message())
            );
            return false;
        }
        if(!exists)
            break;

        if(!RemoveFile(hashedPath, errorCode)){
            NWB_LOGGER_ERROR(NWB_TEXT("Failed to remove old hashed segment '{}' : {}")
                , PathToString<tchar>(hashedPath)
                , StringConvert(FilesystemMutationFailureDetail(errorCode, "segment was not present"))
            );
            return false;
        }

        if(segmentIndex == Limit<usize>::s_Max){
            NWB_LOGGER_ERROR(NWB_TEXT("Segment index overflow while removing old hashed segments"));
            return false;
        }
    }

    return true;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


bool ComputeVolumeMetadataRequirement(const u64 fileCount, u64& outMetadataBytes){
    return __hidden_filesystem::ComputeVolumeMetadataRequirement(fileCount, outMetadataBytes);
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


bool BuildVolume(const Path& outputDirectory, const VolumeBuildConfig& config, const VolumeBuildFileMap& files, VolumeBuildInfo& outBuildInfo){
    outBuildInfo = {};

    const __hidden_filesystem::StagedVolumePaths stagedVolumePaths = __hidden_filesystem::BuildStagedVolumePaths(outputDirectory, config.volumeName.view());
    if(!EnsureEmptyStagedDirectory(stagedVolumePaths.stageDirectory, __hidden_filesystem::s_VolumePublishLogPrefix, "stage directory"))
        return false;
    StagedDirectoryCleanupGuard stageDirectoryCleanup(stagedVolumePaths.stageDirectory, __hidden_filesystem::s_VolumePublishLogPrefix);
    if(!RemoveStagedDirectoryIfPresent(stagedVolumePaths.backupDirectory, __hidden_filesystem::s_VolumePublishLogPrefix, "backup directory"))
        return false;

    Alloc::GlobalArena arena(FilesystemArenaScope::s_BuildVolumeArena);

    {
        VolumeSession volumeSession(arena);
        if(!volumeSession.create(stagedVolumePaths.stageDirectory, config))
            return false;
        volumeSession.reserveFileCapacity(files.size());

        for(const auto& [virtualPath, payloadBytes] : files){
            if(!volumeSession.pushDataDeferred(virtualPath, payloadBytes))
                return false;
        }
        if(!volumeSession.flush())
            return false;

        outBuildInfo.fileCount = volumeSession.fileCount();
        outBuildInfo.segmentCount = static_cast<u64>(volumeSession.segmentCount());
    }

    if(
        !__hidden_filesystem::PromoteStagedVolume(
            stagedVolumePaths,
            outputDirectory,
            config.volumeName.view(),
            static_cast<usize>(outBuildInfo.segmentCount)
        )
    ){
        return false;
    }
    stageDirectoryCleanup.dismiss();

    return true;
}

bool PublishStagedVolume(const StagedDirectoryPaths& stagedPaths, const Path& outputDirectory, const AStringView volumeName, const usize segmentCount){
    return __hidden_filesystem::PromoteStagedVolume(stagedPaths, outputDirectory, volumeName, segmentCount);
}

bool RemoveVolumeSegments(const Path& outputDirectory, const AStringView volumeName){
    if(!__hidden_filesystem::ValidVolumeName(volumeName)){
        __hidden_filesystem::LogFailure(volumeName, "remove", "invalid volume name");
        return false;
    }

    return __hidden_filesystem::RemoveExistingVolumeSegments(outputDirectory, volumeName);
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


VolumeFileSystem::VolumeFileSystem(Alloc::GlobalArena& arena)
    : m_mountDirectory(arena)
    , m_arena(arena)
    , m_segmentPaths(m_arena)
    , m_files(0, Hasher<Name>(), EqualTo<Name>(), m_arena)
{}

VolumeFileSystem::~VolumeFileSystem(){
    unmount();
}


bool VolumeFileSystem::mount(const VolumeMountDesc& desc){
    ErrorCode errorCode;

    ScopedLock lock(m_mutex);
    unmountLocked();

    if(!__hidden_filesystem::ValidVolumeName(desc.volumeName.view())){
        __hidden_filesystem::LogFailure(desc.volumeName.view(), "mount", "invalid volume name");
        return false;
    }

    m_volumeName = desc.volumeName;
    m_mountDirectory = desc.mountDirectory.empty() ? Path(m_mountDirectory.arena(), ".") : desc.mountDirectory;
    m_usage = desc.usage;
    m_writable = (desc.usage != VolumeUsage::RuntimeReadOnly);
    m_maxSegments = desc.maxSegments;

    if(!FileExists(m_mountDirectory, errorCode)){
        if(errorCode){
            __hidden_filesystem::LogFailureWithFsError(m_volumeName, "mount:exists", m_mountDirectory, errorCode);
            return false;
        }

        if(!desc.createIfMissing || !m_writable){
            __hidden_filesystem::LogFailure(m_volumeName, "mount", "mount directory does not exist and creation is disabled");
            return false;
        }

        if(!EnsureDirectories(m_mountDirectory, errorCode)){
            __hidden_filesystem::LogFailureWithFsError(m_volumeName, "mount:create_directories", m_mountDirectory, errorCode);
            return false;
        }
    }
    else if(!IsDirectory(m_mountDirectory, errorCode)){
        __hidden_filesystem::LogFailureWithPath(m_volumeName, "mount:is_directory", m_mountDirectory, "path is not a directory");
        return false;
    }

    if(!scanSegmentsLocked()){
        __hidden_filesystem::LogFailure(m_volumeName, "mount", "segment scan failed");
        unmountLocked();
        return false;
    }

    if(m_segmentPaths.empty()){
        if(!desc.createIfMissing || !m_writable || desc.segmentSize == 0){
            __hidden_filesystem::LogFailure(m_volumeName, "mount", "volume does not exist and creation parameters are invalid");
            unmountLocked();
            return false;
        }

        m_segmentSize = desc.segmentSize;
        m_metadataBytes = desc.metadataSize == 0
            ? __hidden_filesystem::DefaultMetadataBytes(m_segmentSize)
            : desc.metadataSize
        ;

        if(m_metadataBytes <= sizeof(__hidden_filesystem::VolumeHeaderDisk) || m_metadataBytes >= m_segmentSize){
            __hidden_filesystem::LogFailure(
                m_volumeName,
                "mount",
                "metadata size is outside valid segment bounds"
            );
            unmountLocked();
            return false;
        }

        m_nextFreeOffset = m_metadataBytes;

        if(!createSegmentLocked(0)){
            __hidden_filesystem::LogFailure(m_volumeName, "mount", "failed to create first segment");
            unmountLocked();
            return false;
        }
        if(!flushMetadataLocked()){
            __hidden_filesystem::LogFailure(m_volumeName, "mount", "failed to write initial metadata");
            unmountLocked();
            return false;
        }
    }
    else{
        __hidden_filesystem::VolumeHeaderDisk discoveredHeader{};
        if(!__hidden_filesystem::ReadVolumeHeaderFromSegment(m_volumeName, m_segmentPaths[0], discoveredHeader)){
            unmountLocked();
            return false;
        }
        if(NWB_MEMCMP(discoveredHeader.magic, __hidden_filesystem::s_VolumeMagic, sizeof(discoveredHeader.magic)) != 0){
            __hidden_filesystem::LogFailure(m_volumeName, "mount", "magic mismatch");
            unmountLocked();
            return false;
        }
        if(discoveredHeader.segmentSize == 0){
            __hidden_filesystem::LogFailure(m_volumeName, "mount", "segment size is zero");
            unmountLocked();
            return false;
        }
        m_segmentSize = discoveredHeader.segmentSize;

        for(usize segmentIndex = 0; segmentIndex < m_segmentPaths.size(); ++segmentIndex){
            const Path& segmentPath = m_segmentPaths[segmentIndex];
            const u64 segmentFileSize = FileSize(segmentPath, errorCode);
            if(errorCode || segmentFileSize == 0){
                if(errorCode)
                    __hidden_filesystem::LogFailureWithFsError(m_volumeName, "mount:file_size", segmentPath, errorCode);
                else
                    __hidden_filesystem::LogFailureWithPath(m_volumeName, "mount:file_size", segmentPath, "segment size is zero");
                unmountLocked();
                return false;
            }

            const bool isLastSegment = segmentIndex + 1u == m_segmentPaths.size();
            if(!isLastSegment && segmentFileSize != m_segmentSize){
                NWB_LOGGER_WARNING(NWB_TEXT("Filesystem('{}'): mount failed: segment '{}' has size {}, expected {}")
                    , StringConvert(m_volumeName)
                    , StringConvert(segmentPath.string())
                    , segmentFileSize
                    , m_segmentSize
                );
                unmountLocked();
                return false;
            }
            if(isLastSegment && segmentFileSize > m_segmentSize){
                NWB_LOGGER_WARNING(NWB_TEXT("Filesystem('{}'): mount failed: final segment '{}' has size {}, exceeding logical segment size {}")
                    , StringConvert(m_volumeName)
                    , StringConvert(segmentPath.string())
                    , segmentFileSize
                    , m_segmentSize
                );
                unmountLocked();
                return false;
            }
        }

        const u64 firstSegmentFileSize = FileSize(m_segmentPaths[0], errorCode);
        if(errorCode || firstSegmentFileSize < sizeof(__hidden_filesystem::VolumeHeaderDisk)){
            if(errorCode)
                __hidden_filesystem::LogFailureWithFsError(m_volumeName, "mount:file_size", m_segmentPaths[0], errorCode);
            else
                __hidden_filesystem::LogFailureWithPath(m_volumeName, "mount:file_size", m_segmentPaths[0], "segment is smaller than the volume header");
            unmountLocked();
            return false;
        }

        if(desc.segmentSize != 0 && desc.segmentSize != m_segmentSize){
            NWB_LOGGER_WARNING(NWB_TEXT("Filesystem('{}'): mount failed: requested segment size {} does not match volume size {}")
                , StringConvert(m_volumeName)
                , desc.segmentSize
                , m_segmentSize
            );
            unmountLocked();
            return false;
        }

        if(!loadMetadataLocked()){
            __hidden_filesystem::LogFailure(m_volumeName, "mount", "metadata load failed");
            unmountLocked();
            return false;
        }

        if(desc.metadataSize != 0 && desc.metadataSize != m_metadataBytes){
            NWB_LOGGER_WARNING(NWB_TEXT("Filesystem('{}'): mount failed: requested metadata size {} does not match volume metadata size {}")
                , StringConvert(m_volumeName)
                , desc.metadataSize
                , m_metadataBytes
            );
            unmountLocked();
            return false;
        }
    }

    if(m_maxSegments != 0 && m_segmentPaths.size() > m_maxSegments){
        NWB_LOGGER_WARNING(NWB_TEXT("Filesystem('{}'): mount failed: discovered {} segments, maxSegments is {}")
            , StringConvert(m_volumeName)
            , m_segmentPaths.size()
            , m_maxSegments
        );
        unmountLocked();
        return false;
    }

    m_mounted = true;
    return true;
}

void VolumeFileSystem::unmount(){
    ScopedLock lock(m_mutex);
    unmountLocked();
}

bool VolumeFileSystem::mounted()const{
    ScopedLock lock(m_mutex);
    return m_mounted;
}

bool VolumeFileSystem::writable()const{
    ScopedLock lock(m_mutex);
    return m_mounted && m_writable;
}

AStringView VolumeFileSystem::volumeName()const{
    ScopedLock lock(m_mutex);
    return m_volumeName.view();
}

Path VolumeFileSystem::mountDirectory()const{
    ScopedLock lock(m_mutex);
    return m_mountDirectory;
}

u64 VolumeFileSystem::segmentSize()const{
    ScopedLock lock(m_mutex);
    return m_segmentSize;
}

u64 VolumeFileSystem::metadataSize()const{
    ScopedLock lock(m_mutex);
    return m_metadataBytes;
}

usize VolumeFileSystem::segmentCount()const{
    ScopedLock lock(m_mutex);
    return m_segmentPaths.size();
}


u64 VolumeFileSystem::fileCount()const{
    ScopedLock lock(m_mutex);
    return static_cast<u64>(m_files.size());
}

u64 VolumeFileSystem::usedBytes()const{
    ScopedLock lock(m_mutex);

    u64 totalUsedBytes = 0;
    for(const auto& [_, record] : m_files){
        if(!__hidden_filesystem::AddNoOverflow(totalUsedBytes, record.size, totalUsedBytes))
            return Limit<u64>::s_Max;
    }

    return totalUsedBytes;
}

u64 VolumeFileSystem::wastedBytes()const{
    ScopedLock lock(m_mutex);
    if(m_nextFreeOffset <= m_metadataBytes)
        return 0;

    u64 totalUsedBytes = 0;
    for(const auto& [_, record] : m_files){
        if(!__hidden_filesystem::AddNoOverflow(totalUsedBytes, record.size, totalUsedBytes))
            return 0;
    }

    const u64 payloadSpan = m_nextFreeOffset - m_metadataBytes;
    if(totalUsedBytes >= payloadSpan)
        return 0;

    return payloadSpan - totalUsedBytes;
}


bool VolumeFileSystem::writeFileLocked(
    const Name& virtualPath,
    const void* data,
    const usize bytes,
    const MetadataFlushMode::Enum flushMode
){
    if(!m_mounted || !m_writable){
        __hidden_filesystem::LogFailure(m_volumeName, "writeFile", "filesystem is not mounted in writable mode");
        return false;
    }
    if(!virtualPath){
        __hidden_filesystem::LogFailure(m_volumeName, "writeFile", "virtual path is invalid");
        return false;
    }
    if(bytes != 0 && data == nullptr){
        __hidden_filesystem::LogFailure(m_volumeName, "writeFile", "data pointer is null while byte count is non-zero");
        return false;
    }

    const auto itrFind = m_files.find(virtualPath);
    const bool existed = itrFind != m_files.end();

    u64 fileCountAfterWrite = static_cast<u64>(m_files.size());
    if(!existed){
        if(fileCountAfterWrite == Limit<u64>::s_Max){
            __hidden_filesystem::LogFailure(m_volumeName, "writeFile", "file count overflow");
            return false;
        }
        ++fileCountAfterWrite;
    }
    if(!canFitMetadataForFileCountLocked(fileCountAfterWrite)){
        NWB_LOGGER_WARNING(NWB_TEXT("Filesystem('{}'): writeFile failed: metadata area is full for file count {}")
            , StringConvert(m_volumeName)
            , fileCountAfterWrite
        );
        return false;
    }

    const u64 byteCount = static_cast<u64>(bytes);
    u64 newFileEnd = 0;
    if(!__hidden_filesystem::AddNoOverflow(m_nextFreeOffset, byteCount, newFileEnd)){
        __hidden_filesystem::LogFailure(m_volumeName, "writeFile", "offset overflow while reserving payload bytes");
        return false;
    }
    if(!ensureCapacityLocked(newFileEnd))
        return false;

    const u64 writeOffset = m_nextFreeOffset;
    if(byteCount > 0 && !writeBytesLocked(writeOffset, data, byteCount))
        return false;

    FileRecord previousRecord;
    if(existed)
        previousRecord = itrFind.value();
    const u64 previousNextFreeOffset = m_nextFreeOffset;

    m_files.insert_or_assign(virtualPath, FileRecord{ writeOffset, byteCount });
    m_nextFreeOffset = newFileEnd;

    if(flushMode == MetadataFlushMode::Deferred)
        return true;

    if(flushMetadataLocked())
        return true;

    if(existed)
        m_files.insert_or_assign(virtualPath, previousRecord);
    else
        m_files.erase(virtualPath);
    m_nextFreeOffset = previousNextFreeOffset;
    __hidden_filesystem::LogFailure(m_volumeName, "writeFile", "failed to flush metadata after payload write");
    return false;
}

bool VolumeFileSystem::writeFile(const Name& virtualPath, const void* data, const usize bytes){
    ScopedLock lock(m_mutex);
    return writeFileLocked(virtualPath, data, bytes, MetadataFlushMode::Immediate);
}

bool VolumeFileSystem::writeFileDeferred(const Name& virtualPath, const void* data, const usize bytes){
    ScopedLock lock(m_mutex);
    return writeFileLocked(virtualPath, data, bytes, MetadataFlushMode::Deferred);
}

bool VolumeFileSystem::flushMetadata(){
    ScopedLock lock(m_mutex);
    if(!m_mounted || !m_writable){
        __hidden_filesystem::LogFailure(m_volumeName, "flushMetadata", "filesystem is not mounted in writable mode");
        return false;
    }

    return flushMetadataLocked();
}

void VolumeFileSystem::reserveFileCapacity(const usize fileCount){
    ScopedLock lock(m_mutex);
    if(fileCount > m_files.size())
        m_files.reserve(fileCount);
}

bool VolumeFileSystem::readFileRecordLocked(const Name& virtualPath, FileRecord& outRecord)const{
    outRecord = {};
    if(!m_mounted){
        __hidden_filesystem::LogFailure(m_volumeName.view(), "readFile", "filesystem is not mounted");
        return false;
    }
    if(!virtualPath){
        __hidden_filesystem::LogFailure(m_volumeName.view(), "readFile", "virtual path is invalid");
        return false;
    }
    const auto itr = m_files.find(virtualPath);
    if(itr == m_files.end()){
        __hidden_filesystem::LogFailure(m_volumeName.view(), "readFile", "file was not found");
        return false;
    }

    outRecord = itr.value();
    return true;
}

bool VolumeFileSystem::removeFile(const Name& virtualPath){
    ScopedLock lock(m_mutex);
    if(!m_mounted || !m_writable){
        __hidden_filesystem::LogFailure(m_volumeName, "removeFile", "filesystem is not mounted in writable mode");
        return false;
    }
    if(!virtualPath){
        __hidden_filesystem::LogFailure(m_volumeName, "removeFile", "virtual path is invalid");
        return false;
    }
    const auto itr = m_files.find(virtualPath);
    if(itr == m_files.end()){
        __hidden_filesystem::LogFailure(m_volumeName, "removeFile", "file was not found");
        return false;
    }

    const FileRecord removedRecord = itr.value();
    m_files.erase(itr);
    if(flushMetadataLocked())
        return true;

    m_files.insert_or_assign(virtualPath, removedRecord);
    __hidden_filesystem::LogFailure(m_volumeName, "removeFile", "failed to flush metadata after erase");
    return false;
}

bool VolumeFileSystem::fileExists(const Name& virtualPath)const{
    ScopedLock lock(m_mutex);
    if(!m_mounted)
        return false;
    if(!virtualPath)
        return false;
    return m_files.find(virtualPath) != m_files.end();
}

bool VolumeFileSystem::fileSize(const Name& virtualPath, u64& outSize)const{
    ScopedLock lock(m_mutex);
    if(!m_mounted){
        __hidden_filesystem::LogFailure(m_volumeName, "fileSize", "filesystem is not mounted");
        return false;
    }
    if(!virtualPath){
        __hidden_filesystem::LogFailure(m_volumeName, "fileSize", "virtual path is invalid");
        return false;
    }

    const auto itr = m_files.find(virtualPath);
    if(itr == m_files.end()){
        __hidden_filesystem::LogFailure(m_volumeName, "fileSize", "file was not found");
        return false;
    }

    outSize = itr.value().size;
    return true;
}

Vector<Name, VolumeArena> VolumeFileSystem::listFiles()const{
    ScopedLock lock(m_mutex);

    Vector<Name, VolumeArena> output{m_arena};
    output.reserve(m_files.size());
    for(const auto& [path, _] : m_files)
        output.push_back(path);

    Sort(output.begin(), output.end());
    return output;
}


bool VolumeFileSystem::compact(const bool shrinkSegments){
    ScopedLock lock(m_mutex);
    if(!m_mounted || !m_writable){
        __hidden_filesystem::LogFailure(m_volumeName, "compact", "filesystem is not mounted in writable mode");
        return false;
    }

    struct FileLayout{
        Name path;
        u64 sourceOffset = 0;
        u64 destinationOffset = 0;
        u64 size = 0;
    };

    Core::Alloc::ScratchArena scratchArena(FilesystemArenaScope::s_CompactScratch);
    Vector<FileLayout, Core::Alloc::ScratchArena> layouts{ scratchArena };
    layouts.reserve(m_files.size());

    for(const auto& [path, record] : m_files){
        u64 endOffset = 0;
        if(!__hidden_filesystem::AddNoOverflow(record.offset, record.size, endOffset)){
            __hidden_filesystem::LogFailure(m_volumeName, "compact", "offset overflow detected in file layout");
            return false;
        }
        if(record.offset < m_metadataBytes){
            __hidden_filesystem::LogFailure(m_volumeName, "compact", "file layout overlaps metadata region");
            return false;
        }
        if(endOffset > m_nextFreeOffset){
            __hidden_filesystem::LogFailure(m_volumeName, "compact", "file layout exceeds next-free boundary");
            return false;
        }

        layouts.push_back(FileLayout{ path, record.offset, 0, record.size });
    }

    Sort(
        layouts.begin(),
        layouts.end(),
        [](const FileLayout& lhs, const FileLayout& rhs){
            return lhs.sourceOffset < rhs.sourceOffset;
        }
    );

    u64 compactedWriteOffset = m_metadataBytes;
    u64 previousSourceEnd = m_metadataBytes;
    for(auto& layout : layouts){
        if(layout.sourceOffset < previousSourceEnd){
            __hidden_filesystem::LogFailure(m_volumeName, "compact", "file layout overlap detected");
            return false;
        }

        layout.destinationOffset = compactedWriteOffset;

        if(!__hidden_filesystem::AddNoOverflow(layout.sourceOffset, layout.size, previousSourceEnd)){
            __hidden_filesystem::LogFailure(m_volumeName, "compact", "source offset overflow while building compaction plan");
            return false;
        }
        if(!__hidden_filesystem::AddNoOverflow(compactedWriteOffset, layout.size, compactedWriteOffset)){
            __hidden_filesystem::LogFailure(m_volumeName, "compact", "destination offset overflow while building compaction plan");
            return false;
        }
    }

    for(const auto& layout : layouts){
        if(layout.size == 0 || layout.destinationOffset == layout.sourceOffset)
            continue;

        if(!moveBytesLocked(layout.destinationOffset, layout.sourceOffset, layout.size))
            return false;
    }

    const FileMap previousFiles = m_files;
    const u64 previousNextFreeOffset = m_nextFreeOffset;

    for(const auto& layout : layouts)
        m_files.insert_or_assign(layout.path, FileRecord{ layout.destinationOffset, layout.size });
    m_nextFreeOffset = compactedWriteOffset;

    if(!flushMetadataLocked()){
        m_files = previousFiles;
        m_nextFreeOffset = previousNextFreeOffset;
        __hidden_filesystem::LogFailure(m_volumeName, "compact", "failed to flush metadata after move");
        return false;
    }

    if(!shrinkSegments)
        return true;

    if(trimSegmentsForNextFreeOffsetLocked())
        return true;

    __hidden_filesystem::LogFailure(m_volumeName, "compact", "failed to trim trailing segments");
    return false;
}


bool VolumeFileSystem::scanSegmentsLocked(){
    ErrorCode errorCode;

    m_segmentPaths.clear();

    for(usize segmentIndex = 0;; ++segmentIndex){
        const Path hashedSegmentPath = m_mountDirectory / ::MakeVolumeSegmentFileName(m_volumeName.view(), segmentIndex).c_str();

        const bool exists = FileExists(hashedSegmentPath, errorCode);
        if(errorCode){
            __hidden_filesystem::LogFailureWithFsError(m_volumeName, "scanSegments:exists", hashedSegmentPath, errorCode);
            return false;
        }
        if(!exists)
            break;

        if(!IsRegularFile(hashedSegmentPath, errorCode)){
            __hidden_filesystem::LogFailureWithPath(m_volumeName, "scanSegments:is_regular_file", hashedSegmentPath, "segment path is not a regular file");
            return false;
        }

        m_segmentPaths.push_back(hashedSegmentPath);

        if(segmentIndex == Limit<usize>::s_Max){
            __hidden_filesystem::LogFailure(m_volumeName, "scanSegments", "segment index overflow");
            return false;
        }
    }

    return true;
}

bool VolumeFileSystem::createSegmentLocked(const usize segmentIndex){
    if(m_maxSegments != 0 && segmentIndex >= m_maxSegments){
        NWB_LOGGER_WARNING(NWB_TEXT("Filesystem('{}'): createSegment failed: segment index {} exceeds maxSegments {}")
            , StringConvert(m_volumeName)
            , segmentIndex
            , m_maxSegments
        );
        return false;
    }
    if(m_segmentSize == 0){
        __hidden_filesystem::LogFailure(m_volumeName, "createSegment", "segment size is zero");
        return false;
    }

    const Path path = segmentPath(segmentIndex);
    GlobalFilesystemDetail::OutputFileStream stream(
        path,
        GlobalFilesystemDetail::OutputFileStream::binary | GlobalFilesystemDetail::OutputFileStream::trunc
    );
    if(!stream.is_open()){
        __hidden_filesystem::LogFailureWithPath(
            m_volumeName,
            "createSegment:open",
            path,
            __hidden_filesystem::LastErrnoMessage()
        );
        return false;
    }

    GlobalFilesystemDetail::StreamOffset streamOffset = 0;
    if(!__hidden_filesystem::ToStreamOff(m_segmentSize - 1, streamOffset)){
        NWB_LOGGER_WARNING(NWB_TEXT("Filesystem('{}'): createSegment failed: segment size {} cannot be represented as stream offset")
            , StringConvert(m_volumeName)
            , m_segmentSize
        );
        return false;
    }

    stream.seekp(streamOffset);
    if(!stream.good()){
        __hidden_filesystem::LogFailureWithPath(
            m_volumeName,
            "createSegment:seek",
            path,
            __hidden_filesystem::LastErrnoMessage()
        );
        return false;
    }

    char zero = 0;
    stream.write(&zero, 1);
    if(!stream.good()){
        __hidden_filesystem::LogFailureWithPath(
            m_volumeName,
            "createSegment:write",
            path,
            __hidden_filesystem::LastErrnoMessage()
        );
        return false;
    }

    if(segmentIndex == m_segmentPaths.size())
        m_segmentPaths.push_back(path);
    else if(segmentIndex < m_segmentPaths.size())
        m_segmentPaths[segmentIndex] = path;
    else{
        NWB_LOGGER_WARNING(NWB_TEXT("Filesystem('{}'): createSegment failed: segment index {} is non-contiguous (segment count {})")
            , StringConvert(m_volumeName)
            , segmentIndex
            , m_segmentPaths.size()
        );
        return false;
    }

    return true;
}

bool VolumeFileSystem::ensureCapacityLocked(const u64 requiredBytes){
    if(m_segmentSize == 0){
        __hidden_filesystem::LogFailure(m_volumeName, "ensureCapacity", "segment size is zero");
        return false;
    }

    for(;;){
        const u64 segmentCount = static_cast<u64>(m_segmentPaths.size());
        if(segmentCount > Limit<u64>::s_Max / m_segmentSize){
            __hidden_filesystem::LogFailure(m_volumeName, "ensureCapacity", "capacity overflow while computing current volume size");
            return false;
        }

        const u64 capacity = segmentCount * m_segmentSize;
        if(requiredBytes <= capacity)
            return true;

        if(!m_writable){
            NWB_LOGGER_WARNING(NWB_TEXT("Filesystem('{}'): ensureCapacity failed: required {} bytes, current capacity {} bytes, filesystem is read-only")
                , StringConvert(m_volumeName)
                , requiredBytes
                , capacity
            );
            return false;
        }
        if(!createSegmentLocked(m_segmentPaths.size()))
            return false;
    }
}

bool VolumeFileSystem::loadMetadataLocked(){
    __hidden_filesystem::VolumeHeaderDisk header{};
    if(!readBytesLocked(0, &header, sizeof(header))){
        __hidden_filesystem::LogFailure(m_volumeName, "loadMetadata", "failed to read metadata header");
        return false;
    }

    if(NWB_MEMCMP(header.magic, __hidden_filesystem::s_VolumeMagic, sizeof(header.magic)) != 0){
        __hidden_filesystem::LogFailure(m_volumeName, "loadMetadata", "magic mismatch");
        return false;
    }
    if(header.segmentSize != m_segmentSize){
        NWB_LOGGER_WARNING(NWB_TEXT("Filesystem('{}'): loadMetadata failed: segment size {} does not match mounted size {}")
            , StringConvert(m_volumeName)
            , header.segmentSize
            , m_segmentSize
        );
        return false;
    }
    if(header.metadataBytes <= sizeof(header) || header.metadataBytes >= m_segmentSize){
        __hidden_filesystem::LogFailure(m_volumeName, "loadMetadata", "metadata byte range is invalid");
        return false;
    }

    u64 headerTotalBytes = 0;
    if(!__hidden_filesystem::AddNoOverflow(static_cast<u64>(sizeof(header)), header.indexBytes, headerTotalBytes)){
        __hidden_filesystem::LogFailure(m_volumeName, "loadMetadata", "metadata byte overflow");
        return false;
    }
    if(headerTotalBytes > header.metadataBytes){
        __hidden_filesystem::LogFailure(m_volumeName, "loadMetadata", "metadata table exceeds reserved metadata area");
        return false;
    }

    if(header.nextFreeOffset < header.metadataBytes){
        __hidden_filesystem::LogFailure(m_volumeName, "loadMetadata", "next free offset points inside metadata area");
        return false;
    }

    if(static_cast<u64>(m_segmentPaths.size()) > Limit<u64>::s_Max / m_segmentSize){
        __hidden_filesystem::LogFailure(m_volumeName, "loadMetadata", "segment capacity overflow");
        return false;
    }
    const u64 maxVolumeBytes = static_cast<u64>(m_segmentPaths.size()) * m_segmentSize;
    if(header.nextFreeOffset > maxVolumeBytes){
        __hidden_filesystem::LogFailure(m_volumeName, "loadMetadata", "next free offset exceeds volume capacity");
        return false;
    }
    u64 physicalCapacityBytes = 0;
    if(!computePhysicalCapacityLocked(physicalCapacityBytes))
        return false;
    if(header.nextFreeOffset > physicalCapacityBytes){
        __hidden_filesystem::LogFailure(m_volumeName, "loadMetadata", "next free offset exceeds physical volume bytes");
        return false;
    }
    if(header.indexBytes > static_cast<u64>(Limit<usize>::s_Max)){
        __hidden_filesystem::LogFailure(m_volumeName, "loadMetadata", "index byte count exceeds runtime addressable range");
        return false;
    }
    u64 expectedIndexBytes = 0;
    if(!__hidden_filesystem::ComputeVolumeIndexBytes(header.fileCount, expectedIndexBytes)){
        __hidden_filesystem::LogFailure(m_volumeName, "loadMetadata", "file count overflows index entry byte computation");
        return false;
    }

    if(header.indexBytes != expectedIndexBytes){
        NWB_LOGGER_WARNING(NWB_TEXT("Filesystem('{}'): loadMetadata failed: index byte count {} does not match expected {}")
            , StringConvert(m_volumeName)
            , header.indexBytes
            , expectedIndexBytes
        );
        return false;
    }

    Core::Alloc::ScratchArena scratchArena(FilesystemArenaScope::s_LoadMetadataScratch);
    Vector<u8, Core::Alloc::ScratchArena> indexData(
        static_cast<usize>(header.indexBytes),
        0,
        scratchArena
    );
    if(header.indexBytes > 0 && !readBytesLocked(static_cast<u64>(sizeof(header)), indexData.data(), header.indexBytes)){
        __hidden_filesystem::LogFailure(m_volumeName, "loadMetadata", "failed to read metadata index");
        return false;
    }

    FileMap loadedFiles(0, Hasher<Name>(), EqualTo<Name>(), m_arena);
    loadedFiles.reserve(static_cast<usize>(header.fileCount));
    u64 cursor = 0;
    for(u64 i = 0; i < header.fileCount; ++i){
        if(header.indexBytes - cursor < sizeof(__hidden_filesystem::VolumeIndexEntryDisk)){
            __hidden_filesystem::LogFailure(m_volumeName, "loadMetadata", "truncated metadata index entry");
            return false;
        }

        __hidden_filesystem::VolumeIndexEntryDisk entry{};
        NWB_MEMCPY(&entry, sizeof(entry), indexData.data() + static_cast<usize>(cursor), sizeof(entry));
        cursor += sizeof(entry);

        u64 endOffset = 0;
        if(!__hidden_filesystem::AddNoOverflow(entry.offset, entry.size, endOffset)){
            __hidden_filesystem::LogFailure(m_volumeName, "loadMetadata", "index entry offset overflow");
            return false;
        }
        if(entry.offset < header.metadataBytes){
            __hidden_filesystem::LogFailure(m_volumeName, "loadMetadata", "index entry points inside metadata area");
            return false;
        }
        if(endOffset > header.nextFreeOffset){
            __hidden_filesystem::LogFailure(m_volumeName, "loadMetadata", "index entry end exceeds next free offset");
            return false;
        }

        const Name pathName(entry.hash);
        if(!loadedFiles.emplace(pathName, FileRecord{ entry.offset, entry.size }).second){
            __hidden_filesystem::LogFailure(m_volumeName, "loadMetadata", "duplicate path hash in metadata index");
            return false;
        }
    }

    if(cursor != header.indexBytes){
        __hidden_filesystem::LogFailure(m_volumeName, "loadMetadata", "metadata cursor mismatch after index parse");
        return false;
    }

    m_metadataBytes = header.metadataBytes;
    m_nextFreeOffset = header.nextFreeOffset;
    m_files = Move(loadedFiles);

    return true;
}

bool VolumeFileSystem::flushMetadataLocked(){
    if(m_segmentPaths.empty()){
        __hidden_filesystem::LogFailure(m_volumeName, "flushMetadata", "no segments are mounted");
        return false;
    }
    if(m_nextFreeOffset < m_metadataBytes){
        __hidden_filesystem::LogFailure(m_volumeName, "flushMetadata", "next free offset points inside metadata area");
        return false;
    }
    if(static_cast<u64>(m_segmentPaths.size()) > Limit<u64>::s_Max / m_segmentSize){
        __hidden_filesystem::LogFailure(m_volumeName, "flushMetadata", "segment capacity overflow");
        return false;
    }
    if(m_nextFreeOffset > static_cast<u64>(m_segmentPaths.size()) * m_segmentSize){
        __hidden_filesystem::LogFailure(m_volumeName, "flushMetadata", "next free offset exceeds volume capacity");
        return false;
    }

    __hidden_filesystem::VolumeHeaderDisk header{};
    NWB_MEMCPY(header.magic, sizeof(header.magic), __hidden_filesystem::s_VolumeMagic, sizeof(__hidden_filesystem::s_VolumeMagic));
    header.segmentSize = m_segmentSize;
    header.metadataBytes = m_metadataBytes;
    header.fileCount = static_cast<u64>(m_files.size());
    header.nextFreeOffset = m_nextFreeOffset;

    struct MetadataIndexRecord{
        Name path;
        FileRecord file;
    };

    Core::Alloc::ScratchArena scratchArena(FilesystemArenaScope::s_SaveMetadataScratch);
    Vector<MetadataIndexRecord, Core::Alloc::ScratchArena> sortedRecords{ scratchArena };
    sortedRecords.reserve(m_files.size());
    for(const auto& [path, record] : m_files)
        sortedRecords.push_back(MetadataIndexRecord{ path, record });
    Sort(
        sortedRecords.begin(),
        sortedRecords.end(),
        [](const MetadataIndexRecord& lhs, const MetadataIndexRecord& rhs){
            return ::LessNameHash(lhs.path.hash(), rhs.path.hash());
        }
    );

    Vector<u8, Core::Alloc::ScratchArena> indexBytes{scratchArena};
    u64 expectedIndexBytes = 0;
    if(!__hidden_filesystem::ComputeVolumeIndexBytes(header.fileCount, expectedIndexBytes)){
        __hidden_filesystem::LogFailure(m_volumeName, "flushMetadata", "file count overflows index size");
        return false;
    }
    if(expectedIndexBytes > static_cast<u64>(Limit<usize>::s_Max)){
        __hidden_filesystem::LogFailure(m_volumeName, "flushMetadata", "metadata index exceeds runtime addressable range");
        return false;
    }
    indexBytes.reserve(static_cast<usize>(expectedIndexBytes));

    for(const MetadataIndexRecord& recordInfo : sortedRecords){
        const FileRecord& record = recordInfo.file;
        __hidden_filesystem::VolumeIndexEntryDisk entry{};
        entry.hash = recordInfo.path.hash();
        entry.offset = record.offset;
        entry.size = record.size;

        AppendPOD(indexBytes, entry);
    }

    header.indexBytes = static_cast<u64>(indexBytes.size());
    if(header.indexBytes != expectedIndexBytes){
        __hidden_filesystem::LogFailure(m_volumeName, "flushMetadata", "serialized index size mismatch");
        return false;
    }

    u64 totalMetaBytes = 0;
    if(!__hidden_filesystem::AddNoOverflow(static_cast<u64>(sizeof(header)), header.indexBytes, totalMetaBytes)){
        __hidden_filesystem::LogFailure(m_volumeName, "flushMetadata", "metadata byte overflow");
        return false;
    }
    if(totalMetaBytes > m_metadataBytes){
        NWB_LOGGER_WARNING(NWB_TEXT("Filesystem('{}'): flushMetadata failed: metadata requires {} bytes, reserved {} bytes")
            , StringConvert(m_volumeName)
            , totalMetaBytes
            , m_metadataBytes
        );
        return false;
    }

    Vector<u8, Core::Alloc::ScratchArena> metadataBuffer{scratchArena};
    metadataBuffer.reserve(static_cast<usize>(m_metadataBytes));
    AppendPOD(metadataBuffer, header);
    ::BinaryDetail::AppendBytesNoReserveUnchecked(metadataBuffer, indexBytes.data(), indexBytes.size());
    metadataBuffer.resize(static_cast<usize>(m_metadataBytes), 0u);

    if(writeBytesLocked(0, metadataBuffer.data(), metadataBuffer.size()))
        return true;

    __hidden_filesystem::LogFailure(m_volumeName, "flushMetadata", "metadata write failed");
    return false;
}

bool VolumeFileSystem::canFitMetadataForFileCountLocked(const u64 fileCount)const{
    u64 metadataBytes = 0;
    if(!__hidden_filesystem::ComputeVolumeMetadataRequirement(fileCount, metadataBytes))
        return false;

    return metadataBytes <= m_metadataBytes;
}

bool VolumeFileSystem::computePhysicalCapacityLocked(u64& outCapacityBytes)const{
    outCapacityBytes = 0;

    if(m_segmentPaths.empty() || m_segmentSize == 0){
        __hidden_filesystem::LogFailure(m_volumeName, "physicalCapacity", "no mounted segments or segment size is zero");
        return false;
    }

    const usize fullSegmentCount = m_segmentPaths.size() - 1u;
    if(static_cast<u64>(fullSegmentCount) > Limit<u64>::s_Max / m_segmentSize){
        __hidden_filesystem::LogFailure(m_volumeName, "physicalCapacity", "segment capacity overflow");
        return false;
    }

    ErrorCode errorCode;
    const u64 lastSegmentBytes = FileSize(m_segmentPaths.back(), errorCode);
    if(errorCode){
        __hidden_filesystem::LogFailureWithFsError(m_volumeName, "physicalCapacity:file_size", m_segmentPaths.back(), errorCode);
        return false;
    }
    if(lastSegmentBytes == 0 || lastSegmentBytes > m_segmentSize){
        __hidden_filesystem::LogFailure(m_volumeName, "physicalCapacity", "final segment size is outside logical bounds");
        return false;
    }

    const u64 fullSegmentBytes = static_cast<u64>(fullSegmentCount) * m_segmentSize;
    if(__hidden_filesystem::AddNoOverflow(fullSegmentBytes, lastSegmentBytes, outCapacityBytes))
        return true;

    __hidden_filesystem::LogFailure(m_volumeName, "physicalCapacity", "capacity overflow while adding final segment");
    return false;
}

bool VolumeFileSystem::readBytesLocked(const u64 offset, void* data, const u64 byteCount)const{
    if(byteCount == 0)
        return true;
    if(data == nullptr){
        __hidden_filesystem::LogFailure(m_volumeName, "readBytes", "invalid arguments");
        return false;
    }

    u8* outputBytes = static_cast<u8*>(data);
    return __hidden_filesystem::TransferVolumeBytes(
        m_volumeName,
        "readBytes",
        m_segmentPaths,
        m_segmentSize,
        offset,
        byteCount,
        outputBytes,
        __hidden_filesystem::ReadSegmentBytesOp{}
    );
}

bool VolumeFileSystem::writeBytesLocked(const u64 offset, const void* data, const u64 byteCount){
    if(byteCount == 0)
        return true;
    if(data == nullptr){
        __hidden_filesystem::LogFailure(m_volumeName, "writeBytes", "invalid arguments");
        return false;
    }

    u64 endOffset = 0;
    if(!__hidden_filesystem::AddNoOverflow(offset, byteCount, endOffset)){
        __hidden_filesystem::LogFailure(m_volumeName, "writeBytes", "offset overflow");
        return false;
    }
    if(!ensureCapacityLocked(endOffset)){
        __hidden_filesystem::LogFailure(m_volumeName, "writeBytes", "insufficient capacity");
        return false;
    }

    const u8* inputBytes = static_cast<const u8*>(data);
    return __hidden_filesystem::TransferVolumeBytes(
        m_volumeName,
        "writeBytes",
        m_segmentPaths,
        m_segmentSize,
        offset,
        byteCount,
        inputBytes,
        __hidden_filesystem::WriteSegmentBytesOp{}
    );
}

bool VolumeFileSystem::moveBytesLocked(const u64 destinationOffset, const u64 sourceOffset, const u64 byteCount){
    if(byteCount == 0 || destinationOffset == sourceOffset)
        return true;
    if(m_segmentSize == 0){
        __hidden_filesystem::LogFailure(m_volumeName, "moveBytes", "segment size is zero");
        return false;
    }

    u64 sourceEndOffset = 0;
    if(!__hidden_filesystem::AddNoOverflow(sourceOffset, byteCount, sourceEndOffset)){
        __hidden_filesystem::LogFailure(m_volumeName, "moveBytes", "source range overflow");
        return false;
    }

    if(static_cast<u64>(m_segmentPaths.size()) > Limit<u64>::s_Max / m_segmentSize){
        __hidden_filesystem::LogFailure(m_volumeName, "moveBytes", "capacity overflow");
        return false;
    }
    const u64 capacityBytes = static_cast<u64>(m_segmentPaths.size()) * m_segmentSize;
    if(sourceEndOffset > capacityBytes){
        NWB_LOGGER_WARNING(NWB_TEXT("Filesystem('{}'): moveBytes failed: source range [{}..{}) exceeds capacity {}")
            , StringConvert(m_volumeName)
            , sourceOffset
            , sourceEndOffset
            , capacityBytes
        );
        return false;
    }

    u64 destinationEndOffset = 0;
    if(!__hidden_filesystem::AddNoOverflow(destinationOffset, byteCount, destinationEndOffset)){
        __hidden_filesystem::LogFailure(m_volumeName, "moveBytes", "destination range overflow");
        return false;
    }
    if(!ensureCapacityLocked(destinationEndOffset)){
        __hidden_filesystem::LogFailure(m_volumeName, "moveBytes", "failed to ensure destination capacity");
        return false;
    }

    const u64 moveChunkBytes = Min(__hidden_filesystem::s_VolumeMoveChunkBytes, m_segmentSize);
    if(moveChunkBytes == 0 || moveChunkBytes > static_cast<u64>(Limit<usize>::s_Max)){
        __hidden_filesystem::LogFailure(m_volumeName, "moveBytes", "invalid move chunk size");
        return false;
    }

    Core::Alloc::ScratchArena scratchArena(FilesystemArenaScope::s_MoveBytesScratch);
    Vector<u8, Core::Alloc::ScratchArena> moveBuffer(
        static_cast<usize>(moveChunkBytes),
        0,
        scratchArena
    );

    if(destinationOffset < sourceOffset){
        u64 movedBytes = 0;
        while(movedBytes < byteCount){
            const u64 pendingBytes = byteCount - movedBytes;
            const u64 copyBytes = Min(pendingBytes, moveChunkBytes);

            const u64 readOffset = sourceOffset + movedBytes;
            const u64 writeOffset = destinationOffset + movedBytes;
            if(!readBytesLocked(readOffset, moveBuffer.data(), copyBytes))
                return false;
            if(!writeBytesLocked(writeOffset, moveBuffer.data(), copyBytes))
                return false;

            movedBytes += copyBytes;
        }
        return true;
    }

    u64 remainingBytes = byteCount;
    while(remainingBytes > 0){
        const u64 copyBytes = Min(remainingBytes, moveChunkBytes);
        const u64 chunkBegin = remainingBytes - copyBytes;

        const u64 readOffset = sourceOffset + chunkBegin;
        const u64 writeOffset = destinationOffset + chunkBegin;
        if(!readBytesLocked(readOffset, moveBuffer.data(), copyBytes))
            return false;
        if(!writeBytesLocked(writeOffset, moveBuffer.data(), copyBytes))
            return false;

        remainingBytes -= copyBytes;
    }

    return true;
}

bool VolumeFileSystem::trimSegmentsForNextFreeOffsetLocked(){
    ErrorCode errorCode;

    if(!m_writable || m_segmentSize == 0){
        __hidden_filesystem::LogFailure(m_volumeName, "trimSegments", "filesystem is not writable or segment size is zero");
        return false;
    }
    if(m_segmentPaths.empty()){
        __hidden_filesystem::LogFailure(m_volumeName, "trimSegments", "no segments are mounted");
        return false;
    }

    const u64 requiredBytes = Max(m_nextFreeOffset, m_metadataBytes);
    u64 requiredSegments = DivideUp(requiredBytes, m_segmentSize);
    if(requiredSegments == 0)
        requiredSegments = 1;
    if(requiredSegments > static_cast<u64>(m_segmentPaths.size())){
        __hidden_filesystem::LogFailure(m_volumeName, "trimSegments", "required segment count exceeds mounted segment count");
        return false;
    }
    if(requiredSegments > static_cast<u64>(Limit<usize>::s_Max)){
        __hidden_filesystem::LogFailure(m_volumeName, "trimSegments", "required segment count exceeds usize range");
        return false;
    }

    while(m_segmentPaths.size() > static_cast<usize>(requiredSegments)){
        const Path removePath = m_segmentPaths.back();
        if(!RemoveFile(removePath, errorCode)){
            if(errorCode){
                __hidden_filesystem::LogFailureWithFsError(m_volumeName, "trimSegments:remove", removePath, errorCode);
            }
            else{
                __hidden_filesystem::LogFailureWithPath(m_volumeName, "trimSegments:remove", removePath, "segment was not present");
            }
            return false;
        }
        m_segmentPaths.pop_back();
    }

    u64 requiredLastSegmentBytes = requiredBytes % m_segmentSize;
    if(requiredLastSegmentBytes == 0)
        requiredLastSegmentBytes = m_segmentSize;

    const Path& lastSegmentPath = m_segmentPaths.back();
    const u64 currentLastSegmentBytes = FileSize(lastSegmentPath, errorCode);
    if(errorCode){
        __hidden_filesystem::LogFailureWithFsError(m_volumeName, "trimSegments:file_size", lastSegmentPath, errorCode);
        return false;
    }
    if(currentLastSegmentBytes == requiredLastSegmentBytes)
        return true;

    if(!__hidden_filesystem::ResizeFile(lastSegmentPath, requiredLastSegmentBytes, errorCode)){
        __hidden_filesystem::LogFailureWithFsError(m_volumeName, "trimSegments:resize", lastSegmentPath, errorCode);
        return false;
    }

    return true;
}

void VolumeFileSystem::unmountLocked(){
    m_mounted = false;
    m_writable = false;
    m_usage = VolumeUsage::RuntimeReadOnly;

    m_volumeName.clear();
    m_mountDirectory.clear();

    m_segmentSize = 0;
    m_metadataBytes = 0;
    m_nextFreeOffset = 0;
    m_maxSegments = 0;

    m_segmentPaths.clear();
    m_files.clear();
}

Path VolumeFileSystem::segmentPath(const usize segmentIndex)const{
    return m_mountDirectory / ::MakeVolumeSegmentFileName(m_volumeName.view(), segmentIndex).c_str();
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


VolumeSession::VolumeSession(Alloc::GlobalArena& arena)
    : m_volumeFileSystem(arena)
{}


bool VolumeSession::create(const Path& outputDirectory, const VolumeBuildConfig& config){
    if(!__hidden_filesystem::RemoveExistingVolumeSegments(outputDirectory, config.volumeName.view()))
        return false;

    VolumeMountDesc desc(outputDirectory.arena());
    desc.volumeName = config.volumeName;
    desc.mountDirectory = outputDirectory;
    desc.segmentSize = config.segmentSize;
    desc.metadataSize = config.metadataSize;
    desc.createIfMissing = true;
    desc.usage = VolumeUsage::CookWrite;

    if(m_volumeFileSystem.mount(desc))
        return true;

    NWB_LOGGER_ERROR(NWB_TEXT("VolumeSession::create failed to mount volume '{}'"), StringConvert(config.volumeName.view()));
    return false;
}


bool VolumeSession::load(const AStringView volumeName, const Path& mountDirectory){
    VolumeMountDesc desc(mountDirectory.arena());
    if(!desc.volumeName.assign(volumeName))
        return false;
    desc.mountDirectory = mountDirectory;
    desc.createIfMissing = false;
    desc.usage = VolumeUsage::RuntimeReadOnly;

    if(m_volumeFileSystem.mount(desc))
        return true;

    NWB_LOGGER_ERROR(NWB_TEXT("VolumeSession::load failed to mount volume '{}'"), StringConvert(volumeName));
    return false;
}

void VolumeSession::reserveFileCapacity(const usize fileCount){
    m_volumeFileSystem.reserveFileCapacity(fileCount);
}

bool VolumeSession::pushData(const Name& virtualPath, const void* data, const usize bytes){
    if(!m_volumeFileSystem.mounted()){
        NWB_LOGGER_ERROR(NWB_TEXT("VolumeSession::pushData failed: volume is not mounted"));
        return false;
    }
    if(!m_volumeFileSystem.writable()){
        NWB_LOGGER_ERROR(NWB_TEXT("VolumeSession::pushData failed: volume is read-only"));
        return false;
    }
    if(!virtualPath){
        NWB_LOGGER_ERROR(NWB_TEXT("VolumeSession::pushData failed: virtual path is empty"));
        return false;
    }

    if(m_volumeFileSystem.writeFile(virtualPath, data, bytes))
        return true;

    NWB_LOGGER_ERROR(NWB_TEXT("VolumeSession::pushData failed to write '{}'"), StringConvert(virtualPath.c_str()));
    return false;
}

bool VolumeSession::pushData(const AStringView virtualPath, const void* data, const usize bytes){
    if(virtualPath.empty()){
        NWB_LOGGER_ERROR(NWB_TEXT("VolumeSession::pushData failed: virtual path is empty"));
        return false;
    }

    const Name virtualPathName(virtualPath);
    if(!virtualPathName){
        NWB_LOGGER_ERROR(NWB_TEXT("VolumeSession::pushData failed: virtual path is invalid"));
        return false;
    }

    return pushData(virtualPathName, data, bytes);
}

bool VolumeSession::pushDataDeferred(const Name& virtualPath, const void* data, const usize bytes){
    if(!m_volumeFileSystem.mounted()){
        NWB_LOGGER_ERROR(NWB_TEXT("VolumeSession::pushDataDeferred failed: volume is not mounted"));
        return false;
    }
    if(!m_volumeFileSystem.writable()){
        NWB_LOGGER_ERROR(NWB_TEXT("VolumeSession::pushDataDeferred failed: volume is read-only"));
        return false;
    }
    if(!virtualPath){
        NWB_LOGGER_ERROR(NWB_TEXT("VolumeSession::pushDataDeferred failed: virtual path is empty"));
        return false;
    }

    if(m_volumeFileSystem.writeFileDeferred(virtualPath, data, bytes))
        return true;

    NWB_LOGGER_ERROR(NWB_TEXT("VolumeSession::pushDataDeferred failed to write '{}'"), StringConvert(virtualPath.c_str()));
    return false;
}

bool VolumeSession::pushDataDeferred(const AStringView virtualPath, const void* data, const usize bytes){
    if(virtualPath.empty()){
        NWB_LOGGER_ERROR(NWB_TEXT("VolumeSession::pushDataDeferred failed: virtual path is empty"));
        return false;
    }

    const Name virtualPathName(virtualPath);
    if(!virtualPathName){
        NWB_LOGGER_ERROR(NWB_TEXT("VolumeSession::pushDataDeferred failed: virtual path is invalid"));
        return false;
    }

    return pushDataDeferred(virtualPathName, data, bytes);
}

bool VolumeSession::flush(){
    if(!m_volumeFileSystem.mounted()){
        NWB_LOGGER_ERROR(NWB_TEXT("VolumeSession::flush failed: volume is not mounted"));
        return false;
    }
    if(!m_volumeFileSystem.writable()){
        NWB_LOGGER_ERROR(NWB_TEXT("VolumeSession::flush failed: volume is read-only"));
        return false;
    }

    if(m_volumeFileSystem.compact(true))
        return true;

    NWB_LOGGER_ERROR(NWB_TEXT("VolumeSession::flush failed to finalize volume"));
    return false;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_FILESYSTEM_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

