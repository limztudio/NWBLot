// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#pragma once


#include <impl/global.h>
#include <impl/ecs_render/raytrace/prepared_builds.h>
#include <impl/ecs_render/shadow/types.adhd.h>

#include <core/task/gpu/task_graph.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


class RendererRayTracingSystem;
class RendererMeshSystem;
struct RayTracingShadowPreparationResourceSnapshot;
class ShadowPrepareUploadChain;


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


struct ShadowPrepareResourceInputs{
    const RayTracingShadowPreparationResourceSnapshot& rayTracingShadowResources;
    const ShadowPrepareUploadChain& uploads;
    Core::GpuGraphResourceId currentBindlessSlots;
    Core::GpuGraphResourceId materialContextSlots;
    Span<const Core::GpuGraphResourceId> shadowTraceGeometryResources;
    Span<const Core::GpuGraphResourceId> softwareBvhBuildStateResources;
    Span<const PreparedMeshBlasBuild> preparedMeshBlasBuilds;
    b8 sceneTlasBuildGraphOwned;
    b8 meshBlasBuildsGraphOwned;
    b8 meshBlasGeometryBuildInputStatesGraphOwned;
};

struct ShadowPrepareResourcePlan{
    Vector<Core::GpuTaskResourceUse, Core::Alloc::ScratchArena> resourceUses;
    Array<Core::GpuTaskResourceSetUse, s_ShadowPrepareResourceSetUseCapacity> resourceSetUses = {};
    usize resourceSetUseCount = 0u;
    Vector<Core::GpuTaskResourceUse, Core::Alloc::ScratchArena> accelStructFinalizeResourceUses;
    Core::GpuGraphResourceId sceneTlas;


public:
    explicit ShadowPrepareResourcePlan(Core::Alloc::ScratchArena& scratchArena)noexcept
        : resourceUses(scratchArena)
        , accelStructFinalizeResourceUses(scratchArena)
    {}
};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


class ShadowPrepareResourcePlanner final : NoCopy{
public:
    ShadowPrepareResourcePlanner(Core::GpuTaskGraph& graph, RendererRayTracingSystem& raytracingSystem, RendererMeshSystem& meshSystem)noexcept
        : m_graph(graph)
        , m_raytracingSystem(raytracingSystem)
        , m_meshSystem(meshSystem)
    {}


public:
    [[nodiscard]] b8 prepare(const ShadowPrepareResourceInputs& inputs, Core::Alloc::ScratchArena& scratchArena, ShadowPrepareResourcePlan& result);


private:
    Core::GpuTaskGraph& m_graph;
    RendererRayTracingSystem& m_raytracingSystem;
    RendererMeshSystem& m_meshSystem;
};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

