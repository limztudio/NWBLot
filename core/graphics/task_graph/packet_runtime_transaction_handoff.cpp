// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "packet_runtime.h"

#include "task_graph.h"

#include <core/graphics/backend_selection.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_CORE_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


QueueSubmissionToken GpuGraphSubmissionTransaction::packetToken(const GpuSubmissionPacketId& packet)const noexcept{
    ScopedLock lock(m_mutex);
    if(
        !m_valid
        || !packet.valid()
        || packet.generation != m_planGeneration
        || packet.index >= m_packets.size()
    )
        return {};
    const GpuPacketRuntime& runtime = m_packets[packet.index];
    return runtime.state == GpuPacketRuntimeState::Accepted ? runtime.token : QueueSubmissionToken{};
}


QueueSubmissionToken GpuGraphSubmissionTransaction::taskToken(
    const GpuCompiledGraph& compiledGraph,
    const GpuTaskId task
)const noexcept{
    if(!validFor(compiledGraph))
        return {};
    return packetToken(compiledGraph.packetForTask(task));
}
GpuTaskGraphExternalResourceHandoff GpuGraphSubmissionTransaction::externalResourceHandoff(
    const GpuTaskGraph& graph,
    const GpuCompiledGraph& compiledGraph,
    const GpuRecordedGraph& recordedGraph,
    const GpuGraphResourceId resource
)const noexcept{
    if(
        !matchesRecordedAttempt(graph, compiledGraph, recordedGraph)
    )
        return {};

    const GpuCompiledExternalResourceExport* const exportInfo = compiledGraph.externalResourceExport(resource);
    const GpuCompiledExternalResourceExportSource* const sources = exportInfo
        ? compiledGraph.externalResourceExportSources(*exportInfo)
        : nullptr
    ;
    if(!exportInfo || !sources || exportInfo->sourceCount == 0u)
        return {};

    ExternalResourceHandoffScratch* scratch = nullptr;
    for(ExternalResourceHandoffScratch& candidate : m_externalResourceHandoffScratch){
        if(candidate.resource == resource){
            scratch = &candidate;
            break;
        }
    }
    if(!scratch)
        return {};
    scratch->reset();

    const auto appendProducer = [&](const GpuCompiledExternalResourceExportSource& source){
        const GpuSubmissionPacketId packet = compiledGraph.packetForTask(source.producerTask);
        const QueueSubmissionToken producerToken = taskToken(compiledGraph, source.producerTask);
        if(
            !packet.valid()
            || !source.sourceQueue.valid()
            || !producerToken.valid()
            || !producerToken.matchesPhysicalQueue(
                source.sourceQueue.index,
                source.sourceQueue.deviceGeneration
            )
            || !recordedGraph.taskFinalStateSeed(compiledGraph, source.producerTask)
        )
            return false;

        bool foundProducer = false;
        for(const GpuTaskGraphExternalResourceHandoffProducer& producer : scratch->producers){
            if(compiledGraph.packetForTask(producer.producerTask) != packet)
                continue;
            if(
                producer.sourceQueue != source.sourceQueue
                || producer.token.queue != producerToken.queue
                || producer.token.value != producerToken.value
                || producer.token.physicalQueueIndex != producerToken.physicalQueueIndex
                || producer.token.deviceGeneration != producerToken.deviceGeneration
            )
                return false;
            foundProducer = true;
            break;
        }
        if(!foundProducer){
            scratch->producers.push_back(GpuTaskGraphExternalResourceHandoffProducer{
                .producerTask = source.producerTask,
                .sourceQueue = source.sourceQueue,
                .token = producerToken,
            });
        }
        scratch->terminalRanges.push_back(GpuTaskGraphExternalResourceHandoffRange{
            .range = source.range,
            .producerTask = source.producerTask,
            .sourceQueue = source.sourceQueue,
            .token = producerToken,
        });

        for(QueueSubmissionToken& wait : scratch->waitTokens){
            if(
                wait.physicalQueueIndex != producerToken.physicalQueueIndex
                || wait.deviceGeneration != producerToken.deviceGeneration
            )
                continue;
            if(wait.queue != producerToken.queue)
                return false;
            if(producerToken.value > wait.value)
                wait = producerToken;
            return true;
        }
        scratch->waitTokens.push_back(producerToken);
        return true;
    };

    for(u32 sourceIndex = 0u; sourceIndex < exportInfo->sourceCount; ++sourceIndex){
        if(!appendProducer(sources[sourceIndex]))
            return {};
    }
    if(scratch->producers.empty() || scratch->waitTokens.empty())
        return {};

    const CommandListResourceStateHandoff* stateSource = nullptr;
    if(scratch->producers.size() == 1u){
        stateSource = recordedGraph.taskFinalStateSeed(
            compiledGraph,
            scratch->producers[0u].producerTask
        );
    }
    else{
        const GpuTaskGraphResourceView graphResource = graph.resourceAt(resource.index);
        if(graphResource.id != resource)
            return {};

        const CommandListResourceStateHandoff* firstSourceStates = nullptr;
        for(u32 sourceIndex = 0u; sourceIndex < exportInfo->sourceCount; ++sourceIndex){
            const GpuCompiledExternalResourceExportSource& source = sources[sourceIndex];
            const CommandListResourceStateHandoff* const sourceStates = recordedGraph.taskFinalStateSeed(
                compiledGraph,
                source.producerTask
            );
            if(!sourceStates || !sourceStates->validForDeviceGeneration(compiledGraph.deviceGeneration()))
                return {};
            if(!firstSourceStates)
                firstSourceStates = sourceStates;

            CommandListResourceStateHandoff& stateSubset = scratch->stateBranches.emplace_back(scratch->m_arena);
            switch(graphResource.type){
            case GpuGraphResourceType::Texture:{
                Texture* const texture = graph.textureForResource(resource);
                if(!texture || !stateSubset.buildTextureRangeSubset(
                    *sourceStates,
                    texture,
                    source.range.textureSubresources
                ))
                    return {};
                break;
            }
            case GpuGraphResourceType::Buffer:{
                Buffer* const buffer = graph.bufferForResource(resource);
                Buffer* const buffers[] = { buffer };
                if(!buffer || !stateSubset.buildResourceSubset(
                    *sourceStates,
                    nullptr,
                    0u,
                    buffers,
                    LengthOf(buffers)
                ))
                    return {};
                break;
            }
            case GpuGraphResourceType::AccelStruct:{
                RayTracingAccelStruct* const accelStruct = graph.accelStructForResource(resource);
                Buffer* const backingBuffer = accelStruct ? accelStruct->getBackingBuffer() : nullptr;
                Buffer* const buffers[] = { backingBuffer };
                if(!backingBuffer || !stateSubset.buildResourceSubset(
                    *sourceStates,
                    nullptr,
                    0u,
                    buffers,
                    LengthOf(buffers)
                ))
                    return {};
                break;
            }
            default:
                return {};
            }
            // A terminal export reasserts its required final state, so an absent range means the recorded packet
            // cannot safely publish this source. Never reconstruct it from a descriptor creation state.
            if(stateSubset.empty())
                return {};
        }
        // Start from a valid, resource-empty base.  This lets buildFanIn verify that independent terminal ranges
        // are actually disjoint (including ownership metadata) rather than treating an earlier branch as the base
        // and accidentally allowing a later branch to overwrite it.
        scratch->stateBranchPointers.clear();
        scratch->stateBranchPointers.reserve(scratch->stateBranches.size());
        for(const CommandListResourceStateHandoff& branch : scratch->stateBranches)
            scratch->stateBranchPointers.push_back(&branch);
        if(
            !firstSourceStates
            || !scratch->stateSource.buildResourceSubset(*firstSourceStates, nullptr, 0u, nullptr, 0u)
            || !scratch->stateMerge.buildFanIn(
                scratch->stateSource,
                scratch->stateBranchPointers.data(),
                scratch->stateBranchPointers.size()
            )
            || !scratch->stateSource.copyFrom(scratch->stateMerge)
        )
            return {};
        stateSource = &scratch->stateSource;
    }
    if(!stateSource || !stateSource->validForDeviceGeneration(compiledGraph.deviceGeneration()))
        return {};

    GpuTaskGraphExternalResourceHandoff handoff;
    handoff.planGeneration = compiledGraph.planGeneration();
    handoff.resource = exportInfo->resource;
    handoff.destinationQueue = exportInfo->destinationQueue;
    handoff.finalState = exportInfo->finalState;
    handoff.producers = scratch->producers.data();
    handoff.producerCount = scratch->producers.size();
    handoff.waitTokens = scratch->waitTokens.data();
    handoff.waitTokenCount = scratch->waitTokens.size();
    handoff.terminalRangeCount = exportInfo->sourceCount;
    handoff.terminalRanges = scratch->terminalRanges.data();
    handoff.stateSource = stateSource;
    if(handoff.producerCount == 1u){
        handoff.producerTask = handoff.producers[0u].producerTask;
        handoff.sourceQueue = handoff.producers[0u].sourceQueue;
        handoff.token = handoff.producers[0u].token;
    }
    return handoff.validFor(compiledGraph) ? handoff : GpuTaskGraphExternalResourceHandoff{};
}

