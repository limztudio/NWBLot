// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "aftermath.h"
#include "arena_names.h"

#include <global/core/common/log.h>

#if defined(NWB_WITH_AFTERMATH)
#include <GFSDK_Aftermath_GpuCrashDump.h>

#include <global/core/common/aftermath_runtime.h>

#include <global/shared_library.h>
#include <global/sync.h>
#include <global/thread.h>
#endif


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_VULKAN_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace Aftermath{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#if defined(NWB_WITH_AFTERMATH)


namespace __hidden_aftermath{


// Aftermath generates the crash dump asynchronously on a driver thread after device-lost; poll its status
// until the dump callback has run (Finished) or collection failed, bounded so a hung driver cannot wedge the
// device-lost path. NVIDIA recommends waiting "a couple of seconds".
inline constexpr u32 s_PollIntervalMilliseconds = 50u;
inline constexpr u32 s_MaxWaitMilliseconds = 5000u;


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


static Alloc::GlobalArena& DumpArena(){
    static Alloc::GlobalArena arena(VulkanArenaScope::s_AftermathDumpArena);
    return arena;
}


// Process-global Aftermath state: the dynamically loaded runtime, its resolved entry points, and the single
// crash dump collected on device-lost. The dump callback (a driver thread) writes 'dumpBytes' under
// 'dumpMutex'; the device-lost thread reads it after polling the dump status to Finished.
struct State{
    SharedLibrary library;
    PFN_GFSDK_Aftermath_EnableGpuCrashDumps enable = nullptr;
    PFN_GFSDK_Aftermath_DisableGpuCrashDumps disable = nullptr;
    PFN_GFSDK_Aftermath_GetCrashDumpStatus getStatus = nullptr;
    bool active = false;

    Futex dumpMutex;
    Vector<u8, Alloc::GlobalArena> dumpBytes;
    bool dumpReady = false;

    State()
        : dumpBytes(DumpArena())
    {}
};

static State& GetState(){
    static State state;
    return state;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


// GPU crash dump callback (free-threaded). The dump pointer is only valid for the duration of the call, so
// copy it into the process-global buffer for the device-lost thread to ship into the crash package.
static void GFSDK_AFTERMATH_CALL OnGpuCrashDump(const void* pGpuCrashDump, const uint32_t gpuCrashDumpSize, void* pUserData){
    static_cast<void>(pUserData);
    if(!pGpuCrashDump || gpuCrashDumpSize == 0u)
        return;

    State& state = GetState();
    ScopedLock lock(state.dumpMutex);
    state.dumpBytes.resize(gpuCrashDumpSize);
    NWB_MEMCPY(state.dumpBytes.data(), gpuCrashDumpSize, pGpuCrashDump, gpuCrashDumpSize);
    state.dumpReady = true;
}

// Crash dump description callback (free-threaded). Adds application identity to the dump for triage.
static void GFSDK_AFTERMATH_CALL OnCrashDumpDescription(PFN_GFSDK_Aftermath_AddGpuCrashDumpDescription addValue, void* pUserData){
    static_cast<void>(pUserData);
    addValue(static_cast<uint32_t>(GFSDK_Aftermath_GpuCrashDumpDescriptionKey_ApplicationName), s_AppName);
}


};


#endif


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


bool Initialize(){
#if !defined(NWB_WITH_AFTERMATH)
    return false;
#else
    using namespace __hidden_aftermath;

    State& state = GetState();
    if(state.active)
        return true; // process-global: only enable once.

    if(!state.library.open(Common::s_AftermathRuntimeName)){
        NWB_LOGGER_WARNING(NWB_TEXT("Vulkan: NVIDIA Aftermath runtime is not present next to the executable; GPU crash dumps disabled."));
        return false;
    }

    if(
        !state.library.resolve("GFSDK_Aftermath_EnableGpuCrashDumps", state.enable)
        || !state.library.resolve("GFSDK_Aftermath_DisableGpuCrashDumps", state.disable)
        || !state.library.resolve("GFSDK_Aftermath_GetCrashDumpStatus", state.getStatus)
    ){
        NWB_LOGGER_WARNING(NWB_TEXT("Vulkan: NVIDIA Aftermath entry points could not be resolved; GPU crash dumps disabled."));
        state.library.close();
        return false;
    }

    const GFSDK_Aftermath_Result result = state.enable(
        GFSDK_Aftermath_Version_API,
        static_cast<uint32_t>(GFSDK_Aftermath_GpuCrashDumpWatchedApiFlags_Vulkan),
        static_cast<uint32_t>(GFSDK_Aftermath_GpuCrashDumpFeatureFlags_DeferDebugInfoCallbacks),
        &OnGpuCrashDump,
        nullptr,
        &OnCrashDumpDescription,
        nullptr,
        nullptr
    );
    if(!GFSDK_Aftermath_SUCCEED(result)){
        NWB_LOGGER_WARNING(NWB_TEXT("Vulkan: GFSDK_Aftermath_EnableGpuCrashDumps failed (0x{:x}); GPU crash dumps disabled."), static_cast<u32>(result));
        state.library.close();
        return false;
    }

    state.active = true;
    NWB_LOGGER_INFO(NWB_TEXT("Vulkan: NVIDIA Aftermath GPU crash dumps enabled."));
    return true;
#endif
}

void Shutdown(){
#if defined(NWB_WITH_AFTERMATH)
    using namespace __hidden_aftermath;

    State& state = GetState();
    if(!state.active)
        return;

    if(state.disable)
        state.disable();
    state.active = false;
    state.enable = nullptr;
    state.disable = nullptr;
    state.getStatus = nullptr;
    state.library.close();
#endif
}

bool IsActive(){
#if defined(NWB_WITH_AFTERMATH)
    return __hidden_aftermath::GetState().active;
#else
    return false;
#endif
}

GpuCrashDumpView WaitForCrashDump(){
    GpuCrashDumpView view;
#if defined(NWB_WITH_AFTERMATH)
    using namespace __hidden_aftermath;

    State& state = GetState();
    if(!state.active)
        return view;

    // Poll until the dump callback has run (Finished) or collection failed; ignore status-query failures and
    // keep polling until the timeout so older drivers (which may only report 'Unknown') still get a chance to
    // deliver the dump via the callback.
    u32 waited = 0u;
    for(;;){
        GFSDK_Aftermath_CrashDump_Status status = GFSDK_Aftermath_CrashDump_Status_Unknown;
        const GFSDK_Aftermath_Result statusResult = state.getStatus(&status);
        if(
            GFSDK_Aftermath_SUCCEED(statusResult)
            && (status == GFSDK_Aftermath_CrashDump_Status_Finished || status == GFSDK_Aftermath_CrashDump_Status_CollectingDataFailed)
        )
            break;
        if(waited >= s_MaxWaitMilliseconds)
            break;
        SleepMS(s_PollIntervalMilliseconds);
        waited += s_PollIntervalMilliseconds;
    }

    ScopedLock lock(state.dumpMutex);
    if(state.dumpReady && !state.dumpBytes.empty()){
        view.data = state.dumpBytes.data();
        view.size = state.dumpBytes.size();
    }
#endif
    return view;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_VULKAN_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

