// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#pragma once


#include "global.h"
#include "report.h"


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_PERF_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


class Session final : NoCopy{
public:
    explicit Session(Alloc::GlobalArena& arena)
        : m_cpuTiming(arena)
        , m_gpuTiming(arena)
        , m_memory(arena)
    {}


public:
    void setCaptureOptions(const CaptureOptions& options)noexcept;
    void clear()noexcept;
    void beginFrame(u64 frameIndex)noexcept;
    void publishFrame();

    [[nodiscard]] u64 frameIndex()const noexcept{ return m_frameIndex; }
    [[nodiscard]] CaptureOptions captureOptions()const noexcept;
    [[nodiscard]] SessionReport report()const noexcept;

    [[nodiscard]] TimingSink& cpuTimingSink()noexcept{ return m_cpuTiming; }
    [[nodiscard]] TimingSink& gpuTimingSink()noexcept{ return m_gpuTiming; }
    [[nodiscard]] TimingView cpuTimingView()const noexcept{ return TimingView(m_cpuTiming); }
    [[nodiscard]] TimingView gpuTimingView()const noexcept{ return TimingView(m_gpuTiming); }
    [[nodiscard]] MemoryView memoryView()const noexcept{ return MemoryView(m_memory); }

    template<typename Arena>
    void recordMemorySnapshot(const Name& scopeName, const Arena& arena){
        if(!captureOptions().memoryActive())
            return;

        m_memory.recordSnapshot(m_memory.registerScope(scopeName), arena.memoryStats(), m_frameIndex);
    }


private:
    void ensureMemoryScopes();
    void applyEnabledState()noexcept;


private:
    u64 m_frameIndex = 0u;
    TimingRecorder m_cpuTiming;
    TimingRecorder m_gpuTiming;
    MemoryRecorder m_memory;
    bool m_enabled = false;
    bool m_cpuTimingEnabled = true;
    bool m_gpuTimingEnabled = true;
    bool m_memoryEnabled = true;
};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_PERF_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

