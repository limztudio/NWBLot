
#pragma once


#include "global.h"

#include "memory.h"
#include "timing.h"


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_PERF_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


struct CaptureOptions{
    bool enabled = false;
    bool cpuTiming = false;
    bool gpuTiming = false;
    bool memory = false;

    [[nodiscard]] static constexpr CaptureOptions Disabled(){
        return {};
    }

    [[nodiscard]] static constexpr CaptureOptions GpuTimingOnly(){
        CaptureOptions options;
        options.enabled = true;
        options.gpuTiming = true;
        return options;
    }

    [[nodiscard]] static constexpr CaptureOptions All(){
        CaptureOptions options;
        options.enabled = true;
        options.cpuTiming = true;
        options.gpuTiming = true;
        options.memory = true;
        return options;
    }

    [[nodiscard]] bool cpuTimingActive()const{ return enabled && cpuTiming; }
    [[nodiscard]] bool gpuTimingActive()const{ return enabled && gpuTiming; }
    [[nodiscard]] bool memoryActive()const{ return enabled && memory; }
};

struct SessionReport{
    CaptureOptions capture;
    u64 frameIndex = 0u;
    TimingView cpuTiming;
    TimingView gpuTiming;
    MemoryView memory;
};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_PERF_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

