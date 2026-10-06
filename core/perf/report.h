// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


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

    [[nodiscard]] static constexpr CaptureOptions Disabled()noexcept{
        return {};
    }

    [[nodiscard]] static constexpr CaptureOptions GpuTimingOnly()noexcept{
        CaptureOptions options;
        options.enabled = true;
        options.gpuTiming = true;
        return options;
    }

    [[nodiscard]] static constexpr CaptureOptions All()noexcept{
        CaptureOptions options;
        options.enabled = true;
        options.cpuTiming = true;
        options.gpuTiming = true;
        options.memory = true;
        return options;
    }

    [[nodiscard]] bool cpuTimingActive()const noexcept{ return enabled && cpuTiming; }
    [[nodiscard]] bool gpuTimingActive()const noexcept{ return enabled && gpuTiming; }
    [[nodiscard]] bool memoryActive()const noexcept{ return enabled && memory; }
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

