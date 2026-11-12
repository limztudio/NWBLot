// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#pragma once


#include "foundation.h"

#include <global/containers.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_CORE_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


// View into device-lifetime marker history (or the not-found sentinel). GpuCrashTracker must outlive the view.
typedef Pair<b8, AStringView> ResolvedMarker;

class GpuCrashTracker;

// Device-lost reports the last executed marker payload (NV checkpoints / AMD markers); nested regimes stack them.
// One GpuCrashMarkerTracker per API-level command list; 64-bit payload via device-lifetime history.
class GpuCrashMarkerTracker : NoCopy{
public:
    GpuCrashMarkerTracker(GpuCrashTracker& tracker, GraphicsArena& arena)noexcept;


public:
    usize pushEvent(AStringView name);
    void popEvent()noexcept;
    // Clears only active nesting. Published device history remains available for in-flight crash reports.
    void resetEventStack()noexcept;


private:
    GpuCrashTracker& m_tracker;
    // Nested marker labels joined by "/" with an offset stack to pop the most recent segment.
    GraphicsString m_eventStack;
    GraphicsVector<usize> m_eventStackOffsets;
};

// GpuCrashTracker tracks all Device-level constructs needed when reporting a GPU crash.
// It resolves a last-executed marker payload hash back to the original nested marker string.
// There is one GpuCrashTracker per Device.
// Its concurrent map is append-only: exact paths reuse IDs, while published strings never move or disappear.
class GpuCrashTracker : NoCopy{
    friend class GpuCrashMarkerTracker;


public:
    explicit GpuCrashTracker(GraphicsArena& arena)noexcept;


public:
    ResolvedMarker resolveMarker(usize markerHash)noexcept;


private:
    [[nodiscard]] usize internEvent(AStringView eventString);


private:
    GraphicsArena& m_arena;
    ParallelHashMap<usize, GraphicsString, GraphicsArena> m_eventStrings;
};

namespace GpuCrashDumpKind{
    enum Enum : u8{
        None,
        Aftermath,
        RadeonGpuDetective,
    };
};

// Captured GPU crash report: last-executed marker stack and fault info, as text for the crash reporter.
struct GpuCrashReport{
    AString<Alloc::PersistentArena> context;
    AString<Alloc::PersistentArena> details;

    const u8* binaryDump = nullptr;
    usize binaryDumpSize = 0u;
    // Optional binary crash dump (e.g. '.nv-gpudmp'). View; valid only during DispatchGpuCrash.
    GpuCrashDumpKind::Enum binaryDumpKind = GpuCrashDumpKind::None;


public:
    explicit GpuCrashReport(Alloc::PersistentArena& arena)noexcept
        : context(arena)
        , details(arena)
    {}
};

// Process-global sink for backend GPU-crash capture on device-lost. The app registers it (e.g. forward to crash reporter).
typedef void(*GpuCrashSink)(void* userData, const GpuCrashReport& report);

void RegisterGpuCrashSink(GpuCrashSink sink, void* userData)noexcept;
void DispatchGpuCrash(const GpuCrashReport& report);


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_CORE_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

