
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
    void setCaptureOptions(const CaptureOptions& options);
    void clear();
    void beginFrame(u64 frameIndex);
    void publishFrame();

    [[nodiscard]] bool enabled()const{ return m_enabled; }
    [[nodiscard]] bool cpuTimingEnabled()const{ return m_cpuTimingEnabled; }
    [[nodiscard]] bool gpuTimingEnabled()const{ return m_gpuTimingEnabled; }
    [[nodiscard]] bool memoryEnabled()const{ return m_memoryEnabled; }
    [[nodiscard]] u64 frameIndex()const{ return m_frameIndex; }
    [[nodiscard]] CaptureOptions captureOptions()const;
    [[nodiscard]] SessionReport report()const;

    [[nodiscard]] TimingSink& cpuTimingSink(){ return m_cpuTiming; }
    [[nodiscard]] TimingSink& gpuTimingSink(){ return m_gpuTiming; }
    [[nodiscard]] TimingView cpuTimingView()const{ return TimingView(m_cpuTiming); }
    [[nodiscard]] TimingView gpuTimingView()const{ return TimingView(m_gpuTiming); }
    [[nodiscard]] MemoryView memoryView()const{ return MemoryView(m_memory); }
    [[nodiscard]] MemoryScopeId registerMemoryScope(const Name& scopeName);

    template<typename Arena>
    void recordMemorySnapshot(const MemoryScopeId scope, const Arena& arena){
        if(!captureOptions().memoryActive())
            return;

        m_memory.recordArenaSnapshot(scope, arena, m_frameIndex);
    }

    template<typename Arena>
    void recordMemorySnapshot(const Name& scopeName, const Arena& arena){
        if(!captureOptions().memoryActive())
            return;

        m_memory.recordArenaSnapshot(scopeName, arena, m_frameIndex);
    }


private:
    void applyEnabledState();


private:
    TimingRecorder m_cpuTiming;
    TimingRecorder m_gpuTiming;
    MemoryRecorder m_memory;
    u64 m_frameIndex = 0u;
    bool m_enabled = false;
    bool m_cpuTimingEnabled = true;
    bool m_gpuTimingEnabled = true;
    bool m_memoryEnabled = true;
};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_PERF_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

