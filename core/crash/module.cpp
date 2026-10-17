// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "module.h"
#include "internal.h"
#include "arena_names.h"

#include <global/diagnostics.h>
#include <global/scope_exit.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_CRASH_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace __hidden_crash_module{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


inline constexpr u32 s_DiagnosticCaptureCallstackFramesToSkip = 5u;


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


[[nodiscard]] static AStringView PlatformOperationText(const Detail::PlatformOperation::Enum operation)noexcept{
    static constexpr AStringView s_Names[] = {
        "none", "prepare_spool", "prepare_command", "prepare_attributes", "create_request_channel", "create_ack_channel", "create_startup_channel",
        "set_inheritance", "create_ack_event", "create_process", "close_handle", "wait_process", "terminate_process",
        "close_descriptor", "set_descriptor_flags", "prepare_child", "exec_handler", "wait_child", "kill_child",
        "install_signal_stack", "restore_signal_stack", "install_signal_action", "restore_signal_action", "open_emergency_file", "restore_exception_filter",
    };
    const usize index = static_cast<usize>(operation);
    return index < sizeof(s_Names) / sizeof(s_Names[0u]) ? s_Names[index] : AStringView("unknown_operation");
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


template<typename ArenaT>
[[nodiscard]] static ::Path<ArenaT> DefaultCrashRootDirectory(ArenaT& arena){
    const auto executableDirectory = GetExecutableDirectory(arena);
    if(executableDirectory)
        return *executableDirectory / PackageNames::s_DefaultRootDirectoryName;

    const auto currentDirectory = GetCurrentPath(arena);
    if(currentDirectory && !currentDirectory->empty())
        return *currentDirectory / PackageNames::s_DefaultRootDirectoryName;

    return ::Path<ArenaT>(arena, PackageNames::s_DefaultRootDirectoryName);
}

static void StoreBreadcrumb(const AStringView category, const AStringView message)noexcept{
    Detail::FixedBreadcrumb& breadcrumb = Detail::g_State.breadcrumbs[Detail::g_State.nextBreadcrumb % Detail::s_MaxBreadcrumbs];
    breadcrumb.used = 1u;
    breadcrumb.order = Detail::g_State.breadcrumbOrder.fetch_add(1u, MemoryOrder::relaxed);
    CopyFixedBuffer(breadcrumb.category, category.empty() ? AStringView(Detail::s_DefaultBreadcrumbCategory) : category);
    CopyFixedBuffer(breadcrumb.message, message);
    ++Detail::g_State.nextBreadcrumb;
    Detail::PublishCrashStateSnapshot();
}

static bool CapturePolicyAllows(const CrashCapturePolicy& policy, const AStringView event, const AStringView category)noexcept{
    if(category == DiagnosticEventCategory::s_Assert)
        return policy.captureAssertions;
    if(category == DiagnosticEventCategory::s_FatalAssert)
        return policy.captureFatalAssertions;
    if(event == DiagnosticEventName::s_Error)
        return policy.captureLoggerErrors;
    if(event == DiagnosticEventName::s_Fatal)
        return policy.captureLoggerFatals;

    return true;
}

static u64 DiagnosticSiteHash(const DiagnosticEventRecord& record)noexcept{
    u64 hash = s_Fnv64OffsetBasis;
    if(!record.event.empty())
        hash = UpdateFnv64TextExact(hash, record.event);
    if(!record.category.empty())
        hash = UpdateFnv64TextExact(hash, record.category);
    if(!record.expression.empty())
        hash = UpdateFnv64TextExact(hash, record.expression);
    if(!record.message.empty())
        hash = UpdateFnv64TextExact(hash, record.message);
    if(!record.file.empty())
        hash = UpdateFnv64TextExact(hash, record.file);
    hash = UpdateFnv64(hash, reinterpret_cast<const u8*>(&record.line), sizeof(record.line));
    return hash;
}

static bool DiagnosticResultCanSuppressDuplicatePlatformCrash(const CrashDumpResult& result)noexcept{
    return result.status != CrashDumpStatus::NotInstalled && result.status != CrashDumpStatus::RequestFailed && result.status != CrashDumpStatus::PackageWriteFailed;
}

static bool ReserveDiagnosticCapture(const DiagnosticEventRecord& record, const AStringView event, const AStringView category)noexcept{
    NothrowScopedLock lock(Detail::g_State.mutex);
    if(!Detail::g_State.installed)
        return false;
    if(!CapturePolicyAllows(Detail::g_State.capturePolicy, event, category))
        return false;

    const CrashCapturePolicy& policy = Detail::g_State.capturePolicy;
    if(policy.maxDiagnosticDumpsPerProcess != 0u && Detail::g_State.diagnosticCaptureCount >= policy.maxDiagnosticDumpsPerProcess)
        return false;

    Detail::FixedDiagnosticSite* freeSite = nullptr;
    Detail::FixedDiagnosticSite* matchingSite = nullptr;
    const u64 siteHash = DiagnosticSiteHash(record);
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

NWB_NOINLINE static CrashDumpResult CaptureCrashDumpForReason(const Detail::CrashReasonKind::Enum reasonKind, const AStringView category, const AStringView message, Detail::CrashDumpRequestOptions& options){
    const AStringView breadcrumbCategory = category.empty() ? AStringView(Detail::s_ManualDumpCategory) : category;

    {
        ScopedLock lock(Detail::g_State.mutex);
        if(!Detail::g_State.installed)
            return CrashDumpResult{ CrashDumpStatus::NotInstalled };
        if(!category.empty() || !message.empty())
            StoreBreadcrumb(breadcrumbCategory, message);
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

    return Detail::RequestCrashDump(reasonKind, 0u, options);
}

NWB_NOINLINE static void CaptureDiagnosticCrash(const DiagnosticEventRecord& record)noexcept{
    Detail::CrashDumpRequestOptions options;
    options.event = DiagnosticEventNameFromRecord(record);
    options.triggerCategory = record.category;
    options.triggerExpression = record.expression;
    options.triggerMessage = record.message;
    options.triggerFile = record.file;
    options.triggerInstructionPointer = record.instructionPointer;
    options.triggerLine = record.line;
    options.callstackFramesToSkip = s_DiagnosticCaptureCallstackFramesToSkip;
    if(!ReserveDiagnosticCapture(record, options.event, options.triggerCategory))
        return;
    const CrashDumpResult result = CaptureCrashDumpForReason(Detail::CrashReasonKind::ManualDump, options.triggerCategory, options.triggerMessage, options);
    if(record.terminatesProcess && DiagnosticResultCanSuppressDuplicatePlatformCrash(result))
        Detail::SuppressNextPlatformCrashCapture();
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


Detail::PlatformCleanupObservations::~PlatformCleanupObservations()noexcept{
    if(m_failureCount == 0u)
        return;
    char text[192u] = {};
    CopyFixedBuffer(text, "Native crash lifecycle failure: domain=");
    AppendUnsignedToFixedBuffer(text, static_cast<u64>(m_first.domain));
    AppendFixedBuffer(text, ", operation=");
    AppendUnsignedToFixedBuffer(text, static_cast<u64>(m_first.operation));
    if(m_first.domain == Detail::PlatformErrorDomain::Filesystem){
        AppendFixedBuffer(text, ", category=");
        AppendFixedBuffer(text, AStringView(m_first.filesystemError.category().name()));
    }
    AppendFixedBuffer(text, ", code=");
    if(m_first.domain == Detail::PlatformErrorDomain::Filesystem){
        const i64 code = m_first.filesystemError.value();
        if(code < 0)
            AppendFixedBuffer(text, "-");
        AppendUnsignedToFixedBuffer(text, static_cast<u64>(code < 0 ? -code : code));
    }
    else
        AppendUnsignedToFixedBuffer(text, m_first.code);
    AppendFixedBuffer(text, ", total failures=");
    AppendUnsignedToFixedBuffer(text, m_failureCount);
    CaptureDiagnosticEvent(DiagnosticEventRecord{ .event = "warning", .category = "crash_native_lifecycle", .expression = __hidden_crash_module::PlatformOperationText(m_first.operation), .message = AStringView(text), .file = __FILE__, .line = __LINE__, });
}

void Detail::PlatformCleanupObservations::record(const PlatformFailure& failure)noexcept{
    if(m_failureCount == 0u)
        m_first = failure;
    if(m_failureCount != Limit<u64>::s_Max)
        ++m_failureCount;
}


void Detail::PlatformCleanupObservations::recordPrimary(const PlatformFailure& failure)noexcept{
    record(failure);
    m_first = failure;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


template<typename ArenaT>
::Path<ArenaT> DefaultCrashSpoolDirectory(ArenaT& arena){
    return __hidden_crash_module::DefaultCrashRootDirectory(arena);
}

template<typename ArenaT>
::Path<ArenaT> DefaultCrashHandlerExecutablePath(ArenaT& arena){
    const auto executableDirectory = GetExecutableDirectory(arena);
    if(!executableDirectory)
        return ::Path<ArenaT>(arena);

    return *executableDirectory / Detail::s_HandlerExecutableFileName;
}

template<typename ArenaT>
bool InstallCrashHandler(const CrashConfigT<ArenaT>& config){
    Detail::PlatformCleanupObservations observations;
    ScopedLock lock(Detail::g_State.mutex);
    if(Detail::g_State.installed)
        return true;
    Detail::UninstallPlatformResources(observations);
    if(Detail::g_State.platformRestorePending)
        return false;

    CopyFixedBuffer(Detail::g_State.applicationName, config.applicationName);
    CopyFixedBuffer(Detail::g_State.versionText, config.version);
    CopyFixedBuffer(Detail::g_State.buildId, config.buildId);
    CopyFixedBuffer(Detail::g_State.logServerUrl, config.logServerUrl);
    CopyFixedBuffer(Detail::g_State.crashUploadToken, config.crashUploadToken);

    Alloc::ScratchArena scratchArena(CrashArenaScope::s_PlatformInstallationScratch);
    const ::Path<Alloc::ScratchArena> spoolDirectory = config.spoolDirectory.empty() ? DefaultCrashSpoolDirectory(scratchArena) : ::Path<Alloc::ScratchArena>(scratchArena, config.spoolDirectory);
    const ::Path<Alloc::ScratchArena> handlerExecutablePath = config.handlerExecutablePath.empty() ? DefaultCrashHandlerExecutablePath(scratchArena) : ::Path<Alloc::ScratchArena>(scratchArena, config.handlerExecutablePath);
    Detail::g_State.dumpDetailMode = config.dumpDetailMode;
    Detail::g_State.capturePolicy = config.capturePolicy;
    Detail::g_State.spoolRetention = config.spoolRetention;
    Detail::g_State.diagnosticCaptureCount = 0u;
    for(Detail::FixedDiagnosticSite& site : Detail::g_State.diagnosticSites)
        site = Detail::FixedDiagnosticSite{};
    Detail::InitializeDumpArena();
    const AString<Alloc::ScratchArena> spoolDirectoryText = PathToString<char>(scratchArena, spoolDirectory);
    CopyFixedBuffer(Detail::g_State.spoolDirectoryText, AStringView(spoolDirectoryText.data(), spoolDirectoryText.size()));
    const AString<Alloc::ScratchArena> handlerExecutablePathText = PathToString<char>(scratchArena, handlerExecutablePath);
    CopyFixedBuffer(Detail::g_State.handlerExecutablePathText, AStringView(handlerExecutablePathText.data(), handlerExecutablePathText.size()));

    const auto spool = Detail::EnsureCrashSpoolDirectories(spoolDirectory);
    if(!spool){
        observations.recordPrimary(Detail::PlatformFailure{ .operation = Detail::PlatformOperation::PrepareSpool, .domain = Detail::PlatformErrorDomain::Filesystem, .filesystemError = spool.error() });
        return false;
    }

    ScopeExit rollbackPlatform([&]()noexcept{ Detail::UninstallPlatformResources(observations); });
#if !defined(NWB_PLATFORM_ANDROID)
    const auto handler = Detail::StartDesktopHandler(handlerExecutablePath, scratchArena, observations);
    if(!handler){
        observations.recordPrimary(handler.error());
        return false;
    }
#endif

    Detail::PublishCrashStateSnapshot();
    const auto platform = Detail::InstallPlatformHandlers();
    if(!platform){
        observations.recordPrimary(platform.error());
        return false;
    }
    Detail::g_State.installed = true;
    SetDiagnosticEventCallback(__hidden_crash_module::CaptureDiagnosticCrash);
    rollbackPlatform.release();
    return true;
}

void UninstallCrashHandler()noexcept{
    Detail::PlatformCleanupObservations observations;
    NothrowScopedLock lock(Detail::g_State.mutex);
    ClearDiagnosticEventCallback(__hidden_crash_module::CaptureDiagnosticCrash);
    Detail::g_State.installed = false;
    Detail::UninstallPlatformResources(observations);
}

bool SetCrashMetadata(const AStringView key, const AStringView value)noexcept{
    if(key.empty())
        return false;

    NothrowScopedLock lock(Detail::g_State.mutex);

    Detail::FixedMetadata* freeSlot = nullptr;
    for(usize i = 0u; i < Detail::s_MaxMetadata; ++i){
        Detail::FixedMetadata& metadata = Detail::g_State.metadata[i];
        if(metadata.used && AStringView(metadata.key) == key){
            CopyFixedBuffer(metadata.value, value);
            Detail::PublishCrashStateSnapshot();
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
    Detail::PublishCrashStateSnapshot();
    return true;
}

bool SetCrashUploadDestination(const AStringView logServerUrl, const AStringView crashUploadToken)noexcept{
    NothrowScopedLock lock(Detail::g_State.mutex);
    if(!Detail::g_State.installed)
        return false;

    CopyFixedBuffer(Detail::g_State.logServerUrl, logServerUrl);
    CopyFixedBuffer(Detail::g_State.crashUploadToken, crashUploadToken);
    Detail::PublishCrashStateSnapshot();
    return true;
}

void AddCrashBreadcrumb(const AStringView category, const AStringView message)noexcept{
    NothrowScopedLock lock(Detail::g_State.mutex);
    __hidden_crash_module::StoreBreadcrumb(category, message);
}

template ::Path<Alloc::PersistentArena> DefaultCrashSpoolDirectory(Alloc::PersistentArena& arena);
template ::Path<Alloc::PersistentArena> DefaultCrashHandlerExecutablePath(Alloc::PersistentArena& arena);
template bool InstallCrashHandler(const CrashConfigT<Alloc::PersistentArena>& config);

CrashDumpResult CaptureCrashDump(const AStringView category, const AStringView message){
    Detail::CrashDumpRequestOptions options;
    return __hidden_crash_module::CaptureCrashDumpForReason(Detail::CrashReasonKind::ManualDump, category, message, options);
}

CrashDumpResult CaptureGpuCrashDump(const AStringView message, const AStringView binaryDump, const GpuCrashDumpKind::Enum dumpKind){
    Detail::CrashDumpRequestOptions options;
    options.gpuReport = message;
    options.gpuDump = binaryDump;
    options.gpuDumpKind = binaryDump.empty() ? GpuCrashDumpKind::None : dumpKind;
    return __hidden_crash_module::CaptureCrashDumpForReason(Detail::CrashReasonKind::GpuCrash, AStringView(Detail::s_GpuCrashCategory), message, options);
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_CRASH_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

