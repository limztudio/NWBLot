// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "task_graph_test_utils.h"

#include <core/task/gpu/packet_runtime_initial_state_validation.h>
#include <tests/common/vulkan_test_sync.h>

#include <global/timer.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace Tests{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace __hidden_task_graph_packet_preflight_tests{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


using namespace TaskGraphTestUtils;
using TaskGraphTestUtils::TestArena;
using Access = Graphics::GraphicsBackend::VulkanTestDispatchAccess;
using Validation = Graphics::GpuInitialStateHandoffValidation;

constexpr usize s_BenchmarkRepetitions = 8u;


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


struct HandoffContext{
    TestArena testArena;
    Graphics::GraphicsAllocator graphicsAllocator{ testArena.arena };
    Core::CpuTaskScheduler cpuScheduler{ 0u };
    Graphics::GraphicsBackend::VulkanContext context{ graphicsAllocator, cpuScheduler, 1u };
    Graphics::GraphicsBackend::VulkanAllocator allocator{ context };
    Graphics::GraphicsVector<Graphics::TextureHandle> textures{ testArena.arena };
    Graphics::GraphicsVector<Graphics::BufferHandle> buffers{ testArena.arena };
    Graphics::CommandListResourceStateHandoff states{ testArena.arena };
    Core::Alloc::ScratchArena validationScratch{ Name("tests/task/gpu/initial_state_validation") };

    void addResources(const usize count){
        textures.reserve(count);
        buffers.reserve(count);
        for(usize index = 0u; index < count; ++index){
            Graphics::Texture* const texture = NewMetadataOnlyTexture(
                testArena.arena, context, allocator, Graphics::TextureDesc{}.setMipLevels(2u).setArraySize(16u)
            );
            textures.emplace_back(texture, Graphics::TextureHandle::deleter_type(&testArena.arena), AdoptRef);
            Graphics::Buffer* const buffer = NewMetadataOnlyBuffer(
                testArena.arena, context, allocator, Graphics::BufferDesc{}.setByteSize(4096u)
            );
            buffers.emplace_back(buffer, Graphics::BufferHandle::deleter_type(&testArena.arena), AdoptRef);
        }
    }

    void fillDistinctStates(const usize count, const bool permanent){
        auto& textureStates = Access::stateHandoffTextures(states);
        auto& bufferStates = Access::stateHandoffBuffers(states);
        auto& permanentTextures = Access::stateHandoffPermanentTextures(states);
        auto& permanentBuffers = Access::stateHandoffPermanentBuffers(states);
        textureStates.reserve(count);
        bufferStates.reserve(count);
        if(permanent){
            permanentTextures.reserve(count);
            permanentBuffers.reserve(count);
        }
        for(usize index = 0u; index < count; ++index){
            textureStates.push_back({
                .texture = textures[index].get(),
                .state = Graphics::ResourceStates::CopySource,
                .ownerQueue = {},
                .releaseDestinationQueue = {},
            });
            bufferStates.push_back({
                .buffer = buffers[index].get(),
                .state = Graphics::ResourceStates::CopySource,
                .ownerQueue = {},
                .releaseDestinationQueue = {},
                .range = Graphics::BufferRange(0u, 32u),
            });
            if(permanent){
                permanentTextures.push_back({
                    .texture = textures[index].get(),
                    .state = Graphics::ResourceStates::CopySource,
                    .ownerQueue = {},
                    .releaseDestinationQueue = {},
                });
                permanentBuffers.push_back({
                    .buffer = buffers[index].get(),
                    .state = Graphics::ResourceStates::CopySource,
                    .ownerQueue = {},
                    .releaseDestinationQueue = {},
                });
            }
        }
        Access::validateStateHandoff(states, 1u);
    }
};