GpuTaskGraphExternalResourceHandoff GpuGraphSubmissionTransaction::externalResourceHandoff(
    const GpuCompiledGraph& compiledGraph,
    const GpuRecordedGraph& recordedGraph,
    const GpuGraphResourceId resource
)const noexcept{
    if(
        !validFor(compiledGraph)
        || !recordedGraph.validFor(compiledGraph)
        || m_recordingAttemptGeneration == 0u
        || m_recordingAttemptGeneration != recordedGraph.recordingAttemptGeneration()
    )
        return {};

    const GpuCompiledExternalResourceExport* const exportInfo = compiledGraph.externalResourceExport(resource);
    const GpuCompiledExternalResourceExportSource* const sources = exportInfo
        ? compiledGraph.externalResourceExportSources(*exportInfo)
        : nullptr
    ;
    if(
        !exportInfo
        || !sources
        || !exportInfo->producerTask.valid()
        || !exportInfo->sourceQueue.valid()
        || exportInfo->sourceCount == 0u
    )
        return {};

    ExternalResourceHandoffScratch* scratch = nullptr;
    for(ExternalResourceHandoffScratch& candidate : m_externalResourceHandoffScratch){
        if(candidate.resource == resource){
            scratch = &candidate;
            break;
        }
    }
    if(!scratch)
        return {};
    scratch->reset();

    const QueueSubmissionToken producerToken = taskToken(compiledGraph, exportInfo->producerTask);
    const CommandListResourceStateHandoff* const stateSource = recordedGraph.taskFinalStateSeed(
        compiledGraph,
        exportInfo->producerTask
    );
    if(
        !producerToken.valid()
        || !producerToken.matchesPhysicalQueue(
            exportInfo->sourceQueue.index,
            exportInfo->sourceQueue.deviceGeneration
        )
        || !stateSource
        || !stateSource->validForDeviceGeneration(compiledGraph.deviceGeneration())
    )
        return {};

    scratch->producers.push_back(GpuTaskGraphExternalResourceHandoffProducer{
        .producerTask = exportInfo->producerTask,
        .sourceQueue = exportInfo->sourceQueue,
        .token = producerToken,
    });
    for(u32 sourceIndex = 0u; sourceIndex < exportInfo->sourceCount; ++sourceIndex){
        const GpuCompiledExternalResourceExportSource& source = sources[sourceIndex];
        if(
            source.sourceQueue != exportInfo->sourceQueue
            || compiledGraph.packetForTask(source.producerTask)
                != compiledGraph.packetForTask(exportInfo->producerTask)
        )
            return {};
        scratch->terminalRanges.push_back(GpuTaskGraphExternalResourceHandoffRange{
            .range = source.range,
            .producerTask = source.producerTask,
            .sourceQueue = source.sourceQueue,
            .token = producerToken,
        });
    }
    scratch->waitTokens.push_back(producerToken);
    GpuTaskGraphExternalResourceHandoff handoff;
    handoff.planGeneration = compiledGraph.planGeneration();
    handoff.resource = exportInfo->resource;
    handoff.producerTask = exportInfo->producerTask;
    handoff.sourceQueue = exportInfo->sourceQueue;
    handoff.destinationQueue = exportInfo->destinationQueue;
    handoff.finalState = exportInfo->finalState;
    handoff.token = producerToken;
    handoff.producers = scratch->producers.data();
    handoff.producerCount = scratch->producers.size();
    handoff.waitTokens = scratch->waitTokens.data();
    handoff.waitTokenCount = scratch->waitTokens.size();
    handoff.terminalRangeCount = exportInfo->sourceCount;
    handoff.terminalRanges = scratch->terminalRanges.data();
    handoff.stateSource = stateSource;
    return handoff.validFor(compiledGraph) ? handoff : GpuTaskGraphExternalResourceHandoff{};
}

