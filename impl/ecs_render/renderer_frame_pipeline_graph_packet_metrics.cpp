// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include <impl/ecs_render/renderer_frame_pipeline.h>
#include <impl/ecs_render/kernel/timing_names.h>

#include <core/graphics/gpu_timing.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace __hidden_renderer_frame_pipeline_graph_packet_metrics{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


[[nodiscard]] b8 PreparePacketEnvelopeMetrics(
    const Core::GpuTaskGraph::DeclarationReadView& graph,
    const Core::GpuCompiledGraph::ReadView& compiledGraph,
    Core::GpuTimingRecorder& timingRecorder,
    const u64 sourceFrameIndex,
    Core::Alloc::ScratchArena& scratchArena
){
    const Core::GpuSubmissionPacketRange range = compiledGraph.packetTimingEnvelopeRange();
    if(!range.valid() || !compiledGraph.validPacketRange(range))
        return false;

    Vector<Core::GpuPacketEnvelopeMetricScope, Core::Alloc::ScratchArena> packetScopes{scratchArena};
    Vector<Core::GpuPacketEnvelopeMetricQueueOutput, Core::Alloc::ScratchArena> queueOutputs{scratchArena};
    packetScopes.reserve(range.packetCount);
    queueOutputs.reserve(range.packetCount);
    for(usize packetOffset = 0u; packetOffset < range.packetCount; ++packetOffset){
        const Core::GpuSubmissionPacketId packetID = compiledGraph.packetIdAt(range.first.index + packetOffset);
        const Core::GpuCompiledPacketView packetView = compiledGraph.packet(packetID);
        if(!packetView.valid() || !packetView.plan->recordsPacketEnvelopeTiming || packetView.plan->taskCount == 0u)
            return false;

        const auto packetScopeName = Core::GpuTaskPacketTimingScopeName(graph.taskAt(packetView.tasks[0u].index).identity);
        if(!packetScopeName)
            return false;
        packetScopes.push_back(Core::GpuPacketEnvelopeMetricScope{ .scopeName = *packetScopeName, .physicalQueue = packetView.plan->queue, });

        b8 hasQueueOutput = false;
        for(const Core::GpuPacketEnvelopeMetricQueueOutput& output : queueOutputs)
            hasQueueOutput = hasQueueOutput || output.physicalQueue == packetView.plan->queue;
        if(hasQueueOutput)
            continue;

        const auto internalIdleScopeName = RendererGpuTimingScope::DeferredGraphQueueInternalIdle(packetView.plan->queue, scratchArena);
        if(!internalIdleScopeName || !*internalIdleScopeName)
            return false;
        queueOutputs.push_back(Core::GpuPacketEnvelopeMetricQueueOutput{ .physicalQueue = packetView.plan->queue, .internalIdleScopeName = **internalIdleScopeName, });
    }

    return timingRecorder.preparePacketEnvelopeMetrics(
        sourceFrameIndex,
        MakeNotNull(static_cast<const Core::GpuPacketEnvelopeMetricScope*>(packetScopes.data())),
        packetScopes.size(),
        RendererGpuTimingScope::s_DeferredGraphQueueOverlap.identity,
        MakeNotNull(static_cast<const Core::GpuPacketEnvelopeMetricQueueOutput*>(queueOutputs.data())),
        queueOutputs.size()
    );
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


[[nodiscard]] b8 RendererFramePipeline::prepareDeferredGraphPacketEnvelopeMetrics(const Core::GpuTaskGraph::DeclarationReadView& graph, const Core::GpuCompiledGraph::ReadView& compiledGraph, Core::Alloc::ScratchArena& scratchArena){
    return __hidden_renderer_frame_pipeline_graph_packet_metrics::PreparePacketEnvelopeMetrics(graph, compiledGraph, m_graphics.gpuTiming(), m_graphics.getFrameIndex(), scratchArena);
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

