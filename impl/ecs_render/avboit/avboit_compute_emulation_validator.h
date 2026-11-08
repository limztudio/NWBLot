// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#pragma once


#include <impl/ecs_render/shared/frame_types.adhd.h>
#include <core/task/gpu/compiled_graph.h>

#include <global/span.h>


NWB_IMPL_BEGIN


class AvboitComputeEmulationValidator final : NoCopy{
public:
    explicit AvboitComputeEmulationValidator(const Core::GpuCompiledGraph::ReadView& compiledGraph)noexcept : m_compiledGraph(compiledGraph){}


public:
    [[nodiscard]] b8 optionalTaskPrecedesInSharedPacket(const Core::GpuTaskId optionalTask, const Core::GpuTaskId consumerTask)const noexcept;
    [[nodiscard]] b8 taskIsCompiled(const Core::GpuTaskId task)const noexcept;
    [[nodiscard]] b8 taskBoundaryIsOrdered(const Core::GpuTaskId producerTask, const Core::GpuTaskId consumerTask)const noexcept;
    [[nodiscard]] b8 reusedGeometryIsOrdered(const Core::GpuTaskId source, const Core::GpuTaskId consumer, const Core::GpuTaskId localProducer, const usize sharedPhaseCount)const noexcept;
    [[nodiscard]] b8 computeEmulationMerged(const Core::GpuTaskId computeEmulationTask, const Core::GpuTaskId rasterTask, const Core::GpuTaskId streamTask, const Core::GpuPhysicalQueueInfo* const computeEmulationQueue, const Core::GpuPhysicalQueueInfo* const rasterQueue)const noexcept;
    [[nodiscard]] b8 sharedComputeEmulationChainValid(const Span<const Core::GpuTaskId> sharedTasks, const usize phaseCount, const Core::GpuTaskId regularComputeEmulationTask, const Core::GpuTaskId rasterTask, const Core::GpuTaskId streamTask, const Core::GpuPhysicalQueueInfo* const sharedQueue, const Core::GpuPhysicalQueueInfo* const rasterQueue)const noexcept;


private:
    const Core::GpuCompiledGraph::ReadView& m_compiledGraph;
};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
