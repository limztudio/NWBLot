// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#pragma once


#include "operations_file_io.h"
#include "operations_remove.h"

#include <global/diagnostics.h>
#include <global/scope_exit.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


inline constexpr AStringView s_StagedFileTemporarySuffix = ".tmp";
inline constexpr AStringView s_StagedFileBackupSuffix = ".old";
inline constexpr u32 s_StagedFilePairCount = 2u;


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


template<typename ArenaT>
struct StagedFilePairPaths{
    Path<ArenaT> primary;
    Path<ArenaT> secondary;
    Path<ArenaT> primaryTemporary;
    Path<ArenaT> secondaryTemporary;
    Path<ArenaT> primaryBackup;
    Path<ArenaT> secondaryBackup;


public:
    template<typename SourceArenaT>
    [[nodiscard]] static Path<ArenaT> MakeWorkPath(ArenaT& arena, const Path<SourceArenaT>& basePath, const AStringView suffix){
        Path<ArenaT> workPath(arena, basePath);
        workPath += suffix;
        return workPath;
    }

    template<typename SourceArenaT>
    [[nodiscard]] static Path<ArenaT> MakeReplacedExtensionPath(ArenaT& arena, const Path<SourceArenaT>& basePath, const AStringView extension){
        Path<ArenaT> replaced(arena, basePath);
        replaced.replaceExtension(extension);
        return replaced;
    }


public:
    template<typename PrimaryArenaT, typename SecondaryArenaT>
    StagedFilePairPaths(ArenaT& arena, const Path<PrimaryArenaT>& primaryPath, const Path<SecondaryArenaT>& secondaryPath)
        : primary(arena, primaryPath)
        , secondary(arena, secondaryPath)
        , primaryTemporary(MakeWorkPath(arena, primary, s_StagedFileTemporarySuffix))
        , secondaryTemporary(MakeWorkPath(arena, secondary, s_StagedFileTemporarySuffix))
        , primaryBackup(MakeWorkPath(arena, primary, s_StagedFileBackupSuffix))
        , secondaryBackup(MakeWorkPath(arena, secondary, s_StagedFileBackupSuffix))
    {}

    template<typename SourceArenaT>
    StagedFilePairPaths(ArenaT& arena, const Path<SourceArenaT>& primaryPath, const AStringView secondaryExtension)
        : primary(arena, primaryPath)
        , secondary(MakeReplacedExtensionPath(arena, primaryPath, secondaryExtension))
        , primaryTemporary(MakeWorkPath(arena, primary, s_StagedFileTemporarySuffix))
        , secondaryTemporary(MakeWorkPath(arena, secondary, s_StagedFileTemporarySuffix))
        , primaryBackup(MakeWorkPath(arena, primary, s_StagedFileBackupSuffix))
        , secondaryBackup(MakeWorkPath(arena, secondary, s_StagedFileBackupSuffix))
    {}
};


namespace StagedFilePairFailureStage{
    enum Enum : u8{
        None = 0u,
        QueryOutput,
        InspectOutput,
        NonRegularOutput,
        IncompleteExistingPair,
        OutputAlreadyExists,
        QueryWorkPath,
        WorkPathAlreadyExists,
        StagePrimary,
        StageSecondary,
        VerifyPrimary,
        VerifySecondary,
        PreserveExistingOutput,
        FinalizeOutput,
    };
};

struct StagedFilePairResult{
    StagedFilePairFailureStage::Enum failureStage = StagedFilePairFailureStage::None;
    u32 failedIndex = 0u;
    b8 failedWorkPath = false;
    ErrorCode error;
    Array<b8, s_StagedFilePairCount> backedUp = {};
    Array<ErrorCode, s_StagedFilePairCount> removalErrors = {};
    Array<ErrorCode, s_StagedFilePairCount> restorationErrors = {};
    Array<ErrorCode, s_StagedFilePairCount> temporaryErrors = {};
    Array<ErrorCode, s_StagedFilePairCount> backupCleanupErrors = {};
    Array<b8, s_StagedFilePairCount> backupCleanupMissing = {};


public:
    [[nodiscard]] constexpr b8 succeeded()const noexcept{
        return failureStage == StagedFilePairFailureStage::None;
    }
    [[nodiscard]] constexpr explicit operator bool()const noexcept{
        return succeeded();
    }
};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


