// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#pragma once


#include "optical_scene_resources.h"

#include <core/task/gpu/task_graph.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


struct RayTracingOpticalSceneGraphBuffer{
    Core::GpuGraphResourceId resource;
    Core::GpuTaskId uploadTask;
    b8 reused = false;


public:
    [[nodiscard]] b8 valid()const noexcept{ return resource.valid() && (reused || uploadTask.valid()); }
};

// Declare once per frame; persistent metadata writes follow the shared selector reader drain.
// Returned reads retain Common at packet close and carry the latest accepted writer's availability completion.
[[nodiscard]] Expected<RayTracingOpticalSceneGraphBuffer> ImportRayTracingOpticalSceneBuffer(
    const RayTracingOpticalSceneSnapshot& resources,
    Core::GpuTaskId priorReadersDrained,
    Core::GpuTaskGraph& graph,
    Core::Alloc::ScratchArena& scratchArena
);


// Rewritten on primary Graphics after accepted skinning, then retained by all optical readers in this graph.
// The explicit selector drain also orders preceding asynchronous optical readers.
[[nodiscard]] Expected<RayTracingOpticalSceneGraphBuffer> DeclareRayTracingOpticalBoundsFinalize(
    const RayTracingOpticalSceneSnapshot& resources,
    Core::GpuGraphResourceId source,
    Core::GpuTaskId priorReadersDrained,
    Core::GpuTaskGraph& graph,
    Core::Alloc::ScratchArena& scratchArena
);


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

