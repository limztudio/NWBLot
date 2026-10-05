// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "compiled_graph.h"
#include "task_graph.h"

#include <global/hash_utils.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_CORE_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace __hidden_gpu_compiled_graph_statistics{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


using OwnershipTransferKey = NotNull<const GpuCompiledOwnershipTransfer*>;

struct OwnershipTransferSignatureHasher{
    usize operator()(const OwnershipTransferKey& key)const{
        const GpuCompiledOwnershipTransfer& transfer = *key;
        usize hash = Hasher<u64>{}(transfer.resource.generation);
        HashCombine(hash, transfer.resource.index);
        HashCombine(hash, transfer.route);
        HashCombine(hash, transfer.sourceTask.generation);
        HashCombine(hash, transfer.sourceTask.index);
        HashCombine(hash, transfer.destinationTask.generation);
        HashCombine(hash, transfer.destinationTask.index);
        HashCombine(hash, transfer.sourceQueue.index);
        HashCombine(hash, transfer.sourceQueue.deviceGeneration);
        HashCombine(hash, transfer.destinationQueue.index);
        HashCombine(hash, transfer.destinationQueue.deviceGeneration);
        return hash;
    }
};

struct OwnershipTransferSignatureEqual{
    bool operator()(const OwnershipTransferKey& lhs, const OwnershipTransferKey& rhs)const noexcept{
        return
            lhs->resource == rhs->resource
            && lhs->route == rhs->route
            && lhs->sourceTask == rhs->sourceTask
            && lhs->destinationTask == rhs->destinationTask
            && lhs->sourceQueue == rhs->sourceQueue
            && lhs->destinationQueue == rhs->destinationQueue
        ;
    }
};

struct OwnershipResourceHasher{
    usize operator()(const GpuGraphResourceId& resource)const{
        usize hash = Hasher<u64>{}(resource.generation);
        HashCombine(hash, resource.index);
        return hash;
    }
};

struct OwnershipResourceStatistics{
    usize lastAdviceQueueIndex = Limit<usize>::s_Max;
    u8 avoidableSignatureCount = 0u;
};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


