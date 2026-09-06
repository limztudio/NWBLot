// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "packet_runtime.h"

#include "task_graph.h"

#include <core/graphics/backend_selection.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_CORE_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace __hidden_gpu_packet_runtime_transaction_handoff{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


[[nodiscard]] bool ValidForCompiledPlan(
    const GpuTaskGraphExternalResourceHandoff& handoff,
    const GpuCompiledGraph::ReadView& planAccess
)noexcept{
    if(
        !handoff.valid()
        || !planAccess.valid()
        || handoff.planGeneration != planAccess.planGeneration()
        || handoff.resource.generation != planAccess.generation()
        || handoff.destinationQueue.deviceGeneration != planAccess.deviceGeneration()
        || !planAccess.queueInfo(handoff.destinationQueue)
        || !handoff.stateSource->validForDeviceGeneration(planAccess.deviceGeneration())
    )
        return false;

    for(usize producerIndex = 0u; producerIndex < handoff.producerCount; ++producerIndex){
        const GpuTaskGraphExternalResourceHandoffProducer& producer = handoff.producers[producerIndex];
        const GpuCompiledTaskView compiledProducer = planAccess.findTask(producer.producerTask);
        const GpuPhysicalQueueInfo* const sourceQueueInfo = planAccess.queueInfo(producer.sourceQueue);
        if(
            !producer.producerTask.valid()
            || producer.producerTask.generation != planAccess.generation()
            || !compiledProducer.valid()
            || compiledProducer.plan->queue != producer.sourceQueue
            || !producer.sourceQueue.valid()
            || producer.sourceQueue.deviceGeneration != planAccess.deviceGeneration()
            || !sourceQueueInfo
            || !producer.token.valid()
            || producer.token.queue != sourceQueueInfo->queueClass
            || !producer.token.matchesPhysicalQueue(
                producer.sourceQueue.index,
                producer.sourceQueue.deviceGeneration
            )
        )
            return false;

        bool coveredByWait = false;
        for(usize waitIndex = 0u; waitIndex < handoff.waitTokenCount; ++waitIndex){
            const QueueSubmissionToken& wait = handoff.waitTokens[waitIndex];
            if(
                !wait.valid()
                || !wait.hasPhysicalQueueIdentity()
                || wait.deviceGeneration != planAccess.deviceGeneration()
            )
                return false;
            coveredByWait = coveredByWait || (
                wait.queue == producer.token.queue
                && wait.physicalQueueIndex == producer.token.physicalQueueIndex
                && wait.deviceGeneration == producer.token.deviceGeneration
                && wait.value >= producer.token.value
            );
        }
        if(!coveredByWait)
            return false;
    }

    for(usize rangeIndex = 0u; rangeIndex < handoff.terminalRangeCount; ++rangeIndex){
        const GpuTaskGraphExternalResourceHandoffRange& range = handoff.terminalRanges[rangeIndex];
        bool matchesProducer = false;
        for(usize producerIndex = 0u; producerIndex < handoff.producerCount; ++producerIndex){
            const GpuTaskGraphExternalResourceHandoffProducer& producer = handoff.producers[producerIndex];
            matchesProducer = matchesProducer || (
                planAccess.packetForTask(range.producerTask)
                    == planAccess.packetForTask(producer.producerTask)
                && range.sourceQueue == producer.sourceQueue
                && range.token.queue == producer.token.queue
                && range.token.value == producer.token.value
                && range.token.physicalQueueIndex == producer.token.physicalQueueIndex
                && range.token.deviceGeneration == producer.token.deviceGeneration
            );
        }
        if(!matchesProducer)
            return false;
    }

    if(handoff.producerCount != 1u){
        return !handoff.producerTask.valid()
            && !handoff.sourceQueue.valid()
            && !handoff.token.valid()
        ;
    }
    const GpuTaskGraphExternalResourceHandoffProducer& producer = handoff.producers[0u];
    return handoff.producerTask == producer.producerTask
        && handoff.sourceQueue == producer.sourceQueue
        && handoff.token.queue == producer.token.queue
        && handoff.token.value == producer.token.value
        && handoff.token.physicalQueueIndex == producer.token.physicalQueueIndex
        && handoff.token.deviceGeneration == producer.token.deviceGeneration
    ;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


struct GpuTaskGraphExternalResourceHandoffSnapshot::Storage final : NoCopy{
    GraphicsArena& arena;
    GpuTaskGraphExternalResourceHandoff handoff;
    CommandListResourceStateHandoff stateSource;
    CommandListResourceStateHandoff stateMerge;
    GraphicsVector<CommandListResourceStateHandoff> stateBranches;
    GraphicsVector<const CommandListResourceStateHandoff*> stateBranchPointers;
    GraphicsVector<GpuTaskGraphExternalResourceHandoffProducer> producers;
    GraphicsVector<GpuTaskGraphExternalResourceHandoffRange> terminalRanges;
    GraphicsVector<QueueSubmissionToken> waitTokens;
    u64 compiledObjectIdentity = 0u;


    explicit Storage(GraphicsArena& arena)
        : arena(arena)
        , stateSource(arena)
        , stateMerge(arena)
        , stateBranches(arena)
        , stateBranchPointers(arena)
        , producers(arena)
        , terminalRanges(arena)
        , waitTokens(arena)
    {}


    void reset()noexcept{
        handoff = {};
        stateSource.reset();
        stateMerge.reset();
        for(CommandListResourceStateHandoff& branch : stateBranches)
            branch.reset();
        stateBranchPointers.clear();
        producers.clear();
        terminalRanges.clear();
        waitTokens.clear();
        compiledObjectIdentity = 0u;
    }

    void prepare(const usize sourceCount){
        reset();
        stateBranchPointers.reserve(sourceCount);
        producers.reserve(sourceCount);
        terminalRanges.reserve(sourceCount);
        waitTokens.reserve(sourceCount);
        while(stateBranches.size() < sourceCount)
            stateBranches.emplace_back(arena);
        for(usize sourceIndex = 0u; sourceIndex < sourceCount; ++sourceIndex)
            stateBranchPointers.push_back(&stateBranches[sourceIndex]);
    }
};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


GpuTaskGraphExternalResourceHandoffSnapshot::GpuTaskGraphExternalResourceHandoffSnapshot(GraphicsArena& arena)
    : m_arena(arena)
    , m_activeStorage(MakeGlobalUnique<Storage>(arena, arena))
    , m_candidateStorage(MakeGlobalUnique<Storage>(arena, arena))
{}
GpuTaskGraphExternalResourceHandoffSnapshot::~GpuTaskGraphExternalResourceHandoffSnapshot() = default;


const GpuTaskGraphExternalResourceHandoff* GpuTaskGraphExternalResourceHandoffSnapshot::value()const noexcept{
    return m_activeStorage && m_activeStorage->handoff.valid() ? &m_activeStorage->handoff : nullptr;
}

bool GpuTaskGraphExternalResourceHandoffSnapshot::valid()const noexcept{
    return value() != nullptr;
}

bool GpuTaskGraphExternalResourceHandoffSnapshot::validFor(
    const GpuCompiledGraph& compiledGraph,
    const GpuCompiledGraph::ReadView& planAccess
)const noexcept{
    const GpuTaskGraphExternalResourceHandoff* const handoff = value();
    return planAccess.validFor(compiledGraph)
        && handoff
        && m_activeStorage->compiledObjectIdentity == planAccess.objectIdentity()
        && __hidden_gpu_packet_runtime_transaction_handoff::ValidForCompiledPlan(*handoff, planAccess)
    ;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


bool GpuGraphSubmissionTransaction::copyAcceptedPacketTokens(
    const GpuCompiledGraph::ReadView& compiledPlan,
    QueueSubmissionToken* const outTokens,
    const usize tokenCount,
    GpuGraphSubmissionAcceptanceSnapshot& outSnapshot
)const noexcept{
    if(!compiledPlan.valid())
        return false;
    if((tokenCount != 0u && !outTokens) || tokenCount != compiledPlan.packetCount())
        return false;

    NothrowScopedLock lock(m_mutex);
    if(
        !m_valid
        || m_generation != compiledPlan.generation()
        || m_planGeneration != compiledPlan.planGeneration()
        || m_deviceGeneration != compiledPlan.deviceGeneration()
        || m_packets.size() != compiledPlan.packetCount()
        || tokenCount != m_packets.size()
        || m_acceptanceRevision == 0u
    )
        return false;

    for(usize packetIndex = 0u; packetIndex < tokenCount; ++packetIndex){
        const PacketRuntime& runtime = m_packets[packetIndex];
        outTokens[packetIndex] = runtime.state == PacketRuntimeState::Accepted
            ? runtime.token
            : QueueSubmissionToken{}
        ;
    }
    outSnapshot = GpuGraphSubmissionAcceptanceSnapshot{
        .recordingAttemptGeneration = m_recordingAttemptGeneration,
        .acceptanceRevision = m_acceptanceRevision,
    };
    return true;
}

QueueSubmissionToken GpuGraphSubmissionTransaction::packetToken(const GpuSubmissionPacketId& packet)const noexcept{
    NothrowScopedLock lock(m_mutex);
    return packetTokenLocked(packet);
}


QueueSubmissionToken GpuGraphSubmissionTransaction::taskToken(
    const GpuCompiledGraph::ReadView& planAccess,
    const GpuTaskId task
)const noexcept{
    if(!planAccess.valid())
        return {};
    NothrowScopedLock lock(m_mutex);
    return taskTokenLocked(planAccess, task);
}

bool GpuGraphSubmissionTransaction::externalResourceHandoff(
    const GpuTaskGraph& graph,
    const GpuTaskGraph::DeclarationReadView& declarationAccess,
    const GpuCompiledGraph& compiledGraph,
    const GpuCompiledGraph::ReadView& planAccess,
    const GpuRecordedGraph& recordedGraph,
    const GpuGraphResourceId resource,
    GpuTaskGraphExternalResourceHandoffSnapshot& outSnapshot
)const{
    GpuRecordedGraph::ArtifactOperation artifactOperation(
        recordedGraph,
        GpuRecordedGraph::ArtifactOperationMode::Read
    );
    if(!artifactOperation.valid())
        return false;
    NothrowScopedLock lock(m_mutex);

    if(
        !declarationAccess.validFor(graph)
        || !planAccess.validFor(declarationAccess)
        || !planAccess.validFor(compiledGraph)
        || !validForLocked(planAccess)
        || m_recordingAttemptGeneration == 0u
        || !recordedGraph.validForWithinArtifactOperation(
            graph,
            declarationAccess,
            compiledGraph,
            planAccess,
            artifactOperation
        )
        || m_recordingAttemptGeneration
            != recordedGraph.recordingAttemptGenerationWithinArtifactOperation(artifactOperation)
    )
        return false;

    const GpuCompiledExternalResourceExportView exportView = planAccess.externalResourceExport(resource);
    if(
        !exportView.valid()
        || !outSnapshot.m_activeStorage
        || !outSnapshot.m_candidateStorage
    )
        return false;
    const GpuCompiledExternalResourceExport& exportInfo = *exportView.plan;
    const GpuCompiledExternalResourceExportSource* const sources = exportView.sources;

    GpuTaskGraphExternalResourceHandoffSnapshot::Storage& candidate = *outSnapshot.m_candidateStorage;
    candidate.prepare(exportInfo.sourceCount);

    const auto appendProducer = [&](const GpuCompiledExternalResourceExportSource& source){
        const GpuSubmissionPacketId packet = planAccess.packetForTask(source.producerTask);
        const QueueSubmissionToken producerToken = taskTokenLocked(planAccess, source.producerTask);
        if(
            !packet.valid()
            || !source.sourceQueue.valid()
            || !producerToken.valid()
            || !producerToken.matchesPhysicalQueue(
                source.sourceQueue.index,
                source.sourceQueue.deviceGeneration
            )
            || !recordedGraph.packetStateSeed(
                planAccess.packetForTask(source.producerTask),
                artifactOperation
            )
        )
            return false;

        bool foundProducer = false;
        for(const GpuTaskGraphExternalResourceHandoffProducer& producer : candidate.producers){
            if(planAccess.packetForTask(producer.producerTask) != packet)
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
            candidate.producers.push_back(GpuTaskGraphExternalResourceHandoffProducer{
                .producerTask = source.producerTask,
                .sourceQueue = source.sourceQueue,
                .token = producerToken,
            });
        }
        candidate.terminalRanges.push_back(GpuTaskGraphExternalResourceHandoffRange{
            .range = source.range,
            .producerTask = source.producerTask,
            .sourceQueue = source.sourceQueue,
            .token = producerToken,
        });

        for(QueueSubmissionToken& wait : candidate.waitTokens){
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
        candidate.waitTokens.push_back(producerToken);
        return true;
    };

    for(u32 sourceIndex = 0u; sourceIndex < exportInfo.sourceCount; ++sourceIndex){
        if(!appendProducer(sources[sourceIndex]))
            return false;
    }
    if(candidate.producers.empty() || candidate.waitTokens.empty())
        return false;

    if(candidate.producers.size() == 1u){
        const CommandListResourceStateHandoff* const sourceState = recordedGraph.packetStateSeed(
            planAccess.packetForTask(candidate.producers[0u].producerTask),
            artifactOperation
        );
        if(
            !sourceState
            || !sourceState->validForDeviceGeneration(planAccess.deviceGeneration())
            || !candidate.stateSource.copyFrom(*sourceState)
        )
            return false;
    }
    else{
        const GpuTaskGraphResourceView graphResource = declarationAccess.resourceAt(resource.index);
        if(graphResource.id != resource)
            return false;

        const CommandListResourceStateHandoff* firstSourceStates = nullptr;
        for(u32 sourceIndex = 0u; sourceIndex < exportInfo.sourceCount; ++sourceIndex){
            const GpuCompiledExternalResourceExportSource& source = sources[sourceIndex];
            const CommandListResourceStateHandoff* const sourceStates = recordedGraph.packetStateSeed(
                planAccess.packetForTask(source.producerTask),
                artifactOperation
            );
            if(!sourceStates || !sourceStates->validForDeviceGeneration(planAccess.deviceGeneration()))
                return false;
            if(!firstSourceStates)
                firstSourceStates = sourceStates;

            CommandListResourceStateHandoff& stateSubset = candidate.stateBranches[sourceIndex];
            switch(graphResource.type){
            case GpuGraphResourceType::Texture:{
                Texture* const texture = declarationAccess.textureForResource(resource);
                if(
                    !texture
                    || !sourceStates->coversTextureRangeWithOwnership(
                        texture,
                        source.range.textureSubresources,
                        source.sourceQueue,
                        exportInfo.destinationQueue
                    )
                    || !stateSubset.buildTextureRangeSubset(
                        *sourceStates,
                        texture,
                        source.range.textureSubresources
                    )
                )
                    return false;
                break;
            }
            case GpuGraphResourceType::Buffer:{
                Buffer* const buffer = declarationAccess.bufferForResource(resource);
                Buffer* const buffers[] = { buffer };
                if(!buffer || !stateSubset.buildResourceSubset(
                    *sourceStates,
                    nullptr,
                    0u,
                    buffers,
                    LengthOf(buffers)
                ))
                    return false;
                break;
            }
            case GpuGraphResourceType::AccelStruct:{
                RayTracingAccelStruct* const accelStruct = declarationAccess.accelStructForResource(resource);
                Buffer* const backingBuffer = accelStruct ? accelStruct->getBackingBuffer() : nullptr;
                Buffer* const buffers[] = { backingBuffer };
                if(!backingBuffer || !stateSubset.buildResourceSubset(
                    *sourceStates,
                    nullptr,
                    0u,
                    buffers,
                    LengthOf(buffers)
                ))
                    return false;
                break;
            }
            default:
                return false;
            }
            // A terminal export reasserts its required final state, so an absent range means the recorded packet
            // cannot safely publish this source. Never reconstruct it from a descriptor creation state.
            if(stateSubset.empty())
                return false;
        }
        // Start from a valid, resource-empty base.  This lets buildFanIn verify that independent terminal ranges
        // are actually disjoint (including ownership metadata) rather than treating an earlier branch as the base
        // and accidentally allowing a later branch to overwrite it.
        if(
            !firstSourceStates
            || !candidate.stateMerge.buildResourceSubset(*firstSourceStates, nullptr, 0u, nullptr, 0u)
            || !candidate.stateSource.buildFanIn(
                candidate.stateMerge,
                candidate.stateBranchPointers.data(),
                exportInfo.sourceCount,
                m_externalResourceHandoffBuildScratch
            )
        )
            return false;
    }
    if(!candidate.stateSource.validForDeviceGeneration(planAccess.deviceGeneration()))
        return false;

    candidate.handoff.planGeneration = planAccess.planGeneration();
    candidate.compiledObjectIdentity = planAccess.objectIdentity();
    candidate.handoff.resource = exportInfo.resource;
    candidate.handoff.destinationQueue = exportInfo.destinationQueue;
    candidate.handoff.finalState = exportInfo.finalState;
    candidate.handoff.producers = candidate.producers.data();
    candidate.handoff.producerCount = candidate.producers.size();
    candidate.handoff.waitTokens = candidate.waitTokens.data();
    candidate.handoff.waitTokenCount = candidate.waitTokens.size();
    candidate.handoff.terminalRangeCount = candidate.terminalRanges.size();
    candidate.handoff.terminalRanges = candidate.terminalRanges.data();
    candidate.handoff.stateSource = &candidate.stateSource;
    if(candidate.handoff.producerCount == 1u){
        candidate.handoff.producerTask = candidate.handoff.producers[0u].producerTask;
        candidate.handoff.sourceQueue = candidate.handoff.producers[0u].sourceQueue;
        candidate.handoff.token = candidate.handoff.producers[0u].token;
    }
    if(!__hidden_gpu_packet_runtime_transaction_handoff::ValidForCompiledPlan(candidate.handoff, planAccess))
        return false;

    static_assert(
        noexcept(Swap(outSnapshot.m_activeStorage, outSnapshot.m_candidateStorage)),
        "external handoff owner publication must be non-throwing"
    );
    Swap(outSnapshot.m_activeStorage, outSnapshot.m_candidateStorage);
    outSnapshot.m_candidateStorage->reset();
    return true;
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
        if(
            !latest.queue.valid()
            || latest.queue.deviceGeneration != m_deviceGeneration
            || !latest.token.valid()
            || !latest.token.matchesPhysicalQueue(latest.queue.index, latest.queue.deviceGeneration)
        )
            return false;
        // The transaction holds one newest accepted token per physical queue, so this cannot duplicate a producer
        // even when a queue accepted several packets before the recovery tail is armed.
        outTokens.push_back(latest.token);
    }
    return true;
}

QueueSubmissionToken GpuGraphSubmissionTransaction::packetTokenLocked(
    const GpuSubmissionPacketId& packet
)const noexcept{
    if(
        !m_valid
        || !packet.valid()
        || packet.generation != m_planGeneration
        || packet.index >= m_packets.size()
    )
        return {};
    const PacketRuntime& runtime = m_packets[packet.index];
    return runtime.state == PacketRuntimeState::Accepted ? runtime.token : QueueSubmissionToken{};
}

QueueSubmissionToken GpuGraphSubmissionTransaction::taskTokenLocked(
    const GpuCompiledGraph::ReadView& planAccess,
    const GpuTaskId task
)const noexcept{
    if(!validForLocked(planAccess))
        return {};
    return packetTokenLocked(planAccess.packetForTask(task));
}

GpuTaskGraphRuntimeStatistics CollectGpuTaskGraphRuntimeStatistics(
    const GpuCompiledGraph& compiledGraph,
    const GpuCompiledGraph::ReadView& planAccess,
    const GpuRecordedGraph& recordedGraph,
    const GpuGraphSubmissionTransaction& transaction
)noexcept{
    GpuRecordedGraph::ArtifactOperation artifactOperation(
        recordedGraph,
        GpuRecordedGraph::ArtifactOperationMode::Read
    );
    if(!artifactOperation.valid())
        return {};
    GpuGraphSubmissionTransaction::SubmissionOperation submissionOperation(
        transaction,
        GpuGraphSubmissionTransaction::SubmissionOperationMode::OrdinaryPacket,
        &artifactOperation
    );
    if(!submissionOperation.valid())
        return {};
    if(!planAccess.validFor(compiledGraph))
        return {};
    return GpuTaskGraphRuntimeStatistics{
        .compile = planAccess.compileStatistics(),
        .recording = recordedGraph.recordingStatistics(compiledGraph, planAccess),
        .submission = transaction.submissionStatistics(),
    };
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_CORE_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

