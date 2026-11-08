// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "package_internal.h"
#include "package_archive.h"
#include "package_android_emergency.h"
#include "package_upload_transport.h"

#include <core/crash/arena_names.adhd.h>
#include <core/crash/package_values.adhd.h>

#include <global/diagnostics.h>
#include <global/filesystem/retention.h>
#include <global/optional.h>
#include <global/span.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_CRASH_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace CrashDetail{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


inline constexpr StringView s_SpoolRetentionCategory = ::NWB::Core::Crash::PackageValues::s_UploadSpoolRetentionCategory;


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


template<typename ArenaT>
static b8 IsSafePackageName(const ::Path<ArenaT>& path)noexcept{
    const auto name = path.filenameView();
    if(name.empty() || IsDot(name) || IsDotDot(name))
        return false;
    for(const auto ch : name){
        const b8 ok = IsAsciiIdentifierChar(ch) || ch == '-' || ch == '.';
        if(!ok)
            return false;
    }
    return true;
}

template<typename ArenaT>
static CrashStringT<ArenaT> CrashUploadUrl(ArenaT& arena, const AStringView logServerUrl){
    CrashStringT<ArenaT> url{arena};
    if(logServerUrl.empty())
        return url;
    constexpr AStringView suffix(PackageNames::s_CrashUploadEndpoint);
    url.reserve(AddSize(logServerUrl.size(), suffix.size()));
    url += logServerUrl;
    if(url.size() >= suffix.size() && AStringView(url.data() + url.size() - suffix.size(), suffix.size()) == suffix)
        return url;
    if(!url.empty() && url.back() == '/')
        url += PackageNames::s_CrashUploadEndpointName;
    else
        url += PackageNames::s_CrashUploadEndpoint;
    return url;
}

static b8 WriteUploadAttemptText(Alloc::ScratchArena& scratchArena, const ::Path<Alloc::ScratchArena>& packageDirectory, const AStringView state){
    constexpr AStringView s_StatePrefix = ::NWB::Core::Crash::PackageValues::s_UploadStatePrefix;
    const AStringView stateText = state.data() ? state : PackageNames::s_UploadAttemptUnknownState;
    CrashStringT<Alloc::ScratchArena> text(scratchArena);
    text.reserve(AddSize(stateText.size(), s_StatePrefix.size() + 1u));
    text += s_StatePrefix;
    text += stateText;
    text += "\n";
    if(WriteTextFile(packageDirectory / PackageNames::s_UploadAttemptFileName, AStringView(text.data(), text.size())))
        return true;
    CaptureDiagnosticEvent(DiagnosticEventRecord{
        .event = DiagnosticEventName::s_Warning,
        .category = s_PackageErrorCategory,
        .expression = "write_upload_attempt",
        .message = "Crash upload state text could not be written or closed",
        .file = __FILE__,
        .line = __LINE__,
    });
    return false;
}

template<typename ArenaT>
Expected<void, ErrorCode> ApplyCrashSpoolRetention(Alloc::ScratchArena& scratchArena, const ::Path<ArenaT>& spoolDirectory, const CrashSpoolRetentionConfig& retention, const AStringView protectedPendingPackageName){
    const ::Path<Alloc::ScratchArena> operationSpoolDirectory(scratchArena, spoolDirectory);
    const auto isSafePackageDirectory = [&](const ::Path<Alloc::ScratchArena>& path)->Expected<b8, ErrorCode>{
        const auto directory = IsDirectory(path);
        if(!directory)
            return MakeUnexpected(directory.error());
        return *directory && IsSafePackageName(path);
    };

    const auto shouldRetainPendingPackageDirectory = [&](const ::Path<Alloc::ScratchArena>& path)->Expected<b8, ErrorCode>{
        const auto safeDirectory = isSafePackageDirectory(path);
        if(!safeDirectory)
            return MakeUnexpected(safeDirectory.error());
        if(!*safeDirectory)
            return false;

        if(protectedPendingPackageName.empty())
            return true;

        const CrashStringT<Alloc::ScratchArena> packageName = PathToString<char>(scratchArena, path.filenameView());
        return AStringView(packageName.data(), packageName.size()) != protectedPendingPackageName;
    };

    Optional<ErrorCode> firstError;
    const auto pending = ApplyDirectoryRetention(scratchArena, PendingDirectory(operationSpoolDirectory), retention.maxPendingPackages, shouldRetainPendingPackageDirectory);
    if(!pending)
        firstError = pending.error();
    const auto uploaded = ApplyDirectoryRetention(scratchArena, UploadedDirectory(operationSpoolDirectory), retention.maxUploadedPackages, isSafePackageDirectory);
    if(!uploaded && !firstError)
        firstError = uploaded.error();
    const auto failed = ApplyDirectoryRetention(scratchArena, FailedDirectory(operationSpoolDirectory), retention.maxFailedPackages, isSafePackageDirectory);
    if(!failed && !firstError)
        firstError = failed.error();
    const auto uploading = ApplyDirectoryRetention(scratchArena, UploadingDirectory(operationSpoolDirectory), retention.maxUploadingPackages, isSafePackageDirectory);
    if(!uploading && !firstError)
        firstError = uploading.error();
    if(firstError)
        return MakeUnexpected(*firstError);
    return {};
}

