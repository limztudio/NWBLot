// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "package_internal.h"
#include "arena_names.h"

#include <global/filesystem/retention.h>

#include <cstddef>

#include <curl/curl.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_CRASH_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace Detail{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


inline constexpr usize s_AuthorizationBearerPrefixLength = sizeof("Authorization: Bearer ") - 1u;
inline constexpr long s_CurlOptionEnabled = 1L;
inline constexpr long s_CrashUploadConnectTimeoutMilliseconds = 1000L;
inline constexpr long s_CrashUploadTimeoutMilliseconds = 5000L;
inline constexpr long s_HttpSuccessStatusBegin = 200L;
inline constexpr long s_HttpSuccessStatusEnd = 300L;


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


static Futex s_CurlAllocatorMutex;
static Futex s_CurlGlobalInitMutex;
static bool s_CurlGlobalInitialized = false;

static void* CrashCurlAllocate(const usize size)noexcept{
    ScopedLock lock(s_CurlAllocatorMutex);

    return AllocateArenaCMemory(DumpArena(), size);
}

static void CrashCurlDeallocate(void* const ptr)noexcept{
    ScopedLock lock(s_CurlAllocatorMutex);

    DeallocateArenaCMemory(DumpArena(), ptr);
}

static void* CrashCurlMalloc(const size_t size)noexcept{
    return CrashCurlAllocate(static_cast<usize>(size));
}

static void CrashCurlFree(void* const ptr)noexcept{
    CrashCurlDeallocate(ptr);
}

static void* CrashCurlRealloc(void* const ptr, const size_t size)noexcept{
    ScopedLock lock(s_CurlAllocatorMutex);

    return ReallocateArenaCMemory(DumpArena(), ptr, static_cast<usize>(size));
}

static char* CrashCurlStrdup(const char* const text)noexcept{
    ScopedLock lock(s_CurlAllocatorMutex);

    return DuplicateArenaCString(DumpArena(), text);
}

static void* CrashCurlCalloc(const size_t count, const size_t size)noexcept{
    ScopedLock lock(s_CurlAllocatorMutex);

    return ZeroAllocateArenaCMemory(DumpArena(), static_cast<usize>(count), static_cast<usize>(size));
}

[[nodiscard]] static bool EnsureCrashCurlGlobalInit(){
    ScopedLock lock(s_CurlGlobalInitMutex);
    if(s_CurlGlobalInitialized)
        return true;

    [[maybe_unused]] Alloc::PersistentArena& dumpArena = DumpArena();
    const CURLcode result = curl_global_init_mem(
        CURL_GLOBAL_ALL,
        CrashCurlMalloc,
        CrashCurlFree,
        CrashCurlRealloc,
        CrashCurlStrdup,
        CrashCurlCalloc
    );
    if(result != CURLE_OK)
        return false;

    s_CurlGlobalInitialized = true;
    return true;
}

template<typename ArenaT>
static bool IsSafePackageName(ArenaT& arena, const ::Path<ArenaT>& path){
    const CrashStringT<ArenaT> name = PathToString<char>(arena, path.filename());
    if(name.empty() || name == "." || name == "..")
        return false;

    for(const char ch : name){
        const bool ok =
            (ch >= 'a' && ch <= 'z')
            || (ch >= 'A' && ch <= 'Z')
            || (ch >= '0' && ch <= '9')
            || ch == '-'
            || ch == '_'
            || ch == '.'
        ;
        if(!ok)
            return false;
    }
    return true;
}

template<typename ArenaT>
static CrashStringT<ArenaT> CrashUploadUrl(ArenaT& arena, const char* logServerUrl){
    CrashStringT<ArenaT> url{arena};
    if(logServerUrl)
        url += logServerUrl;
    if(url.empty())
        return url;

    constexpr AStringView suffix(PackageNames::s_CrashUploadEndpoint);
    if(url.size() >= suffix.size() && AStringView(url.data() + url.size() - suffix.size(), suffix.size()) == suffix)
        return url;
    if(!url.empty() && url.back() == '/')
        url += PackageNames::s_CrashUploadEndpointName;
    else
        url += PackageNames::s_CrashUploadEndpoint;
    return url;
}

