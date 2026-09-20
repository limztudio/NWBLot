// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#pragma once


#include <impl/ecs_render/material/renderer_draw_types.h>
#include <impl/ecs_render/shared/renderer_frame_bindings.h>

#include <core/graphics/gpu_timing.h>
#include <core/task/gpu/task_graph.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


class RendererMaterialSystem;
class RendererMeshSystem;
struct DeferredFrameTargets;


// One declaration-local registry deduplicates immutable object vertices and indices across every material raster phase.
class ObjectGeometryCacheGraph final : NoCopy{
public:
    ObjectGeometryCacheGraph(
        Core::GpuTaskGraph& graph,
        RendererMaterialSystem& materialSystem,
        RendererMeshSystem& meshSystem,
        Core::Alloc::ScratchArena& arena
    );

    [[nodiscard]] bool prepare(
        const MaterialPassDrawItem* draws,
        usize drawCount,
        const ECSRenderDetail::MeshFrameBindingSnapshot& frameBindings,
        const DeferredFrameTargets& targets,
        Core::GpuTaskId& dependency,
        Vector<Core::GpuTaskResourceUse, Core::Alloc::ScratchArena>& rasterUses,
        Core::Alloc::ScratchArena& scratchArena,
        Core::GpuTimingSubmissionTicket* timingTicket = nullptr,
        Core::GpuTimingSubmissionTicket** rebindableTimingTicket = nullptr
    );

private:
    struct Entry{
        MaterialPassDrawItem draw;
        Core::GpuGraphResourceId resource;
        Core::GpuTaskId producer;
    };

    Core::GpuTaskGraph& m_graph;
    RendererMaterialSystem& m_materialSystem;
    RendererMeshSystem& m_meshSystem;
    Vector<Entry, Core::Alloc::ScratchArena> m_entries;
    usize m_phaseCount = 0u;
};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

