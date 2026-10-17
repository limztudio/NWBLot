// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "volume_staging.h"
#include "volume_staging_detail.h"
#include "volume_storage_detail.h"

#include <core/alloc/scratch.h>
#include <core/common/log.h>
#include <core/filesystem/volume_naming.h>

#include <global/allocation_size.h>
#include <global/filesystem/cleanup.h>
#include <global/limit.h>
#include <global/scope_exit.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_FILESYSTEM_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


bool RemoveStagedDirectoryIfPresent(const Path& directoryPath, const AStringView operationName, const AStringView label){

    if(const auto operation = RemoveAllIfExists(directoryPath); !operation){
        const ErrorCode errorCode = operation.error();
        NWB_LOGGER_ERROR(NWB_TEXT("{}: failed to remove {} '{}': {}"), StringConvert(operationName), StringConvert(label), PathToString<tchar>(directoryPath), StringConvert(errorCode.message()));
        return false;
    }

    return true;
}

void CleanupStagedDirectoryBestEffort(const Path& directoryPath, const AStringView operationName, const AStringView label){

    if(const auto operation = RemoveAllIfExists(directoryPath); !operation){
        const ErrorCode errorCode = operation.error();
        NWB_LOGGER_WARNING(NWB_TEXT("{}: failed to remove {} '{}': {}"), StringConvert(operationName), StringConvert(label), PathToString<tchar>(directoryPath), StringConvert(errorCode.message()));
    }
}