template<typename ArenaT>
static bool UploadPackage(
    ArenaT& arena,
    const CrashStringT<ArenaT>& url,
    const CrashBytesT<ArenaT>& archiveBytes,
    const AStringView crashUploadToken
){
    CURL* curl = curl_easy_init();
    if(!curl)
        return false;

    curl_slist* headers = nullptr;
    CrashStringT<ArenaT> authorizationHeader{arena};
    if(!crashUploadToken.empty()){
        authorizationHeader.reserve(crashUploadToken.size() + s_AuthorizationBearerPrefixLength);
        authorizationHeader += "Authorization: Bearer ";
        authorizationHeader += crashUploadToken;
        headers = curl_slist_append(headers, authorizationHeader.c_str());
        if(!headers){
            curl_easy_cleanup(curl);
            return false;
        }
    }

    bool ok = true;
    ok = ok && curl_easy_setopt(curl, CURLOPT_URL, url.c_str()) == CURLE_OK;
    ok = ok && curl_easy_setopt(curl, CURLOPT_POST, s_CurlOptionEnabled) == CURLE_OK;
    ok = ok && curl_easy_setopt(curl, CURLOPT_NOSIGNAL, s_CurlOptionEnabled) == CURLE_OK;
    ok = ok && curl_easy_setopt(curl, CURLOPT_CONNECTTIMEOUT_MS, s_CrashUploadConnectTimeoutMilliseconds) == CURLE_OK;
    ok = ok && curl_easy_setopt(curl, CURLOPT_TIMEOUT_MS, s_CrashUploadTimeoutMilliseconds) == CURLE_OK;
    ok = ok && curl_easy_setopt(curl, CURLOPT_POSTFIELDS, reinterpret_cast<const char*>(archiveBytes.data())) == CURLE_OK;
    ok = ok && curl_easy_setopt(curl, CURLOPT_POSTFIELDSIZE_LARGE, static_cast<curl_off_t>(archiveBytes.size())) == CURLE_OK;
    if(headers)
        ok = ok && curl_easy_setopt(curl, CURLOPT_HTTPHEADER, headers) == CURLE_OK;

    if(ok)
        ok = curl_easy_perform(curl) == CURLE_OK;

    long responseCode = 0;
    if(ok)
        ok = curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &responseCode) == CURLE_OK
            && responseCode >= s_HttpSuccessStatusBegin
            && responseCode < s_HttpSuccessStatusEnd
        ;

    if(headers)
        curl_slist_free_all(headers);
    curl_easy_cleanup(curl);
    return ok;
}

template<typename ArenaT>
static bool WriteUploadAttemptText(ArenaT& arena, const ::Path<ArenaT>& packageDirectory, const char* state){
    CrashStringT<ArenaT> text{arena};
    text += "state=";
    text += state ? state : PackageNames::s_UploadAttemptUnknownState;
    text += "\n";
    return WriteTextFile(packageDirectory / PackageNames::s_UploadAttemptFileName, AStringView(text.data(), text.size()));
}

template<typename ArenaT>
bool ApplyCrashSpoolRetention(
    ArenaT& arena,
    const ::Path<ArenaT>& spoolDirectory,
    const CrashSpoolRetentionConfig& retention,
    const AStringView protectedPendingPackageName
){
    const auto isSafePackageDirectory = [&](const ::Path<ArenaT>& path){
        ErrorCode entryError;
        return IsDirectory(path, entryError) && !entryError && IsSafePackageName(arena, path);
    };

    const auto shouldRetainPendingPackageDirectory = [&](const ::Path<ArenaT>& path){
        if(!isSafePackageDirectory(path))
            return false;

        if(protectedPendingPackageName.empty())
            return true;

        const CrashStringT<ArenaT> packageName = PathToString<char>(arena, path.filename());
        return AStringView(packageName.data(), packageName.size()) != protectedPendingPackageName;
    };

    bool ok = true;
    ok = ApplyDirectoryRetention(
        arena,
        PendingDirectory(spoolDirectory),
        retention.maxPendingPackages,
        shouldRetainPendingPackageDirectory
    ) && ok;
    ok = ApplyDirectoryRetention(
        arena,
        UploadedDirectory(spoolDirectory),
        retention.maxUploadedPackages,
        isSafePackageDirectory
    ) && ok;
    ok = ApplyDirectoryRetention(
        arena,
        FailedDirectory(spoolDirectory),
        retention.maxFailedPackages,
        isSafePackageDirectory
    ) && ok;
    ok = ApplyDirectoryRetention(
        arena,
        UploadingDirectory(spoolDirectory),
        retention.maxUploadingPackages,
        isSafePackageDirectory
    ) && ok;
    return ok;
}

