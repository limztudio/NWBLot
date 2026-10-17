// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "rhi/device.h"

#include <global/termination.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_CORE_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


static constexpr AStringView s_NotFoundMarkerString = "ERROR: could not resolve marker";


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


GpuCrashMarkerTracker::GpuCrashMarkerTracker(GpuCrashTracker& tracker, GraphicsArena& arena)noexcept
    : m_tracker(tracker)
    , m_eventStack(arena)
    , m_eventStackOffsets(arena)
{}

usize GpuCrashMarkerTracker::pushEvent(const AStringView name){
    GraphicsString nextEventStack(m_eventStack, m_eventStack.get_allocator());
    if(!nextEventStack.empty())
        nextEventStack.append("/");
    nextEventStack.append(name);

    const usize markerHash = m_tracker.internEvent(nextEventStack);
    m_eventStackOffsets.push_back(m_eventStack.size());
    m_eventStack = Move(nextEventStack);
    return markerHash;
}

void GpuCrashMarkerTracker::popEvent()noexcept{
    if(m_eventStackOffsets.empty())
        return;

    static_assert(noexcept(m_eventStack.pop_back()), "GPU marker stack shrink must remain non-throwing");
    static_assert(noexcept(m_eventStackOffsets.pop_back()), "GPU marker offset release must remain non-throwing");
    const usize previousSize = m_eventStackOffsets.back();
    if(previousSize > m_eventStack.size())
        TerminateInvariant();
    while(m_eventStack.size() > previousSize)
        m_eventStack.pop_back();
    m_eventStackOffsets.pop_back();
}

void GpuCrashMarkerTracker::resetEventStack()noexcept{
    m_eventStack.clear();
    m_eventStackOffsets.clear();
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


GpuCrashTracker::GpuCrashTracker(GraphicsArena& arena)noexcept
    : m_arena(arena)
    , m_eventStrings(0u, Hasher<usize>(), EqualTo<usize>(), arena)
{}

ResolvedMarker GpuCrashTracker::resolveMarker(usize markerHash){
    const auto found = m_eventStrings.find(markerHash);
    if(found != m_eventStrings.end())
        return MakePair(true, AStringView(found->second.data(), found->second.size()));

    return MakePair(false, s_NotFoundMarkerString);
}

usize GpuCrashTracker::internEvent(const AStringView eventString){
    usize markerHash = Hasher<AStringView>{}(eventString);
    if(markerHash == 0u)
        markerHash = 1u;

    for(;;){
        const auto found = m_eventStrings.find(markerHash);
        if(found != m_eventStrings.end()){
            if(found->second == eventString)
                return markerHash;
        }
        else{
            GraphicsString storedEvent(eventString, m_arena);
            const auto inserted = m_eventStrings.emplace(markerHash, Move(storedEvent));
            if(inserted.second || inserted.first->second == eventString)
                return markerHash;
        }

        ++markerHash;
        if(markerHash == 0u)
            ++markerHash;
    }
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


static GpuCrashSink s_GpuCrashSink = nullptr;
static void* s_GpuCrashSinkUserData = nullptr;


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


void RegisterGpuCrashSink(GpuCrashSink sink, void* userData)noexcept{
    s_GpuCrashSink = sink;
    s_GpuCrashSinkUserData = userData;
}

void DispatchGpuCrash(const GpuCrashReport& report){
    if(s_GpuCrashSink)
        s_GpuCrashSink(s_GpuCrashSinkUserData, report);
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_CORE_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

