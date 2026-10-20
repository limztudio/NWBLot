// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "package_internal.h"
#include "arena_names.h"

#include <global/diagnostics.h>
#include <global/filesystem/retention.h>
#include <global/optional.h>
#include <global/scope_exit.h>

#include <cstddef>

#include <curl/curl.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_CRASH_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace Detail{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


inline constexpr StringView s_AuthorizationBearerPrefix = "Authorization: Bearer ";
inline constexpr usize s_AuthorizationBearerPrefixLength = s_AuthorizationBearerPrefix.size();
inline constexpr long s_CurlOptionEnabled = 1L;
inline constexpr long s_CrashUploadConnectTimeoutMilliseconds = 1000L;
inline constexpr long s_CrashUploadTimeoutMilliseconds = 5000L;
inline constexpr long s_HttpSuccessStatusBegin = 200L;
inline constexpr long s_HttpSuccessStatusEnd = 300L;
inline constexpr char s_SafeNameDigitFirst = '0';
inline constexpr char s_SafeNameDigitLast = '9';
inline constexpr StringView s_UploadCategory = "crash_package_upload";
inline constexpr StringView s_SpoolRetentionCategory = "crash_spool_retention";

static_assert(sizeof(size_t) == sizeof(usize), "libcurl callback sizes must match the supported native address size");


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


static Futex s_CurlAllocatorMutex;
static Futex s_CurlGlobalInitMutex;
static bool s_CurlGlobalInitialized = false;


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


// PersistentArena rounds C allocations to MaxAlign; reject unsupported round-up before its throwing size arithmetic.
[[nodiscard]] static constexpr bool CanAllocateCrashCurlBytes(const usize size)noexcept{
    constexpr usize s_MaxRoundedBytes = Limit<usize>::s_Max - (alignof(MaxAlign) - 1u);
    return size <= s_MaxRoundedBytes;
}

static void* CrashCurlAllocate(const usize size)noexcept{
    if(!CanAllocateCrashCurlBytes(size))
        return nullptr;
    NothrowScopedLock lock(s_CurlAllocatorMutex);

    return AllocateArenaCMemory(DumpArena(), size);
}

static void CrashCurlDeallocate(void* const ptr)noexcept{
    NothrowScopedLock lock(s_CurlAllocatorMutex);

    DeallocateArenaCMemory(DumpArena(), ptr);
}

static void* CrashCurlMalloc(const size_t size)noexcept{
    return CrashCurlAllocate(static_cast<usize>(size));
}

static void CrashCurlFree(void* const ptr)noexcept{
    CrashCurlDeallocate(ptr);
}

static void* CrashCurlRealloc(void* const ptr, const size_t size)noexcept{
    if(!CanAllocateCrashCurlBytes(static_cast<usize>(size)))
        return nullptr;
    NothrowScopedLock lock(s_CurlAllocatorMutex);

    return ReallocateArenaCMemory(DumpArena(), ptr, static_cast<usize>(size));
}

static char* CrashCurlStrdup(const char* const text)noexcept{
    if(!text)
        return nullptr;
    const AStringView source(text);
    if(source.size() == Limit<usize>::s_Max || !CanAllocateCrashCurlBytes(source.size() + 1u))
        return nullptr;
    NothrowScopedLock lock(s_CurlAllocatorMutex);

    return DuplicateArenaCString(DumpArena(), source);
}

static void* CrashCurlCalloc(const size_t count, const size_t size)noexcept{
    if(count != 0u && size > Limit<usize>::s_Max / count)
        return nullptr;
    if(!CanAllocateCrashCurlBytes(static_cast<usize>(count * size)))
        return nullptr;
    NothrowScopedLock lock(s_CurlAllocatorMutex);

    return ZeroAllocateArenaCMemory(DumpArena(), static_cast<usize>(count), static_cast<usize>(size));
}

[[nodiscard]] static bool EnsureCrashCurlGlobalInit(){
    CURLcode result = CURLE_OK;
    {
        NothrowScopedLock lock(s_CurlGlobalInitMutex);
        if(s_CurlGlobalInitialized)
            return true;
        InitializeDumpArena();
        result = curl_global_init_mem(CURL_GLOBAL_ALL, CrashCurlMalloc, CrashCurlFree, CrashCurlRealloc, CrashCurlStrdup, CrashCurlCalloc);
        if(result == CURLE_OK){
            s_CurlGlobalInitialized = true;
            return true;
        }
    }
    const DiagnosticEventText diagnostic = MakeDiagnosticEventText("Crash upload initialization failed: curl code={} {}", static_cast<i64>(result), curl_easy_strerror(result));
    CaptureDiagnosticEvent(DiagnosticEventRecord{ .event = s_WarningEventName, .category = s_UploadCategory, .expression = "initialize_upload_runtime", .message = diagnostic.view(), .file = __FILE__, .line = __LINE__, });
    return false;
}

