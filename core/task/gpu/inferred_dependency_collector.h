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
    struct DependencyPairIndices{
        usize rawEdge = 0u;
        usize firstInferredEdge = Limit<usize>::s_Max;
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
    void reserve(usize expectedEdgeCount);
    void appendRaw(const GpuTaskDependencyEdge& edge);
    void appendInferred(const GpuTaskDependencyEdge& edge);

private:
    [[nodiscard]] DependencyPairIndices& appendRawEdge(const GpuTaskDependencyEdge& edge);
    GraphicsVector<GpuTaskDependencyEdge>& m_edges;
    GraphicsVector<GpuTaskDependencyEdge>& m_inferredEdges;
    usize& m_explicitEdgeCount;
    usize& m_inferredEdgeCount;
    usize& m_resourceVersionEdgeCount;
    HashMap<u64, DependencyPairIndices, Alloc::ScratchArena, GpuTaskDependencyPairHasher, EqualTo<u64>> m_dependencyPairs;
    Vector<usize, Alloc::ScratchArena> m_nextInferredEdges;
    HashSet<usize, Alloc::ScratchArena, InferredDependencyHasher, InferredDependencyEqual> m_inferredEdgeIndices;
    usize m_expectedEdgeCount = 0u;
};


NWB_CORE_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
