// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "volume_staging.h"
#include "volume_staging_detail.h"
#include "volume_storage_detail.h"
#include "arena_names.h"

#include <core/alloc/scratch.h>
#include <core/common/log.h>
#include <global/filesystem/volume_naming.h>
#include <global/limit.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_FILESYSTEM_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


bool RemoveStagedDirectoryIfPresent(const Path& directoryPath, const AStringView operationName, const AStringView label){

    if(const auto operation = RemoveAllIfExists(directoryPath); !operation){
        const ErrorCode errorCode = operation.error();
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

    if(const auto operation = RemoveAllIfExists(directoryPath); !operation){
        const ErrorCode errorCode = operation.error();
        NWB_LOGGER_WARNING(NWB_TEXT("{}: failed to remove {} '{}': {}")
            , StringConvert(operationName)
            , StringConvert(label)
            , PathToString<tchar>(directoryPath)
            , StringConvert(errorCode.message())
        );
    }
}

bool EnsureEmptyStagedDirectory(const Path& directoryPath, const AStringView operationName, const AStringView label){

    if(const auto operation = ::EnsureEmptyDirectory(directoryPath); !operation){
        const ErrorCode errorCode = operation.error();
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
            m_operationName,
            m_label
        );
}

void StagedDirectoryCleanupGuard::dismiss()noexcept{
    m_active = false;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace FilesystemVolumeStagingDetail{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


constexpr AStringView s_StagedVolumeTokenPrefix = "volume_";
constexpr char s_StagedVolumeKeySeparator = '|';
constexpr usize s_StagedVolumeHashDigits = sizeof(u64) * 2u;

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



using StagedVolumePaths = StagedDirectoryPaths;

template<typename FileNameVector>
static bool RestoreVolumeSegments(const Path& fromDirectory, const Path& toDirectory, const FileNameVector& fileNames);

StagedVolumePaths BuildStagedVolumePaths(const Path& outputDirectory, const AStringView volumeName){
    Core::Alloc::ScratchArena scratchArena(FilesystemArenaScope::s_StagedVolumePathsScratch);
    AString<Core::Alloc::ScratchArena> stageKey = PathToString<char>(scratchArena, outputDirectory);
    stageKey += s_StagedVolumeKeySeparator;
    stageKey += volumeName;

    AString<Core::Alloc::ScratchArena> stageToken{scratchArena};
    stageToken.reserve(s_StagedVolumeTokenPrefix.size() + s_StagedVolumeHashDigits);
    stageToken += s_StagedVolumeTokenPrefix;
    AppendHexU64<char, Core::Alloc::ScratchArena>(ComputeFnv64Text(AStringView(stageKey)), stageToken);
    return BuildStagedDirectoryPaths(scratchArena, outputDirectory, stageToken);
}

static Expected<Vector<Path, Core::Alloc::ScratchArena>> MoveExistingVolumeSegments(
    Core::Alloc::ScratchArena& scratchArena,
    const Path& fromDirectory,
    const Path& toDirectory,
    const AStringView volumeName
){
    Vector<Path, Core::Alloc::ScratchArena> movedFileNames{scratchArena};

    const auto rollbackMovedFiles = [&]() -> void {
        if(movedFileNames.empty())
            return;

        if(!RestoreVolumeSegments(toDirectory, fromDirectory, movedFileNames)){
            NWB_LOGGER_WARNING(NWB_TEXT("Filesystem volume publish: failed to roll back existing output volume after backup failure"));
            return;
        }

        CleanupStagedDirectoryBestEffort(toDirectory, s_VolumePublishLogPrefix, "backup directory");
        movedFileNames.clear();
    };

    const auto sourceExists = FileExists(fromDirectory);
    if(!sourceExists){
        const ErrorCode& errorCode = sourceExists.error();
        NWB_LOGGER_ERROR(NWB_TEXT("Filesystem volume publish: failed to query output directory '{}': {}")
            , PathToString<tchar>(fromDirectory)
            , StringConvert(errorCode.message())
        );
        return MakeUnexpected(Failure{});
    }
    if(!*sourceExists)
        return movedFileNames;

    const auto directory = IsDirectory(fromDirectory);
    if(!directory || !*directory){
        if(!directory){
            const ErrorCode& errorCode = directory.error();
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
        return MakeUnexpected(Failure{});
    }

    bool destinationCreated = false;
    const auto ensureDestination = [&]() -> bool {
        if(destinationCreated)
            return true;

        if(const auto operation = EnsureDirectories(toDirectory); !operation){
            const ErrorCode errorCode = operation.error();
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
        if(const auto operation = RenamePath(currentPath, toDirectory / currentPath.filename()); !operation){
            const ErrorCode errorCode = operation.error();
            NWB_LOGGER_ERROR(NWB_TEXT("Filesystem volume publish: failed to move existing segment '{}' to backup: {}")
                , PathToString<tchar>(currentPath)
                , StringConvert(errorCode.message())
            );
            rollbackMovedFiles();
            return false;
        }

        movedFileNames.push_back(currentPath.filename());
        return true;
    };

    for(usize segmentIndex = 0u;; ++segmentIndex){
        const Path currentPath = ::MakeVolumeSegmentPath(fromDirectory, volumeName, segmentIndex);
        const auto exists = FileExists(currentPath);
        if(!exists){
            const ErrorCode& errorCode = exists.error();
            NWB_LOGGER_ERROR(NWB_TEXT("Filesystem volume publish: failed to query volume segment '{}': {}")
                , PathToString<tchar>(currentPath)
                , StringConvert(errorCode.message())
            );
            rollbackMovedFiles();
            return MakeUnexpected(Failure{});
        }
        if(!*exists)
            break;

        if(!moveSegmentToBackup(currentPath))
            return MakeUnexpected(Failure{});

        if(segmentIndex == Limit<usize>::s_Max){
            NWB_LOGGER_ERROR(NWB_TEXT("Filesystem volume publish: segment index overflow while backing up existing volume"));
            rollbackMovedFiles();
            return MakeUnexpected(Failure{});
        }
    }

    return movedFileNames;
}

template<typename FileNameVector>
static bool RestoreVolumeSegments(const Path& fromDirectory, const Path& toDirectory, const FileNameVector& fileNames){

    if(fileNames.empty())
        return true;
    if(const auto operation = EnsureDirectories(toDirectory); !operation){
        const ErrorCode errorCode = operation.error();
        NWB_LOGGER_WARNING(NWB_TEXT("Filesystem volume publish: failed to recreate output directory '{}' during rollback: {}")
            , PathToString<tchar>(toDirectory)
            , StringConvert(errorCode.message())
        );
        return false;
    }

    for(const Path& fileName : fileNames){
        const Path sourcePath = fromDirectory / fileName;
        const Path destinationPath = toDirectory / fileName;
        if(const auto operation = RenamePath(sourcePath, destinationPath); !operation){
            const ErrorCode errorCode = operation.error();
            NWB_LOGGER_WARNING(NWB_TEXT("Filesystem volume publish: failed to restore backup segment '{}' during rollback: {}")
                , PathToString<tchar>(sourcePath)
                , StringConvert(errorCode.message())
            );
            return false;
        }
    }

    return true;
}

struct VolumePromotionFailure{
    usize movedCount = 0u;
};

static Expected<void, VolumePromotionFailure> MoveStagedVolumeSegments(
    const Path& fromDirectory,
    const Path& toDirectory,
    const AStringView volumeName,
    const usize segmentCount
){

    usize movedCount = 0u;

    if(segmentCount == 0){
        NWB_LOGGER_ERROR(NWB_TEXT("Filesystem volume publish: staged volume '{}' did not produce any segments"), StringConvert(volumeName));
        return MakeUnexpected(VolumePromotionFailure{ .movedCount = movedCount });
    }
    if(const auto operation = EnsureDirectories(toDirectory); !operation){
        const ErrorCode errorCode = operation.error();
        NWB_LOGGER_ERROR(NWB_TEXT("Filesystem volume publish: failed to create output directory '{}': {}")
            , PathToString<tchar>(toDirectory)
            , StringConvert(errorCode.message())
        );
        return MakeUnexpected(VolumePromotionFailure{ .movedCount = movedCount });
    }

    for(usize segmentIndex = 0u; segmentIndex < segmentCount; ++segmentIndex){
        const Path sourcePath = ::MakeVolumeSegmentPath(fromDirectory, volumeName, segmentIndex);
        const Path destinationPath = ::MakeVolumeSegmentPath(toDirectory, volumeName, segmentIndex);
        if(const auto operation = RenamePath(sourcePath, destinationPath); !operation){
            const ErrorCode errorCode = operation.error();
            NWB_LOGGER_ERROR(NWB_TEXT("Filesystem volume publish: failed to promote staged segment '{}' to '{}': {}")
                , PathToString<tchar>(sourcePath)
                , PathToString<tchar>(destinationPath)
                , StringConvert(errorCode.message())
            );
            return MakeUnexpected(VolumePromotionFailure{ .movedCount = movedCount });
        }

        ++movedCount;
    }

    return {};
}

static void RemovePromotedVolumeSegmentsBestEffort(const Path& outputDirectory, const AStringView volumeName, const usize segmentCount){

    for(usize segmentIndex = 0u; segmentIndex < segmentCount; ++segmentIndex){
        const Path segmentPath = ::MakeVolumeSegmentPath(outputDirectory, volumeName, segmentIndex);
        if(const auto operation = RemoveFile(segmentPath); !operation || !*operation){
            const ErrorCode errorCode = operation ? ErrorCode{} : operation.error();
            NWB_LOGGER_WARNING(NWB_TEXT("Filesystem volume publish: failed to remove promoted segment '{}' after failed promotion: {}")
                , PathToString<tchar>(segmentPath)
                , StringConvert(FilesystemMutationFailureDetail(errorCode, "segment was not present"))
            );
        }
    }
}

bool PromoteStagedVolume(const StagedVolumePaths& stagedPaths, const Path& outputDirectory, const AStringView volumeName, const usize segmentCount){
    Core::Alloc::ScratchArena scratchArena(FilesystemArenaScope::s_PromoteStagedVolumeScratch);
    const auto movedBackupFiles = MoveExistingVolumeSegments(scratchArena, outputDirectory, stagedPaths.backupDirectory, volumeName);
    if(!movedBackupFiles)
        return false;

    const auto promoted = MoveStagedVolumeSegments(stagedPaths.stageDirectory, outputDirectory, volumeName, segmentCount);
    if(!promoted){
        RemovePromotedVolumeSegmentsBestEffort(outputDirectory, volumeName, promoted.error().movedCount);
        if(RestoreVolumeSegments(stagedPaths.backupDirectory, outputDirectory, *movedBackupFiles)){
            CleanupStagedDirectoryBestEffort(stagedPaths.backupDirectory, s_VolumePublishLogPrefix, "backup directory");
            CleanupStagedDirectoryBestEffort(stagedPaths.stageDirectory, s_VolumePublishLogPrefix, "stage directory");
        }
        return false;
    }

    CleanupStagedDirectoryBestEffort(stagedPaths.backupDirectory, s_VolumePublishLogPrefix, "backup directory");
    CleanupStagedDirectoryBestEffort(stagedPaths.stageDirectory, s_VolumePublishLogPrefix, "stage directory");

    return true;
}

bool RemoveExistingVolumeSegments(const Path& outputDirectory, const AStringView volumeName){

    const auto outputExists = FileExists(outputDirectory);
    if(!outputExists){
        const ErrorCode& errorCode = outputExists.error();
        NWB_LOGGER_ERROR(NWB_TEXT("Failed to query output directory '{}' : {}")
            , PathToString<tchar>(outputDirectory)
            , StringConvert(errorCode.message())
        );
        return false;
    }
    if(!*outputExists)
        return true;

    const auto directory = IsDirectory(outputDirectory);
    if(!directory || !*directory){
        if(!directory){
            const ErrorCode& errorCode = directory.error();
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

    for(usize segmentIndex = 0u;; ++segmentIndex){
        const Path hashedPath = ::MakeVolumeSegmentPath(outputDirectory, volumeName, segmentIndex);

        const auto exists = FileExists(hashedPath);
        if(!exists){
            const ErrorCode& errorCode = exists.error();
            NWB_LOGGER_ERROR(NWB_TEXT("Failed to query hashed segment '{}' : {}")
                , PathToString<tchar>(hashedPath)
                , StringConvert(errorCode.message())
            );
            return false;
        }
        if(!*exists)
            break;

        if(const auto operation = RemoveFile(hashedPath); !operation || !*operation){
            const ErrorCode errorCode = operation ? ErrorCode{} : operation.error();
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


bool PublishStagedVolume(const StagedDirectoryPaths& stagedPaths, const Path& outputDirectory, const AStringView volumeName, const usize segmentCount){
    return FilesystemVolumeStagingDetail::PromoteStagedVolume(stagedPaths, outputDirectory, volumeName, segmentCount);
}

bool RemoveVolumeSegments(const Path& outputDirectory, const AStringView volumeName){
    if(!::ValidVolumeName(volumeName)){
        FilesystemVolumeDetail::LogFailure(volumeName, FilesystemVolumeDetail::s_VolumeOpRemove, "invalid volume name");
        return false;
    }

    return FilesystemVolumeStagingDetail::RemoveExistingVolumeSegments(outputDirectory, volumeName);
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_FILESYSTEM_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

