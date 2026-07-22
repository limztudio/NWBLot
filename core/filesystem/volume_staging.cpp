// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "volume_staging.h"
#include "volume_staging_detail.h"
#include "arena_names.h"

#include <core/alloc/scratch.h>
#include <core/common/log.h>
#include <global/filesystem/volume_naming.h>
#include <global/limit.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_FILESYSTEM_BEGIN


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


namespace __hidden_filesystem_staging{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


constexpr char s_StagedVolumeTokenPrefix[] = "volume_";
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

static void LogFailure(const AStringView volumeName, const AStringView operation, const AStringView detail){
    NWB_LOGGER_WARNING(NWB_TEXT("Filesystem('{}'): {} failed: {}")
        , StringConvert(volumeName)
        , StringConvert(operation)
        , StringConvert(detail)
    );
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

bool PromoteStagedVolume(const StagedVolumePaths& stagedPaths, const Path& outputDirectory, const AStringView volumeName, const usize segmentCount){
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

bool RemoveExistingVolumeSegments(const Path& outputDirectory, const AStringView volumeName){
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





////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


bool PublishStagedVolume(const StagedDirectoryPaths& stagedPaths, const Path& outputDirectory, const AStringView volumeName, const usize segmentCount){
    return __hidden_filesystem_staging::PromoteStagedVolume(stagedPaths, outputDirectory, volumeName, segmentCount);
}

bool RemoveVolumeSegments(const Path& outputDirectory, const AStringView volumeName){
    if(!::ValidVolumeName(volumeName)){
        __hidden_filesystem_staging::LogFailure(volumeName, "remove", "invalid volume name");
        return false;
    }

    return __hidden_filesystem_staging::RemoveExistingVolumeSegments(outputDirectory, volumeName);
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_FILESYSTEM_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

