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
inline constexpr usize s_LifecycleFailureTextCapacity = 192u;


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


[[nodiscard]] static AStringView PlatformOperationText(const CrashDetail::PlatformOperation::Enum operation)noexcept{
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
    CrashDetail::FixedBreadcrumb& breadcrumb = CrashDetail::g_State.breadcrumbs[CrashDetail::g_State.nextBreadcrumb % CrashDetail::s_MaxBreadcrumbs];
    breadcrumb.used = 1u;
    breadcrumb.order = CrashDetail::g_State.breadcrumbOrder.fetch_add(1u, MemoryOrder::relaxed);
    CopyFixedBuffer(breadcrumb.category, category.empty() ? AStringView(CrashDetail::s_DefaultBreadcrumbCategory) : category);
    CopyFixedBuffer(breadcrumb.message, message);
    ++CrashDetail::g_State.nextBreadcrumb;
    CrashDetail::PublishCrashStateSnapshot();
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
    NothrowScopedLock lock(CrashDetail::g_State.mutex);
    if(!CrashDetail::g_State.installed)
        return false;
    if(!CapturePolicyAllows(CrashDetail::g_State.capturePolicy, event, category))
        return false;

    const CrashCapturePolicy& policy = CrashDetail::g_State.capturePolicy;
    if(policy.maxDiagnosticDumpsPerProcess != 0u && CrashDetail::g_State.diagnosticCaptureCount >= policy.maxDiagnosticDumpsPerProcess)
        return false;

    CrashDetail::FixedDiagnosticSite* freeSite = nullptr;
    CrashDetail::FixedDiagnosticSite* matchingSite = nullptr;
    const u64 siteHash = DiagnosticSiteHash(record);
    for(usize i = 0u; i < CrashDetail::s_MaxDiagnosticSites; ++i){
        CrashDetail::FixedDiagnosticSite& site = CrashDetail::g_State.diagnosticSites[i];
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

    ++CrashDetail::g_State.diagnosticCaptureCount;
    if(matchingSite)
        ++matchingSite->captureCount;
    return true;
}

NWB_NOINLINE static CrashDumpResult CaptureCrashDumpForReason(const CrashDetail::CrashReasonKind::Enum reasonKind, const AStringView category, const AStringView message, CrashDetail::CrashDumpRequestOptions& options){
    const AStringView breadcrumbCategory = category.empty() ? AStringView(CrashDetail::s_ManualDumpCategory) : category;

    {
        ScopedLock lock(CrashDetail::g_State.mutex);
        if(!CrashDetail::g_State.installed)
            return CrashDumpResult{ CrashDumpStatus::NotInstalled };
        if(!category.empty() || !message.empty())
            StoreBreadcrumb(breadcrumbCategory, message);
    }

    if(options.triggerCategory.empty())
        options.triggerCategory = breadcrumbCategory;
    if(options.triggerMessage.empty())
        options.triggerMessage = message;

    options.waitMilliseconds = CrashDetail::s_ManualCrashDumpWaitMilliseconds;
    if(options.callstackFramesToSkip == 0u)
        options.callstackFramesToSkip = 1u;
    CrashDetail::ManualDumpContextStorage contextStorage;
    CrashDetail::CaptureManualDumpContext(options, contextStorage);

    return CrashDetail::RequestCrashDump(reasonKind, 0u, options);
}

NWB_NOINLINE static void CaptureDiagnosticCrash(const DiagnosticEventRecord& record)noexcept{
    CrashDetail::CrashDumpRequestOptions options;
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
    const CrashDumpResult result = CaptureCrashDumpForReason(CrashDetail::CrashReasonKind::ManualDump, options.triggerCategory, options.triggerMessage, options);
    if(record.terminatesProcess && DiagnosticResultCanSuppressDuplicatePlatformCrash(result))
        CrashDetail::SuppressNextPlatformCrashCapture();
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


CrashDetail::PlatformCleanupObservations::~PlatformCleanupObservations()noexcept{
    if(m_failureCount == 0u)
        return;
    char text[__hidden_crash_module::s_LifecycleFailureTextCapacity] = {};
    CopyFixedBuffer(text, "Native crash lifecycle failure: domain=");
    AppendUnsignedToFixedBuffer(text, static_cast<u64>(m_first.domain));
    AppendFixedBuffer(text, ", operation=");
    AppendUnsignedToFixedBuffer(text, static_cast<u64>(m_first.operation));
    if(m_first.domain == CrashDetail::PlatformErrorDomain::Filesystem){
        AppendFixedBuffer(text, ", category=");
        AppendFixedBuffer(text, AStringView(m_first.filesystemError.category().name()));
    }
    AppendFixedBuffer(text, ", code=");
    if(m_first.domain == CrashDetail::PlatformErrorDomain::Filesystem){
        const i64 code = m_first.filesystemError.value();
        if(code < 0)
            AppendFixedBuffer(text, "-");
        AppendUnsignedToFixedBuffer(text, static_cast<u64>(code < 0 ? -code : code));
    }
    else
        AppendUnsignedToFixedBuffer(text, m_first.code);
    AppendFixedBuffer(text, ", total failures=");
    AppendUnsignedToFixedBuffer(text, m_failureCount);
    CaptureDiagnosticEvent(DiagnosticEventRecord{ .event = CrashDetail::s_WarningEventName, .category = "crash_native_lifecycle", .expression = __hidden_crash_module::PlatformOperationText(m_first.operation), .message = AStringView(text), .file = __FILE__, .line = __LINE__, });
}

void CrashDetail::PlatformCleanupObservations::record(const PlatformFailure& failure)noexcept{
    if(m_failureCount == 0u)
        m_first = failure;
    if(m_failureCount != Limit<u64>::s_Max)
        ++m_failureCount;
}


void CrashDetail::PlatformCleanupObservations::recordPrimary(const PlatformFailure& failure)noexcept{
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

    return *executableDirectory / CrashDetail::s_HandlerExecutableFileName;
}

template<typename ArenaT>
bool InstallCrashHandler(const CrashConfigT<ArenaT>& config){
    CrashDetail::PlatformCleanupObservations observations;
    ScopedLock lock(CrashDetail::g_State.mutex);
    if(CrashDetail::g_State.installed)
        return true;
    CrashDetail::UninstallPlatformResources(observations);
    if(CrashDetail::g_State.platformRestorePending)
        return false;

    CopyFixedBuffer(CrashDetail::g_State.applicationName, config.applicationName);
    CopyFixedBuffer(CrashDetail::g_State.versionText, config.version);
    CopyFixedBuffer(CrashDetail::g_State.buildId, config.buildId);
    CopyFixedBuffer(CrashDetail::g_State.logServerUrl, config.logServerUrl);
    CopyFixedBuffer(CrashDetail::g_State.crashUploadToken, config.crashUploadToken);

    Alloc::ScratchArena scratchArena(CrashArenaScope::s_PlatformInstallationScratch);
    const ::Path<Alloc::ScratchArena> spoolDirectory = config.spoolDirectory.empty() ? DefaultCrashSpoolDirectory(scratchArena) : ::Path<Alloc::ScratchArena>(scratchArena, config.spoolDirectory);
    const ::Path<Alloc::ScratchArena> handlerExecutablePath = config.handlerExecutablePath.empty() ? DefaultCrashHandlerExecutablePath(scratchArena) : ::Path<Alloc::ScratchArena>(scratchArena, config.handlerExecutablePath);
    CrashDetail::g_State.dumpDetailMode = config.dumpDetailMode;
    CrashDetail::g_State.capturePolicy = config.capturePolicy;
    CrashDetail::g_State.spoolRetention = config.spoolRetention;
    CrashDetail::g_State.diagnosticCaptureCount = 0u;
    for(CrashDetail::FixedDiagnosticSite& site : CrashDetail::g_State.diagnosticSites)
        site = CrashDetail::FixedDiagnosticSite{};
    CrashDetail::InitializeDumpArena();
    const AString<Alloc::ScratchArena> spoolDirectoryText = PathToString<char>(scratchArena, spoolDirectory);
    CopyFixedBuffer(CrashDetail::g_State.spoolDirectoryText, AStringView(spoolDirectoryText.data(), spoolDirectoryText.size()));
    const AString<Alloc::ScratchArena> handlerExecutablePathText = PathToString<char>(scratchArena, handlerExecutablePath);
    CopyFixedBuffer(CrashDetail::g_State.handlerExecutablePathText, AStringView(handlerExecutablePathText.data(), handlerExecutablePathText.size()));

    const auto spool = CrashDetail::EnsureCrashSpoolDirectories(spoolDirectory);
    if(!spool){
        observations.recordPrimary(CrashDetail::PlatformFailure{ .operation = CrashDetail::PlatformOperation::PrepareSpool, .domain = CrashDetail::PlatformErrorDomain::Filesystem, .filesystemError = spool.error() });
        return false;
    }

    ScopeExit rollbackPlatform([&]()noexcept{ CrashDetail::UninstallPlatformResources(observations); });
#if !defined(NWB_PLATFORM_ANDROID)
    const auto handler = CrashDetail::StartDesktopHandler(handlerExecutablePath, scratchArena, observations);
    if(!handler){
        observations.recordPrimary(handler.error());
        return false;
    }
#endif

    CrashDetail::PublishCrashStateSnapshot();
    const auto platform = CrashDetail::InstallPlatformHandlers();
    if(!platform){
        observations.recordPrimary(platform.error());
        return false;
    }
    CrashDetail::g_State.installed = true;
    SetDiagnosticEventCallback(__hidden_crash_module::CaptureDiagnosticCrash);
    rollbackPlatform.release();
    return true;
}

void UninstallCrashHandler()noexcept{
    CrashDetail::PlatformCleanupObservations observations;
    NothrowScopedLock lock(CrashDetail::g_State.mutex);
    ClearDiagnosticEventCallback(__hidden_crash_module::CaptureDiagnosticCrash);
    CrashDetail::g_State.installed = false;
    CrashDetail::UninstallPlatformResources(observations);
}

bool SetCrashMetadata(const AStringView key, const AStringView value)noexcept{
    if(key.empty())
        return false;

    NothrowScopedLock lock(CrashDetail::g_State.mutex);

    CrashDetail::FixedMetadata* freeSlot = nullptr;
    for(usize i = 0u; i < CrashDetail::s_MaxMetadata; ++i){
        CrashDetail::FixedMetadata& metadata = CrashDetail::g_State.metadata[i];
        if(metadata.used && AStringView(metadata.key) == key){
            CopyFixedBuffer(metadata.value, value);
            CrashDetail::PublishCrashStateSnapshot();
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
    CrashDetail::PublishCrashStateSnapshot();
    return true;
}

bool SetCrashUploadDestination(const AStringView logServerUrl, const AStringView crashUploadToken)noexcept{
    NothrowScopedLock lock(CrashDetail::g_State.mutex);
    if(!CrashDetail::g_State.installed)
        return false;

    CopyFixedBuffer(CrashDetail::g_State.logServerUrl, logServerUrl);
    CopyFixedBuffer(CrashDetail::g_State.crashUploadToken, crashUploadToken);
    CrashDetail::PublishCrashStateSnapshot();
    return true;
}

void AddCrashBreadcrumb(const AStringView category, const AStringView message)noexcept{
    NothrowScopedLock lock(CrashDetail::g_State.mutex);
    __hidden_crash_module::StoreBreadcrumb(category, message);
}

template ::Path<Alloc::PersistentArena> DefaultCrashSpoolDirectory(Alloc::PersistentArena& arena);
template ::Path<Alloc::PersistentArena> DefaultCrashHandlerExecutablePath(Alloc::PersistentArena& arena);
template bool InstallCrashHandler(const CrashConfigT<Alloc::PersistentArena>& config);

CrashDumpResult CaptureCrashDump(const AStringView category, const AStringView message){
    CrashDetail::CrashDumpRequestOptions options;
    return __hidden_crash_module::CaptureCrashDumpForReason(CrashDetail::CrashReasonKind::ManualDump, category, message, options);
}

CrashDumpResult CaptureGpuCrashDump(const AStringView message, const AStringView binaryDump, const GpuCrashDumpKind::Enum dumpKind){
    CrashDetail::CrashDumpRequestOptions options;
    options.gpuReport = message;
    options.gpuDump = binaryDump;
    options.gpuDumpKind = binaryDump.empty() ? GpuCrashDumpKind::None : dumpKind;
    return __hidden_crash_module::CaptureCrashDumpForReason(CrashDetail::CrashReasonKind::GpuCrash, AStringView(CrashDetail::s_GpuCrashCategory), message, options);
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_CRASH_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