const QueueSubmissionToken* GpuGraphSubmissionTransaction::latestAcceptedToken(
    const GpuPhysicalQueueId& queue
)const noexcept{
    // This borrowed inspection pointer is intended for a caller that serializes reset/query access. Submission and
    // cancellation paths use value copies instead, so they never retain transaction-owned vector storage.
    for(const LatestAcceptedQueueToken& latest : m_latestAcceptedQueueTokens){
        if(latest.queue == queue && latest.token.valid())
            return &latest.token;
    }
    return nullptr;
}

bool GpuGraphSubmissionTransaction::appendAcceptedQueueFrontierWaitTokens(
    const GpuPhysicalQueueId& destinationQueue,
    Vector<QueueSubmissionToken, Alloc::ScratchArena>& outTokens
)const{
    ScopedLock lock(m_mutex);
    if(!m_valid || !destinationQueue.valid() || destinationQueue.deviceGeneration != m_deviceGeneration)
        return false;

    for(const LatestAcceptedQueueToken& latest : m_latestAcceptedQueueTokens){
        if(latest.queue == destinationQueue)
            continue;
        if(!latest.queue.valid() || latest.queue.deviceGeneration != m_deviceGeneration || !latest.token.valid())
            return false;
        // The transaction holds one newest accepted token per physical queue, so this cannot duplicate a producer
        // even when a queue accepted several packets before the recovery tail is armed.
        outTokens.push_back(latest.token);
    }
    return true;
}

const GpuPacketRuntime* GpuGraphSubmissionTransaction::packetRuntime(
    const GpuSubmissionPacketId& packet
)const noexcept{
    // See latestAcceptedToken(): test/diagnostic inspection must serialize reset/query access around this borrowed
    // pointer. Runtime submission paths query packet tokens by value under m_mutex.
    if(!m_valid || !packet.valid() || packet.generation != m_planGeneration || packet.index >= m_packets.size())
        return nullptr;
    return &m_packets[packet.index];
}


GpuTaskGraphRuntimeStatistics CollectGpuTaskGraphRuntimeStatistics(
    const GpuCompiledGraph& compiledGraph,
    const GpuRecordedGraph& recordedGraph,
    const GpuGraphSubmissionTransaction& transaction
)noexcept{
    return GpuTaskGraphRuntimeStatistics{
        .compile = compiledGraph.compileStatistics(),
        .recording = recordedGraph.recordingStatistics(compiledGraph),
        .submission = transaction.submissionStatistics(),
    };
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_CORE_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

