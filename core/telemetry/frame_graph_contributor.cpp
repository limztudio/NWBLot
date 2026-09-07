// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "frame_graph_contributor.h"


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_TELEMETRY_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace __hidden_frame_graph_contributor{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


[[nodiscard]] static u64 PhysicalQueueIdentity(const u32 ownerNodeIndex, const FrameGraphPhysicalQueueId queue)noexcept{
    return (static_cast<u64>(ownerNodeIndex) << 32u) | (static_cast<u64>(queue.index) << 16u) | queue.deviceGeneration;
}

[[nodiscard]] static u64 PacketIdentity(const u32 ownerNodeIndex, const u32 packetIndex)noexcept{
    return (static_cast<u64>(ownerNodeIndex) << 32u) | packetIndex;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


FrameGraphBuilder::FrameGraphBuilder(
    FrameGraphNodeDescs& nodes,
    FrameGraphEdgeDescs& edges,
    FrameGraphPendingNameEdges& pendingNameEdges,
    const u64 frameIndex
)
    : m_nodes(nodes)
    , m_edges(edges)
    , m_pendingNameEdges(pendingNameEdges)
    , m_physicalQueueIdentities(nodes.get_allocator().arena())
    , m_packetIdentities(nodes.get_allocator().arena())
    , m_frameIndex(frameIndex)
{}

FrameGraphBuilder::FrameGraphBuilder(
    FrameGraphNodeDescs& nodes,
    FrameGraphEdgeDescs& edges,
    FrameGraphPendingNameEdges& pendingNameEdges,
    FrameGraphPhysicalQueueRuntimeStatisticsRecords& physicalQueueRuntimeStatistics,
    const u64 frameIndex
)
    : FrameGraphBuilder(nodes, edges, pendingNameEdges, frameIndex)
{
    m_physicalQueueRuntimeStatistics = &physicalQueueRuntimeStatistics;
    m_physicalQueueIdentities.reserve(physicalQueueRuntimeStatistics.size());
    // Coalesce seeded identities without changing malformed source rows; the payload codec still validates those rows.
    for(const auto& record : physicalQueueRuntimeStatistics){
        m_physicalQueueIdentities.insert({
            __hidden_frame_graph_contributor::PhysicalQueueIdentity(record.ownerNodeIndex, record.statistics.queue)
        });
    }
}

FrameGraphBuilder::FrameGraphBuilder(
    FrameGraphNodeDescs& nodes,
    FrameGraphEdgeDescs& edges,
    FrameGraphPendingNameEdges& pendingNameEdges,
    FrameGraphPhysicalQueueRuntimeStatisticsRecords& physicalQueueRuntimeStatistics,
    FrameGraphPacketSubmissionStatisticsRecords& packetSubmissionStatistics,
    const u64 frameIndex
)
    : FrameGraphBuilder(nodes, edges, pendingNameEdges, physicalQueueRuntimeStatistics, frameIndex)
{
    m_packetSubmissionStatistics = &packetSubmissionStatistics;
    m_packetIdentities.reserve(packetSubmissionStatistics.size());
    for(const auto& record : packetSubmissionStatistics){
        m_packetIdentities.insert({
            __hidden_frame_graph_contributor::PacketIdentity(record.ownerNodeIndex, record.packetIndex)
        });
    }
}


bool FrameGraphBuilder::addPhysicalQueueRuntimeStatistics(
    const FrameGraphNodeHandle owner,
    const FrameGraphPhysicalQueueRuntimeStatistics& statistics
){
    if(
        !m_physicalQueueRuntimeStatistics
        || !owner.valid()
        || owner.index >= m_nodes.size()
        || m_nodes[owner.index].kind != FrameGraphNodeKind::Pass
        || !IsValidFrameGraphPhysicalQueueRuntimeStatisticsForOwner(
            statistics,
            m_nodes[owner.index].runtimeStatistics
        )
    )
        return false;

    const u64 identity = __hidden_frame_graph_contributor::PhysicalQueueIdentity(owner.index, statistics.queue);
    if(m_physicalQueueIdentities.contains(identity))
        return false;
    if(m_physicalQueueRuntimeStatistics->size() == m_physicalQueueRuntimeStatistics->max_size())
        return false;

    const FrameGraphPhysicalQueueRuntimeStatisticsRecord record{
        .ownerNodeIndex = owner.index,
        .statistics = statistics,
    };
    ContainerDetail::ReserveGrowingCapacity(*m_physicalQueueRuntimeStatistics, m_physicalQueueRuntimeStatistics->size() + 1u);
    if(!m_physicalQueueIdentities.insert(identity).second)
        return false;
    m_physicalQueueRuntimeStatistics->push_back(record);
    return true;
}

bool FrameGraphBuilder::addPacketSubmissionStatistics(
    const FrameGraphNodeHandle owner,
    const FrameGraphPacketSubmissionStatisticsRecord& statistics
){
    if(
        !m_packetSubmissionStatistics
        || !owner.valid()
        || owner.index >= m_nodes.size()
        || m_nodes[owner.index].kind != FrameGraphNodeKind::Pass
        || statistics.ownerNodeIndex != owner.index
        || !IsValidFrameGraphPacketSubmissionStatistics(statistics)
    )
        return false;

    const FrameGraphRuntimeStatistics& ownerStatistics = m_nodes[owner.index].runtimeStatistics;
    if(
        !IsValidFrameGraphRuntimeStatistics(ownerStatistics)
        || statistics.packetGeneration != ownerStatistics.planGeneration
        || statistics.packetIndex >= ownerStatistics.compile.packetCount
        || statistics.queue.deviceGeneration != ownerStatistics.deviceGeneration
        || statistics.taskCount > ownerStatistics.submission.acceptedTaskCount
        || statistics.commandListCount > ownerStatistics.submission.nativeCommandListCount
        || statistics.plannedWaitTokenCount > ownerStatistics.submission.plannedWaitTokenCount
        || statistics.sameQueueWaitElisionCount > ownerStatistics.submission.sameQueueWaitElisionCount
        || statistics.timelineWaitCount > ownerStatistics.submission.timelineWaitCount
        || statistics.mergedTimelineWaitCount > ownerStatistics.submission.mergedTimelineWaitCount
        || statistics.submissionSeconds > ownerStatistics.submission.submissionSeconds
        || (
            statistics.joinsAcceptedQueueFrontier
            && ownerStatistics.submission.acceptedFrontierSubmissionCount == 0u
        )
        || (statistics.recoverySubmission && ownerStatistics.submission.recoverySubmissionCount == 0u)
    )
        return false;

    const u64 identity = __hidden_frame_graph_contributor::PacketIdentity(owner.index, statistics.packetIndex);
    if(m_packetIdentities.contains(identity))
        return false;
    if(m_packetSubmissionStatistics->size() == m_packetSubmissionStatistics->max_size())
        return false;

    const FrameGraphPacketSubmissionStatisticsRecord record = statistics;
    ContainerDetail::ReserveGrowingCapacity(*m_packetSubmissionStatistics, m_packetSubmissionStatistics->size() + 1u);
    if(!m_packetIdentities.insert(identity).second)
        return false;
    m_packetSubmissionStatistics->push_back(record);
    return true;
}

void FrameGraphBuilder::addEdge(const FrameGraphNodeHandle from, const FrameGraphNodeHandle to, const FrameGraphEdgeKind::Enum kind, const u8 flags){
    if(!from.valid() || !to.valid())
        return;

    m_edges.push_back(FrameGraphEdgeDesc{
        .fromNodeIndex = from.index,
        .toNodeIndex = to.index,
        .kind = kind,
        .flags = flags,
    });
}

void FrameGraphBuilder::dependsOnByName(const FrameGraphNodeHandle from, const Name& dependencyName, const u8 flags){
    if(!from.valid())
        return;

    m_pendingNameEdges.push_back(FrameGraphPendingNameEdge{
        .toName = dependencyName,
        .fromNodeIndex = from.index,
        .kind = FrameGraphEdgeKind::DependsOn,
        .flags = flags,
    });
}

FrameGraphNodeHandle FrameGraphBuilder::addNode(
    const Name& name,
    const AStringView label,
    const FrameGraphNodeKind::Enum kind,
    const FrameGraphPassMetadata& metadata,
    const u8 flags
){
    const u32 index = static_cast<u32>(m_nodes.size());
    m_nodes.push_back(FrameGraphNodeDesc{
        .name = name,
        .label = label,
        .kind = kind,
        .flags = flags,
        .queueAssignment = metadata.queueAssignment,
        .compiledTask = metadata.compiledTask,
        .runtimeStatistics = metadata.runtimeStatistics,
    });
    return FrameGraphNodeHandle{ .index = index };
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_TELEMETRY_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

