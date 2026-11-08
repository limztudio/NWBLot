// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "presentation_endpoint_validator.h"
#include "compiler_internal.h"


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_CORE_BEGIN


b8 GpuPresentationEndpointValidator::validate(const GpuTaskGraph::DeclarationReadView& graph, const Span<const GpuTaskId> topologicalOrder, const Span<const usize> incomingOffsets, const Span<const u32> incomingProducers, Alloc::ScratchArena& scratchArena, GpuTaskGraphAnalysisDiagnostic& diagnostic){
    using namespace GpuTaskGraphCompilerDetail;
    const auto fail = [&](const GpuTaskGraphAnalysisStatus::Enum status, const GpuTaskId task = {}, const GpuTaskId relatedTask = {}, const GpuGraphResourceId resource = {})noexcept{
        diagnostic.status = status;
        diagnostic.task = task;
        diagnostic.relatedTask = relatedTask;
        diagnostic.resource = resource;
        diagnostic.resourceVersion = {};
        return false;
    };
    if(const GpuPresentEndpoint* const endpoint = graph.presentEndpoint()){
        NWB_ASSERT(graph.validTask(endpoint->producer) && graph.validResource(endpoint->backBuffer));

        const GpuTaskGraphTaskView producer = graph.taskAt(endpoint->producer.index);
        const GpuTaskGraphResourceView backBuffer = graph.resourceAt(endpoint->backBuffer.index);
        const Texture* const backBufferTexture = graph.textureForResource(endpoint->backBuffer);
        if(
            backBuffer.type != GpuGraphResourceType::Texture
            || !backBuffer.hasBackendResource
            || !backBufferTexture
            || backBuffer.externalFinalReleaseDestinationQueue.valid()
            || (backBuffer.initialState != ResourceStates::Unknown && backBuffer.initialState != ResourceStates::Present)
            || backBuffer.externalFinalState != ResourceStates::Present
        )
            return fail(GpuTaskGraphAnalysisStatus::InvalidPresentationEndpoint, producer.id, {}, backBuffer.id);

        Vector<u8, Alloc::ScratchArena> reachesProducer(graph.taskCount(), scratchArena);
        for(usize taskIndex = 0u; taskIndex < reachesProducer.size(); ++taskIndex)
            reachesProducer[taskIndex] = 0u;
        reachesProducer[endpoint->producer.index] = 1u;
        for(usize orderIndex = topologicalOrder.size(); orderIndex > 0u; --orderIndex){
            const GpuTaskId consumer = topologicalOrder[orderIndex - 1u];
            if(!reachesProducer[consumer.index])
                continue;
            for(usize edgeIndex = incomingOffsets[consumer.index]; edgeIndex < incomingOffsets[consumer.index + 1u]; ++edgeIndex)
                reachesProducer[incomingProducers[edgeIndex]] = 1u;
        }

        b8 hasBackBufferWriter = false;
        for(usize taskIndex = 0u; taskIndex < graph.taskCount(); ++taskIndex){
            const GpuTaskGraphTaskView task = graph.taskAt(taskIndex);
            for(usize useIndex = 0u; useIndex < task.resourceUseCount; ++useIndex){
                const GpuTaskResourceUse& use = task.resourceUses[useIndex];
                if(use.resource != endpoint->backBuffer)
                    continue;

                if(!reachesProducer[task.id.index]){
                    return fail(GpuTaskGraphAnalysisStatus::InvalidPresentationEndpoint, endpoint->producer, task.id, endpoint->backBuffer);
                }
                if(!IsWriteAccess(use.access))
                    continue;
                if(use.requiredState == ResourceStates::Unknown || (use.requiredState & ResourceStates::Present) != ResourceStates::Unknown){
                    return fail(GpuTaskGraphAnalysisStatus::InvalidPresentationEndpoint, endpoint->producer, task.id, endpoint->backBuffer);
                }
                hasBackBufferWriter = true;
            }
        }
        if(!hasBackBufferWriter)
            return fail(GpuTaskGraphAnalysisStatus::InvalidPresentationEndpoint, endpoint->producer, {}, endpoint->backBuffer);
    }
    return true;
}


NWB_CORE_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
