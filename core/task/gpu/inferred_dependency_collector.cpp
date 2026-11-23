// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "inferred_dependency_collector.h"


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_CORE_BEGIN


namespace __hidden_inferred_dependency_collector{
inline constexpr u32 s_InvalidEdgeIndex = Limit<u32>::s_Max;
inline constexpr u32 s_IndexedInferredEdges = Limit<u32>::s_Max - 1u;
inline constexpr usize s_InlineInferredReasonCount = 8u;
inline constexpr u8 s_InlineConsumerRawEdgeLimit = 16u;
inline constexpr u8 s_PromotedConsumerMarker = 0xFFu;
};
using namespace __hidden_inferred_dependency_collector;


usize GpuInferredDependencyCollector::InferredDependencyHasher::operator()(const usize index)const noexcept{
    const GpuTaskDependencyEdge& edge = (*edges)[index];
    const u64 taskPair = PackGpuTaskDependencyPairKey(edge.producer.index, edge.consumer.index);
    usize hash = GpuTaskDependencyPairHasher{}(taskPair);
    HashCombine(hash, edge.resource.index);
    HashCombine(hash, edge.resourceVersion.index);
    HashCombine(hash, edge.hazard);
    return hash;
}

b8 GpuInferredDependencyCollector::InferredDependencyEqual::operator()(const usize lhsIndex, const usize rhsIndex)const noexcept{
    const GpuTaskDependencyEdge& lhs = (*edges)[lhsIndex];
    const GpuTaskDependencyEdge& rhs = (*edges)[rhsIndex];
    return lhs.producer == rhs.producer && lhs.consumer == rhs.consumer && lhs.resource == rhs.resource && lhs.resourceVersion == rhs.resourceVersion && lhs.hazard == rhs.hazard;
}

GpuInferredDependencyCollector::GpuInferredDependencyCollector(Alloc::ScratchArena& arena, GraphicsVector<GpuTaskDependencyEdge>& edges, GraphicsVector<GpuTaskDependencyEdge>& inferredEdges, usize& explicitEdgeCount, usize& inferredEdgeCount, usize& resourceVersionEdgeCount)
    : m_edges(edges), m_inferredEdges(inferredEdges), m_explicitEdgeCount(explicitEdgeCount), m_inferredEdgeCount(inferredEdgeCount), m_resourceVersionEdgeCount(resourceVersionEdgeCount)
    , m_firstRawEdgeByConsumer(arena)
    , m_consumerRawEdgeCount(arena)
    , m_rawEdgeMeta(arena)
    , m_nextInferredEdges(arena)
    , m_dependencyPairs(0, GpuTaskDependencyPairHasher(), EqualTo<u64>(), arena)
    , m_inferredEdgeIndices(0u, InferredDependencyHasher{ MakeNotNull(&inferredEdges) }, InferredDependencyEqual{ MakeNotNull(&inferredEdges) }, arena)
{}

void GpuInferredDependencyCollector::reserve(const usize expectedEdgeCount, const usize taskCount){
    m_expectedEdgeCount = expectedEdgeCount;
    if(taskCount != 0u && m_firstRawEdgeByConsumer.empty()){
        m_firstRawEdgeByConsumer.assign(taskCount, s_InvalidEdgeIndex);
        m_consumerRawEdgeCount.assign(taskCount, 0u);
    }
    else if(taskCount == 0u)
        m_dependencyPairs.reserve(expectedEdgeCount);
    m_rawEdgeMeta.reserve(expectedEdgeCount);
    m_nextInferredEdges.reserve(expectedEdgeCount);
    m_edges.reserve(expectedEdgeCount);
    m_inferredEdges.reserve(expectedEdgeCount);
}

void GpuInferredDependencyCollector::appendRaw(const GpuTaskDependencyEdge& edge){
    ensureRawEdge(edge);
}

