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
    b8 enabled = false;
    b8 cpuTiming = false;
    b8 gpuTiming = false;
    b8 memory = false;


public:
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

    [[nodiscard]] b8 cpuTimingActive()const noexcept{ return enabled && cpuTiming; }

    [[nodiscard]] b8 gpuTimingActive()const noexcept{ return enabled && gpuTiming; }

    [[nodiscard]] b8 memoryActive()const noexcept{ return enabled && memory; }
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

