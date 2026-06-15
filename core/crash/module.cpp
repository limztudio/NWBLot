// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "internal.h"

#include <global/diagnostics.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_CRASH_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace __hidden_crash_module{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


struct BreadcrumbSnapshot{
    char spoolDirectoryText[Detail::s_MaxPathText] = {};
    Detail::FixedBreadcrumb breadcrumbs[Detail::s_MaxBreadcrumbs] = {};
    usize breadcrumbCount = 0u;
};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


template<typename ArenaT>
[[nodiscard]] static ::Path<ArenaT> __hidden_default_crash_root_directory(ArenaT& arena){
    ::Path<ArenaT> executableDirectory(arena);
    if(GetExecutableDirectory(executableDirectory))
        return executableDirectory / PackageNames::s_DefaultRootDirectoryName;

    ErrorCode error;
    ::Path<ArenaT> currentDirectory(arena);
    if(GetCurrentPath(currentDirectory, error) && !currentDirectory.empty())
        return currentDirectory / PackageNames::s_DefaultRootDirectoryName;

    return ::Path<ArenaT>(arena, PackageNames::s_DefaultRootDirectoryName);
}

template<typename ArenaT>
static void __hidden_store_current_breadcrumbs(ArenaT& arena, const BreadcrumbSnapshot& snapshot){
    if(snapshot.spoolDirectoryText[0] == 0 || snapshot.breadcrumbCount == 0u)
        return;

    const ::Path<ArenaT> breadcrumbPath = ::Path<ArenaT>(arena, snapshot.spoolDirectoryText) / PackageNames::s_CurrentBreadcrumbsFileName;
    OutputFileStream stream(breadcrumbPath.c_str(), s_FileOpenBinary | s_FileOpenTruncate);
    if(!stream.is_open())
        return;

    for(usize i = 0u; i < snapshot.breadcrumbCount; ++i){
        const Detail::FixedBreadcrumb& breadcrumb = snapshot.breadcrumbs[i];
        stream
            << breadcrumb.order
            << " ["
            << breadcrumb.category
            << "] "
            << breadcrumb.message
            << '\n'
        ;
    }
}

static void __hidden_snapshot_current_breadcrumbs(BreadcrumbSnapshot& outSnapshot){
    outSnapshot = BreadcrumbSnapshot{};
    CopyFixedBuffer(outSnapshot.spoolDirectoryText, Detail::g_State.spoolDirectoryText);
    if(outSnapshot.spoolDirectoryText[0] == 0)
        return;

    const usize begin = Detail::g_State.nextBreadcrumb >= Detail::s_MaxBreadcrumbs
        ? Detail::g_State.nextBreadcrumb - Detail::s_MaxBreadcrumbs
        : 0u
    ;
    const usize end = Detail::g_State.nextBreadcrumb;
    for(usize i = begin; i < end; ++i){
        const Detail::FixedBreadcrumb& breadcrumb = Detail::g_State.breadcrumbs[i % Detail::s_MaxBreadcrumbs];
        if(!breadcrumb.used)
            continue;

        if(outSnapshot.breadcrumbCount >= Detail::s_MaxBreadcrumbs)
            break;

        outSnapshot.breadcrumbs[outSnapshot.breadcrumbCount++] = breadcrumb;
    }
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
#if defined(NWB_PLATFORM_ANDROID)
    options.writePackageInProcess = true;
#endif

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
        static_cast<void>(__hidden_capture_crash_dump(options.triggerCategory, options.triggerMessage, options));
    }
    catch(...){
    }
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


template<typename ArenaT>
::Path<ArenaT> DefaultCrashSpoolDirectory(ArenaT& arena){
    return __hidden_crash_module::__hidden_default_crash_root_directory(arena);
}

template<typename ArenaT>
::Path<ArenaT> DefaultCrashHandlerExecutablePath(ArenaT& arena){
    ::Path<ArenaT> executableDirectory(arena);
    if(!GetExecutableDirectory(executableDirectory))
        return ::Path<ArenaT>(arena);

    return executableDirectory / Detail::s_HandlerExecutableFileName;
}

