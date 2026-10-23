// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#pragma once


#include <impl/global.h>
#include <impl/ecs_render/csg/task_graph_opaque_compute_tasks.h>
#include <impl/ecs_render/material/task_graph_opaque_compute_tasks.h>

#include <core/task/gpu/task_graph.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


struct DeferredGraphResourceImportResult;


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


struct OpaqueComputeEmulationProducerInputs{
    const DeferredGraphResourceImportResult& resources;
    Core::GpuTaskResourceSetUse materialGeometrySetUse{};
    Core::GpuTaskResourceSetUse materialSampledTextureSetUse{};
    Core::BufferRange materialInstanceRange{};
    Core::BufferRange materialTypedRange{};
    Core::BufferRange csgReceiverRange{};
    Core::BufferRange csgCutterRange{};
    usize instanceCount = 0u;
    usize materialTypedByteCount = 0u;
    bool materialDrawBuffersUploaded = false;
    bool csgFrameBuffersUploaded = false;
    bool materialFrameStatesGraphOwned = false;
    bool materialGeometryStatesGraphOwned = false;
};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


// Graph-owned compute producers that write generated vertices right before the G-buffer rasterizes them.
class OpaqueComputeEmulationProducerBuilder final : NoCopy{
public:
    explicit OpaqueComputeEmulationProducerBuilder(Core::GpuTaskGraph& graph)noexcept
        : m_graph(graph)
    {}


public:
    [[nodiscard]] Expected<Core::GpuTaskId> declareRegular(
        Core::Alloc::ScratchArena& scratchArena,
        const OpaqueComputeEmulationProducerInputs& inputs,
        const Core::GpuTaskResourceSetUse& outputUavSetUse,
        const Core::GpuTaskId& dependency,
        ECSRenderDetail::OpaqueRegularComputeEmulationGraphTask::Payload& payload
    );
    [[nodiscard]] Expected<Core::GpuTaskId> declareCsgReceiver(
        Core::Alloc::ScratchArena& scratchArena,
        const OpaqueComputeEmulationProducerInputs& inputs,
        const ECSRenderDetail::CsgGraphResourceSnapshot& csgResources,
        const Core::GpuTaskResourceSetUse& outputUavSetUse,
        const Core::GpuTaskId& dependency,
        ECSRenderDetail::OpaqueCsgReceiverComputeEmulationGraphTask::Payload& payload
    );


private:
    Core::GpuTaskGraph& m_graph;
};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

