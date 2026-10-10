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

void ExpectWaitStatistics(
    const Graphics::GpuPhysicalQueueId queue,
    const Graphics::QueueSubmissionToken* const tokens,
    const usize count
){
    Graphics::Alloc::ScratchArena scratch(s_TaskGraphScratchArena);
    const Graphics::GpuPacketRuntimeDetail::PacketWaitStatistics actual = Graphics::GpuPacketRuntimeDetail::CountPacketWaitStatistics(
        queue, tokens, count, scratch
    );
    Graphics::Alloc::ScratchArena referenceScratch(s_TaskGraphScratchArena);
    Vector<u32, Graphics::Alloc::ScratchArena> identities(referenceScratch);
    identities.reserve(count);
    usize sameQueueCount = 0u;
    for(usize index = 0u; index < count; ++index){
        const Graphics::QueueSubmissionToken& token = tokens[index];
        if(token.matchesPhysicalQueue(queue.index, queue.deviceGeneration))
            ++sameQueueCount;
        else
            identities.push_back((static_cast<u32>(token.deviceGeneration) << 16u) | token.physicalQueueIndex);
    }
    Sort(identities.begin(), identities.end());
    const usize uniqueQueueCount = identities.empty() ? 0u : static_cast<usize>(Unique(identities.begin(), identities.end()) - identities.begin());
    EXPECT_EQ(actual.plannedWaitTokenCount, count);
    EXPECT_EQ(actual.sameQueueWaitElisionCount, sameQueueCount);
    EXPECT_EQ(actual.timelineWaitCount, uniqueQueueCount);
    EXPECT_EQ(actual.mergedTimelineWaitCount, count - sameQueueCount - uniqueQueueCount);
}


TEST(GpuTaskGraphSubmissionValidation, CountsDistinctPhysicalQueueGenerationsWithoutChangingWaitTokens){
    TestArena testArena;
    const Graphics::GpuPhysicalQueueId queue{ .index = 41u, .deviceGeneration = 9u };
    Graphics::GraphicsVector<Graphics::QueueSubmissionToken> tokens(testArena.arena);
    for(usize index = 0u; index < 24u; ++index){
        tokens.push_back({
            .value = index + 1u,
            .physicalQueueIndex = static_cast<u16>(index < 4u ? queue.index : 300u + index % 12u),
            .deviceGeneration = static_cast<u16>(queue.deviceGeneration + (index % 5u == 0u ? 1u : 0u)),
            .queue = Graphics::CommandQueue::Compute,
        });
    }
    const auto original = tokens;
    ExpectWaitStatistics(queue, nullptr, 0u);
    ExpectWaitStatistics(queue, tokens.data(), tokens.size());
    Sort(tokens.begin(), tokens.end(), [](const auto& first, const auto& second){ return first.value > second.value; });
    ExpectWaitStatistics(queue, tokens.data(), tokens.size());
    Sort(tokens.begin(), tokens.end(), [](const auto& first, const auto& second){ return first.value < second.value; });
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

