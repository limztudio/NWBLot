// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "task_graph_test_utils.h"

#include <core/task/gpu/packet_runtime_internal.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace Tests{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace __hidden_task_graph_submission_validation_tests{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


using namespace TaskGraphTestUtils;
using TaskGraphTestUtils::TestArena;

void ExpectWaitStatistics(const Core::GpuPhysicalQueueId queue, const Span<const Core::QueueSubmissionToken> tokens = {}){
    Core::Alloc::ScratchArena scratch(s_TaskGraphScratchArena);
    const Core::GpuPacketRuntimeDetail::PacketWaitStatistics actual = Core::GpuPacketRuntimeDetail::CountPacketWaitStatistics(queue, tokens, scratch);
    Core::Alloc::ScratchArena referenceScratch(s_TaskGraphScratchArena);
    Vector<u32, Core::Alloc::ScratchArena> identities(referenceScratch);
    identities.reserve(tokens.size());
    usize sameQueueCount = 0u;
    for(const Core::QueueSubmissionToken& token : tokens){
        if(token.matchesPhysicalQueue(queue.index, queue.deviceGeneration))
            ++sameQueueCount;
        else
            identities.push_back((static_cast<u32>(token.deviceGeneration) << 16u) | token.physicalQueueIndex);
    }
    Sort(identities.begin(), identities.end());
    const usize uniqueQueueCount = identities.empty() ? 0u : static_cast<usize>(Unique(identities.begin(), identities.end()) - identities.begin());
    EXPECT_EQ(actual.plannedWaitTokenCount, tokens.size());
    EXPECT_EQ(actual.sameQueueWaitElisionCount, sameQueueCount);
    EXPECT_EQ(actual.timelineWaitCount, uniqueQueueCount);
    EXPECT_EQ(actual.mergedTimelineWaitCount, tokens.size() - sameQueueCount - uniqueQueueCount);
}


TEST(GpuTaskGraphSubmissionValidation, CountsDistinctPhysicalQueueGenerationsWithoutChangingWaitTokens){
    TestArena testArena;
    const Core::GpuPhysicalQueueId queue{ .index = 41u, .deviceGeneration = 9u };
    Core::GraphicsVector<Core::QueueSubmissionToken> tokens(testArena.arena);
    for(usize index = 0u; index < 24u; ++index){
        tokens.push_back({
            .value = index + 1u,
            .physicalQueueIndex = static_cast<u16>(index < 4u ? queue.index : 300u + index % 12u),
            .deviceGeneration = static_cast<u16>(queue.deviceGeneration + (index % 5u == 0u ? 1u : 0u)),
            .queue = Core::CommandQueue::Compute,
        });
    }
    const auto original = tokens;
    ExpectWaitStatistics(queue);
    ExpectWaitStatistics(queue, tokens);
    Sort(tokens.begin(), tokens.end(), [](const auto& first, const auto& second)noexcept(
        noexcept(first.value > second.value)
        && (!IsReference_V<decltype(first.value > second.value)> || IsNothrowConvertible_V<decltype(first.value > second.value), Decay_T<decltype(first.value > second.value)>>)
    ){ return first.value > second.value; }
    );
    ExpectWaitStatistics(queue, tokens);
    Sort(tokens.begin(), tokens.end(), [](const auto& first, const auto& second)noexcept(
        noexcept(first.value < second.value)
        && (!IsReference_V<decltype(first.value < second.value)> || IsNothrowConvertible_V<decltype(first.value < second.value), Decay_T<decltype(first.value < second.value)>>)
    ){ return first.value < second.value; }
    );
    ASSERT_EQ(tokens.size(), original.size());
    EXPECT_EQ(NWB_MEMCMP(tokens.data(), original.data(), tokens.size() * sizeof(tokens[0u])), 0);
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

