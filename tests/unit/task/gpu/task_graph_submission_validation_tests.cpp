// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "task_graph_test_utils.h"

#include <tests/common/vulkan_test_sync.h>
#include <core/task/gpu/packet_runtime_internal.h>
#include <global/timer.h>


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
    const usize count){
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

void BenchmarkClusteredWaits(const usize count){
    TestArena testArena;
    const Graphics::GpuPhysicalQueueId queue{ .index = 41u, .deviceGeneration = 9u };
    Graphics::GraphicsVector<Graphics::QueueSubmissionToken> tokens(testArena.arena);
    tokens.reserve(count);
    for(usize index = 0u; index < count; ++index){
        tokens.push_back({
            .value = index + 1u,
            .physicalQueueIndex = static_cast<u16>(index < count / 2u ? 3u : 60001u),
            .deviceGeneration = queue.deviceGeneration,
            .queue = Graphics::CommandQueue::Compute,
        });
    }
    u64 minimumNanoseconds = Limit<u64>::s_Max;
    for(usize iteration = 0u; iteration < 4u; ++iteration){
        Graphics::Alloc::ScratchArena scratch(s_TaskGraphScratchArena);
        const Timer begin = TimerNow();
        const Graphics::GpuPacketRuntimeDetail::PacketWaitStatistics statistics = Graphics::GpuPacketRuntimeDetail::CountPacketWaitStatistics(
            queue, tokens.data(), tokens.size(), scratch
        );
        const u64 nanoseconds = DurationInNS<u64>(TimerNow(), begin);
        EXPECT_EQ(statistics.plannedWaitTokenCount, count);
        EXPECT_EQ(statistics.sameQueueWaitElisionCount, 0u);
        EXPECT_EQ(statistics.timelineWaitCount, 2u);
        EXPECT_EQ(statistics.mergedTimelineWaitCount, count - 2u);
        EXPECT_EQ(scratch.memoryStats().allocationCount, 0u);
        if(iteration != 0u)
            minimumNanoseconds = Min(minimumNanoseconds, nanoseconds);
    }
    RecordUnsignedTestProperty("wait_statistics_ns", minimumNanoseconds);
}

[[nodiscard]] bool ValidatePacketOwnership(
    const Graphics::GpuTaskGraph::DeclarationReadView& declarations,
    const Graphics::GpuCompiledGraph::ReadView& plan,
    const Graphics::GpuSubmissionPacketId packetID){
    const Graphics::GpuCompiledPacketView packet = plan.packet(packetID);
    if(!packet.valid())
        return false;
    for(u32 index = 0u; index < packet.plan->externalDependencyCount; ++index){
        if(!declarations.externalCompletionToken(packet.externalDependencies[index]))
            return false;
    }
    return Graphics::GpuPacketRuntimeDetail::ValidateInitialOwnershipCompletions(declarations, plan, packetID);
}

void CheckInitialOwnershipFanIn(const usize count, const bool benchmark){
    ASSERT_GT(count, 0u);
    TestArena testArena;
    Graphics::GpuTaskGraph graph(testArena.arena);
    Graphics::GpuPhysicalQueueInfo queues[] = { GraphicsQueue(3u), GraphicsQueue(19u), DedicatedComputeQueue(41u) };
    queues[1u].queueIndex = 1u;
    const Graphics::GpuPhysicalQueueTopology topology{ .queues = queues, .queueCount = LengthOf(queues) };
    Graphics::CommandListResourceStateHandoff stateSource(testArena.arena);
    Graphics::GraphicsBackend::VulkanTestDispatchAccess::validateStateHandoff(stateSource, queues[0u].id.deviceGeneration);
    Graphics::GraphicsVector<Graphics::GpuTaskResourceUse> uses(testArena.arena);
    uses.reserve(count);
    Graphics::GpuExternalCompletionId firstCompletion;
    Graphics::QueueSubmissionToken firstToken;
    for(usize index = 0u; index < count; ++index){
        char identityText[32u] = {};
        const AStringView suffix = FormatDecimal(index, identityText);
        const Graphics::GpuPhysicalQueueInfo& sourceQueue = queues[index % 2u];
        const Graphics::QueueSubmissionToken token{
            .value = index + 7u,
            .physicalQueueIndex = sourceQueue.id.index,
            .deviceGeneration = sourceQueue.id.deviceGeneration,
            .queue = sourceQueue.queueClass,
        };
        const Graphics::GpuExternalCompletionId completion = graph.importExternalCompletion(
            Graphics::GpuExternalCompletionDesc{}
                .setIdentity(DeriveName(Name("tests/submission_validation/owner_completion/"), suffix))
                .setMarkerLabel("Owner Completion")
                .setToken(token)
        );
        ASSERT_TRUE(completion.valid());
        if(index == 0u){
            firstCompletion = completion;
            firstToken = token;
        }
        const Graphics::GpuGraphInitialOwnerHandoffSourceDesc source{
            .range = {},
            .sourceQueue = sourceQueue.id,
            .destinationQueue = queues[2u].id,
            .completion = completion,
            .minimumCompletionToken = token,
            .stateSource = &stateSource,
        };
        const Graphics::GpuGraphResourceId resource = graph.importResource(
            Graphics::GpuGraphResourceDesc{}
                .setIdentity(DeriveName(Name("tests/submission_validation/owned_resource/"), suffix))
                .setMarkerLabel("Owned Resource")
                .setType(Graphics::GpuGraphResourceType::AccelStruct)
                .setInitialState(Graphics::ResourceStates::AccelStructRead)
                .setInitialOwnerHandoffSources(&source, 1u)
        );
        ASSERT_TRUE(resource.valid());
        uses.push_back({
            .resource = resource,
            .range = {},
            .requiredState = Graphics::ResourceStates::AccelStructRead,
            .access = Graphics::GpuTaskResourceAccess::Read,
        });
    }
    const Graphics::GpuExternalCompletionId ordinaryCompletion = graph.importExternalCompletion(
        Graphics::GpuExternalCompletionDesc{}
            .setIdentity(Name("tests/submission_validation/ordinary_completion"))
            .setMarkerLabel("Ordinary Completion")
            .setToken(firstToken)
    );
    ASSERT_TRUE(ordinaryCompletion.valid());
    const Graphics::GpuTaskId task = graph.addTask(
        Graphics::GpuTaskDesc{}
            .setIdentity(Name("tests/submission_validation/ownership_fan_in"))
            .setMarkerLabel("Ownership Fan In")
            .setResourceUses(uses.data(), uses.size())
            .setExternalDependencies(&ordinaryCompletion, 1u),
        ComputeCommands()
    );
    ASSERT_TRUE(task.valid());
    Graphics::GpuTaskGraphAnalysis analysis(testArena.arena);
    Graphics::GpuTaskGraphQueueAssignments assignments(testArena.arena);
    Graphics::GpuCompiledGraph compiledGraph(testArena.arena);
    ASSERT_TRUE(Compile(graph, analysis, topology, assignments, compiledGraph));
    const GpuTaskGraphReadViews reads(graph, compiledGraph);
    ASSERT_TRUE(reads.valid());
    const Graphics::GpuSubmissionPacketId packetID = reads.compiled.packetForTask(task);
    const Graphics::GpuCompiledPacketView packet = reads.compiled.packet(packetID);
    ASSERT_TRUE(packet.valid());
    ASSERT_EQ(packet.plan->queue, queues[2u].id);
    ASSERT_EQ(packet.plan->externalDependencyCount, count + 1u);
    const ArenaMemoryStats before = testArena.arena.memoryStats();
    u64 minimumNanoseconds = Limit<u64>::s_Max;
    for(usize iteration = 0u; iteration < (benchmark ? 4u : 1u); ++iteration){
        const Timer begin = TimerNow();
        const bool valid = ValidatePacketOwnership(reads.declarations, reads.compiled, packetID);
        const u64 nanoseconds = DurationInNS<u64>(TimerNow(), begin);
        ASSERT_TRUE(valid);
        if(iteration != 0u || !benchmark)
            minimumNanoseconds = Min(minimumNanoseconds, nanoseconds);
    }
    EXPECT_EQ(testArena.arena.memoryStats().allocationCount, before.allocationCount);
    EXPECT_EQ(testArena.arena.memoryStats().usedBytes, before.usedBytes);
    if(benchmark)
        RecordUnsignedTestProperty("ownership_validation_ns", minimumNanoseconds);
    const Graphics::GpuTaskGraphResourceView firstResource = reads.declarations.resourceAt(uses.front().resource.index);
    ASSERT_EQ(firstResource.initialOwnerHandoffSourceCount, 1u);
    const Graphics::GpuTaskGraphInitialOwnerHandoffSourceView& firstSource = firstResource.initialOwnerHandoffSources[0u];
    EXPECT_EQ(firstSource.completion, firstCompletion);
    const Graphics::GpuCompiledTaskView compiledTask = reads.compiled.findTask(task);
    ASSERT_TRUE(compiledTask.valid());
    const Graphics::GpuCompiledBarrier* firstAcquire = nullptr;
    for(u32 index = 0u; index < compiledTask.plan->prologueBarrierCount; ++index){
        const Graphics::GpuCompiledBarrier& barrier = compiledTask.prologueBarriers[index];
        if(barrier.isInitialOwnerHandoff && barrier.resource == uses.front().resource){
            ASSERT_EQ(firstAcquire, nullptr);
            firstAcquire = &barrier;
        }
    }
    ASSERT_NE(firstAcquire, nullptr);
    const auto validateToken = [&](const Graphics::QueueSubmissionToken& token){
        return Graphics::GpuPacketRuntimeDetail::ValidateInitialOwnershipCompletionToken(
            firstSource, *firstAcquire, queues[0u], token, reads.compiled.deviceGeneration()
        );
    };
    Graphics::QueueSubmissionToken invalidToken = firstToken;
    --invalidToken.value;
    EXPECT_FALSE(validateToken(invalidToken));
    invalidToken = firstToken;
    invalidToken.physicalQueueIndex = queues[1u].id.index;
    EXPECT_FALSE(validateToken(invalidToken));
    invalidToken = firstToken;
    ++invalidToken.deviceGeneration;
    EXPECT_FALSE(validateToken(invalidToken));
    invalidToken = firstToken;
    invalidToken.queue = Graphics::CommandQueue::Transfer;
    EXPECT_FALSE(validateToken(invalidToken));
    invalidToken = firstToken;
    ++invalidToken.value;
    EXPECT_TRUE(validateToken(invalidToken));
    EXPECT_TRUE(ValidatePacketOwnership(reads.declarations, reads.compiled, packetID));
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

TEST(GpuTaskGraphSubmissionValidation, ValidatesEveryInitialOwnerMinimumPhysicalQueueAndGeneration){
    CheckInitialOwnershipFanIn(8u, false);
}

TEST(GpuTaskGraphSubmissionValidation, DISABLED_ClusteredWaitBenchmark1024Tokens){
    BenchmarkClusteredWaits(1024u);
}

TEST(GpuTaskGraphSubmissionValidation, DISABLED_ClusteredWaitBenchmark4096Tokens){
    BenchmarkClusteredWaits(4096u);
}

TEST(GpuTaskGraphSubmissionValidation, DISABLED_OwnershipFanInBenchmark128Sources){
    CheckInitialOwnershipFanIn(128u, true);
}

TEST(GpuTaskGraphSubmissionValidation, DISABLED_OwnershipFanInBenchmark512Sources){
    CheckInitialOwnershipFanIn(512u, true);
}

TEST(GpuTaskGraphSubmissionValidation, DISABLED_OwnershipFanInBenchmark2048Sources){
    CheckInitialOwnershipFanIn(2048u, true);
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