void GpuCompiledGraph::buildPlanStatistics(
    const GpuTaskGraphDeclarationReadView& graph,
    Alloc::ScratchArena& scratchArena){
    using namespace __hidden_gpu_compiled_graph_statistics;

    GpuTaskGraphCompileStatistics& statistics = m_compileStatistics;
    m_physicalQueueCompileStatistics.reserve(m_queueTopology.size());
    for(const GpuPhysicalQueueInfo& queue : m_queueTopology){
        m_physicalQueueCompileStatistics.push_back({
            .graphGeneration = m_generation,
            .planGeneration = m_planGeneration,
            .queue = queue.id,
            .queueClass = queue.queueClass,
            .deviceGeneration = m_deviceGeneration,
        });
    }
    const auto countOwnershipBarriers = [](
        GpuTaskGraphPhysicalQueueCompileStatistics& physical,
        const GraphicsVector<GpuCompiledBarrier>& barriers,
        const u32 barrierOffset,
        const u32 barrierCount){
        if(barrierCount == 0u || barrierOffset > barriers.size() || barrierCount > barriers.size() - barrierOffset)
            return;

        const GpuCompiledBarrier* const taskBarriers = barriers.data() + barrierOffset;
        for(u32 barrierIndex = 0u; barrierIndex < barrierCount; ++barrierIndex){
            switch(taskBarriers[barrierIndex].type){
            case GpuCompiledBarrierType::TextureOwnershipRelease:
            case GpuCompiledBarrierType::BufferOwnershipRelease:
            case GpuCompiledBarrierType::AccelStructOwnershipRelease:
                ++physical.ownershipReleaseBarrierCount;
                break;
            case GpuCompiledBarrierType::TextureOwnershipAcquire:
            case GpuCompiledBarrierType::BufferOwnershipAcquire:
            case GpuCompiledBarrierType::AccelStructOwnershipAcquire:
                ++physical.ownershipAcquireBarrierCount;
                break;
            default:
                break;
            }
        }
    };
    for(const GpuCompiledTask& compiledTask : m_tasks){
        const GpuTaskGraphTaskView task = graph.taskAt(compiledTask.task.index);
        statistics.resourceUseCount += task.resourceUseCount;
        statistics.directResourceUseCount += task.directResourceUseCount;
        statistics.declaredResourceSetUseCount += task.declaredResourceSetUseCount;
        statistics.expandedResourceSetMemberUseCount += task.expandedResourceSetMemberUseCount;
        if(task.hasPayload){
            ++statistics.payloadObjectCount;
            statistics.payloadObjectBytes += task.payloadObjectSize;
        }
        if(compiledTask.packetizationDecision < GpuTaskPacketizationDecision::kCount)
            ++statistics.packetizationDecisionCounts[compiledTask.packetizationDecision];

        const GpuPhysicalQueueInfo* const queue = queueInfo(compiledTask.queue);
        if(queue && queue->queueClass < CommandQueue::kCount){
            ++statistics.taskCountByQueueClass[queue->queueClass];
            auto& physical = m_physicalQueueCompileStatistics[static_cast<usize>(queue - m_queueTopology.data())];
            ++physical.taskCount;
            physical.prologueBarrierCount += compiledTask.prologueBarrierCount;
            physical.epilogueBarrierCount += compiledTask.epilogueBarrierCount;
            countOwnershipBarriers(
                physical, m_prologueBarriers, compiledTask.prologueBarrierOffset, compiledTask.prologueBarrierCount
            );
            countOwnershipBarriers(
                physical, m_epilogueBarriers, compiledTask.epilogueBarrierOffset, compiledTask.epilogueBarrierCount
            );
        }
    }
    for(usize packetIndex = 0u; packetIndex < m_packets.size(); ++packetIndex){
        const GpuSubmissionPacket& packet = m_packets[packetIndex];
        if(packet.taskCount > 1u)
            statistics.mergedTaskCount += packet.taskCount - 1u;
        if(packet.recordingFrontier != Limit<u32>::s_Max)
            statistics.recordingFrontierCount = Max(
                statistics.recordingFrontierCount,
                static_cast<usize>(packet.recordingFrontier) + 1u
            );

        const GpuPhysicalQueueInfo* const queue = queueInfo(packet.queue);
        if(queue && queue->queueClass < CommandQueue::kCount){
            ++statistics.packetCountByQueueClass[queue->queueClass];
            auto& physical = m_physicalQueueCompileStatistics[static_cast<usize>(queue - m_queueTopology.data())];
            ++physical.packetCount;
            if(packet.taskCount > 1u)
                physical.mergedTaskCount += packet.taskCount - 1u;
        }

        const GpuPacketDependency* const dependencies = packet.dependencyCount > 0u
            ? m_packetDependencies.data() + packet.dependencyOffset
            : nullptr
        ;
        for(u32 dependencyIndex = 0u; dependencies && dependencyIndex < packet.dependencyCount; ++dependencyIndex){
            const GpuPacketDependency& dependency = dependencies[dependencyIndex];
            if(
                !dependency.producer.valid()
                || dependency.producer.generation != m_planGeneration
                || dependency.producer.index >= m_packets.size()
            )
                continue;
            const GpuSubmissionPacket& producer = m_packets[dependency.producer.index];
            if(producer.queue == packet.queue)
                continue;

            ++statistics.crossQueuePacketDependencyCount;
            const GpuPhysicalQueueInfo* const producerQueue = queueInfo(producer.queue);
            if(producerQueue && queue && producerQueue->familyIndex != queue->familyIndex)
                ++statistics.crossFamilyPacketDependencyCount;
        }
    }
    const auto countBarriers = [&](const GraphicsVector<GpuCompiledBarrier>& barriers){
        for(const GpuCompiledBarrier& barrier : barriers){
            switch(barrier.type){
            case GpuCompiledBarrierType::TextureTransition:
            case GpuCompiledBarrierType::BufferTransition:
            case GpuCompiledBarrierType::AccelStructTransition:
                ++statistics.transitionBarrierCount;
                break;
            case GpuCompiledBarrierType::TextureUav:
            case GpuCompiledBarrierType::BufferUav:
            case GpuCompiledBarrierType::AccelStructUav:
                ++statistics.uavBarrierCount;
                break;
            case GpuCompiledBarrierType::TextureOwnershipRelease:
            case GpuCompiledBarrierType::BufferOwnershipRelease:
            case GpuCompiledBarrierType::AccelStructOwnershipRelease:
                ++statistics.ownershipReleaseBarrierCount;
                break;
            case GpuCompiledBarrierType::TextureOwnershipAcquire:
            case GpuCompiledBarrierType::BufferOwnershipAcquire:
            case GpuCompiledBarrierType::AccelStructOwnershipAcquire:
                ++statistics.ownershipAcquireBarrierCount;
                break;
            case GpuCompiledBarrierType::TextureStateExport:
            case GpuCompiledBarrierType::BufferStateExport:
            case GpuCompiledBarrierType::AccelStructStateExport:
                ++statistics.stateExportBarrierCount;
                break;
            default:
                break;
            }
        }
    };
    countBarriers(m_prologueBarriers);
    countBarriers(m_epilogueBarriers);
    if(m_ownershipTransfers.empty())
        return;

    HashSet<OwnershipTransferKey, Alloc::ScratchArena, OwnershipTransferSignatureHasher, OwnershipTransferSignatureEqual> signatures(
        0u, OwnershipTransferSignatureHasher(), OwnershipTransferSignatureEqual(), scratchArena
    );
    HashMap<GpuGraphResourceId, OwnershipResourceStatistics, Alloc::ScratchArena, OwnershipResourceHasher, EqualTo<GpuGraphResourceId>> resources(
        0u, OwnershipResourceHasher(), EqualTo<GpuGraphResourceId>(), scratchArena
    );
    signatures.reserve(m_ownershipTransfers.size());
    resources.reserve(Min(graph.resourceCount(), m_ownershipTransfers.size()));
    const auto statisticsForQueue = [&](const GpuPhysicalQueueId& id) -> GpuTaskGraphPhysicalQueueCompileStatistics*{
        const GpuPhysicalQueueInfo* const queue = queueInfo(id);
        if(!queue || queue->queueClass >= CommandQueue::kCount)
            return nullptr;
        return &m_physicalQueueCompileStatistics[static_cast<usize>(queue - m_queueTopology.data())];
    };
    statistics.logicalOwnershipTransferCount = m_ownershipTransfers.size();
    for(const GpuCompiledOwnershipTransfer& transfer : m_ownershipTransfers){
        if(transfer.route < GpuOwnershipTransferRoute::kCount)
            ++statistics.logicalOwnershipTransferCountByRoute[transfer.route];
        if(transfer.concurrentSharingCouldAvoid)
            ++statistics.concurrentSharingCouldAvoidTransferCount;

        GpuTaskGraphPhysicalQueueCompileStatistics* const source = statisticsForQueue(transfer.sourceQueue);
        GpuTaskGraphPhysicalQueueCompileStatistics* const destination = statisticsForQueue(transfer.destinationQueue);
        if(source)
            ++source->outgoingLogicalOwnershipTransferCount;
        if(destination)
            ++destination->incomingLogicalOwnershipTransferCount;

        const auto resourceInsertion = resources.try_emplace(transfer.resource);
        if(!signatures.insert(OwnershipTransferKey{ &transfer }).second)
            continue;

        ++statistics.logicalOwnershipTransferSignatureCount;
        const bool repeatedSignature = !resourceInsertion.second;
        if(repeatedSignature)
            ++statistics.repeatedOwnershipTransferSignatureCount;
        if(source){
            ++source->outgoingLogicalOwnershipTransferSignatureCount;
            if(repeatedSignature)
                ++source->outgoingRepeatedOwnershipTransferSignatureCount;
        }
        if(destination){
            ++destination->incomingLogicalOwnershipTransferSignatureCount;
            if(repeatedSignature)
                ++destination->incomingRepeatedOwnershipTransferSignatureCount;
        }

        // Only the first occurrence of a signature contributes avoidability, even when later fragments differ.
        OwnershipResourceStatistics& resource = resourceInsertion.first.value();
        if(transfer.concurrentSharingCouldAvoid && resource.avoidableSignatureCount < 2u){
            ++resource.avoidableSignatureCount;
            if(
                resource.avoidableSignatureCount == 2u
                && transfer.resource.generation == graph.generation()
                && transfer.resource.index < graph.resourceCount()
                && graph.resourceAt(transfer.resource.index).id == transfer.resource
            )
                ++statistics.concurrentSharingAdviceResourceCount;
        }
    }
    for(usize queueIndex = 0u; queueIndex < m_physicalQueueCompileStatistics.size(); ++queueIndex){
        GpuTaskGraphPhysicalQueueCompileStatistics& physical = m_physicalQueueCompileStatistics[queueIndex];
        if(physical.queueClass >= CommandQueue::kCount)
            continue;
        for(const GpuCompiledOwnershipTransfer& transfer : m_ownershipTransfers){
            if(
                !transfer.concurrentSharingCouldAvoid
                || (transfer.sourceQueue != physical.queue && transfer.destinationQueue != physical.queue)
            )
                continue;

            const auto resource = resources.find(transfer.resource);
            if(resource == resources.end())
                continue;
            OwnershipResourceStatistics& resourceStatistics = resource.value();
            if(resourceStatistics.avoidableSignatureCount < 2u || resourceStatistics.lastAdviceQueueIndex == queueIndex)
                continue;
            resourceStatistics.lastAdviceQueueIndex = queueIndex;
            ++physical.concurrentSharingAdviceResourceCount;
        }
    }
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_CORE_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