static void BenchmarkHandoffValidation(const usize stateCount, const bool permanent){
    HandoffContext context;
    context.addResources(stateCount);
    context.fillDistinctStates(stateCount, permanent);
    u64 validationNanoseconds = 0u;
    u64 queryNanoseconds = 0u;
    u64 scratchPeakBytes = 0u;
    for(usize iteration = 0u; iteration <= s_BenchmarkRepetitions; ++iteration){
        Core::Alloc::ScratchArena scratch{ Name("tests/task/gpu/initial_state_validation") };
        {
            const Timer validationBegin = TimerNow();
            Validation validation(context.states, scratch);
            ASSERT_TRUE(validation.validate());
            if(iteration != 0u)
                validationNanoseconds += DurationInNS<u64>(TimerNow(), validationBegin);
            if(permanent){
                const Timer queryBegin = TimerNow();
                for(usize index = 0u; index < stateCount; ++index){
                    Graphics::ResourceStates::Mask state = Graphics::ResourceStates::Unknown;
                    ASSERT_TRUE(validation.permanentTextureState(context.textures[index].get(), state));
                    EXPECT_EQ(state, Graphics::ResourceStates::CopySource);
                    ASSERT_TRUE(validation.permanentBufferState(context.buffers[index].get(), state));
                    EXPECT_EQ(state, Graphics::ResourceStates::CopySource);
                }
                if(iteration != 0u)
                    queryNanoseconds += DurationInNS<u64>(TimerNow(), queryBegin);
            }
        }
        scratchPeakBytes = Max(scratchPeakBytes, scratch.memoryStats().peakUsedBytes);
        EXPECT_EQ(scratch.memoryStats().usedBytes, 0u);
    }
    RecordUnsignedTestProperty("validation_ns", validationNanoseconds);
    RecordUnsignedTestProperty("permanent_query_ns", queryNanoseconds);
    testing::Test::RecordProperty("state_count_per_type", static_cast<i32>(stateCount));
    testing::Test::RecordProperty("repetitions", static_cast<i32>(s_BenchmarkRepetitions));
    RecordUnsignedTestProperty("scratch_peak_bytes", scratchPeakBytes);
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


TEST(GpuPacketPreflight, TextureConflictsRequireExactResourceAndSubresourceStateAgreement){
    const u32 stateCounts[] = { 2u, 12u };
    for(const u32 stateCount : stateCounts){
        SCOPED_TRACE(stateCount);
        HandoffContext context;
        context.addResources(2u);
        auto& states = Access::stateHandoffTextures(context.states);
        for(u32 index = 0u; index < stateCount; ++index){
            states.push_back({
                .texture = context.textures[0u].get(),
                .mipLevel = index % 2u,
                .arraySlice = index / 2u,
                .state = index == 1u ? Graphics::ResourceStates::CopyDest : Graphics::ResourceStates::CopySource,
                .ownerQueue = {},
                .releaseDestinationQueue = {},
            });
        }
        states.push_back({
            .texture = context.textures[1u].get(),
            .state = Graphics::ResourceStates::CopyDest,
            .ownerQueue = {},
            .releaseDestinationQueue = {},
        });
        states.push_back({});
        states.push_back(states[0u]);
        Access::validateStateHandoff(context.states, 1u);
        Graphics::CommandListResourceStateHandoff snapshot(context.testArena.arena);
        ASSERT_TRUE(snapshot.copyFrom(context.states));
        EXPECT_TRUE(Validation(context.states, context.validationScratch).validate());
        EXPECT_TRUE(context.states.equivalentTo(snapshot));

        states.back().state = Graphics::ResourceStates::CopyDest;
        ASSERT_TRUE(snapshot.copyFrom(context.states));
        EXPECT_FALSE(Validation(context.states, context.validationScratch).validate());
        EXPECT_TRUE(context.states.equivalentTo(snapshot));
    }
}

TEST(GpuPacketPreflight, BufferConflictsRejectOverlapAndWrappedRangesWithoutChangingSnapshot){
    const u32 stateCounts[] = { 2u, 12u };
    for(const u32 stateCount : stateCounts){
        SCOPED_TRACE(stateCount);
        HandoffContext context;
        context.addResources(2u);
        auto& states = Access::stateHandoffBuffers(context.states);
        for(u32 index = 0u; index < stateCount; ++index){
            const u32 rangeIndex = (index * 5u) % stateCount;
            states.push_back({
                .buffer = context.buffers[0u].get(),
                .state = Graphics::ResourceStates::CopySource,
                .ownerQueue = {},
                .releaseDestinationQueue = {},
                .range = Graphics::BufferRange(rangeIndex * 32u, 32u),
            });
        }
        states.push_back({
            .buffer = context.buffers[1u].get(),
            .state = Graphics::ResourceStates::CopySource,
            .ownerQueue = {},
            .releaseDestinationQueue = {},
            .range = Graphics::BufferRange(0u, 32u),
        });
        states.push_back({});
        Access::validateStateHandoff(context.states, 1u);
        Graphics::CommandListResourceStateHandoff snapshot(context.testArena.arena);
        ASSERT_TRUE(snapshot.copyFrom(context.states));
        EXPECT_TRUE(Validation(context.states, context.validationScratch).validate());
        EXPECT_TRUE(context.states.equivalentTo(snapshot));

        states.push_back(states[0u]);
        ASSERT_TRUE(snapshot.copyFrom(context.states));
        EXPECT_FALSE(Validation(context.states, context.validationScratch).validate());
        EXPECT_TRUE(context.states.equivalentTo(snapshot));
        states.pop_back();
        const u64 tailOffset = stateCount * 32u;
        states.push_back({
            .buffer = context.buffers[0u].get(),
            .state = Graphics::ResourceStates::CopySource,
            .ownerQueue = {},
            .releaseDestinationQueue = {},
            .range = Graphics::BufferRange(tailOffset, Graphics::BufferRange::s_AllBytes),
        });
        EXPECT_TRUE(Validation(context.states, context.validationScratch).validate());
        states.push_back({
            .buffer = context.buffers[0u].get(),
            .state = Graphics::ResourceStates::CopySource,
            .ownerQueue = {},
            .releaseDestinationQueue = {},
            .range = Graphics::BufferRange(tailOffset + 128u, 1u),
        });
        ASSERT_TRUE(snapshot.copyFrom(context.states));
        EXPECT_FALSE(Validation(context.states, context.validationScratch).validate());
        EXPECT_TRUE(context.states.equivalentTo(snapshot));
        states.pop_back();
        states.pop_back();
        states.push_back({
            .buffer = context.buffers[0u].get(),
            .state = Graphics::ResourceStates::CopySource,
            .ownerQueue = {},
            .releaseDestinationQueue = {},
            .range = Graphics::BufferRange(512u, Limit<u64>::s_Max - 8u),
        });
        ASSERT_TRUE(snapshot.copyFrom(context.states));
        EXPECT_FALSE(Validation(context.states, context.validationScratch).validate());
        EXPECT_TRUE(context.states.equivalentTo(snapshot));
    }
}

TEST(GpuPacketPreflight, PermanentStatesRequireAgreementWithTransientStatesAndDuplicateDeclarations){
    const usize stateCounts[] = { 2u, 12u };
    for(const usize stateCount : stateCounts){
        SCOPED_TRACE(stateCount);
        HandoffContext context;
        context.addResources(stateCount + 1u);
        context.fillDistinctStates(stateCount, true);
        auto& permanentTextures = Access::stateHandoffPermanentTextures(context.states);
        auto& permanentBuffers = Access::stateHandoffPermanentBuffers(context.states);
        permanentTextures.push_back({});
        permanentBuffers.push_back({});
        permanentTextures.push_back(permanentTextures[0u]);
        permanentBuffers.push_back(permanentBuffers[0u]);
        Graphics::ResourceStates::Mask state = Graphics::ResourceStates::Unknown;
        Validation validation(context.states, context.validationScratch);
        ASSERT_TRUE(validation.validate());
        ASSERT_TRUE(validation.permanentTextureState(context.textures[0u].get(), state));
        EXPECT_EQ(state, Graphics::ResourceStates::CopySource);
        ASSERT_TRUE(validation.permanentBufferState(context.buffers[0u].get(), state));
        EXPECT_EQ(state, Graphics::ResourceStates::CopySource);
        ASSERT_TRUE(validation.permanentTextureState(context.textures[stateCount].get(), state));
        EXPECT_EQ(state, Graphics::ResourceStates::Unknown);
        ASSERT_TRUE(validation.permanentBufferState(context.buffers[stateCount].get(), state));
        EXPECT_EQ(state, Graphics::ResourceStates::Unknown);

        Graphics::CommandListResourceStateHandoff snapshot(context.testArena.arena);
        permanentTextures.back().state = Graphics::ResourceStates::CopyDest;
        ASSERT_TRUE(snapshot.copyFrom(context.states));
        EXPECT_FALSE(Validation(context.states, context.validationScratch).validate());
        EXPECT_TRUE(context.states.equivalentTo(snapshot));
        permanentTextures.back().state = Graphics::ResourceStates::CopySource;
        auto& transientTextures = Access::stateHandoffTextures(context.states);
        transientTextures[0u].state = Graphics::ResourceStates::CopyDest;
        ASSERT_TRUE(snapshot.copyFrom(context.states));
        EXPECT_FALSE(Validation(context.states, context.validationScratch).validate());
        EXPECT_TRUE(context.states.equivalentTo(snapshot));
        transientTextures[0u].state = Graphics::ResourceStates::CopySource;
        permanentBuffers.back().state = Graphics::ResourceStates::CopyDest;
        ASSERT_TRUE(snapshot.copyFrom(context.states));
        EXPECT_FALSE(Validation(context.states, context.validationScratch).validate());
        EXPECT_TRUE(context.states.equivalentTo(snapshot));
        permanentBuffers.back().state = Graphics::ResourceStates::CopySource;
        auto& transientBuffers = Access::stateHandoffBuffers(context.states);
        transientBuffers[0u].state = Graphics::ResourceStates::CopyDest;
        ASSERT_TRUE(snapshot.copyFrom(context.states));
        EXPECT_FALSE(Validation(context.states, context.validationScratch).validate());
        EXPECT_TRUE(context.states.equivalentTo(snapshot));
        transientBuffers[0u].state = Graphics::ResourceStates::CopySource;
        permanentBuffers[0u].state = Graphics::ResourceStates::Unknown;
        ASSERT_TRUE(snapshot.copyFrom(context.states));
        EXPECT_FALSE(Validation(context.states, context.validationScratch).validate());
        EXPECT_TRUE(context.states.equivalentTo(snapshot));
    }
}

TEST(GpuPacketPreflight, DISABLED_TransientHandoffBenchmark8States){
    BenchmarkHandoffValidation(8u, false);
}

TEST(GpuPacketPreflight, DISABLED_TransientHandoffBenchmark1024States){
    BenchmarkHandoffValidation(1024u, false);
}

TEST(GpuPacketPreflight, DISABLED_TransientHandoffBenchmark4096States){
    BenchmarkHandoffValidation(4096u, false);
}

TEST(GpuPacketPreflight, DISABLED_PermanentHandoffBenchmark8States){
    BenchmarkHandoffValidation(8u, true);
}

TEST(GpuPacketPreflight, DISABLED_PermanentHandoffBenchmark1024States){
    BenchmarkHandoffValidation(1024u, true);
}

TEST(GpuPacketPreflight, DISABLED_PermanentHandoffBenchmark4096States){
    BenchmarkHandoffValidation(4096u, true);
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

