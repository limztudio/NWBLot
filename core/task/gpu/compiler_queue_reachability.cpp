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
    const usize wordsPerRow
){
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


template<typename RowRanges>
static void SymmetrizeCompactTaskRelations(
    Vector<u64, Alloc::ScratchArena>& words,
    const Vector<usize, Alloc::ScratchArena>& offsets,
    const RowRanges& ranges,
    const usize taskCount,
    const usize wordsPerRow
){
    const auto readWord = [&](const usize task, const usize word){
        const auto& range = ranges[task];
        return word < range.m_begin || word >= range.m_end
            ? static_cast<u64>(0u)
            : words[offsets[task] + word - range.m_begin]
        ;
    };
    const auto appendWord = [&](const usize task, const usize word, const u64 bits){
        if(bits == 0u)
            return;
        const auto& range = ranges[task];
        NWB_ASSERT(word >= range.m_begin && word < range.m_end);
        words[offsets[task] + word - range.m_begin] |= bits;
    };
    for(usize rowBlock = 0u; rowBlock < wordsPerRow; ++rowBlock){
        const usize rowBase = rowBlock * s_BitsPerWord;
        const usize rowCount = Min(s_BitsPerWord, taskCount - rowBase);
        usize columnEnd = rowBlock + 1u;
        for(usize row = 0u; row < rowCount; ++row)
            columnEnd = Max(columnEnd, ranges[rowBase + row].m_end);
        for(usize columnBlock = rowBlock; columnBlock < columnEnd; ++columnBlock){
            const usize columnBase = columnBlock * s_BitsPerWord;
            const usize columnCount = Min(s_BitsPerWord, taskCount - columnBase);
            Array<u64, s_BitsPerWord> forward = {};
            u64 forwardBits = 0u;
            for(usize row = 0u; row < rowCount; ++row){
                forward[row] = readWord(rowBase + row, columnBlock);
                forwardBits |= forward[row];
            }
            if(rowBlock == columnBlock){
                if(forwardBits != 0u){
                    TransposeTaskRelationTile(forward);
                    for(usize row = 0u; row < rowCount; ++row)
                        appendWord(rowBase + row, columnBlock, forward[row]);
                }
                continue;
            }
            Array<u64, s_BitsPerWord> backward = {};
            u64 backwardBits = 0u;
            for(usize row = 0u; row < columnCount; ++row){
                backward[row] = readWord(columnBase + row, rowBlock);
                backwardBits |= backward[row];
            }
            if(forwardBits != 0u){
                TransposeTaskRelationTile(forward);
                for(usize row = 0u; row < columnCount; ++row)
                    appendWord(columnBase + row, rowBlock, forward[row]);
            }
            if(backwardBits != 0u){
                TransposeTaskRelationTile(backward);
                for(usize row = 0u; row < rowCount; ++row)
                    appendWord(rowBase + row, columnBlock, backward[row]);
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
    , m_rowOffsets(scratchArena)
{}

bool GpuTaskSchedulingReachability::reaches(
    const GpuTaskId& source,
    const GpuTaskId& destination
)const noexcept{
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
    const usize destinationWord = destination.index / s_BitsPerWord;
    const auto& range = m_relatedWordRanges[source.index];
    if(destinationWord < range.m_begin || destinationWord >= range.m_end)
        return false;
    const usize wordIndex = m_rowOffsets.empty()
        ? source.index * m_wordsPerRow + destinationWord
        : m_rowOffsets[source.index] + destinationWord - range.m_begin;
    const u64 mask = static_cast<u64>(1u) << (destination.index % s_BitsPerWord);
    return (m_words[wordIndex] & mask) != 0u;
}

bool GpuTaskSchedulingReachability::transitivelyIndependent(
    const GpuTaskId& lhs,
    const GpuTaskId& rhs
)const noexcept{
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
    const usize destinationWord = rhs.index / s_BitsPerWord;
    const auto& range = m_relatedWordRanges[lhs.index];
    if(destinationWord < range.m_begin || destinationWord >= range.m_end)
        return true;
    const usize wordIndex = m_rowOffsets.empty()
        ? lhs.index * m_wordsPerRow + destinationWord
        : m_rowOffsets[lhs.index] + destinationWord - range.m_begin;
    const u64 mask = static_cast<u64>(1u) << (rhs.index % s_BitsPerWord);
    return (m_words[wordIndex] & mask) == 0u;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


bool BuildGpuTaskSchedulingReachability(
    const GpuTaskGraph::DeclarationReadView& graph,
    const GpuTaskGraphAnalysis& analysis,
    GpuTaskSchedulingReachability& outReachability
){
    constexpr usize s_BitsPerWord = sizeof(u64) * 8u;

    outReachability.m_words.clear();
    outReachability.m_topologicalRanks.clear();
    outReachability.m_relatedWordRanges.clear();
    outReachability.m_rowOffsets.clear();
    outReachability.m_totalOrder = false;
    outReachability.m_graphGeneration = 0u;
    outReachability.m_taskCount = 0u;
    outReachability.m_wordsPerRow = 0u;
    outReachability.m_valid = false;
    const auto fail = [&outReachability](){
        outReachability.m_words.clear();
        outReachability.m_topologicalRanks.clear();
        outReachability.m_relatedWordRanges.clear();
        outReachability.m_rowOffsets.clear();
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
    constexpr usize s_SmallDenseWordCount = 4u;
    if(wordsPerRow > s_SmallDenseWordCount){
        outReachability.m_relatedWordRanges.resize(taskCount);
        Vector<GpuTaskSchedulingReachability::WordRange, Alloc::ScratchArena> descendantRanges(
            taskCount,
            outReachability.m_relatedWordRanges.get_allocator()
        );
        for(usize taskIndex = 0u; taskIndex < taskCount; ++taskIndex){
            const usize selfWord = taskIndex / s_BitsPerWord;
            descendantRanges[taskIndex] = { .m_begin = selfWord, .m_end = selfWord + 1u };
            outReachability.m_relatedWordRanges[taskIndex] = descendantRanges[taskIndex];
        }
        for(usize orderIndex = analysis.topologicalOrder().size(); orderIndex > 0u; --orderIndex){
            const GpuTaskId source = analysis.topologicalOrder()[orderIndex - 1u];
            if(!source.valid() || source.generation != graph.generation() || source.index >= taskCount)
                return fail();
            auto& sourceRange = descendantRanges[source.index];
            const auto consumers = analysis.schedulingConsumers(source);
            for(usize consumer = 0u; consumer < consumers.taskCount; ++consumer){
                const usize child = consumers[consumer];
                if(child >= taskCount || child == source.index)
                    return fail();
                const auto& childRange = descendantRanges[child];
                sourceRange.m_begin = Min(sourceRange.m_begin, childRange.m_begin);
                sourceRange.m_end = Max(sourceRange.m_end, childRange.m_end);
            }
        }
        // Forward ancestry bounds stay separate until every producer has propagated its own ancestors.
        for(const GpuTaskId task : analysis.topologicalOrder()){
            auto& range = outReachability.m_relatedWordRanges[task.index];
            const auto producers = analysis.schedulingProducers(task);
            for(usize producer = 0u; producer < producers.taskCount; ++producer){
                const auto& parentRange = outReachability.m_relatedWordRanges[producers[producer]];
                range.m_begin = Min(range.m_begin, parentRange.m_begin);
                range.m_end = Max(range.m_end, parentRange.m_end);
            }
        }
        usize compactWordCount = 0u;
        for(usize taskIndex = 0u; taskIndex < taskCount; ++taskIndex){
            auto& range = outReachability.m_relatedWordRanges[taskIndex];
            range.m_begin = Min(range.m_begin, descendantRanges[taskIndex].m_begin);
            range.m_end = Max(range.m_end, descendantRanges[taskIndex].m_end);
            const usize span = range.m_end - range.m_begin;
            if(span > Limit<usize>::s_Max - compactWordCount)
                return fail();
            compactWordCount += span;
        }
        // Include temporary bounds and retained offsets so compact rows reduce peak scratch use.
        const usize compactBudget = (totalWordCount - totalWordCount / 4u) * sizeof(u64);
        const usize compactBytes = compactWordCount * sizeof(u64);
        usize metadataBytes = 0u;
        const bool compactRows =
            TryMultiply<usize>(taskCount, sizeof(usize) + sizeof(GpuTaskSchedulingReachability::WordRange), metadataBytes)
            && compactBytes <= compactBudget
            && metadataBytes <= compactBudget - compactBytes
        ;
        // Retaining the bounds costs at most 1/32 of a dense matrix at this width; smaller dense rows release them.
        constexpr usize s_MinDenseBoundsReuseWordsPerRow = 64u;
        if(compactRows || wordsPerRow >= s_MinDenseBoundsReuseWordsPerRow){
            if(compactRows)
                outReachability.m_rowOffsets.resize(taskCount);
            outReachability.m_topologicalRanks.resize(taskCount);
            outReachability.m_words.resize(compactRows ? compactWordCount : totalWordCount, 0u);
            usize rowOffset = 0u;
            for(usize taskIndex = 0u; taskIndex < taskCount; ++taskIndex){
                const auto range = outReachability.m_relatedWordRanges[taskIndex];
                const usize firstWord = compactRows ? range.m_begin : 0u;
                if(compactRows)
                    outReachability.m_rowOffsets[taskIndex] = rowOffset;
                outReachability.m_words[rowOffset + taskIndex / s_BitsPerWord - firstWord] |=
                    static_cast<u64>(1u) << (taskIndex % s_BitsPerWord);
                rowOffset += compactRows ? range.m_end - range.m_begin : wordsPerRow;
            }
            for(usize orderIndex = analysis.topologicalOrder().size(); orderIndex > 0u; --orderIndex){
                const GpuTaskId source = analysis.topologicalOrder()[orderIndex - 1u];
                outReachability.m_topologicalRanks[source.index] = static_cast<u32>(orderIndex - 1u);
                const usize sourceFirstWord = compactRows ? outReachability.m_relatedWordRanges[source.index].m_begin : 0u;
                const usize sourceOffset = compactRows ? outReachability.m_rowOffsets[source.index] : source.index * wordsPerRow;
                const auto consumers = analysis.schedulingConsumers(source);
                for(usize consumer = 0u; consumer < consumers.taskCount; ++consumer){
                    const usize child = consumers[consumer];
                    const usize childFirstWord = compactRows ? outReachability.m_relatedWordRanges[child].m_begin : 0u;
                    const usize childOffset = compactRows ? outReachability.m_rowOffsets[child] : child * wordsPerRow;
                    const auto descendantRange = descendantRanges[child];
                    const usize sourceBegin = sourceOffset + descendantRange.m_begin - sourceFirstWord;
                    const usize childBegin = childOffset + descendantRange.m_begin - childFirstWord;
                    const usize wordCount = descendantRange.m_end - descendantRange.m_begin;
                    // Snapshot the bounds before writes so the closure OR can be vectorized.
                    for(usize word = 0u; word < wordCount; ++word)
                        outReachability.m_words[sourceBegin + word] |= outReachability.m_words[childBegin + word];
                }
            }
            if(compactRows){
                __hidden_gpu_task_queue_reachability::SymmetrizeCompactTaskRelations(
                    outReachability.m_words,
                    outReachability.m_rowOffsets,
                    outReachability.m_relatedWordRanges,
                    taskCount,
                    wordsPerRow
                );
            }
            else
                __hidden_gpu_task_queue_reachability::SymmetrizeTaskRelations(outReachability.m_words, taskCount, wordsPerRow);
            outReachability.m_valid = true;
            return true;
        }
    }
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

