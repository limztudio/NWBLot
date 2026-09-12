// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#pragma once


#include <impl/global.h>
#include <impl/ecs_render/shared/renderer_frame_bindings.h>

#include <core/graphics/gpu_timing.h>
#include <core/task/gpu/task_graph.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


class RendererRayTracingSystem;

struct DeferredFrameTargets;

// Surfel-GI lifecycle owns pool-initialization clears plus lifecycle plus snapshot-copy declaration.
struct SurfelGiLifecycleInputs{
    DeferredFrameTargets* targets = nullptr;
    Core::GpuTaskId dependency;
    Core::GpuGraphResourceId surfelPool;
    Core::GpuGraphResourceId surfelCellHead;
    Core::GpuGraphResourceId surfelCounter;
    Core::GpuGraphResourceId surfelFreeList;
    Core::GpuGraphResourceId surfelPoolSnapshot;
    Core::GpuGraphResourceId surfelCellHeadSnapshot;
    u64 poolSnapshotByteSize = 0u;
    u64 cellHeadSnapshotByteSize = 0u;
    Core::GpuTaskSchedulingHint scheduling;
    const Core::GpuExternalCompletionId* externalDependencies = nullptr;
    usize externalDependencyCount = 0u;
    Core::GpuTaskExternalStateSource computeStateSource;
    Core::GpuTaskExternalStateSource counterStateSource;
    bool hasWork = false;
};

struct SurfelGiLifecycleResult{
    Core::GpuTaskId preparationTask;
    Core::GpuTaskId initializationLifecycleTask;
    Core::GpuTaskId snapshotCopyTask;
    Core::GpuTaskId dependency;
};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


class SurfelGiLifecycleBuilder final : NoCopy{
public:
    SurfelGiLifecycleBuilder(
        Core::GpuTaskGraph& graph,
        RendererRayTracingSystem& raytracingSystem
    );


public:
    [[nodiscard]] bool declare(
        const SurfelGiLifecycleInputs& inputs,
        SurfelGiLifecycleResult& outResult
    );


private:
    Core::GpuTaskGraph& m_graph;
    RendererRayTracingSystem& m_raytracingSystem;
};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

