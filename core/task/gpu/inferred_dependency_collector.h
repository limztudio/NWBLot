// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#pragma once


#include "compiler.h"
#include "dependency_pair_hash.h"

#include <global/hash_utils.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_CORE_BEGIN


class GpuInferredDependencyCollector final : NoCopy{
private:
    struct RawEdgeMeta{
        u32 nextRawEdge = Limit<u32>::s_Max;
        u32 firstInferred = Limit<u32>::s_Max;
        u32 producerIndex = Limit<u32>::s_Max;
    };
    struct InferredDependencyHasher{
        NotNull<const GraphicsVector<GpuTaskDependencyEdge>*> edges;
        [[nodiscard]] usize operator()(usize index)const noexcept;
    };
    struct InferredDependencyEqual{
        NotNull<const GraphicsVector<GpuTaskDependencyEdge>*> edges;
        [[nodiscard]] b8 operator()(usize lhsIndex, usize rhsIndex)const noexcept;
    };

public:
    GpuInferredDependencyCollector(Alloc::ScratchArena& arena, GraphicsVector<GpuTaskDependencyEdge>& edges, GraphicsVector<GpuTaskDependencyEdge>& inferredEdges, usize& explicitEdgeCount, usize& inferredEdgeCount, usize& resourceVersionEdgeCount);
    void reserve(usize expectedEdgeCount, usize taskCount = 0u);
    void appendRaw(const GpuTaskDependencyEdge& edge);
    void appendInferred(const GpuTaskDependencyEdge& edge);

private:
    void ensureRawEdge(const GpuTaskDependencyEdge& edge, u32* outRawEdgeIndex = nullptr);
    GraphicsVector<GpuTaskDependencyEdge>& m_edges;
    GraphicsVector<GpuTaskDependencyEdge>& m_inferredEdges;
    usize& m_explicitEdgeCount;
    usize& m_inferredEdgeCount;
    usize& m_resourceVersionEdgeCount;
    Vector<u32, Alloc::ScratchArena> m_firstRawEdgeByConsumer;
    Vector<u8, Alloc::ScratchArena> m_consumerRawEdgeCount;
    Vector<RawEdgeMeta, Alloc::ScratchArena> m_rawEdgeMeta;
    Vector<u32, Alloc::ScratchArena> m_nextInferredEdges;
    HashMap<u64, u32, Alloc::ScratchArena, GpuTaskDependencyPairHasher, EqualTo<u64>> m_dependencyPairs;
    HashSet<usize, Alloc::ScratchArena, InferredDependencyHasher, InferredDependencyEqual> m_inferredEdgeIndices;
    usize m_expectedEdgeCount = 0u;
};


NWB_CORE_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