template<typename ArenaT>
static bool IsSafePackageName(const ::Path<ArenaT>& path){
    const ::Path<ArenaT> filename = path.filename();
    const auto name = filename.native();
    if(name.empty() || GlobalFilesystemPathDetail::IsDot(name) || GlobalFilesystemPathDetail::IsDotDot(name))
        return false;
    for(const auto ch : name){
        const bool ok = (ch >= 'a' && ch <= 'z') || (ch >= 'A' && ch <= 'Z') || (ch >= s_SafeNameDigitFirst && ch <= s_SafeNameDigitLast) || ch == '-' || ch == '_' || ch == '.';
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

struct UploadFailure{
    AStringView operation{};
    Optional<CURLcode> curlCode{};
    Optional<long> responseCode{};
};

static void CaptureUploadFailure(const UploadFailure& failure)noexcept{
    if(failure.curlCode){
        const DiagnosticEventText diagnostic = MakeDiagnosticEventText("Crash upload failed: curl code={} {}", static_cast<i64>(*failure.curlCode), curl_easy_strerror(*failure.curlCode));
        CaptureDiagnosticEvent(DiagnosticEventRecord{ .event = s_WarningEventName, .category = s_UploadCategory, .expression = failure.operation, .message = diagnostic.view(), .file = __FILE__, .line = __LINE__, });
        return;
    }
    if(failure.responseCode){
        const DiagnosticEventText diagnostic = MakeDiagnosticEventText("Crash upload failed: HTTP status={}", static_cast<i64>(*failure.responseCode));
        CaptureDiagnosticEvent(DiagnosticEventRecord{ .event = s_WarningEventName, .category = s_UploadCategory, .expression = failure.operation, .message = diagnostic.view(), .file = __FILE__, .line = __LINE__, });
        return;
    }
    CaptureDiagnosticEvent(DiagnosticEventRecord{ .event = s_WarningEventName, .category = s_UploadCategory, .expression = failure.operation, .message = "Crash upload SDK resource admission failed", .file = __FILE__, .line = __LINE__, });
}

template<typename ArenaT>
static Expected<void, UploadFailure> UploadPackage(Alloc::ScratchArena& scratchArena, const CrashStringT<Alloc::ScratchArena>& url, const CrashBytesT<ArenaT>& archiveBytes, const AStringView crashUploadToken){
    if(!CanRepresentU64<curl_off_t>(static_cast<u64>(archiveBytes.size())))
        return MakeUnexpected(UploadFailure{ .operation = "admit_upload_size" });
    if(crashUploadToken.size() > Limit<usize>::s_Max - s_AuthorizationBearerPrefixLength)
        return MakeUnexpected(UploadFailure{ .operation = "admit_authorization_size" });
    CrashStringT<Alloc::ScratchArena> authorizationHeader(scratchArena);
    if(!crashUploadToken.empty()){
        authorizationHeader.reserve(crashUploadToken.size() + s_AuthorizationBearerPrefixLength);
        authorizationHeader += s_AuthorizationBearerPrefix;
        authorizationHeader += crashUploadToken;
    }
    CURL* const curl = curl_easy_init();
    if(!curl)
        return MakeUnexpected(UploadFailure{ .operation = "initialize_upload" });
    ScopeExit releaseCurl([curl]()noexcept{ curl_easy_cleanup(curl); });
    curl_slist* headers = nullptr;
    ScopeExit releaseHeaders([&]()noexcept{
        if(headers)
            curl_slist_free_all(headers);
    }
    );
    if(!crashUploadToken.empty()){
        headers = curl_slist_append(nullptr, authorizationHeader.c_str());
        if(!headers)
            return MakeUnexpected(UploadFailure{ .operation = "prepare_authorization_header" });
    }
    const auto setOption = [&](const AStringView operation, const auto option, const auto value)noexcept->Expected<void, UploadFailure>{
        const CURLcode result = curl_easy_setopt(curl, option, value);
        if(result != CURLE_OK)
            return MakeUnexpected(UploadFailure{ .operation = operation, .curlCode = result });
        return {};
    };
    const auto urlOption = setOption("set_upload_url", CURLOPT_URL, url.c_str());
    if(!urlOption)
        return MakeUnexpected(urlOption.error());
    const auto postOption = setOption("set_upload_post", CURLOPT_POST, s_CurlOptionEnabled);
    if(!postOption)
        return MakeUnexpected(postOption.error());
    const auto signalOption = setOption("set_upload_nosignal", CURLOPT_NOSIGNAL, s_CurlOptionEnabled);
    if(!signalOption)
        return MakeUnexpected(signalOption.error());
    const auto connectOption = setOption("set_upload_connect_timeout", CURLOPT_CONNECTTIMEOUT_MS, s_CrashUploadConnectTimeoutMilliseconds);
    if(!connectOption)
        return MakeUnexpected(connectOption.error());
    const auto timeoutOption = setOption("set_upload_timeout", CURLOPT_TIMEOUT_MS, s_CrashUploadTimeoutMilliseconds);
    if(!timeoutOption)
        return MakeUnexpected(timeoutOption.error());
    const auto bytesOption = setOption("set_upload_bytes", CURLOPT_POSTFIELDS, reinterpret_cast<const char*>(archiveBytes.data()));
    if(!bytesOption)
        return MakeUnexpected(bytesOption.error());
    const auto sizeOption = setOption("set_upload_size", CURLOPT_POSTFIELDSIZE_LARGE, static_cast<curl_off_t>(archiveBytes.size()));
    if(!sizeOption)
        return MakeUnexpected(sizeOption.error());
    if(headers){
        const auto headersOption = setOption("set_upload_headers", CURLOPT_HTTPHEADER, headers);
        if(!headersOption)
            return MakeUnexpected(headersOption.error());
    }
    const CURLcode performed = curl_easy_perform(curl);
    if(performed != CURLE_OK)
        return MakeUnexpected(UploadFailure{ .operation = "perform_upload", .curlCode = performed });
    long responseCode = 0L;
    const CURLcode queried = curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &responseCode);
    if(queried != CURLE_OK)
        return MakeUnexpected(UploadFailure{ .operation = "query_upload_response", .curlCode = queried });
    if(responseCode < s_HttpSuccessStatusBegin || responseCode >= s_HttpSuccessStatusEnd)
        return MakeUnexpected(UploadFailure{ .operation = "admit_upload_response", .responseCode = responseCode });
    return {};
}

static bool WriteUploadAttemptText(Alloc::ScratchArena& scratchArena, const ::Path<Alloc::ScratchArena>& packageDirectory, const AStringView state){
    constexpr AStringView s_StatePrefix = "state=";
    const AStringView stateText = state.data() ? state : PackageNames::s_UploadAttemptUnknownState;
    CrashStringT<Alloc::ScratchArena> text(scratchArena);
    text.reserve(AddSize(stateText.size(), s_StatePrefix.size() + 1u));
    text += s_StatePrefix;
    text += stateText;
    text += "\n";
    if(WriteTextFile(packageDirectory / PackageNames::s_UploadAttemptFileName, AStringView(text.data(), text.size())))
        return true;
    CaptureDiagnosticEvent(DiagnosticEventRecord{ .event = s_WarningEventName, .category = s_PackageErrorCategory, .expression = "write_upload_attempt", .message = "Crash upload state text could not be written or closed", .file = __FILE__, .line = __LINE__, });
    return false;
}

template<typename ArenaT>
Expected<void, ErrorCode> ApplyCrashSpoolRetention(Alloc::ScratchArena& scratchArena, const ::Path<ArenaT>& spoolDirectory, const CrashSpoolRetentionConfig& retention, const AStringView protectedPendingPackageName){
    const ::Path<Alloc::ScratchArena> operationSpoolDirectory(scratchArena, spoolDirectory);
    const auto isSafePackageDirectory = [&](const ::Path<Alloc::ScratchArena>& path)->Expected<bool, ErrorCode>{
        const auto directory = IsDirectory(path);
        if(!directory)
            return MakeUnexpected(directory.error());
        return *directory && IsSafePackageName(path);
    };

    const auto shouldRetainPendingPackageDirectory = [&](const ::Path<Alloc::ScratchArena>& path)->Expected<bool, ErrorCode>{
        const auto safeDirectory = isSafePackageDirectory(path);
        if(!safeDirectory)
            return MakeUnexpected(safeDirectory.error());
        if(!*safeDirectory)
            return false;

        if(protectedPendingPackageName.empty())
            return true;

        const ::Path<Alloc::ScratchArena> filename = path.filename();
        const CrashStringT<Alloc::ScratchArena> packageName = PathToString<char>(scratchArena, filename);
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
static bool UploadPackageDirectory(
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
    const auto archiveBytes = BuildPackageArchive(arena, scratchArena, uploadingPackageDirectory);
    if(!archiveBytes){
        CaptureCrashPackageFilesystemFailure("build_upload_archive", archiveBytes.error());
        const auto failed = ::MovePathToDirectory(uploadingPackageDirectory, FailedDirectory(spoolDirectory));
        if(!failed)
            CaptureCrashPackageFilesystemFailure("retain_failed_upload_archive", failed.error());
        return false;
    }
    const auto uploaded = UploadPackage(scratchArena, url, *archiveBytes, crashUploadToken);
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
    CaptureUploadFailure(uploaded.error());
    if(!WriteUploadAttemptText(scratchArena, uploadingPackageDirectory, PackageNames::s_UploadAttemptRetryPendingState))
        return false;
    const auto pending = ::MovePathToDirectory(uploadingPackageDirectory, PendingDirectory(spoolDirectory));
    if(!pending)
        CaptureCrashPackageFilesystemFailure("retain_pending_upload_retry", pending.error());
    return false;
}

static bool RecoverUploadingPackageDirectories(Alloc::ScratchArena& scratchArena, const ::Path<Alloc::ScratchArena>& spoolDirectory){
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
    bool ok = true;
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

#if defined(NWB_PLATFORM_ANDROID)
static bool WriteAndroidCollectionNote(Alloc::ScratchArena& scratchArena, const CrashRequest& request){
    constexpr AStringView s_CollectionNote = "application_exit_info=not_collected_by_native_layer\n" "detail=Java/Kotlin host should attach ApplicationExitInfo tombstone data on next launch\n";
    const CrashStringT<Alloc::ScratchArena> text(s_CollectionNote, scratchArena);
    return WriteTextFile(RequestPendingDirectory(scratchArena, request) / PackageNames::s_AndroidCollectionFileName, AStringView(text.data(), text.size()));
}

static void CollectAndroidEmergencyRecord(Alloc::ScratchArena& scratchArena, const CrashUploadSnapshot& snapshot){
    const ::Path<Alloc::ScratchArena> recordPath = ::Path<Alloc::ScratchArena>(scratchArena, snapshot.spoolDirectory) / PackageNames::s_AndroidEmergencyRequestFileName;
    CrashBytesT<Alloc::ScratchArena> bytes(scratchArena);
    const auto read = ReadBinaryFile(recordPath, bytes);
    if(!read){
        if(read.error() != ErrorCondition::no_such_file_or_directory)
            CaptureCrashPackageFilesystemFailure("read_android_emergency_record", read.error());
        return;
    }
    if(bytes.size() < sizeof(CrashRequest))
        return;
    CrashRequest request;
    const usize offset = bytes.size() - sizeof(CrashRequest);
    NWB_MEMCPY(&request, sizeof(request), bytes.data() + offset, sizeof(request));
    if(IsCurrentCrashRequest(request)){
        if(!WriteCrashPackage(request))
            return;
        if(!WriteAndroidCollectionNote(scratchArena, request))
            CaptureDiagnosticEvent(DiagnosticEventRecord{
                .event = s_WarningEventName,
                .category = s_PackageErrorCategory,
                .expression = "write_android_collection_note",
                .message = "Android collection note could not be written or closed",
                .file = __FILE__,
                .line = __LINE__,
            }
            );
    }
    CrashBytesT<Alloc::ScratchArena> empty(scratchArena);
    if(!WriteBinaryFile(recordPath, empty))
        CaptureDiagnosticEvent(DiagnosticEventRecord{
            .event = s_WarningEventName,
            .category = s_PackageErrorCategory,
            .expression = "clear_android_emergency_record",
            .message = "Collected Android emergency record could not be cleared or closed",
            .file = __FILE__,
            .line = __LINE__,
        }
        );
}
#else
static void CollectAndroidEmergencyRecord(Alloc::ScratchArena& scratchArena, const CrashUploadSnapshot& snapshot)noexcept{
    static_cast<void>(scratchArena);
    static_cast<void>(snapshot);
}
#endif

template<typename ArenaT>
bool FlushPendingCrashReportsImpl(ArenaT& arena, const CrashUploadSnapshot& snapshot){
    if(snapshot.spoolDirectory[0] == 0)
        return false;
    Alloc::ScratchArena scratchArena(CrashArenaScope::s_UploadScratch);
    CollectAndroidEmergencyRecord(scratchArena, snapshot);

    const ::Path<Alloc::ScratchArena> spoolDirectory(scratchArena, snapshot.spoolDirectory);
    const bool recoveryOk = RecoverUploadingPackageDirectories(scratchArena, spoolDirectory);
    const auto retentionBeforeUpload = ApplyCrashSpoolRetention(scratchArena, spoolDirectory, snapshot.spoolRetention, AStringView(snapshot.protectedPendingPackageName));
    if(!retentionBeforeUpload){
        const DiagnosticEventText diagnostic = MakeDiagnosticEventText("Crash spool retention failed before upload: {} {}", retentionBeforeUpload.error().category().name(), retentionBeforeUpload.error().value());
        CaptureDiagnosticEvent(DiagnosticEventRecord{ .event = s_WarningEventName, .category = s_SpoolRetentionCategory, .message = diagnostic.view(), .file = __FILE__, .line = __LINE__, });
    }
    const bool retentionOk = static_cast<bool>(retentionBeforeUpload);

    const CrashStringT<Alloc::ScratchArena> url = CrashUploadUrl(scratchArena, snapshot.logServerUrl);
    if(url.empty())
        return false;

    if(!EnsureCrashCurlGlobalInit())
        return false;

    bool allUploaded = true;
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

        if(!UploadPackageDirectory(arena, scratchArena, spoolDirectory, entry.path(), url, AStringView(snapshot.crashUploadToken)))
            allUploaded = false;
    }

    const auto retentionAfterUpload = ApplyCrashSpoolRetention(scratchArena, spoolDirectory, snapshot.spoolRetention);
    if(!retentionAfterUpload){
        const DiagnosticEventText diagnostic = MakeDiagnosticEventText("Crash spool retention failed after upload: {} {}", retentionAfterUpload.error().category().name(), retentionAfterUpload.error().value());
        CaptureDiagnosticEvent(DiagnosticEventRecord{ .event = s_WarningEventName, .category = s_SpoolRetentionCategory, .message = diagnostic.view(), .file = __FILE__, .line = __LINE__, });
    }
    return allUploaded && recoveryOk && retentionOk && static_cast<bool>(retentionAfterUpload);
}

bool FlushCrashReportsForRequest(const CrashRequest& request){
    if(!IsCurrentCrashRequest(request))
        return false;

    CrashUploadSnapshot snapshot;
    snapshot.spoolRetention = request.spoolRetention;
    CopyFixedBuffer(snapshot.spoolDirectory, request.spoolDirectory);
    CopyFixedBuffer(snapshot.logServerUrl, request.logServerUrl);
    CopyFixedBuffer(snapshot.crashUploadToken, request.crashUploadToken);
    CopyFixedBuffer(snapshot.protectedPendingPackageName, request.crashId);

    // Archive and upload run in the out-of-process handler, outside signal/exception constraints, so use a growable arena rather than the fixed in-process DumpArena.
    Alloc::GlobalArena uploadArena(CrashArenaScope::s_UploadArena);
    return FlushPendingCrashReportsImpl(uploadArena, snapshot);
}

template Expected<void, ErrorCode> ApplyCrashSpoolRetention(Alloc::ScratchArena& scratchArena, const ::Path<Alloc::GlobalArena>& spoolDirectory, const CrashSpoolRetentionConfig& retention, AStringView protectedPendingPackageName);
template Expected<void, ErrorCode> ApplyCrashSpoolRetention(Alloc::ScratchArena& scratchArena, const ::Path<Alloc::ScratchArena>& spoolDirectory, const CrashSpoolRetentionConfig& retention, AStringView protectedPendingPackageName);
template bool FlushPendingCrashReportsImpl(Alloc::GlobalArena& arena, const CrashUploadSnapshot& snapshot);

template Expected<void, ErrorCode> ApplyCrashSpoolRetention(Alloc::ScratchArena& scratchArena, const ::Path<Alloc::PersistentArena>& spoolDirectory, const CrashSpoolRetentionConfig& retention, AStringView protectedPendingPackageName);
template bool FlushPendingCrashReportsImpl(Alloc::PersistentArena& arena, const CrashUploadSnapshot& snapshot);


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_CRASH_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