template<typename ArenaT>
bool InstallCrashHandler(ArenaT& arena, const CrashConfigT<ArenaT>& config){
    ScopedLock lock(Detail::g_State.mutex);
    if(Detail::g_State.installed)
        return true;

    CopyFixedBuffer(Detail::g_State.applicationName, config.applicationName);
    CopyFixedBuffer(Detail::g_State.versionText, config.version);
    CopyFixedBuffer(Detail::g_State.buildId, config.buildId);
    CopyFixedBuffer(Detail::g_State.logServerUrl, config.logServerUrl);
    CopyFixedBuffer(Detail::g_State.crashUploadToken, config.crashUploadToken);

    const ::Path<ArenaT> spoolDirectory = config.spoolDirectory.empty()
        ? DefaultCrashSpoolDirectory(arena)
        : config.spoolDirectory
    ;
    const ::Path<ArenaT> handlerExecutablePath = config.handlerExecutablePath.empty()
        ? DefaultCrashHandlerExecutablePath(arena)
        : ::Path<ArenaT>(arena, config.handlerExecutablePath)
    ;
    Detail::g_State.dumpDetailMode = config.dumpDetailMode;
    Detail::g_State.enableGpuDumps = config.enableGpuDumps;
    Detail::g_State.capturePolicy = config.capturePolicy;
    Detail::g_State.spoolRetention = config.spoolRetention;
    Detail::g_State.diagnosticCaptureCount = 0u;
    for(Detail::FixedDiagnosticSite& site : Detail::g_State.diagnosticSites)
        site = Detail::FixedDiagnosticSite{};
    static_cast<void>(Detail::DumpArena());
    const AString<ArenaT> spoolDirectoryText = PathToString<char>(arena, spoolDirectory);
    CopyFixedBuffer(Detail::g_State.spoolDirectoryText, AStringView(spoolDirectoryText.data(), spoolDirectoryText.size()));
    const AString<ArenaT> handlerExecutablePathText = PathToString<char>(arena, handlerExecutablePath);
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

template<typename ArenaT>
bool AddCrashBreadcrumb(ArenaT& arena, const AStringView category, const AStringView message){
    __hidden_crash_module::BreadcrumbSnapshot snapshot;

    {
        ScopedLock lock(Detail::g_State.mutex);

        __hidden_crash_module::__hidden_store_breadcrumb(category, message);
        __hidden_crash_module::__hidden_snapshot_current_breadcrumbs(snapshot);
    }

    __hidden_crash_module::__hidden_store_current_breadcrumbs(arena, snapshot);

    return true;
}

template ::Path<Alloc::PersistentArena> DefaultCrashSpoolDirectory(Alloc::PersistentArena& arena);
template ::Path<Alloc::PersistentArena> DefaultCrashHandlerExecutablePath(Alloc::PersistentArena& arena);
template bool InstallCrashHandler(Alloc::PersistentArena& arena, const CrashConfigT<Alloc::PersistentArena>& config);
template bool AddCrashBreadcrumb(Alloc::PersistentArena& arena, AStringView category, AStringView message);

CrashDumpResult CaptureCrashDump(const AStringView category, const AStringView message){
    Detail::CrashDumpRequestOptions options;
    return __hidden_crash_module::__hidden_capture_crash_dump(category, message, options);
}

bool FlushPendingCrashReports(Alloc::GlobalArena& arena){
    Detail::CrashUploadSnapshot snapshot;
    {
        ScopedLock lock(Detail::g_State.mutex);
        snapshot.spoolRetention = Detail::g_State.spoolRetention;
        CopyFixedBuffer(snapshot.spoolDirectory, Detail::g_State.spoolDirectoryText);
        CopyFixedBuffer(snapshot.logServerUrl, Detail::g_State.logServerUrl);
        CopyFixedBuffer(snapshot.crashUploadToken, Detail::g_State.crashUploadToken);
    }

    return Detail::FlushPendingCrashReportsImpl(arena, snapshot);
}

bool RegisterGpuCrashProvider(const GpuCrashProvider& provider){
    if(!provider.writeAttachment)
        return false;

    ScopedLock lock(Detail::g_State.mutex);
    if(Detail::g_State.gpuProviderCount >= Detail::s_MaxGpuCrashProviders)
        return false;

    Detail::g_State.gpuProviders[Detail::g_State.gpuProviderCount++] = provider;
    return true;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_CRASH_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

