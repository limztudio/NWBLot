// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#pragma once


#include "reflection_system.h"

#include <core/graphics/gpu_timing.h>
#include <core/task/gpu/task_graph.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace RendererTaskGraphDetail{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


struct ReflectionGraphInputs{
    // Surface reads include the view/deferred selectors, both F0/roughness textures, opaque depth/normal and
    // captured glass depth/normal. Hardware additionally reads the frozen scene/material/lighting declarations.
    const Core::GpuTaskResourceUse* surfaceReads = nullptr;
    usize surfaceReadCount = 0u;
    const Core::GpuTaskResourceUse* hardwareReads = nullptr;
    usize hardwareReadCount = 0u;
    const Core::GpuTaskResourceSetUse* hardwareSetReads = nullptr;
    usize hardwareSetReadCount = 0u;
    const bool* hardwarePreparationReady = nullptr;
    Core::GpuTimingSubmissionTicket* timingTicket = nullptr;
    bool* hardwareDispatchLogged = nullptr;
    bool* fallbackDispatchLogged = nullptr;
};

struct ReflectionGraphResult{
    Core::GpuTaskId completion;
    Core::GpuGraphResourceId opaqueRadiance;
    Core::GpuGraphResourceId glassRadiance;
    Core::GpuGraphResourceId frameParameters;
    Core::GpuGraphResourceId counters;

    [[nodiscard]] bool valid()const noexcept{ return completion.valid(); }
};

// Input arrays are copied by graph declaration; their scratch lifetime need not reach recording. The preparation
// outcome, timing ticket and diagnostic latches are frame-owned and must outlive the accepted/discarded packet.
[[nodiscard]] ReflectionGraphResult DeclareReflectionTasks(
    Core::GpuTaskGraph& graph,
    Core::GraphicsRuntime& graphics,
    Core::Alloc::ScratchArena& scratchArena,
    const ReflectionFrameSnapshot& resources,
    const ReflectionGraphInputs& inputs,
    Core::GpuTaskId dependency
);


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

