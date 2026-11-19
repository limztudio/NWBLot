// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#pragma once


#include "foundation.h"

#include <global/containers.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_CORE_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


typedef Pair<b8, AStringView> ResolvedMarker;

class GpuCrashTracker;

class GpuCrashMarkerTracker : NoCopy{
public:
    GpuCrashMarkerTracker(GpuCrashTracker& tracker, GraphicsArena& arena)noexcept;


public:
    usize pushEvent(AStringView name);
    void popEvent()noexcept;
    void resetEventStack()noexcept;


private:
    GpuCrashTracker& m_tracker;
    GraphicsString m_eventStack;
    GraphicsVector<usize> m_eventStackOffsets;
};

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

struct GpuCrashReport{
    AString<Alloc::PersistentArena> context;
    AString<Alloc::PersistentArena> details;

    const u8* binaryDump = nullptr;
    usize binaryDumpSize = 0u;
    GpuCrashDumpKind::Enum binaryDumpKind = GpuCrashDumpKind::None;


public:
    explicit GpuCrashReport(Alloc::PersistentArena& arena)noexcept
        : context(arena)
        , details(arena)
    {}
};

typedef void(*GpuCrashSink)(void* userData, const GpuCrashReport& report);

void RegisterGpuCrashSink(GpuCrashSink sink, void* userData)noexcept;
void DispatchGpuCrash(const GpuCrashReport& report);


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_CORE_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

