// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#pragma once


#include <impl/ecs_render/raytrace/raytracing_system.h>

#include <core/task/gpu/task_graph.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


struct PreparedMeshSwBvhGraphResources{
    PreparedMeshSwBvhBuild build;
    Core::GpuGraphResourceId position;
    Core::GpuGraphResourceId triangleIndex;
    Core::GpuGraphResourceId node;
    Core::GpuGraphResourceId parent;
    Core::GpuGraphResourceId sortKeys;
    Core::GpuGraphResourceId sortPayload;
    Core::GpuGraphResourceId visitCounter;
};

using PreparedMeshSwBvhGraphResourceVector = Vector<PreparedMeshSwBvhGraphResources, Core::Alloc::ScratchArena>;


// The caller reserves output storage at its declaration phase boundary. Keep complete owning snapshots in build
// order; a missing graph identity clears every row so the caller can retain its aggregate native fallback.
[[nodiscard]] bool ResolvePreparedSoftwareBvhGraphResources(
    const Core::GpuTaskGraph& graph,
    const PreparedMeshSwBvhBuildVector& builds,
    PreparedMeshSwBvhGraphResourceVector& outResources
);


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

