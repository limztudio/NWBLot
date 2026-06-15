// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "internal.h"

#include <global/diagnostics.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_CRASH_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace __hidden_crash_module{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


[[nodiscard]] static Detail::CrashPath __hidden_default_crash_root_directory(Detail::CrashArena& arena){
    Detail::CrashPath executableDirectory(arena);
    if(GetExecutableDirectory(executableDirectory))
        return executableDirectory / PackageNames::s_DefaultRootDirectoryName;

    ErrorCode error;
    Detail::CrashPath currentDirectory(arena);
    if(GetCurrentPath(currentDirectory, error) && !currentDirectory.empty())
        return currentDirectory / PackageNames::s_DefaultRootDirectoryName;

    return Detail::CrashPath(arena, PackageNames::s_DefaultRootDirectoryName);
}

static void __hidden_store_breadcrumb(const AStringView category, const AStringView message){
    Detail::FixedBreadcrumb& breadcrumb = Detail::g_State.breadcrumbs[Detail::g_State.nextBreadcrumb % Detail::s_MaxBreadcrumbs];
    breadcrumb.used = 1u;
    breadcrumb.order = Detail::g_State.breadcrumbOrder.fetch_add(1u, MemoryOrder::relaxed);
    CopyFixedBuffer(breadcrumb.category, category.empty() ? AStringView(Detail::s_DefaultBreadcrumbCategory) : category);
    CopyFixedBuffer(breadcrumb.message, message);
    ++Detail::g_State.nextBreadcrumb;
}

static bool __hidden_capture_policy_allows(const CrashCapturePolicy& policy, const AStringView event, const AStringView category){
    if(category == DiagnosticEventCategory::s_Assert)
        return policy.captureAssertions;
    if(category == DiagnosticEventCategory::s_FatalAssert)
        return policy.captureFatalAssertions;
    if(event == DiagnosticEventName::s_Error)
        return policy.captureLoggerErrors;
    if(event == DiagnosticEventName::s_FatalError)
        return policy.captureLoggerFatals;

    return true;
}

static u64 __hidden_diagnostic_site_hash(const DiagnosticEventRecord& record)noexcept{
    u64 hash = FNV64_OFFSET_BASIS;
    if(record.event)
        hash = UpdateFnv64TextExact(hash, AStringView(record.event));
    if(record.category)
        hash = UpdateFnv64TextExact(hash, AStringView(record.category));
    if(record.expression)
        hash = UpdateFnv64TextExact(hash, AStringView(record.expression));
    if(record.message)
        hash = UpdateFnv64TextExact(hash, AStringView(record.message));
    if(record.file)
        hash = UpdateFnv64TextExact(hash, AStringView(record.file));
    hash = UpdateFnv64(hash, reinterpret_cast<const u8*>(&record.line), sizeof(record.line));
    return hash;
}

static bool __hidden_diagnostic_result_can_suppress_duplicate_platform_crash(const CrashDumpResult& result)noexcept{
    return result.status != CrashDumpStatus::NotInstalled
        && result.status != CrashDumpStatus::RequestFailed
        && result.status != CrashDumpStatus::PackageWriteFailed
    ;
}

static bool __hidden_reserve_diagnostic_capture(const DiagnosticEventRecord& record, const AStringView event, const AStringView category){
    ScopedLock lock(Detail::g_State.mutex);
    if(!Detail::g_State.installed)
        return false;
    if(!__hidden_capture_policy_allows(Detail::g_State.capturePolicy, event, category))
        return false;

    const CrashCapturePolicy& policy = Detail::g_State.capturePolicy;
    if(policy.maxDiagnosticDumpsPerProcess != 0u && Detail::g_State.diagnosticCaptureCount >= policy.maxDiagnosticDumpsPerProcess)
        return false;

    Detail::FixedDiagnosticSite* freeSite = nullptr;
    Detail::FixedDiagnosticSite* matchingSite = nullptr;
    const u64 siteHash = __hidden_diagnostic_site_hash(record);
    for(usize i = 0u; i < Detail::s_MaxDiagnosticSites; ++i){
        Detail::FixedDiagnosticSite& site = Detail::g_State.diagnosticSites[i];
        if(site.used && site.hash == siteHash){
            matchingSite = &site;
            break;
        }

        if(!site.used && !freeSite)
            freeSite = &site;
    }

    if(policy.maxDiagnosticDumpsPerSite != 0u){
        if(matchingSite && matchingSite->captureCount >= policy.maxDiagnosticDumpsPerSite)
            return false;
        if(!matchingSite && !freeSite)
            return false;
    }

    if(!matchingSite && freeSite){
        freeSite->used = 1u;
        freeSite->hash = siteHash;
        matchingSite = freeSite;
    }

    ++Detail::g_State.diagnosticCaptureCount;
    if(matchingSite)
        ++matchingSite->captureCount;
    return true;
}