template<typename ArenaT>
static bool UploadPackageDirectory(
    ArenaT& arena,
    const ::Path<ArenaT>& spoolDirectory,
    const ::Path<ArenaT>& packageDirectory,
    const CrashStringT<ArenaT>& url,
    const AStringView crashUploadToken
){
    if(url.empty())
        return false;

    const ::Path<ArenaT> uploadingPackageDirectory = UploadingDirectory(spoolDirectory) / packageDirectory.filename();
    if(!::MovePathToDirectory(packageDirectory, UploadingDirectory(spoolDirectory)))
        return false;

    [[maybe_unused]] const bool uploadingStateWritten = WriteUploadAttemptText(arena, uploadingPackageDirectory, PackageNames::s_UploadAttemptUploadingState);

    CrashBytesT<ArenaT> archiveBytes{arena};
    if(!BuildPackageArchive(arena, uploadingPackageDirectory, archiveBytes)){
        [[maybe_unused]] const bool movedToFailed = ::MovePathToDirectory(uploadingPackageDirectory, FailedDirectory(spoolDirectory));
        return false;
    }

    if(UploadPackage(arena, url, archiveBytes, crashUploadToken)){
        [[maybe_unused]] const bool uploadedStateWritten = WriteUploadAttemptText(arena, uploadingPackageDirectory, PackageNames::s_UploadAttemptUploadedState);
        return ::MovePathToDirectory(uploadingPackageDirectory, UploadedDirectory(spoolDirectory));
    }

    [[maybe_unused]] const bool retryStateWritten = WriteUploadAttemptText(arena, uploadingPackageDirectory, PackageNames::s_UploadAttemptRetryPendingState);
    [[maybe_unused]] const bool movedToPending = ::MovePathToDirectory(uploadingPackageDirectory, PendingDirectory(spoolDirectory));
    return false;
}

template<typename ArenaT>
static bool RecoverUploadingPackageDirectories(ArenaT& arena, const ::Path<ArenaT>& spoolDirectory){
    const ::Path<ArenaT> uploadingDirectory = UploadingDirectory(spoolDirectory);
    ErrorCode error;
    if(!IsDirectory(uploadingDirectory, error) || error)
        return !error;

    DirectoryIterator directory(uploadingDirectory, error);
    if(error)
        return false;

    bool ok = true;
    for(const auto& entry : directory){
        ErrorCode entryError;
        if(!IsDirectory(entry.path(), entryError) || entryError || !IsSafePackageName(arena, entry.path()))
            continue;

        if(!WriteUploadAttemptText(arena, entry.path(), PackageNames::s_UploadAttemptRetryInterruptedState))
            ok = false;
        if(!::MovePathToDirectory(entry.path(), PendingDirectory(spoolDirectory)))
            ok = false;
    }

    return ok;
}

#if defined(NWB_PLATFORM_ANDROID)
static void WriteAndroidCollectionNote(Alloc::PersistentArena& arena, const CrashRequest& request){
    CrashStringT<Alloc::PersistentArena> text{arena};
    text += "application_exit_info=not_collected_by_native_layer\n";
    text += "detail=Java/Kotlin host should attach ApplicationExitInfo tombstone data on next launch\n";
    [[maybe_unused]] const bool collectionNoteWritten = WriteTextFile(
        RequestPendingDirectory(arena, request) / PackageNames::s_AndroidCollectionFileName,
        AStringView(text.data(), text.size())
    );
}

static void CollectAndroidEmergencyRecord(const CrashUploadSnapshot& snapshot){
    Alloc::PersistentArena& dumpArena = DumpArena();
    const ::Path<Alloc::PersistentArena> recordPath =
        ::Path<Alloc::PersistentArena>(dumpArena, snapshot.spoolDirectory) / PackageNames::s_AndroidEmergencyRequestFileName
    ;

    CrashBytesT<Alloc::PersistentArena> bytes{dumpArena};
    ErrorCode readError;
    if(!ReadBinaryFile(recordPath, bytes, readError) || bytes.size() < sizeof(CrashRequest))
        return;

    CrashRequest request;
    const usize offset = bytes.size() - sizeof(CrashRequest);
    NWB_MEMCPY(&request, sizeof(request), bytes.data() + offset, sizeof(request));
    if(request.magic == s_RequestMagic && request.version == s_RequestVersion){
        if(WriteCrashPackage(request))
            WriteAndroidCollectionNote(dumpArena, request);
    }

    CrashBytesT<Alloc::PersistentArena> empty{dumpArena};
    [[maybe_unused]] const bool emergencyRecordCleared = WriteBinaryFile(recordPath, empty);
}
#else
static void CollectAndroidEmergencyRecord(const CrashUploadSnapshot& snapshot){
    static_cast<void>(snapshot);
}
#endif

