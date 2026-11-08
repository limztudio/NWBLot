// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "compiler_internal.h"
#include <core/task/gpu/compiler_analysis_types.adhd.h>

#include <global/bit_matrix.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_CORE_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace GpuTaskGraphCompilerDetail{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


GpuTaskSchedulingReachability::GpuTaskSchedulingReachability(Alloc::ScratchArena& scratchArena)noexcept
    : m_words(scratchArena)
    , m_topologicalRanks(scratchArena)
    , m_relatedWordRanges(scratchArena)
    , m_rowOffsets(scratchArena)
{}

b8 GpuTaskSchedulingReachability::reaches(const GpuTaskId& source, const GpuTaskId& destination)const noexcept{
    if(!m_valid || !source.valid() || !destination.valid() || source.generation != m_graphGeneration || destination.generation != m_graphGeneration || source.index >= m_taskCount || destination.index >= m_taskCount || source == destination)
        return false;
    if(m_totalOrder)
        return m_topologicalRanks[source.index] < m_topologicalRanks[destination.index];
    if(m_words.empty() || m_topologicalRanks[source.index] >= m_topologicalRanks[destination.index])
        return false;
    return hasRelatedBit(source.index, destination.index);
}

b8 GpuTaskSchedulingReachability::transitivelyIndependent(const GpuTaskId& lhs, const GpuTaskId& rhs)const noexcept{
    if(!m_valid || !lhs.valid() || !rhs.valid() || lhs.generation != m_graphGeneration || rhs.generation != m_graphGeneration || lhs.index >= m_taskCount || rhs.index >= m_taskCount || lhs == rhs)
        return false;
    if(m_totalOrder)
        return false;
    if(m_words.empty())
        return true;
    return !hasRelatedBit(lhs.index, rhs.index);
}