NWB_NOINLINE static CrashDumpResult __hidden_capture_crash_dump(const AStringView category, const AStringView message, Detail::CrashDumpRequestOptions& options){
    const AStringView breadcrumbCategory = category.empty() ? AStringView(Detail::s_ManualDumpCategory) : category;

    {
        ScopedLock lock(Detail::g_State.mutex);
        if(!Detail::g_State.installed)
            return CrashDumpResult{ CrashDumpStatus::NotInstalled };
        if(!category.empty() || !message.empty())
            __hidden_store_breadcrumb(breadcrumbCategory, message);
    }

    if(options.triggerCategory.empty())
        options.triggerCategory = breadcrumbCategory;
    if(options.triggerMessage.empty())
        options.triggerMessage = message;

    options.waitMilliseconds = Detail::s_ManualCrashDumpWaitMilliseconds;
    if(options.callstackFramesToSkip == 0u)
        options.callstackFramesToSkip = 1u;
    Detail::ManualDumpContextStorage contextStorage;
    Detail::CaptureManualDumpContext(options, contextStorage);

    return Detail::RequestCrashDump(Detail::CrashReasonKind::ManualDump, 0u, options);
}

NWB_NOINLINE static void __hidden_capture_diagnostic_crash(const DiagnosticEventRecord& record)noexcept{
    try{
        Detail::CrashDumpRequestOptions options;
        const char* const diagnosticEventName = DiagnosticEventNameFromRecord(record);
        options.event = diagnosticEventName ? AStringView(diagnosticEventName) : AStringView();
        options.triggerCategory = record.category ? AStringView(record.category) : AStringView();
        options.triggerExpression = record.expression ? AStringView(record.expression) : AStringView();
        options.triggerMessage = record.message ? AStringView(record.message) : AStringView();
        options.triggerFile = record.file ? AStringView(record.file) : AStringView();
        options.triggerInstructionPointer = record.instructionPointer;
        options.triggerLine = record.line;
        options.callstackFramesToSkip = 5u;
        if(!__hidden_reserve_diagnostic_capture(record, options.event, options.triggerCategory))
            return;
        const CrashDumpResult result = __hidden_capture_crash_dump(options.triggerCategory, options.triggerMessage, options);
        if(record.terminatesProcess && __hidden_diagnostic_result_can_suppress_duplicate_platform_crash(result))
            Detail::SuppressNextPlatformCrashCapture();
    }
    catch(...){
    }
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


::Path<Alloc::PersistentArena> DefaultCrashSpoolDirectory(Alloc::PersistentArena& arena){
    return __hidden_crash_module::__hidden_default_crash_root_directory(arena);
}

::Path<Alloc::PersistentArena> DefaultCrashHandlerExecutablePath(Alloc::PersistentArena& arena){
    ::Path<Alloc::PersistentArena> executableDirectory(arena);
    if(!GetExecutableDirectory(executableDirectory))
        return ::Path<Alloc::PersistentArena>(arena);

    return executableDirectory / Detail::s_HandlerExecutableFileName;
}

bool InstallCrashHandler(Alloc::PersistentArena& arena, const CrashConfig& config){
    ScopedLock lock(Detail::g_State.mutex);
    if(Detail::g_State.installed)
        return true;

    CopyFixedBuffer(Detail::g_State.applicationName, config.applicationName);
    CopyFixedBuffer(Detail::g_State.versionText, config.version);
    CopyFixedBuffer(Detail::g_State.buildId, config.buildId);
    CopyFixedBuffer(Detail::g_State.logServerUrl, config.logServerUrl);
    CopyFixedBuffer(Detail::g_State.crashUploadToken, config.crashUploadToken);

    const ::Path<Alloc::PersistentArena> spoolDirectory = config.spoolDirectory.empty()
        ? DefaultCrashSpoolDirectory(arena)
        : config.spoolDirectory
    ;
    const ::Path<Alloc::PersistentArena> handlerExecutablePath = config.handlerExecutablePath.empty()
        ? DefaultCrashHandlerExecutablePath(arena)
        : ::Path<Alloc::PersistentArena>(arena, config.handlerExecutablePath)
    ;
    Detail::g_State.dumpDetailMode = config.dumpDetailMode;
    Detail::g_State.capturePolicy = config.capturePolicy;
    Detail::g_State.spoolRetention = config.spoolRetention;
    Detail::g_State.diagnosticCaptureCount = 0u;
    for(Detail::FixedDiagnosticSite& site : Detail::g_State.diagnosticSites)
        site = Detail::FixedDiagnosticSite{};
    static_cast<void>(Detail::DumpArena());
    const AString<Alloc::PersistentArena> spoolDirectoryText = PathToString<char>(arena, spoolDirectory);
    CopyFixedBuffer(Detail::g_State.spoolDirectoryText, AStringView(spoolDirectoryText.data(), spoolDirectoryText.size()));
    const AString<Alloc::PersistentArena> handlerExecutablePathText = PathToString<char>(arena, handlerExecutablePath);
    CopyFixedBuffer(Detail::g_State.handlerExecutablePathText, AStringView(handlerExecutablePathText.data(), handlerExecutablePathText.size()));

    if(!Detail::EnsureCrashSpoolDirectories(spoolDirectory))
        return false;

#if !defined(NWB_PLATFORM_ANDROID)
    if(!Detail::StartDesktopHandler(handlerExecutablePath))
        return false;
#endif

    Detail::InstallPlatformHandlers();
    Detail::g_State.installed = true;
    SetDiagnosticEventCallback(__hidden_crash_module::__hidden_capture_diagnostic_crash);
    return true;
}

void UninstallCrashHandler(){
    ScopedLock lock(Detail::g_State.mutex);
    if(!Detail::g_State.installed)
        return;

    ClearDiagnosticEventCallback(__hidden_crash_module::__hidden_capture_diagnostic_crash);
    Detail::UninstallPlatformResources();
    Detail::g_State.installed = false;
    Detail::g_State.handlerStarted = false;
}

bool SetCrashMetadata(const AStringView key, const AStringView value){
    if(key.empty())
        return false;

    ScopedLock lock(Detail::g_State.mutex);

    Detail::FixedMetadata* freeSlot = nullptr;
    for(usize i = 0u; i < Detail::s_MaxMetadata; ++i){
        Detail::FixedMetadata& metadata = Detail::g_State.metadata[i];
        if(metadata.used && AStringView(metadata.key) == key){
            CopyFixedBuffer(metadata.value, value);
            return true;
        }

        if(!metadata.used && !freeSlot)
            freeSlot = &metadata;
    }

    if(!freeSlot)
        return false;

    CopyFixedBuffer(freeSlot->key, key);
    CopyFixedBuffer(freeSlot->value, value);
    freeSlot->used = 1u;
    return true;
}

bool AddCrashBreadcrumb(const AStringView category, const AStringView message){
    ScopedLock lock(Detail::g_State.mutex);
    __hidden_crash_module::__hidden_store_breadcrumb(category, message);
    return true;
}

CrashDumpResult CaptureCrashDump(const AStringView category, const AStringView message){
    Detail::CrashDumpRequestOptions options;
    return __hidden_crash_module::__hidden_capture_crash_dump(category, message, options);
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_CRASH_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