template<typename ArenaT>
[[nodiscard]] const Path<ArenaT>& FailedStagedFilePairPath(const StagedFilePairPaths<ArenaT>& paths, const StagedFilePairResult& result)noexcept{
    if(result.failedWorkPath || result.failureStage == StagedFilePairFailureStage::QueryWorkPath || result.failureStage == StagedFilePairFailureStage::WorkPathAlreadyExists){
        switch(result.failedIndex){
        case 0u: return paths.primaryTemporary;
        case 1u: return paths.secondaryTemporary;
        case 2u: return paths.primaryBackup;
        default: return paths.secondaryBackup;
        }
    }
    return result.failedIndex == 0u ? paths.primary : paths.secondary;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace GlobalFilesystemStagingDetail{


[[nodiscard]] inline b8 AcceptPreCommitValidation(const StagedFilePairResult& check, StagedFilePairResult& result)noexcept{
    if(check || check.failureStage == StagedFilePairFailureStage::WorkPathAlreadyExists)
        return true;
    result.failureStage = check.failureStage;
    result.failedIndex = check.failedIndex;
    result.failedWorkPath = check.failedWorkPath;
    result.error = check.error;
    return false;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


template<typename ArenaT>
void CaptureCleanupDiagnostic(const AStringView diagnosticTag, const char* const messageFormat, const Path<ArenaT>& path, const ErrorCode& error)noexcept{
    const DiagnosticEventText diagnostic = MakeDiagnosticEventText(messageFormat, path.c_str(), error.value());
    CaptureDiagnosticEvent(diagnosticTag, diagnostic.view(), __FILE__, __LINE__);
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


template<typename ArenaT>
[[nodiscard]] inline StagedFilePairResult ValidateStagedFilePairPaths(
    const StagedFilePairPaths<ArenaT>& paths,
    const b8 allowOverwrite,
    const b8 requireCompleteExistingPair
)noexcept{
    StagedFilePairResult result;
    const Array<NotNull<const Path<ArenaT>*>, s_StagedFilePairCount> outputs = {
        MakeNotNull(&paths.primary),
        MakeNotNull(&paths.secondary),
    };
    u32 presentCount = 0u;
    u32 firstPresentIndex = 0u;
    for(u32 index = 0u; index < s_StagedFilePairCount; ++index){
        const auto exists = FileExistsNoFollow(*outputs[index]);
        if(!exists){
            result.failureStage = StagedFilePairFailureStage::QueryOutput;
            result.failedIndex = index;
            result.error = exists.error();
            return result;
        }
        if(!*exists)
            continue;

        const auto regular = IsRegularFile(*outputs[index]);
        if(!regular){
            result.failureStage = StagedFilePairFailureStage::InspectOutput;
            result.failedIndex = index;
            result.error = regular.error();
            return result;
        }
        if(!*regular){
            result.failureStage = StagedFilePairFailureStage::NonRegularOutput;
            result.failedIndex = index;
            result.error = MakeErrorCode(ErrorCondition::invalid_argument);
            return result;
        }
        if(presentCount == 0u)
            firstPresentIndex = index;
        result.backedUp[index] = true;
        ++presentCount;
    }

    if(presentCount != 0u){
        if(requireCompleteExistingPair && presentCount != s_StagedFilePairCount){
            result.failureStage = StagedFilePairFailureStage::IncompleteExistingPair;
            result.failedIndex = firstPresentIndex;
            result.error = MakeErrorCode(ErrorCondition::invalid_argument);
            return result;
        }
        if(!allowOverwrite){
            result.failureStage = StagedFilePairFailureStage::OutputAlreadyExists;
            result.failedIndex = firstPresentIndex;
            result.error = MakeErrorCode(ErrorCondition::file_exists);
            return result;
        }
    }

    const Array<NotNull<const Path<ArenaT>*>, 4u> workPaths = {
        MakeNotNull(&paths.primaryTemporary),
        MakeNotNull(&paths.secondaryTemporary),
        MakeNotNull(&paths.primaryBackup),
        MakeNotNull(&paths.secondaryBackup),
    };
    for(u32 index = 0u; index < static_cast<u32>(workPaths.size()); ++index){
        const auto exists = FileExistsNoFollow(*workPaths[index]);
        if(!exists){
            result.failureStage = StagedFilePairFailureStage::QueryWorkPath;
            result.failedIndex = index;
            result.error = exists.error();
            return result;
        }
        if(!*exists)
            continue;
        if(!requireCompleteExistingPair){
            const auto regular = IsRegularFile(*workPaths[index]);
            if(!regular){
                result.failureStage = StagedFilePairFailureStage::InspectOutput;
                result.failedIndex = index;
                result.failedWorkPath = true;
                result.error = regular.error();
                return result;
            }
            if(!*regular){
                result.failureStage = StagedFilePairFailureStage::NonRegularOutput;
                result.failedIndex = index;
                result.failedWorkPath = true;
                result.error = MakeErrorCode(ErrorCondition::invalid_argument);
                return result;
            }
        }
        result.failureStage = StagedFilePairFailureStage::WorkPathAlreadyExists;
        result.failedIndex = index;
        result.error = MakeErrorCode(ErrorCondition::file_exists);
        return result;
    }

    return result;
}


template<typename ArenaT>
[[nodiscard]] inline StagedFilePairResult StageAndPublishFilePair(
    const StagedFilePairPaths<ArenaT>& paths,
    const void* const primaryData,
    const usize primaryByteCount,
    const void* const secondaryData,
    const usize secondaryByteCount,
    const b8 allowOverwrite,
    const b8 requireCompleteExistingPair,
    const b8 verifyStagedBytes,
    const AStringView diagnosticTag
)noexcept{
    StagedFilePairResult result = ValidateStagedFilePairPaths(paths, allowOverwrite, requireCompleteExistingPair);
    if(!result)
        return result;

    for(u32 index = 0u; index < s_StagedFilePairCount; ++index)
        result.backedUp[index] = false;

    const Array<NotNull<const Path<ArenaT>*>, s_StagedFilePairCount> outputs = {
        MakeNotNull(&paths.primary),
        MakeNotNull(&paths.secondary),
    };
    const Array<NotNull<const Path<ArenaT>*>, s_StagedFilePairCount> temporaries = {
        MakeNotNull(&paths.primaryTemporary),
        MakeNotNull(&paths.secondaryTemporary),
    };
    const Array<NotNull<const Path<ArenaT>*>, s_StagedFilePairCount> backups = {
        MakeNotNull(&paths.primaryBackup),
        MakeNotNull(&paths.secondaryBackup),
    };
    Array<b8, s_StagedFilePairCount> ownedTemporary = {};
    Array<b8, s_StagedFilePairCount> published = {};

    const auto rollback = [&]()noexcept{
        for(u32 reverseIndex = s_StagedFilePairCount; reverseIndex > 0u; --reverseIndex){
            const u32 current = reverseIndex - 1u;
            if(published[current]){
                const auto removed = RemoveFile(*outputs[current]);
                if(!removed){
                    result.removalErrors[current] = removed.error();
                    GlobalFilesystemStagingDetail::CaptureCleanupDiagnostic(
                        diagnosticTag,
                        "publication cleanup failed removing new '{}', error {}",
                        *outputs[current],
                        result.removalErrors[current]
                    );
                }
            }
            if(result.backedUp[current] && !result.removalErrors[current]){
                const auto restored = RenamePath(*backups[current], *outputs[current]);
                if(!restored){
                    result.restorationErrors[current] = restored.error();
                    GlobalFilesystemStagingDetail::CaptureCleanupDiagnostic(
                        diagnosticTag,
                        "publication cleanup failed restoring backup '{}', error {}",
                        *backups[current],
                        result.restorationErrors[current]
                    );
                }
            }
            if(ownedTemporary[current]){
                const auto removed = RemoveFile(*temporaries[current]);
                if(!removed){
                    result.temporaryErrors[current] = removed.error();
                    GlobalFilesystemStagingDetail::CaptureCleanupDiagnostic(
                        diagnosticTag,
                        "publication cleanup failed removing temporary '{}', error {}",
                        *temporaries[current],
                        result.temporaryErrors[current]
                    );
                }
            }
        }
    };
    ScopeExit restoreOnFailure(rollback);

    const auto publish = [&]()noexcept -> b8{
        const auto primaryStaged = StageNewBinaryFileDurable(paths.primaryTemporary, primaryData, primaryByteCount, ownedTemporary[0u]);
        if(!primaryStaged){
            result.failureStage = StagedFilePairFailureStage::StagePrimary;
            result.failedIndex = 0u;
            result.error = primaryStaged.error();
            return false;
        }
        const auto secondaryStaged = StageNewBinaryFileDurable(paths.secondaryTemporary, secondaryData, secondaryByteCount, ownedTemporary[1u]);
        if(!secondaryStaged){
            result.failureStage = StagedFilePairFailureStage::StageSecondary;
            result.failedIndex = 1u;
            result.error = secondaryStaged.error();
            return false;
        }

        if(verifyStagedBytes){
            if(!VerifyBinaryFileBytes(paths.primaryTemporary, primaryData, primaryByteCount)){
                result.failureStage = StagedFilePairFailureStage::VerifyPrimary;
                result.failedIndex = 0u;
                result.error = MakeErrorCode(ErrorCondition::io_error);
                return false;
            }
            if(!VerifyBinaryFileBytes(paths.secondaryTemporary, secondaryData, secondaryByteCount)){
                result.failureStage = StagedFilePairFailureStage::VerifySecondary;
                result.failedIndex = 1u;
                result.error = MakeErrorCode(ErrorCondition::io_error);
                return false;
            }
        }

        const StagedFilePairResult preCommitCheck = ValidateStagedFilePairPaths(paths, allowOverwrite, requireCompleteExistingPair);
        if(!GlobalFilesystemStagingDetail::AcceptPreCommitValidation(preCommitCheck, result))
            return false;

        for(u32 index = 0u; index < s_StagedFilePairCount; ++index){
            const auto present = FileExistsNoFollow(*outputs[index]);
            if(!present){
                result.failureStage = StagedFilePairFailureStage::QueryOutput;
                result.failedIndex = index;
                result.error = present.error();
                return false;
            }
            if(!*present)
                continue;

            const auto backupExists = FileExistsNoFollow(*backups[index]);
            if(!backupExists){
                result.failureStage = StagedFilePairFailureStage::QueryWorkPath;
                result.failedIndex = index + s_StagedFilePairCount;
                result.error = backupExists.error();
                return false;
            }
            if(*backupExists){
                result.failureStage = StagedFilePairFailureStage::WorkPathAlreadyExists;
                result.failedIndex = index + s_StagedFilePairCount;
                result.error = MakeErrorCode(ErrorCondition::file_exists);
                return false;
            }

            const auto preserved = RenamePath(*outputs[index], *backups[index]);
            if(!preserved){
                result.failureStage = StagedFilePairFailureStage::PreserveExistingOutput;
                result.failedIndex = index;
                result.error = preserved.error();
                return false;
            }
            result.backedUp[index] = true;
        }

        for(u32 index = 0u; index < s_StagedFilePairCount; ++index){
            const auto finalized = RenamePath(*temporaries[index], *outputs[index]);
            if(!finalized){
                result.failureStage = StagedFilePairFailureStage::FinalizeOutput;
                result.failedIndex = index;
                result.error = finalized.error();
                return false;
            }
            published[index] = true;
            ownedTemporary[index] = false;
        }
        return true;
    };

    if(!publish()){
        rollback();
        restoreOnFailure.release();
        return result;
    }

    restoreOnFailure.release();
    for(u32 index = 0u; index < s_StagedFilePairCount; ++index){
        if(!result.backedUp[index])
            continue;
        const auto removed = RemoveFile(*backups[index]);
        if(!removed)
            result.backupCleanupErrors[index] = removed.error();
        else if(!*removed)
            result.backupCleanupMissing[index] = true;
    }
    return result;
}

template<typename ArenaT>
[[nodiscard]] inline StagedFilePairResult StageAndPublishFilePair(
    const StagedFilePairPaths<ArenaT>& paths,
    const Span<const u8> primaryBytes,
    const AStringView secondaryText,
    const b8 allowOverwrite,
    const b8 requireCompleteExistingPair,
    const b8 verifyStagedBytes,
    const AStringView diagnosticTag
)noexcept{
    return StageAndPublishFilePair(
        paths,
        primaryBytes.data(),
        primaryBytes.size(),
        secondaryText.data(),
        secondaryText.size(),
        allowOverwrite,
        requireCompleteExistingPair,
        verifyStagedBytes,
        diagnosticTag
    );
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