b8 GpuTaskSchedulingReachability::hasRelatedBit(const u32 sourceIndex, const u32 destinationIndex)const noexcept{
    const usize destinationWord = destinationIndex / s_BitsPerWord;
    const auto& range = m_relatedWordRanges[sourceIndex];
    if(destinationWord < range.begin || destinationWord >= range.end)
        return false;
    const usize wordIndex = m_rowOffsets.empty() ? sourceIndex * m_wordsPerRow + destinationWord : m_rowOffsets[sourceIndex] + destinationWord - range.begin;
    const u64 mask = static_cast<u64>(1u) << (destinationIndex % s_BitsPerWord);
    return (m_words[wordIndex] & mask) != 0u;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


b8 BuildGpuTaskSchedulingReachability(const GpuTaskGraph::DeclarationReadView& graph, const GpuTaskGraphAnalysis& analysis, GpuTaskSchedulingReachability& outReachability){
    const auto reset = [&outReachability]()noexcept{
        outReachability.m_words.clear();
        outReachability.m_topologicalRanks.clear();
        outReachability.m_relatedWordRanges.clear();
        outReachability.m_rowOffsets.clear();
        outReachability.m_totalOrder = false;
        outReachability.m_graphGeneration = 0u;
        outReachability.m_taskCount = 0u;
        outReachability.m_wordsPerRow = 0u;
        outReachability.m_valid = false;
    };
    reset();
    const auto fail = [&reset]()noexcept{
        reset();
        return false;
    };
    if(!analysis.validFor(graph))
        return fail();

    // Graph storage keeps task indices below u32 max, and valid analysis holds one order entry plus adjacency per task.
    const usize taskCount = graph.taskCount();
    NWB_ASSERT(taskCount <= static_cast<usize>(Limit<u32>::s_Max));
    if(analysis.schedulingEdges().empty()){
        outReachability.m_graphGeneration = graph.generation();
        outReachability.m_taskCount = taskCount;
        outReachability.m_valid = true;
        return true;
    }
    NWB_ASSERT(analysis.topologicalOrder().size() == taskCount);
    b8 totalOrder = taskCount > 1u;
    for(usize orderIndex = 0u; orderIndex < taskCount && totalOrder; ++orderIndex){
        const GpuTaskId source = analysis.topologicalOrder()[orderIndex];
        NWB_ASSERT(source.valid() && source.generation == graph.generation() && source.index < taskCount);
        const b8 finalTask = orderIndex + 1u == taskCount;
        b8 reachesNext = finalTask;
        const GpuTaskGraphSchedulingTaskIndexView consumers = analysis.schedulingConsumers(source);
        for(usize consumerOffset = 0u; consumerOffset < consumers.taskCount; ++consumerOffset){
            const u32 consumerIndex = consumers[consumerOffset];
            NWB_ASSERT(consumerIndex < taskCount && consumerIndex != source.index);
            if(!finalTask && consumerIndex == analysis.topologicalOrder()[orderIndex + 1u].index)
                reachesNext = true;
        }
        totalOrder = reachesNext;
    }
    if(totalOrder){
        // Direct edges between adjacent topological tasks prove all pairs ordered, so overlap cannot exist.
        outReachability.m_topologicalRanks.resize(taskCount);
        for(usize orderIndex = 0u; orderIndex < taskCount; ++orderIndex)
            outReachability.m_topologicalRanks[analysis.topologicalOrder()[orderIndex].index] = static_cast<u32>(orderIndex);
        outReachability.m_graphGeneration = graph.generation();
        outReachability.m_taskCount = taskCount;
        outReachability.m_totalOrder = true;
        outReachability.m_valid = true;
        return true;
    }
    const usize wordsPerRow = taskCount == 0u ? 0u : (taskCount - 1u) / s_BitsPerWord + 1u;
    const auto totalWordCountResult = TryMultiply<usize>(taskCount, wordsPerRow);
    if(!totalWordCountResult)
        return fail();
    const usize totalWordCount = *totalWordCountResult;
    if(totalWordCount > Limit<usize>::s_Max / sizeof(u64) || totalWordCount > outReachability.m_words.max_size())
        return fail();

    outReachability.m_graphGeneration = graph.generation();
    outReachability.m_taskCount = taskCount;
    outReachability.m_wordsPerRow = wordsPerRow;
    constexpr usize s_SmallDenseWordCount = s_GpuTaskSmallDenseWordCount;
    if(wordsPerRow > s_SmallDenseWordCount){
        outReachability.m_relatedWordRanges.resize(taskCount);
        Vector<GpuTaskSchedulingReachability::WordRange, Alloc::ScratchArena> descendantRanges(taskCount, outReachability.m_relatedWordRanges.get_allocator());
        for(usize taskIndex = 0u; taskIndex < taskCount; ++taskIndex){
            const usize selfWord = taskIndex / s_BitsPerWord;
            descendantRanges[taskIndex] = { .begin = selfWord, .end = selfWord + 1u };
            outReachability.m_relatedWordRanges[taskIndex] = descendantRanges[taskIndex];
        }
        for(usize orderIndex = analysis.topologicalOrder().size(); orderIndex > 0u; --orderIndex){
            const GpuTaskId source = analysis.topologicalOrder()[orderIndex - 1u];
            NWB_ASSERT(source.valid() && source.generation == graph.generation() && source.index < taskCount);
            auto& sourceRange = descendantRanges[source.index];
            const auto consumers = analysis.schedulingConsumers(source);
            for(usize consumer = 0u; consumer < consumers.taskCount; ++consumer){
                const usize child = consumers[consumer];
                NWB_ASSERT(child < taskCount && child != source.index);
                const auto& childRange = descendantRanges[child];
                sourceRange.begin = Min(sourceRange.begin, childRange.begin);
                sourceRange.end = Max(sourceRange.end, childRange.end);
            }
        }
        // Forward ancestry bounds stay separate until every producer has propagated its own ancestors.
        for(const GpuTaskId task : analysis.topologicalOrder()){
            auto& range = outReachability.m_relatedWordRanges[task.index];
            const auto producers = analysis.schedulingProducers(task);
            for(usize producer = 0u; producer < producers.taskCount; ++producer){
                const auto& parentRange = outReachability.m_relatedWordRanges[producers[producer]];
                range.begin = Min(range.begin, parentRange.begin);
                range.end = Max(range.end, parentRange.end);
            }
        }
        usize compactWordCount = 0u;
        for(usize taskIndex = 0u; taskIndex < taskCount; ++taskIndex){
            auto& range = outReachability.m_relatedWordRanges[taskIndex];
            range.begin = Min(range.begin, descendantRanges[taskIndex].begin);
            range.end = Max(range.end, descendantRanges[taskIndex].end);
            const usize span = range.end - range.begin;
            if(span > Limit<usize>::s_Max - compactWordCount)
                return fail();
            compactWordCount += span;
        }
        // Include temporary bounds and retained offsets so compact rows reduce peak scratch use.
        constexpr usize s_CompactBudgetReserveDivisor = s_GpuTaskCompactBudgetReserveDivisor;
        const usize compactBudget = (totalWordCount - totalWordCount / s_CompactBudgetReserveDivisor) * sizeof(u64);
        const usize compactBytes = compactWordCount * sizeof(u64);
        const auto metadataBytes = TryMultiply<usize>(taskCount, sizeof(usize) + sizeof(GpuTaskSchedulingReachability::WordRange));
        const b8 compactRows = metadataBytes && compactBytes <= compactBudget && *metadataBytes <= compactBudget - compactBytes;
        // Retaining the bounds costs at most 1/32 of a dense matrix at this width; smaller dense rows release them.
        constexpr usize s_MinDenseBoundsReuseWordsPerRow = s_GpuTaskMinDenseBoundsReuseWordsPerRow;
        if(compactRows || wordsPerRow >= s_MinDenseBoundsReuseWordsPerRow){
            if(compactRows)
                outReachability.m_rowOffsets.resize(taskCount);
            outReachability.m_topologicalRanks.resize(taskCount);
            outReachability.m_words.resize(compactRows ? compactWordCount : totalWordCount, 0u);
            usize rowOffset = 0u;
            for(usize taskIndex = 0u; taskIndex < taskCount; ++taskIndex){
                const auto range = outReachability.m_relatedWordRanges[taskIndex];
                const usize firstWord = compactRows ? range.begin : 0u;
                if(compactRows)
                    outReachability.m_rowOffsets[taskIndex] = rowOffset;
                outReachability.m_words[rowOffset + taskIndex / s_BitsPerWord - firstWord] |= static_cast<u64>(1u) << (taskIndex % s_BitsPerWord);
                rowOffset += compactRows ? range.end - range.begin : wordsPerRow;
            }
            for(usize orderIndex = analysis.topologicalOrder().size(); orderIndex > 0u; --orderIndex){
                const GpuTaskId source = analysis.topologicalOrder()[orderIndex - 1u];
                outReachability.m_topologicalRanks[source.index] = static_cast<u32>(orderIndex - 1u);
                const usize sourceFirstWord = compactRows ? outReachability.m_relatedWordRanges[source.index].begin : 0u;
                const usize sourceOffset = compactRows ? outReachability.m_rowOffsets[source.index] : source.index * wordsPerRow;
                const auto consumers = analysis.schedulingConsumers(source);
                for(usize consumer = 0u; consumer < consumers.taskCount; ++consumer){
                    const usize child = consumers[consumer];
                    const usize childFirstWord = compactRows ? outReachability.m_relatedWordRanges[child].begin : 0u;
                    const usize childOffset = compactRows ? outReachability.m_rowOffsets[child] : child * wordsPerRow;
                    const auto descendantRange = descendantRanges[child];
                    const usize sourceBegin = sourceOffset + descendantRange.begin - sourceFirstWord;
                    const usize childBegin = childOffset + descendantRange.begin - childFirstWord;
                    const usize wordCount = descendantRange.end - descendantRange.begin;
                    // Snapshot the bounds before writes so the closure OR can be vectorized.
                    for(usize word = 0u; word < wordCount; ++word)
                        outReachability.m_words[sourceBegin + word] |= outReachability.m_words[childBegin + word];
                }
            }
            if(compactRows){
                SymmetrizeCompactBitMatrix(outReachability.m_words, outReachability.m_rowOffsets, outReachability.m_relatedWordRanges, taskCount, wordsPerRow);
            }
            else
                SymmetrizeDenseBitMatrix(outReachability.m_words, taskCount, wordsPerRow);
            outReachability.m_valid = true;
            return true;
        }
    }
    outReachability.m_words.resize(totalWordCount, 0u);
    // Reverse topology completes each consumer's nonzero descendant span before its producer reads it.
    outReachability.m_relatedWordRanges.resize(taskCount);
    for(auto& range : outReachability.m_relatedWordRanges)
        range = { .begin = wordsPerRow, .end = 0u };
    for(usize orderIndex = analysis.topologicalOrder().size(); orderIndex > 0u; --orderIndex){
        const GpuTaskId source = analysis.topologicalOrder()[orderIndex - 1u];
        NWB_ASSERT(source.valid() && source.generation == graph.generation() && source.index < taskCount);

        const usize sourceRowOffset = source.index * wordsPerRow;
        const GpuTaskGraphSchedulingTaskIndexView consumers = analysis.schedulingConsumers(source);
        for(usize consumerOffset = 0u; consumerOffset < consumers.taskCount; ++consumerOffset){
            const usize consumerIndex = consumers[consumerOffset];
            NWB_ASSERT(consumerIndex < taskCount && consumerIndex != source.index);
            const usize consumerRowOffset = consumerIndex * wordsPerRow;
            const auto& consumerRange = outReachability.m_relatedWordRanges[consumerIndex];
            for(usize wordIndex = consumerRange.begin; wordIndex < consumerRange.end; ++wordIndex){
                outReachability.m_words[sourceRowOffset + wordIndex] |= outReachability.m_words[consumerRowOffset + wordIndex];
            }
            const usize consumerWordIndex = consumerIndex / s_BitsPerWord;
            const usize consumerWord = sourceRowOffset + consumerWordIndex;
            outReachability.m_words[consumerWord] |= static_cast<u64>(1u) << (consumerIndex % s_BitsPerWord);
            auto& sourceRange = outReachability.m_relatedWordRanges[source.index];
            sourceRange.begin = Min(sourceRange.begin, Min(consumerRange.begin, consumerWordIndex));
            sourceRange.end = Max(sourceRange.end, Max(consumerRange.end, consumerWordIndex + 1u));
        }
    }
    // Symmetrize the completed directed closure with bounded tile storage; ranks preserve strict direction.
    SymmetrizeDenseBitMatrix(outReachability.m_words, taskCount, wordsPerRow);
    outReachability.m_topologicalRanks.resize(taskCount);
    for(usize orderIndex = 0u; orderIndex < analysis.topologicalOrder().size(); ++orderIndex)
        outReachability.m_topologicalRanks[analysis.topologicalOrder()[orderIndex].index] = static_cast<u32>(orderIndex);
    for(usize taskIndex = 0u; taskIndex < taskCount; ++taskIndex){
        const usize rowOffset = taskIndex * wordsPerRow;
        outReachability.m_words[rowOffset + taskIndex / s_BitsPerWord] |= static_cast<u64>(1u) << (taskIndex % s_BitsPerWord);
        auto& range = outReachability.m_relatedWordRanges[taskIndex];
        range = { .begin = wordsPerRow, .end = 0u };
        for(usize wordIndex = 0u; wordIndex < wordsPerRow; ++wordIndex){
            const u64 word = outReachability.m_words[rowOffset + wordIndex];
            if(word != 0u){
                range.begin = Min(range.begin, wordIndex);
                range.end = wordIndex + 1u;
            }
        }
    }
    outReachability.m_valid = true;
    return true;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_CORE_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

