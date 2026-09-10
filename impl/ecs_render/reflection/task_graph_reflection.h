// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#pragma once


#include "reflection_system.h"

#include <core/task/gpu/task_graph.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace RendererTaskGraphDetail{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


struct ReflectionGraphInputs{
    Core::GpuGraphResourceId opaqueDepth;
    Core::GpuGraphResourceId opaqueColor;
    // Surface reads cover view/deferred selectors, F0/roughness, depth/normal, glass captures.
    const Core::GpuTaskResourceUse* surfaceReads = nullptr;
    usize surfaceReadCount = 0u;
    const Core::GpuTaskResourceUse* hardwareReads = nullptr;
    usize hardwareReadCount = 0u;
    const Core::GpuTaskResourceSetUse* hardwareSetReads = nullptr;
    usize hardwareSetReadCount = 0u;
    const bool* hardwarePreparationReady = nullptr;
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

// Inputs copy at declaration; outcomes and latches are frame-owned past packet lifetime.
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

