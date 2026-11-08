// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "package_upload_transport.h"

#include <core/crash/arena_names.adhd.h>
#include <core/crash/package_values.adhd.h>

#include <global/diagnostics.h>
#include <global/optional.h>
#include <global/scope_exit.h>

#include <curl/curl.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_CRASH_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace CrashDetail{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


inline constexpr StringView s_AuthorizationBearerPrefix = ::NWB::Core::Crash::PackageValues::s_UploadAuthorizationBearerPrefix;
inline constexpr usize s_AuthorizationBearerPrefixLength = s_AuthorizationBearerPrefix.size();
inline constexpr long s_CurlOptionEnabled = ::NWB::Core::Crash::PackageValues::s_UploadCurlOptionEnabled;
inline constexpr long s_CrashUploadConnectTimeoutMilliseconds = ::NWB::Core::Crash::PackageValues::s_UploadCrashUploadConnectTimeoutMilliseconds;
inline constexpr long s_CrashUploadTimeoutMilliseconds = ::NWB::Core::Crash::PackageValues::s_UploadCrashUploadTimeoutMilliseconds;
inline constexpr long s_HttpSuccessStatusBegin = ::NWB::Core::Crash::PackageValues::s_UploadHttpSuccessStatusBegin;
inline constexpr long s_HttpSuccessStatusEnd = ::NWB::Core::Crash::PackageValues::s_UploadHttpSuccessStatusEnd;
inline constexpr StringView s_UploadCategory = ::NWB::Core::Crash::PackageValues::s_UploadUploadCategory;



////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


static Futex s_CurlAllocatorMutex;
static Futex s_CurlGlobalInitMutex;
static b8 s_CurlGlobalInitialized = false;


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


// PersistentArena rounds C allocations to MaxAlign; reject unsupported round-up before its throwing size arithmetic.
[[nodiscard]] static constexpr b8 CanAllocateCrashCurlBytes(const usize size)noexcept{
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

static void* CrashCurlRealloc(void* const ptr, const usize size)noexcept{
    if(!CanAllocateCrashCurlBytes(size))
        return nullptr;
    NothrowScopedLock lock(s_CurlAllocatorMutex);

    return ReallocateArenaCMemory(DumpArena(), ptr, size);
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

static void* CrashCurlCalloc(const usize count, const usize size)noexcept{
    if(count != 0u && size > Limit<usize>::s_Max / count)
        return nullptr;
    if(!CanAllocateCrashCurlBytes(count * size))
        return nullptr;
    NothrowScopedLock lock(s_CurlAllocatorMutex);

    return ZeroAllocateArenaCMemory(DumpArena(), count, size);
}

b8 CrashUploadTransport::Initialize(){
    CURLcode result = CURLE_OK;
    {
        NothrowScopedLock lock(s_CurlGlobalInitMutex);
        if(s_CurlGlobalInitialized)
            return true;
        InitializeDumpArena();
        result = curl_global_init_mem(CURL_GLOBAL_ALL, &CrashCurlAllocate, &CrashCurlDeallocate, &CrashCurlRealloc, &CrashCurlStrdup, &CrashCurlCalloc);
        if(result == CURLE_OK){
            s_CurlGlobalInitialized = true;
            return true;
        }
    }
    const DiagnosticEventText diagnostic = MakeDiagnosticEventText("Crash upload initialization failed: curl code={} {}", static_cast<i64>(result), curl_easy_strerror(result));
    CaptureDiagnosticEvent(DiagnosticEventRecord{ .event = DiagnosticEventName::s_Warning, .category = s_UploadCategory, .expression = "initialize_upload_runtime", .message = diagnostic.view(), .file = __FILE__, .line = __LINE__, });
    return false;
}

struct UploadFailure{
    AStringView operation{};
    Optional<CURLcode> curlCode{};
    Optional<long> responseCode{};
};

static void CaptureUploadFailure(const UploadFailure& failure)noexcept{
    if(failure.curlCode){
        const DiagnosticEventText diagnostic = MakeDiagnosticEventText("Crash upload failed: curl code={} {}", static_cast<i64>(*failure.curlCode), curl_easy_strerror(*failure.curlCode));
        CaptureDiagnosticEvent(DiagnosticEventRecord{ .event = DiagnosticEventName::s_Warning, .category = s_UploadCategory, .expression = failure.operation, .message = diagnostic.view(), .file = __FILE__, .line = __LINE__, });
        return;
    }
    if(failure.responseCode){
        const DiagnosticEventText diagnostic = MakeDiagnosticEventText("Crash upload failed: HTTP status={}", static_cast<i64>(*failure.responseCode));
        CaptureDiagnosticEvent(DiagnosticEventRecord{ .event = DiagnosticEventName::s_Warning, .category = s_UploadCategory, .expression = failure.operation, .message = diagnostic.view(), .file = __FILE__, .line = __LINE__, });
        return;
    }
    CaptureDiagnosticEvent(DiagnosticEventRecord{ .event = DiagnosticEventName::s_Warning, .category = s_UploadCategory, .expression = failure.operation, .message = "Crash upload SDK resource admission failed", .file = __FILE__, .line = __LINE__, });
}

static Expected<void, UploadFailure> UploadPackage(Alloc::ScratchArena& scratchArena, const CrashStringT<Alloc::ScratchArena>& url, const Span<const u8> archiveBytes, const AStringView crashUploadToken){
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
    });
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

b8 CrashUploadTransport::Upload(Alloc::ScratchArena& scratchArena, const CrashStringT<Alloc::ScratchArena>& url, const Span<const u8> archiveBytes, const AStringView crashUploadToken){
    const auto result = UploadPackage(scratchArena, url, archiveBytes, crashUploadToken);
    if(result)
        return true;
    CaptureUploadFailure(result.error());
    return false;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_CRASH_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

