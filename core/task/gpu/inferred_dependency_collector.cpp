// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "inferred_dependency_collector.h"


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_CORE_BEGIN


namespace __hidden_inferred_dependency_collector{
inline constexpr usize s_IndexedInferredEdges = Limit<usize>::s_Max - 1u;
inline constexpr usize s_InlineInferredReasonCount = 8u;
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
    , m_dependencyPairs(0, GpuTaskDependencyPairHasher(), EqualTo<u64>(), arena)
    , m_nextInferredEdges(arena)
    , m_inferredEdgeIndices(0u, InferredDependencyHasher{ MakeNotNull(&inferredEdges) }, InferredDependencyEqual{ MakeNotNull(&inferredEdges) }, arena)
{}

void GpuInferredDependencyCollector::reserve(const usize expectedEdgeCount){
    m_expectedEdgeCount = expectedEdgeCount;
    m_dependencyPairs.reserve(expectedEdgeCount);
    m_nextInferredEdges.reserve(expectedEdgeCount);
    m_edges.reserve(expectedEdgeCount);
    m_inferredEdges.reserve(expectedEdgeCount);
}

void GpuInferredDependencyCollector::appendRaw(const GpuTaskDependencyEdge& edge){
    (void)appendRawEdge(edge);
}

GpuInferredDependencyCollector::DependencyPairIndices& GpuInferredDependencyCollector::appendRawEdge(const GpuTaskDependencyEdge& edge){
    const u64 pairKey = PackGpuTaskDependencyPairKey(edge.producer.index, edge.consumer.index);
    auto [pair, inserted] = m_dependencyPairs.try_emplace(pairKey, DependencyPairIndices{ m_edges.size() });
    if(inserted){
        m_edges.push_back(edge);
        if(edge.hazard == GpuTaskHazardType::Explicit)
            ++m_explicitEdgeCount;
    }
    else{
        GpuTaskDependencyEdge& existing = m_edges[pair->second.rawEdge];
        if(edge.hazard == GpuTaskHazardType::Explicit && existing.hazard != GpuTaskHazardType::Explicit){
            existing = edge;
            ++m_explicitEdgeCount;
        }
    }
    return pair.value();
}

void GpuInferredDependencyCollector::appendInferred(const GpuTaskDependencyEdge& edge){
    FOUNDATION_ASSERT(edge.hazard != GpuTaskHazardType::Explicit);

    DependencyPairIndices& pair = appendRawEdge(edge);
    b8 indexed = pair.firstInferredEdge == s_IndexedInferredEdges;
    if(!indexed){
        usize reasonCount = 0u;
        for(usize index = pair.firstInferredEdge; index != Limit<usize>::s_Max; index = m_nextInferredEdges[index]){
            const GpuTaskDependencyEdge& previous = m_inferredEdges[index];
            if(previous.resource == edge.resource && previous.resourceVersion == edge.resourceVersion && previous.hazard == edge.hazard)
                return;
            ++reasonCount;
        }
        if(reasonCount == s_InlineInferredReasonCount){
            if(m_inferredEdgeIndices.empty())
                m_inferredEdgeIndices.reserve(m_expectedEdgeCount / 2u + 1u);
            for(usize index = pair.firstInferredEdge; index != Limit<usize>::s_Max; index = m_nextInferredEdges[index]){
                if(!m_inferredEdgeIndices.insert(index).second)
                    FOUNDATION_FATAL_ASSERT_MSG(false, "Inferred dependency promotion requires unique existing reasons");
            }
            // Edge vectors cannot reach this reserved ordinal; it marks a pair promoted to indexed lookup.
            pair.firstInferredEdge = s_IndexedInferredEdges;
            indexed = true;
        }
    }
    const usize index = m_inferredEdges.size();
    m_inferredEdges.push_back(edge);
    // Only a duplicate candidate is removed; every indexed ordinal keeps its original immutable edge.
    if(indexed && !m_inferredEdgeIndices.insert(index).second){
        m_inferredEdges.pop_back();
        return;
    }
    if(pair.firstInferredEdge == Limit<usize>::s_Max)
        ++m_inferredEdgeCount;
    m_nextInferredEdges.push_back(indexed ? Limit<usize>::s_Max : pair.firstInferredEdge);
    if(!indexed)
        pair.firstInferredEdge = index;
    if(edge.hazard == GpuTaskHazardType::VersionDependency || edge.hazard == GpuTaskHazardType::VersionLifetime)
        ++m_resourceVersionEdgeCount;
}


NWB_CORE_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