void GpuInferredDependencyCollector::ensureRawEdge(const GpuTaskDependencyEdge& edge, u32* const outRawEdgeIndex){
    const u32 consumer = edge.consumer.index;
    const u32 producer = edge.producer.index;

    if(consumer < m_firstRawEdgeByConsumer.size() && m_consumerRawEdgeCount[consumer] != s_PromotedConsumerMarker){
        for(u32 edgeIndex = m_firstRawEdgeByConsumer[consumer]; edgeIndex != s_InvalidEdgeIndex; edgeIndex = m_rawEdgeMeta[edgeIndex].nextRawEdge){
            if(m_rawEdgeMeta[edgeIndex].producerIndex != producer)
                continue;
            GpuTaskDependencyEdge& existing = m_edges[edgeIndex];
            if(edge.hazard == GpuTaskHazardType::Explicit && existing.hazard != GpuTaskHazardType::Explicit){
                existing = edge;
                ++m_explicitEdgeCount;
            }
            if(outRawEdgeIndex)
                *outRawEdgeIndex = edgeIndex;
            return;
        }

        FOUNDATION_FATAL_ASSERT_MSG(m_edges.size() < s_IndexedInferredEdges, "GpuInferredDependencyCollector raw edge index overflow");
        const u32 newEdgeIndex = static_cast<u32>(m_edges.size());
        m_edges.push_back(edge);
        if(edge.hazard == GpuTaskHazardType::Explicit)
            ++m_explicitEdgeCount;

        u8& count = m_consumerRawEdgeCount[consumer];
        if(count < s_InlineConsumerRawEdgeLimit){
            m_rawEdgeMeta.push_back(RawEdgeMeta{ .nextRawEdge = m_firstRawEdgeByConsumer[consumer], .firstInferred = s_InvalidEdgeIndex, .producerIndex = producer });
            m_firstRawEdgeByConsumer[consumer] = newEdgeIndex;
            ++count;
            if(outRawEdgeIndex)
                *outRawEdgeIndex = newEdgeIndex;
            return;
        }

        if(m_dependencyPairs.empty())
            m_dependencyPairs.reserve(Max<usize>(m_expectedEdgeCount, static_cast<usize>(s_InlineConsumerRawEdgeLimit) * 2u));
        for(u32 edgeIndex = m_firstRawEdgeByConsumer[consumer]; edgeIndex != s_InvalidEdgeIndex; edgeIndex = m_rawEdgeMeta[edgeIndex].nextRawEdge){
            const u64 existingKey = PackGpuTaskDependencyPairKey(m_rawEdgeMeta[edgeIndex].producerIndex, consumer);
            m_dependencyPairs.emplace(existingKey, edgeIndex);
        }
        const u64 newKey = PackGpuTaskDependencyPairKey(producer, consumer);
        m_dependencyPairs.emplace(newKey, newEdgeIndex);
        m_rawEdgeMeta.push_back(RawEdgeMeta{ .producerIndex = producer });
        count = s_PromotedConsumerMarker;
        if(outRawEdgeIndex)
            *outRawEdgeIndex = newEdgeIndex;
        return;
    }

    const u64 pairKey = PackGpuTaskDependencyPairKey(producer, consumer);
    const u32 candidateIndex = static_cast<u32>(m_edges.size());
    if(m_dependencyPairs.empty() && m_expectedEdgeCount != 0u)
        m_dependencyPairs.reserve(m_expectedEdgeCount);
    auto [pair, inserted] = m_dependencyPairs.try_emplace(pairKey, candidateIndex);
    if(inserted){
        FOUNDATION_FATAL_ASSERT_MSG(m_edges.size() < s_IndexedInferredEdges, "GpuInferredDependencyCollector raw edge index overflow");
        m_edges.push_back(edge);
        if(edge.hazard == GpuTaskHazardType::Explicit)
            ++m_explicitEdgeCount;
        m_rawEdgeMeta.push_back(RawEdgeMeta{ .producerIndex = producer });
        if(outRawEdgeIndex)
            *outRawEdgeIndex = candidateIndex;
        return;
    }

    const u32 rawIndex = pair.value();
    GpuTaskDependencyEdge& existing = m_edges[rawIndex];
    if(edge.hazard == GpuTaskHazardType::Explicit && existing.hazard != GpuTaskHazardType::Explicit){
        existing = edge;
        ++m_explicitEdgeCount;
    }
    if(outRawEdgeIndex)
        *outRawEdgeIndex = rawIndex;
}

void GpuInferredDependencyCollector::appendInferred(const GpuTaskDependencyEdge& edge){
    FOUNDATION_ASSERT(edge.hazard != GpuTaskHazardType::Explicit);

    u32 rawEdgeIndex = 0u;
    ensureRawEdge(edge, &rawEdgeIndex);
    RawEdgeMeta& pair = m_rawEdgeMeta[rawEdgeIndex];
    b8 indexed = pair.firstInferred == s_IndexedInferredEdges;
    if(!indexed){
        usize reasonCount = 0u;
        for(u32 index = pair.firstInferred; index != s_InvalidEdgeIndex; index = m_nextInferredEdges[index]){
            const GpuTaskDependencyEdge& previous = m_inferredEdges[index];
            if(previous.resource == edge.resource && previous.resourceVersion == edge.resourceVersion && previous.hazard == edge.hazard)
                return;
            ++reasonCount;
        }
        if(reasonCount == s_InlineInferredReasonCount){
            if(m_inferredEdgeIndices.empty())
                m_inferredEdgeIndices.reserve(m_expectedEdgeCount / 2u + 1u);
            for(u32 index = pair.firstInferred; index != s_InvalidEdgeIndex; index = m_nextInferredEdges[index]){
                if(!m_inferredEdgeIndices.insert(index).second)
                    FOUNDATION_FATAL_ASSERT_MSG(false, "Inferred dependency promotion requires unique existing reasons");
            }
            pair.firstInferred = s_IndexedInferredEdges;
            indexed = true;
        }
    }
    FOUNDATION_FATAL_ASSERT_MSG(m_inferredEdges.size() < s_IndexedInferredEdges, "GpuInferredDependencyCollector inferred edge index overflow");
    const u32 index = static_cast<u32>(m_inferredEdges.size());
    m_inferredEdges.push_back(edge);
    if(indexed && !m_inferredEdgeIndices.insert(index).second){
        m_inferredEdges.pop_back();
        return;
    }
    if(pair.firstInferred == s_InvalidEdgeIndex)
        ++m_inferredEdgeCount;
    m_nextInferredEdges.push_back(indexed ? s_InvalidEdgeIndex : pair.firstInferred);
    if(!indexed)
        pair.firstInferred = index;
    if(edge.hazard == GpuTaskHazardType::VersionDependency || edge.hazard == GpuTaskHazardType::VersionLifetime)
        ++m_resourceVersionEdgeCount;
}


NWB_CORE_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

