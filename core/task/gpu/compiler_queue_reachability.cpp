// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "compiler_internal.h"


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_CORE_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace GpuTaskGraphCompilerDetail{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace __hidden_gpu_task_queue_reachability{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


inline constexpr usize s_BitsPerWord = sizeof(u64) * 8u;


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


static void TransposeTaskRelationTile(Array<u64, s_BitsPerWord>& tile)noexcept{
    u64 mask = 0x00000000FFFFFFFFull;
    for(usize shift = s_BitsPerWord / 2u; shift != 0u; shift /= 2u){
        // Exchange each row-address bit with the corresponding column-address bit, starting with the high bit.
        for(usize block = 0u; block < s_BitsPerWord; block += shift * 2u){
            for(usize offset = 0u; offset < shift; ++offset){
                u64& low = tile[block + offset];
                u64& high = tile[block + offset + shift];
                const u64 exchanged = ((low >> shift) ^ high) & mask;
                low ^= exchanged << shift;
                high ^= exchanged;
            }
        }
        if(shift > 1u)
            mask ^= mask << (shift / 2u);
    }
}

static void SymmetrizeTaskRelations(
    Vector<u64, Alloc::ScratchArena>& words,
    const usize taskCount,
    const usize wordsPerRow){
    for(usize rowBlock = 0u; rowBlock < wordsPerRow; ++rowBlock){
        const usize rowBase = rowBlock * s_BitsPerWord;
        const usize rowCount = Min(s_BitsPerWord, taskCount - rowBase);
        for(usize columnBlock = rowBlock; columnBlock < wordsPerRow; ++columnBlock){
            const usize columnBase = columnBlock * s_BitsPerWord;
            const usize columnCount = Min(s_BitsPerWord, taskCount - columnBase);
            Array<u64, s_BitsPerWord> forward = {};
            u64 forwardBits = 0u;
            for(usize row = 0u; row < rowCount; ++row){
                forward[row] = words[(rowBase + row) * wordsPerRow + columnBlock];
                forwardBits |= forward[row];
            }
            if(rowBlock == columnBlock){
                if(forwardBits != 0u){
                    TransposeTaskRelationTile(forward);
                    for(usize row = 0u; row < rowCount; ++row)
                        words[(rowBase + row) * wordsPerRow + columnBlock] |= forward[row];
                }
                continue;
            }
            Array<u64, s_BitsPerWord> backward = {};
            u64 backwardBits = 0u;
            for(usize row = 0u; row < columnCount; ++row){
                backward[row] = words[(columnBase + row) * wordsPerRow + rowBlock];
                backwardBits |= backward[row];
            }
            if(forwardBits != 0u){
                TransposeTaskRelationTile(forward);
                for(usize row = 0u; row < columnCount; ++row)
                    words[(columnBase + row) * wordsPerRow + rowBlock] |= forward[row];
            }
            if(backwardBits != 0u){
                TransposeTaskRelationTile(backward);
                for(usize row = 0u; row < rowCount; ++row)
                    words[(rowBase + row) * wordsPerRow + columnBlock] |= backward[row];
            }
        }
    }
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


GpuTaskSchedulingReachability::GpuTaskSchedulingReachability(Alloc::ScratchArena& scratchArena)
    : m_words(scratchArena)
    , m_topologicalRanks(scratchArena)
    , m_relatedWordRanges(scratchArena)
{}

bool GpuTaskSchedulingReachability::reaches(
    const GpuTaskId& source,
    const GpuTaskId& destination)const noexcept{
    constexpr usize s_BitsPerWord = sizeof(u64) * 8u;

    if(
        !m_valid
        || !source.valid()
        || !destination.valid()
        || source.generation != m_graphGeneration
        || destination.generation != m_graphGeneration
        || source.index >= m_taskCount
        || destination.index >= m_taskCount
        || source == destination
    )
        return false;
    if(m_totalOrder)
        return m_topologicalRanks[source.index] < m_topologicalRanks[destination.index];
    if(m_words.empty() || m_topologicalRanks[source.index] >= m_topologicalRanks[destination.index])
        return false;
    const usize wordIndex = source.index * m_wordsPerRow + destination.index / s_BitsPerWord;
    const u64 mask = static_cast<u64>(1u) << (destination.index % s_BitsPerWord);
    return (m_words[wordIndex] & mask) != 0u;
}

bool GpuTaskSchedulingReachability::transitivelyIndependent(
    const GpuTaskId& lhs,
    const GpuTaskId& rhs)const noexcept{
    constexpr usize s_BitsPerWord = sizeof(u64) * 8u;

    if(
        !m_valid
        || !lhs.valid()
        || !rhs.valid()
        || lhs.generation != m_graphGeneration
        || rhs.generation != m_graphGeneration
        || lhs.index >= m_taskCount
        || rhs.index >= m_taskCount
        || lhs == rhs
    )
        return false;
    if(m_totalOrder)
        return false;
    if(m_words.empty())
        return true;
    const usize wordIndex = lhs.index * m_wordsPerRow + rhs.index / s_BitsPerWord;
    const u64 mask = static_cast<u64>(1u) << (rhs.index % s_BitsPerWord);
    return (m_words[wordIndex] & mask) == 0u;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


bool BuildGpuTaskSchedulingReachability(
    const GpuTaskGraph::DeclarationReadView& graph,
    const GpuTaskGraphAnalysis& analysis,
    GpuTaskSchedulingReachability& outReachability){
    constexpr usize s_BitsPerWord = sizeof(u64) * 8u;

    outReachability.m_words.clear();
    outReachability.m_topologicalRanks.clear();
    outReachability.m_relatedWordRanges.clear();
    outReachability.m_totalOrder = false;
    outReachability.m_graphGeneration = 0u;
    outReachability.m_taskCount = 0u;
    outReachability.m_wordsPerRow = 0u;
    outReachability.m_valid = false;
    const auto fail = [&outReachability](){
        outReachability.m_words.clear();
        outReachability.m_topologicalRanks.clear();
        outReachability.m_relatedWordRanges.clear();
        outReachability.m_totalOrder = false;
        outReachability.m_graphGeneration = 0u;
        outReachability.m_taskCount = 0u;
        outReachability.m_wordsPerRow = 0u;
        outReachability.m_valid = false;
        return false;
    };
    if(!analysis.validFor(graph))
        return fail();

    const usize taskCount = graph.taskCount();
    if(taskCount > static_cast<usize>(Limit<u32>::s_Max))
        return fail();
    if(analysis.schedulingEdges().empty()){
        outReachability.m_graphGeneration = graph.generation();
        outReachability.m_taskCount = taskCount;
        outReachability.m_valid = true;
        return true;
    }
    bool totalOrder = taskCount > 1u && analysis.topologicalOrder().size() == taskCount;
    for(usize orderIndex = 0u; orderIndex < taskCount && totalOrder; ++orderIndex){
        const GpuTaskId source = analysis.topologicalOrder()[orderIndex];
        if(!source.valid() || source.generation != graph.generation() || source.index >= taskCount)
            return fail();
        const bool finalTask = orderIndex + 1u == taskCount;
        bool reachesNext = finalTask;
        const GpuTaskGraphSchedulingTaskIndexView consumers = analysis.schedulingConsumers(source);
        for(usize consumerOffset = 0u; consumerOffset < consumers.taskCount; ++consumerOffset){
            const u32 consumerIndex = consumers[consumerOffset];
            if(consumerIndex >= taskCount || consumerIndex == source.index)
                return fail();
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
    usize totalWordCount = 0u;
    if(
        !TryMultiply<usize>(taskCount, wordsPerRow, totalWordCount)
        || totalWordCount > Limit<usize>::s_Max / sizeof(u64)
        || totalWordCount > outReachability.m_words.max_size()
    )
        return fail();

    outReachability.m_graphGeneration = graph.generation();
    outReachability.m_taskCount = taskCount;
    outReachability.m_wordsPerRow = wordsPerRow;
    outReachability.m_words.resize(totalWordCount, 0u);
    // Reverse topology completes each consumer's nonzero descendant span before its producer reads it.
    outReachability.m_relatedWordRanges.resize(taskCount);
    for(auto& range : outReachability.m_relatedWordRanges)
        range = { .m_begin = wordsPerRow, .m_end = 0u };
    for(usize orderIndex = analysis.topologicalOrder().size(); orderIndex > 0u; --orderIndex){
        const GpuTaskId source = analysis.topologicalOrder()[orderIndex - 1u];
        if(
            !source.valid()
            || source.generation != graph.generation()
            || source.index >= taskCount
        )
            return fail();

        const usize sourceRowOffset = source.index * wordsPerRow;
        const GpuTaskGraphSchedulingTaskIndexView consumers = analysis.schedulingConsumers(source);
        for(usize consumerOffset = 0u; consumerOffset < consumers.taskCount; ++consumerOffset){
            const usize consumerIndex = consumers[consumerOffset];
            if(consumerIndex >= taskCount || consumerIndex == source.index)
                return fail();
            const usize consumerRowOffset = consumerIndex * wordsPerRow;
            const auto& consumerRange = outReachability.m_relatedWordRanges[consumerIndex];
            for(usize wordIndex = consumerRange.m_begin; wordIndex < consumerRange.m_end; ++wordIndex){
                outReachability.m_words[sourceRowOffset + wordIndex] |=
                    outReachability.m_words[consumerRowOffset + wordIndex]
                ;
            }
            const usize consumerWordIndex = consumerIndex / s_BitsPerWord;
            const usize consumerWord = sourceRowOffset + consumerWordIndex;
            outReachability.m_words[consumerWord] |= static_cast<u64>(1u) << (consumerIndex % s_BitsPerWord);
            auto& sourceRange = outReachability.m_relatedWordRanges[source.index];
            sourceRange.m_begin = Min(sourceRange.m_begin, Min(consumerRange.m_begin, consumerWordIndex));
            sourceRange.m_end = Max(sourceRange.m_end, Max(consumerRange.m_end, consumerWordIndex + 1u));
        }
    }
    // Symmetrize the completed directed closure with bounded tile storage; ranks preserve strict direction.
    __hidden_gpu_task_queue_reachability::SymmetrizeTaskRelations(outReachability.m_words, taskCount, wordsPerRow);
    outReachability.m_topologicalRanks.resize(taskCount);
    for(usize orderIndex = 0u; orderIndex < analysis.topologicalOrder().size(); ++orderIndex)
        outReachability.m_topologicalRanks[analysis.topologicalOrder()[orderIndex].index] = static_cast<u32>(orderIndex);
    for(usize taskIndex = 0u; taskIndex < taskCount; ++taskIndex){
        const usize rowOffset = taskIndex * wordsPerRow;
        outReachability.m_words[rowOffset + taskIndex / s_BitsPerWord] |= static_cast<u64>(1u) << (taskIndex % s_BitsPerWord);
        auto& range = outReachability.m_relatedWordRanges[taskIndex];
        range = { .m_begin = wordsPerRow, .m_end = 0u };
        for(usize wordIndex = 0u; wordIndex < wordsPerRow; ++wordIndex){
            const u64 word = outReachability.m_words[rowOffset + wordIndex];
            if(word != 0u){
                range.m_begin = Min(range.m_begin, wordIndex);
                range.m_end = wordIndex + 1u;
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

