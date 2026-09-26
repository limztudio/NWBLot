// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#pragma once


#include "light_space_shadow.h"

#include <core/alloc/scratch.h>
#include <core/task/gpu/task_graph.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_CORE_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


class GraphicsRuntime;


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_CORE_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


struct LightSpaceShadowGraphInputs{
    Core::GraphicsRuntime& graphics;
    Core::Alloc::GlobalArena& arena;
    Core::Alloc::ScratchArena& scratchArena;
    const bool& shadowPrepared;
    const LightSpaceShadowSnapshot& snapshot;
    Core::GpuTaskId dependency;
    const Core::GpuTaskResourceUse* sceneReads = nullptr;
    usize sceneReadCount = 0u;
    const Core::GpuTaskResourceSetUse* sceneReadSets = nullptr;
    usize sceneReadSetCount = 0u;
    const Core::GpuTaskExternalStateSource* stateSources = nullptr;
    usize stateSourceCount = 0u;
};

struct LightSpaceShadowGraph{
    Core::GpuGraphResourceId counts;
    Core::GpuGraphResourceId events;
    Core::GpuGraphResourceId views;
    Core::GpuGraphResourceId drawArguments;
    Core::GpuGraphResourceId depth;
    Core::GpuTaskId viewUpload;
    Core::GpuTaskId countsClear;
    Core::GpuTaskId viewFit;
    Core::GpuTaskId opaqueCapture;
    Core::GpuTaskId transparentCapture;
    Core::GpuTaskId shade;
    Core::GpuTaskId ready;

    [[nodiscard]] bool valid()const noexcept{
        return counts.valid() && events.valid() && views.valid() && depth.valid() && ready.valid();
    }
};

// Capture and reuse import the same map generation; accepted state sources and frame ordering preserve availability.
[[nodiscard]] LightSpaceShadowGraph DeclareLightSpaceShadowMaps(Core::GpuTaskGraph& graph, const LightSpaceShadowGraphInputs& inputs);


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

