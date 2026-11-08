// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#pragma once


#include <impl/global.h>

#include <core/task/gpu/task_graph.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


class RendererRayTracingSystem;
struct DeferredFrameTargets;
struct RayTracingDeferredGraphResourceSnapshot;
struct RayTracingSurfelPersistentResourceSnapshot;


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


struct SurfelGiResourceInputs{
    const DeferredFrameTargets& deferredTargets;
    const RayTracingDeferredGraphResourceSnapshot& rayTracingResources;
    const RayTracingSurfelPersistentResourceSnapshot& rayTracingSurfelResources;
    Core::GpuGraphResourceId worldPosition;
    Core::GpuGraphResourceId normal;
    Core::GpuGraphResourceId surfelIrradiance;
    Core::GpuGraphResourceId currentBindlessSlots;
    Core::GpuGraphResourceId sceneShading;
    Core::GpuGraphResourceId lights;
    Core::GpuGraphResourceId materialContextSlots;
    Span<const Core::GpuGraphResourceId> traceGeometryResources;
    b8 traceGeometryStatesGraphOwned;
    b8 useHwTrace;
    b8 hasSurfelWork;
};

struct SurfelGiResourcePlan{
    Core::GpuGraphResourceId surfelIrradianceHalf;
    Core::GpuGraphResourceId sceneGeometryDomain;
    Core::GpuGraphResourceId shadowInstanceMaterials;
    Core::GpuGraphResourceId shadowMaterialTyped;
    Core::GpuGraphResourceId shadowInstances;
    Core::GpuGraphResourceId csgTraceContext;
    Core::GpuGraphResourceId surfelConstants;
    Core::GpuGraphResourceId surfelPool;
    Core::GpuGraphResourceId surfelGuidePool;
    Core::GpuGraphResourceId surfelCellHead;
    Core::GpuGraphResourceId surfelCounter;
    Core::GpuGraphResourceId surfelTraceIndirectArgs;
    Core::GpuGraphResourceId surfelFreeList;
    Core::GpuGraphResourceId surfelPoolSnapshot;
    Core::GpuGraphResourceId surfelCellHeadSnapshot;
    Core::GpuGraphResourceId sceneBvhNodes;
    Core::GpuGraphResourceId sceneInstances;
    Core::GpuGraphResourceId tlas;
    Vector<Core::GpuTaskResourceUse, Core::Alloc::ScratchArena> resourceUses;
    Vector<Core::GpuTaskResourceUse, Core::Alloc::ScratchArena> traceResourceUses;
    b8 graphOwnsSurfelGiResolve = false;


public:
    explicit SurfelGiResourcePlan(Core::Alloc::ScratchArena& scratchArena)noexcept
        : resourceUses(scratchArena)
        , traceResourceUses(scratchArena)
    {}
};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


class SurfelGiResourceBuilder final : NoCopy{
public:
    SurfelGiResourceBuilder(Core::GpuTaskGraph& graph, RendererRayTracingSystem& raytracingSystem)noexcept
        : m_graph(graph)
        , m_raytracingSystem(raytracingSystem)
    {}


public:
    [[nodiscard]] b8 prepare(const SurfelGiResourceInputs& inputs, SurfelGiResourcePlan& result);


private:
    Core::GpuTaskGraph& m_graph;
    RendererRayTracingSystem& m_raytracingSystem;
};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