bool EnsureEmptyStagedDirectory(const Path& directoryPath, const AStringView operationName, const AStringView label){

    if(const auto operation = ::EnsureEmptyDirectory(directoryPath); !operation){
        const ErrorCode errorCode = operation.error();
        NWB_LOGGER_ERROR(NWB_TEXT("{}: failed to create {} '{}': {}"), StringConvert(operationName), StringConvert(label), PathToString<tchar>(directoryPath), StringConvert(errorCode.message()));
        return false;
    }

    return true;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


StagedDirectoryCleanupGuard::StagedDirectoryCleanupGuard(const Path& directoryPath, const AStringView operationName, const AStringView label)
    : m_directoryPath(directoryPath)
    , m_operationName(operationName)
    , m_label(label)
{}

StagedDirectoryCleanupGuard::~StagedDirectoryCleanupGuard()noexcept{
    if(!m_active)
        return;
    if(const auto removed = RemoveAllForCleanup(m_directoryPath); !removed){
        const DiagnosticEventText diagnostic = MakeDiagnosticEventText("{}: failed to remove {} '{}', error {}", m_operationName, m_label, m_directoryPath.c_str(), removed.error().value());
        CaptureDiagnosticEvent(DiagnosticEventRecord{ .event = "warning", .category = "filesystem_volume_cleanup", .message = diagnostic.view(), .file = __FILE__, .line = __LINE__, });
    }
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


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


using StagedVolumePaths = StagedDirectoryPaths;
using ScratchPath = ::Path<Alloc::ScratchArena>;

struct VolumeSegmentMove{
    ScratchPath sourcePath;
    ScratchPath destinationPath;


public:
    VolumeSegmentMove(Alloc::ScratchArena& scratchArena, const Path& fromDirectory, const Path& toDirectory, const AStringView fileName)
        : sourcePath(scratchArena)
        , destinationPath(scratchArena)
    {
        sourcePath = fromDirectory;
        sourcePath /= fileName;
        destinationPath = toDirectory;
        destinationPath /= fileName;
    }
};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


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

StagedVolumePaths BuildStagedVolumePaths(const Path& outputDirectory, const AStringView volumeName, Alloc::ScratchArena& scratchArena){
    AString<Core::Alloc::ScratchArena> stageKey = PathToString<char>(scratchArena, outputDirectory);
    stageKey += s_StagedVolumeKeySeparator;
    stageKey += volumeName;

    AString<Core::Alloc::ScratchArena> stageToken{scratchArena};
    stageToken.reserve(s_StagedVolumeTokenPrefix.size() + s_StagedVolumeHashDigits);
    stageToken += s_StagedVolumeTokenPrefix;
    AppendHexU64<char, Core::Alloc::ScratchArena>(ComputeFnv64Text(AStringView(stageKey)), stageToken);
    return BuildStagedDirectoryPaths(scratchArena, outputDirectory, stageToken);
}

static Expected<usize> CountExistingVolumeSegments(const Path& fromDirectory, const AStringView volumeName, Alloc::ScratchArena& scratchArena){
    const auto sourceExists = FileExists(fromDirectory);
    if(!sourceExists){
        const ErrorCode& errorCode = sourceExists.error();
        NWB_LOGGER_ERROR(NWB_TEXT("Filesystem volume publish: failed to query output directory '{}': {}"), PathToString<tchar>(fromDirectory), StringConvert(errorCode.message()));
        return MakeUnexpected(Failure{});
    }
    if(!*sourceExists)
        return 0u;

    const auto directory = IsDirectory(fromDirectory);
    if(!directory || !*directory){
        if(!directory){
            const ErrorCode& errorCode = directory.error();
            NWB_LOGGER_ERROR(NWB_TEXT("Filesystem volume publish: failed to inspect output directory '{}': {}"), PathToString<tchar>(fromDirectory), StringConvert(errorCode.message()));
        }
        else{
            NWB_LOGGER_ERROR(NWB_TEXT("Filesystem volume publish: output path '{}' is not a directory"), PathToString<tchar>(fromDirectory));
        }
        return MakeUnexpected(Failure{});
    }

    for(usize segmentIndex = 0u;; ++segmentIndex){
        ScratchPath currentPath(scratchArena, fromDirectory);
        currentPath /= MakeVolumeSegmentFileName(volumeName, segmentIndex).view();
        const auto exists = FileExists(currentPath);
        if(!exists){
            const ErrorCode& errorCode = exists.error();
            NWB_LOGGER_ERROR(NWB_TEXT("Filesystem volume publish: failed to query volume segment '{}': {}"), PathToString<tchar>(currentPath), StringConvert(errorCode.message()));
            return MakeUnexpected(Failure{});
        }
        if(!*exists)
            return segmentIndex;
        if(segmentIndex == Limit<usize>::s_Max){
            NWB_LOGGER_ERROR(NWB_TEXT("Filesystem volume publish: segment index overflow while inspecting existing volume"));
            return MakeUnexpected(Failure{});
        }
    }
}

bool PromoteStagedVolume(const StagedVolumePaths& stagedPaths, const Path& outputDirectory, const AStringView volumeName, const usize segmentCount, Alloc::ScratchArena& scratchArena, const VolumePublicationObserver observer){
    if(segmentCount == 0u){
        NWB_LOGGER_ERROR(NWB_TEXT("Filesystem volume publish: staged volume '{}' did not produce any segments"), StringConvert(volumeName));
        return false;
    }
    const auto existingSegmentCount = CountExistingVolumeSegments(outputDirectory, volumeName, scratchArena);
    if(!existingSegmentCount)
        return false;

    Vector<VolumeSegmentMove, Alloc::ScratchArena> moves(scratchArena);
    moves.reserve(AddSize(*existingSegmentCount, segmentCount));
    // Release prepared paths in reverse construction order before the shared scratch plan storage.
    ScopeExit releaseMovePaths([&]()noexcept{
        while(!moves.empty())
            moves.pop_back();
    });
    for(usize segmentIndex = 0u; segmentIndex < *existingSegmentCount; ++segmentIndex)
        moves.emplace_back(scratchArena, outputDirectory, stagedPaths.backupDirectory, MakeVolumeSegmentFileName(volumeName, segmentIndex).view());
    for(usize segmentIndex = 0u; segmentIndex < segmentCount; ++segmentIndex)
        moves.emplace_back(scratchArena, stagedPaths.stageDirectory, outputDirectory, MakeVolumeSegmentFileName(volumeName, segmentIndex).view());

    if(const auto operation = EnsureDirectories(outputDirectory); !operation){
        const ErrorCode errorCode = operation.error();
        NWB_LOGGER_ERROR(NWB_TEXT("Filesystem volume publish: failed to create output directory '{}': {}"), PathToString<tchar>(outputDirectory), StringConvert(errorCode.message()));
        return false;
    }
    if(*existingSegmentCount != 0u){
        if(const auto operation = EnsureDirectories(stagedPaths.backupDirectory); !operation){
            const ErrorCode errorCode = operation.error();
            NWB_LOGGER_ERROR(NWB_TEXT("Filesystem volume publish: failed to create backup directory '{}': {}"), PathToString<tchar>(stagedPaths.backupDirectory), StringConvert(errorCode.message()));
            return false;
        }
    }

    bool promoted = true;
    bool rollbackComplete = true;
    usize movedCount = 0u;
    {
        ScopeExit rollbackMoves([&]()noexcept{
            while(movedCount != 0u){
                const VolumeSegmentMove& move = moves[--movedCount];
                if(movedCount < *existingSegmentCount){
                    if(const auto restored = RenamePath(move.destinationPath, move.sourcePath); !restored){
                        rollbackComplete = false;
                        const DiagnosticEventText diagnostic = MakeDiagnosticEventText("Failed to restore volume segment '{}', error {}", move.sourcePath.c_str(), restored.error().value());
                        CaptureDiagnosticEvent(DiagnosticEventRecord{ .event = "warning", .category = "filesystem_volume_cleanup", .message = diagnostic.view(), .file = __FILE__, .line = __LINE__, });
                    }
                }
                else{
                    if(const auto removed = RemoveFile(move.destinationPath); !removed || !*removed){
                        rollbackComplete = false;
                        const i32 errorValue = removed ? 0 : removed.error().value();
                        const DiagnosticEventText diagnostic = MakeDiagnosticEventText("Failed to remove promoted volume segment '{}', error {}", move.destinationPath.c_str(), errorValue);
                        CaptureDiagnosticEvent(DiagnosticEventRecord{ .event = "warning", .category = "filesystem_volume_cleanup", .message = diagnostic.view(), .file = __FILE__, .line = __LINE__, });
                    }
                }
            }
        });
        for(const VolumeSegmentMove& move : moves){
            if(const auto operation = RenamePath(move.sourcePath, move.destinationPath); !operation){
                const ErrorCode errorCode = operation.error();
                const AStringView action = movedCount < *existingSegmentCount ? "back up existing" : "promote staged";
                NWB_LOGGER_ERROR(NWB_TEXT("Filesystem volume publish: failed to {} segment '{}' to '{}': {}"), StringConvert(action), PathToString<tchar>(move.sourcePath), PathToString<tchar>(move.destinationPath), StringConvert(errorCode.message()));
                promoted = false;
                break;
            }
            ++movedCount;
        }
        if(promoted){
            rollbackMoves.release();
            if(observer.commit)
                observer.commit(observer.context);
        }
    }
    if(promoted || rollbackComplete){
        CleanupStagedDirectoryBestEffort(stagedPaths.backupDirectory, s_VolumePublishLogPrefix, "backup directory");
        CleanupStagedDirectoryBestEffort(stagedPaths.stageDirectory, s_VolumePublishLogPrefix, "stage directory");
    }
    return promoted;
}

bool RemoveExistingVolumeSegments(const Path& outputDirectory, const AStringView volumeName, Alloc::ScratchArena& scratchArena){

    const auto outputExists = FileExists(outputDirectory);
    if(!outputExists){
        const ErrorCode& errorCode = outputExists.error();
        NWB_LOGGER_ERROR(NWB_TEXT("Failed to query output directory '{}' : {}"), PathToString<tchar>(outputDirectory), StringConvert(errorCode.message()));
        return false;
    }
    if(!*outputExists)
        return true;

    const auto directory = IsDirectory(outputDirectory);
    if(!directory || !*directory){
        if(!directory){
            const ErrorCode& errorCode = directory.error();
            NWB_LOGGER_ERROR(NWB_TEXT("Failed to inspect output directory '{}' : {}"), PathToString<tchar>(outputDirectory), StringConvert(errorCode.message()));
        }
        else{
            NWB_LOGGER_ERROR(NWB_TEXT("Failed to remove old segments: output path '{}' is not a directory"), PathToString<tchar>(outputDirectory));
        }
        return false;
    }

    ScratchPath hashedPath(scratchArena);
    for(usize segmentIndex = 0u;; ++segmentIndex){
        hashedPath = outputDirectory;
        hashedPath /= MakeVolumeSegmentFileName(volumeName, segmentIndex).view();

        const auto exists = FileExists(hashedPath);
        if(!exists){
            const ErrorCode& errorCode = exists.error();
            NWB_LOGGER_ERROR(NWB_TEXT("Failed to query hashed segment '{}' : {}"), PathToString<tchar>(hashedPath), StringConvert(errorCode.message()));
            return false;
        }
        if(!*exists)
            break;

        if(const auto operation = RemoveFile(hashedPath); !operation || !*operation){
            const ErrorCode errorCode = operation ? ErrorCode{} : operation.error();
            NWB_LOGGER_ERROR(NWB_TEXT("Failed to remove old hashed segment '{}' : {}"), PathToString<tchar>(hashedPath), StringConvert(FilesystemMutationFailureDetail(errorCode, "segment was not present")));
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


bool PublishStagedVolume(const StagedDirectoryPaths& stagedPaths, const Path& outputDirectory, const AStringView volumeName, const usize segmentCount, Alloc::ScratchArena& scratchArena){
    return FilesystemVolumeStagingDetail::PromoteStagedVolume(stagedPaths, outputDirectory, volumeName, segmentCount, scratchArena);
}

bool RemoveVolumeSegments(const Path& outputDirectory, const AStringView volumeName, Alloc::ScratchArena& scratchArena){
    if(!ValidVolumeName(volumeName)){
        FilesystemVolumeDetail::LogFailure(volumeName, FilesystemVolumeDetail::s_VolumeOpRemove, "invalid volume name");
        return false;
    }

    return FilesystemVolumeStagingDetail::RemoveExistingVolumeSegments(outputDirectory, volumeName, scratchArena);
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_FILESYSTEM_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