template<typename ArenaT>
static b8 UploadPackageDirectory(
    ArenaT& arena,
    Alloc::ScratchArena& scratchArena,
    const ::Path<Alloc::ScratchArena>& spoolDirectory,
    const ::Path<Alloc::ScratchArena>& packageDirectory,
    const CrashStringT<Alloc::ScratchArena>& url,
    const AStringView crashUploadToken
){
    if(url.empty())
        return false;
    const auto uploading = ::MovePathToDirectory(packageDirectory, UploadingDirectory(spoolDirectory));
    if(!uploading){
        CaptureCrashPackageFilesystemFailure("begin_package_upload", uploading.error());
        return false;
    }
    const ::Path<Alloc::ScratchArena>& uploadingPackageDirectory = *uploading;
    if(!WriteUploadAttemptText(scratchArena, uploadingPackageDirectory, PackageNames::s_UploadAttemptUploadingState))
        return false;
    const auto archiveBytes = CrashPackageArchive::build(arena, scratchArena, uploadingPackageDirectory);
    if(!archiveBytes){
        CaptureCrashPackageFilesystemFailure("build_upload_archive", archiveBytes.error());
        const auto failed = ::MovePathToDirectory(uploadingPackageDirectory, FailedDirectory(spoolDirectory));
        if(!failed)
            CaptureCrashPackageFilesystemFailure("retain_failed_upload_archive", failed.error());
        return false;
    }
    const b8 uploaded = CrashUploadTransport::upload(scratchArena, url, *archiveBytes, crashUploadToken);
    if(uploaded){
        if(!WriteUploadAttemptText(scratchArena, uploadingPackageDirectory, PackageNames::s_UploadAttemptUploadedState))
            return false;
        const auto retained = ::MovePathToDirectory(uploadingPackageDirectory, UploadedDirectory(spoolDirectory));
        if(!retained){
            CaptureCrashPackageFilesystemFailure("retain_uploaded_package", retained.error());
            return false;
        }
        return true;
    }
    if(!WriteUploadAttemptText(scratchArena, uploadingPackageDirectory, PackageNames::s_UploadAttemptRetryPendingState))
        return false;
    const auto pending = ::MovePathToDirectory(uploadingPackageDirectory, PendingDirectory(spoolDirectory));
    if(!pending)
        CaptureCrashPackageFilesystemFailure("retain_pending_upload_retry", pending.error());
    return false;
}

static b8 RecoverUploadingPackageDirectories(Alloc::ScratchArena& scratchArena, const ::Path<Alloc::ScratchArena>& spoolDirectory){
    const ::Path<Alloc::ScratchArena> uploadingDirectory = UploadingDirectory(spoolDirectory);
    const auto uploadingIsDirectory = IsDirectory(uploadingDirectory);
    if(!uploadingIsDirectory){
        CaptureCrashPackageFilesystemFailure("query_interrupted_uploads", uploadingIsDirectory.error());
        return false;
    }
    if(!*uploadingIsDirectory)
        return true;
    const auto directory = DirectoryIterator<Alloc::ScratchArena>::Create(uploadingDirectory);
    if(!directory){
        CaptureCrashPackageFilesystemFailure("enumerate_interrupted_uploads", directory.error());
        return false;
    }
    b8 ok = true;
    for(const auto& entry : *directory){
        const auto entryIsDirectory = IsDirectory(entry.path());
        if(!entryIsDirectory){
            CaptureCrashPackageFilesystemFailure("query_interrupted_package", entryIsDirectory.error());
            ok = false;
            continue;
        }
        if(!*entryIsDirectory || !IsSafePackageName(entry.path()))
            continue;
        if(!WriteUploadAttemptText(scratchArena, entry.path(), PackageNames::s_UploadAttemptRetryInterruptedState))
            ok = false;
        const auto pending = ::MovePathToDirectory(entry.path(), PendingDirectory(spoolDirectory));
        if(!pending){
            CaptureCrashPackageFilesystemFailure("recover_interrupted_package", pending.error());
            ok = false;
        }
    }
    return ok;
}

