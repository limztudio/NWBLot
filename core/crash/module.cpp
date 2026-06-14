// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "internal.h"

#include <global/diagnostics.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_CRASH_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace __hidden_crash_module{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


template<typename ArenaT>
[[nodiscard]] static ::Path<ArenaT> __hidden_default_crash_root_directory(ArenaT& arena){
    ::Path<ArenaT> executableDirectory(arena);
    if(GetExecutableDirectory(executableDirectory))
        return executableDirectory / "crashes";

    ErrorCode error;
    ::Path<ArenaT> currentDirectory(arena);
    if(GetCurrentPath(currentDirectory, error) && !currentDirectory.empty())
        return currentDirectory / "crashes";

    return ::Path<ArenaT>(arena, "crashes");
}

template<typename ArenaT>
static void __hidden_store_current_breadcrumbs(ArenaT& arena){
    const ::Path<ArenaT> breadcrumbPath = ::Path<ArenaT>(arena, Detail::g_State.spoolDirectoryText) / "breadcrumbs_current.txt";
    OutputFileStream stream(breadcrumbPath.c_str(), s_FileOpenBinary | s_FileOpenTruncate);
    if(!stream.is_open())
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

static void __hidden_store_breadcrumb(const AStringView category, const AStringView message){
    Detail::FixedBreadcrumb& breadcrumb = Detail::g_State.breadcrumbs[Detail::g_State.nextBreadcrumb % Detail::s_MaxBreadcrumbs];
    breadcrumb.used = 1u;
    breadcrumb.order = Detail::g_State.breadcrumbOrder.fetch_add(1u, MemoryOrder::relaxed);
    CopyFixedBuffer(breadcrumb.category, category.empty() ? AStringView("general") : category);
    CopyFixedBuffer(breadcrumb.message, message);
    ++Detail::g_State.nextBreadcrumb;
}

static CrashDumpResult __hidden_capture_crash_dump(const AStringView category, const AStringView message, Detail::CrashDumpRequestOptions& options){
    const AStringView breadcrumbCategory = category.empty() ? AStringView("manual_dump") : category;

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

    options.waitMilliseconds = 10000u;
    Detail::ManualDumpContextStorage contextStorage;
    Detail::CaptureManualDumpContext(options, contextStorage);
#if defined(NWB_PLATFORM_ANDROID)
    options.writePackageInProcess = true;
#endif

    return Detail::RequestCrashDump(Detail::CrashReasonKind::ManualDump, 0u, options);
}

static void __hidden_capture_diagnostic_crash(const DiagnosticCrashRecord& record)noexcept{
    try{
        Detail::CrashDumpRequestOptions options;
        options.triggerCategory = record.category ? AStringView(record.category) : AStringView();
        options.triggerMessage = record.message ? AStringView(record.message) : AStringView();
        options.triggerFile = record.file ? AStringView(record.file) : AStringView();
        options.triggerLine = record.line;
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

#if defined(NWB_PLATFORM_WINDOWS)
    return executableDirectory / "crash_handler.exe";
#else
    return executableDirectory / "crash_handler";
#endif
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
    const AString<ArenaT> spoolDirectoryText = PathToString<char>(arena, spoolDirectory);
    CopyFixedBuffer(Detail::g_State.spoolDirectoryText, AStringView(spoolDirectoryText.data(), spoolDirectoryText.size()));
    const AString<ArenaT> handlerExecutablePathText = PathToString<char>(arena, handlerExecutablePath);
    CopyFixedBuffer(Detail::g_State.handlerExecutablePathText, AStringView(handlerExecutablePathText.data(), handlerExecutablePathText.size()));

    if(!Detail::EnsureCrashSpoolDirectories(spoolDirectory))
        return false;

#if !defined(NWB_PLATFORM_ANDROID)
    static_cast<void>(Detail::StartDesktopHandler(handlerExecutablePath));
#endif

    Detail::InstallPlatformHandlers();
    Detail::g_State.installed = true;
    SetDiagnosticCrashCaptureCallback(__hidden_crash_module::__hidden_capture_diagnostic_crash);
    return true;
}

void UninstallCrashHandler(){
    ScopedLock lock(Detail::g_State.mutex);
    if(!Detail::g_State.installed)
        return;

    ClearDiagnosticCrashCaptureCallback(__hidden_crash_module::__hidden_capture_diagnostic_crash);
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
    ScopedLock lock(Detail::g_State.mutex);

    __hidden_crash_module::__hidden_store_breadcrumb(category, message);

    if(Detail::g_State.spoolDirectoryText[0] != 0)
        __hidden_crash_module::__hidden_store_current_breadcrumbs(arena);

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
    ScopedLock lock(Detail::g_State.mutex);
    return Detail::FlushPendingCrashReportsImpl(arena);
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