template<typename ArenaT>
bool FlushPendingCrashReportsImpl(ArenaT& arena, const CrashUploadSnapshot& snapshot){
    CollectAndroidEmergencyRecord(snapshot);

    if(snapshot.spoolDirectory[0] == 0)
        return false;

    const ::Path<ArenaT> spoolDirectory(arena, snapshot.spoolDirectory);
    const bool recoveryOk = RecoverUploadingPackageDirectories(arena, spoolDirectory);
    bool retentionOk = ApplyCrashSpoolRetention(
        arena,
        spoolDirectory,
        snapshot.spoolRetention,
        AStringView(snapshot.protectedPendingPackageName)
    );

    const CrashStringT<ArenaT> url = CrashUploadUrl(arena, snapshot.logServerUrl);
    if(url.empty())
        return false;

    if(!EnsureCrashCurlGlobalInit())
        return false;

    bool allUploaded = true;
    const ::Path<ArenaT> pendingDirectory = PendingDirectory(spoolDirectory);
    ErrorCode error;
    if(!IsDirectory(pendingDirectory, error) || error)
        return !error && recoveryOk && retentionOk;

    DirectoryIterator directory(pendingDirectory, error);
    if(error)
        return false;

    for(const auto& entry : directory){
        ErrorCode entryError;
        if(!IsDirectory(entry.path(), entryError) || entryError || !IsSafePackageName(arena, entry.path()))
            continue;

        if(!UploadPackageDirectory(arena, spoolDirectory, entry.path(), url, AStringView(snapshot.crashUploadToken)))
            allUploaded = false;
    }

    retentionOk = ApplyCrashSpoolRetention(arena, spoolDirectory, snapshot.spoolRetention) && retentionOk;
    return allUploaded && recoveryOk && retentionOk;
}

bool FlushCrashReportsForRequest(const CrashRequest& request){
    if(request.magic != s_RequestMagic || request.version != s_RequestVersion)
        return false;

    CrashUploadSnapshot snapshot;
    snapshot.spoolRetention = request.spoolRetention;
    CopyFixedBuffer(snapshot.spoolDirectory, request.spoolDirectory);
    CopyFixedBuffer(snapshot.logServerUrl, request.logServerUrl);
    CopyFixedBuffer(snapshot.crashUploadToken, request.crashUploadToken);
    CopyFixedBuffer(snapshot.protectedPendingPackageName, request.crashId);

    // The archive + upload run here in the out-of-process crash handler (a stable, normal process), NOT in the
    // crashing client under signal/exception constraints. So this path uses a GROWABLE arena instead of the
    // fixed in-process DumpArena, letting a package of ANY minidump size be archived and uploaded without
    // overflowing a fixed reservation. (The fixed 512 KiB DumpArena could not hold even a small minidump plus
    // its archive copy, so BuildPackageArchive overflowed and the handler died mid-archive, stranding packages
    // in spool/uploading and keeping crash callstacks from ever reaching the log server.)
    Alloc::GlobalArena uploadArena(CrashArenaScope::s_UploadArena);
    return FlushPendingCrashReportsImpl(uploadArena, snapshot);
}

template bool ApplyCrashSpoolRetention(
    Alloc::GlobalArena& arena,
    const ::Path<Alloc::GlobalArena>& spoolDirectory,
    const CrashSpoolRetentionConfig& retention,
    AStringView protectedPendingPackageName
);
template bool FlushPendingCrashReportsImpl(Alloc::GlobalArena& arena, const CrashUploadSnapshot& snapshot);

template bool ApplyCrashSpoolRetention(
    Alloc::PersistentArena& arena,
    const ::Path<Alloc::PersistentArena>& spoolDirectory,
    const CrashSpoolRetentionConfig& retention,
    AStringView protectedPendingPackageName
);
template bool FlushPendingCrashReportsImpl(Alloc::PersistentArena& arena, const CrashUploadSnapshot& snapshot);


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_CRASH_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