template<typename ArenaT>
b8 FlushPendingCrashReportsImpl(ArenaT& arena, const CrashUploadSnapshot& snapshot){
    if(snapshot.spoolDirectory[0] == 0)
        return false;
    Alloc::ScratchArena scratchArena(CrashArenaScope::s_UploadScratch);
    CrashAndroidEmergencyCollector::collect(scratchArena, snapshot);

    const ::Path<Alloc::ScratchArena> spoolDirectory(scratchArena, snapshot.spoolDirectory.data());
    const b8 recoveryOk = RecoverUploadingPackageDirectories(scratchArena, spoolDirectory);
    const auto retentionBeforeUpload = ApplyCrashSpoolRetention(scratchArena, spoolDirectory, snapshot.spoolRetention, AStringView(snapshot.protectedPendingPackageName.data()));
    if(!retentionBeforeUpload){
        const DiagnosticEventText diagnostic = MakeDiagnosticEventText("Crash spool retention failed before upload: {} {}", retentionBeforeUpload.error().category().name(), retentionBeforeUpload.error().value());
        CaptureDiagnosticEvent(DiagnosticEventRecord{ .event = DiagnosticEventName::s_Warning, .category = s_SpoolRetentionCategory, .message = diagnostic.view(), .file = __FILE__, .line = __LINE__, });
    }
    const b8 retentionOk = static_cast<b8>(retentionBeforeUpload);

    const CrashStringT<Alloc::ScratchArena> url = CrashUploadUrl(scratchArena, snapshot.logServerUrl.data());
    if(url.empty())
        return false;

    if(!CrashUploadTransport::initialize())
        return false;

    b8 allUploaded = true;
    const ::Path<Alloc::ScratchArena> pendingDirectory = PendingDirectory(spoolDirectory);
    const auto pendingIsDirectory = IsDirectory(pendingDirectory);
    if(!pendingIsDirectory){
        CaptureCrashPackageFilesystemFailure("query_pending_uploads", pendingIsDirectory.error());
        return false;
    }
    if(!*pendingIsDirectory)
        return recoveryOk && retentionOk;

    const auto directory = DirectoryIterator<Alloc::ScratchArena>::Create(pendingDirectory);
    if(!directory){
        CaptureCrashPackageFilesystemFailure("enumerate_pending_uploads", directory.error());
        return false;
    }

    for(const auto& entry : *directory){
        const auto entryIsDirectory = IsDirectory(entry.path());
        if(!entryIsDirectory){
            CaptureCrashPackageFilesystemFailure("query_pending_package", entryIsDirectory.error());
            allUploaded = false;
            continue;
        }
        if(!*entryIsDirectory || !IsSafePackageName(entry.path()))
            continue;

        if(!UploadPackageDirectory(arena, scratchArena, spoolDirectory, entry.path(), url, AStringView(snapshot.crashUploadToken.data())))
            allUploaded = false;
    }

    const auto retentionAfterUpload = ApplyCrashSpoolRetention(scratchArena, spoolDirectory, snapshot.spoolRetention);
    if(!retentionAfterUpload){
        const DiagnosticEventText diagnostic = MakeDiagnosticEventText("Crash spool retention failed after upload: {} {}", retentionAfterUpload.error().category().name(), retentionAfterUpload.error().value());
        CaptureDiagnosticEvent(DiagnosticEventRecord{ .event = DiagnosticEventName::s_Warning, .category = s_SpoolRetentionCategory, .message = diagnostic.view(), .file = __FILE__, .line = __LINE__, });
    }
    return allUploaded && recoveryOk && retentionOk && static_cast<b8>(retentionAfterUpload);
}

b8 FlushCrashReportsForRequest(const CrashRequest& request){
    if(!CrashProtocol::IsCurrentCrashRequest(request))
        return false;

    CrashUploadSnapshot snapshot;
    snapshot.spoolRetention = request.spoolRetention;
    CopyFixedBuffer(snapshot.spoolDirectory, request.spoolDirectory);
    CopyFixedBuffer(snapshot.logServerUrl, request.logServerUrl);
    CopyFixedBuffer(snapshot.crashUploadToken, request.crashUploadToken);
    CopyFixedBuffer(snapshot.protectedPendingPackageName, request.crashId);

    // Out-of-process handler: no signal/exception limits, so a growable arena replaces the fixed DumpArena.
    Alloc::GlobalArena uploadArena(CrashArenaScope::s_UploadArena);
    return FlushPendingCrashReportsImpl(uploadArena, snapshot);
}

template Expected<void, ErrorCode> ApplyCrashSpoolRetention(Alloc::ScratchArena& scratchArena, const ::Path<Alloc::GlobalArena>& spoolDirectory, const CrashSpoolRetentionConfig& retention, AStringView protectedPendingPackageName);
template Expected<void, ErrorCode> ApplyCrashSpoolRetention(Alloc::ScratchArena& scratchArena, const ::Path<Alloc::ScratchArena>& spoolDirectory, const CrashSpoolRetentionConfig& retention, AStringView protectedPendingPackageName);
template b8 FlushPendingCrashReportsImpl(Alloc::GlobalArena& arena, const CrashUploadSnapshot& snapshot);

template Expected<void, ErrorCode> ApplyCrashSpoolRetention(Alloc::ScratchArena& scratchArena, const ::Path<Alloc::PersistentArena>& spoolDirectory, const CrashSpoolRetentionConfig& retention, AStringView protectedPendingPackageName);
template b8 FlushPendingCrashReportsImpl(Alloc::PersistentArena& arena, const CrashUploadSnapshot& snapshot);


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_CRASH_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

