// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#pragma once


#include <impl/global.h>
#include <impl/ecs_render/raytrace/graph_snapshots.h>

#include <core/task/gpu/task_graph.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


class RendererMeshSystem;
class RendererRayTracingSystem;


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


// Graph imports for the preflighted trace geometry buffers, trace material textures, their resource sets, and software BVH build state.
struct FrameGraphTraceGeometryResult{
    Vector<Core::GpuGraphResourceId, Core::Alloc::ScratchArena> traceGeometry;
    Vector<Core::GpuGraphResourceId, Core::Alloc::ScratchArena> hardwareTraceGeometry;
    Vector<Core::GpuGraphResourceId, Core::Alloc::ScratchArena> hardwareTraceAttributes;
    Vector<Core::GpuGraphResourceId, Core::Alloc::ScratchArena> softwareTraceGeometry;
    Vector<Core::GpuGraphResourceId, Core::Alloc::ScratchArena> softwareBvhBuildState;
    Core::GpuGraphResourceSetId shadowTraceGeometrySet;
    Core::GpuGraphResourceSetId softwareTraceGeometrySet;
    Core::GpuGraphResourceSetId traceMaterialSampledTextureSet;
    Core::GpuGraphResourceSetId hardwareTraceGeometrySet;
    Core::GpuGraphResourceSetId hardwareTraceAttributeSet;
    bool softwareTraceResourcesPrepared = false;


public:
    explicit FrameGraphTraceGeometryResult(Core::Alloc::ScratchArena& scratchArena)noexcept
        : traceGeometry(scratchArena)
        , hardwareTraceGeometry(scratchArena)
        , hardwareTraceAttributes(scratchArena)
        , softwareTraceGeometry(scratchArena)
        , softwareBvhBuildState(scratchArena)
    {}
};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


class FrameGraphTraceGeometryImporter final : NoCopy{
public:
    FrameGraphTraceGeometryImporter(Core::GpuTaskGraph& graph, RendererRayTracingSystem& raytracingSystem, RendererMeshSystem& meshSystem)noexcept
        : m_graph(graph)
        , m_raytracingSystem(raytracingSystem)
        , m_meshSystem(meshSystem)
    {}


public:
    [[nodiscard]] Expected<FrameGraphTraceGeometryResult> declare(Core::Alloc::ScratchArena& scratchArena, const RayTracingShadowPreparationResourceSnapshot& rayTracingShadowResources);


private:
    [[nodiscard]] Core::GpuGraphResourceSetId importSet(const Vector<Core::GpuGraphResourceId, Core::Alloc::ScratchArena>& members, const Name& identity, AStringView markerLabel);


private:
    Core::GpuTaskGraph& m_graph;
    RendererRayTracingSystem& m_raytracingSystem;
    RendererMeshSystem& m_meshSystem;
};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

