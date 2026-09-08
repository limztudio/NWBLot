// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include <core/graphics/rhi/command.h>
#include <core/alloc/scratch.h>

#include <tests/common/graphics_metadata_test_objects.h>
#include <tests/common/vulkan_test_sync.h>

#include <gtest/gtest.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace __hidden_buffer_range_handoff_tests{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


using namespace NWB;
namespace Core = NWB::Core;
using Access = Core::GraphicsBackend::VulkanTestDispatchAccess;
using Handoff = Core::CommandListResourceStateHandoff;

inline constexpr u16 s_DeviceGeneration = 17u;
inline constexpr Core::GpuPhysicalQueueId s_Owner{ 3u, s_DeviceGeneration };
inline constexpr Core::GpuPhysicalQueueId s_Destination{ 7u, s_DeviceGeneration };

struct RangeContext{
    Core::Alloc::GlobalArena arena{ Name("tests/buffer_range_handoff/inputs") };
    Core::GraphicsAllocator graphicsAllocator{ arena };
    Core::Alloc::CpuTaskScheduler cpuScheduler{ 0u };
    Core::GraphicsBackend::VulkanContext context{ graphicsAllocator, cpuScheduler, s_DeviceGeneration };
    Core::GraphicsBackend::VulkanAllocator allocator{ context };
    Core::BufferHandle buffer{
        Tests::NewMetadataOnlyBuffer(arena, context, allocator, Core::BufferDesc{}.setByteSize(256u)),
        Core::BufferHandle::deleter_type(&arena),
        AdoptRef
    };

    void addState(
        Handoff& handoff,
        const Core::BufferRange range,
        const Core::ResourceStates::Mask state,
        const Core::GpuPhysicalQueueId owner = s_Owner,
        const Core::GpuPhysicalQueueId destination = {}
    ){
        Access::stateHandoffBuffers(handoff).push_back({
            .buffer = buffer.get(),
            .state = state,
            .ownerQueue = owner,
            .releaseDestinationQueue = destination,
            .range = range,
        });
        Access::validateStateHandoff(handoff, s_DeviceGeneration);
    }
};


TEST(BufferRangeHandoff, FanInCombinesDisjointBranchChangesAgainstWholeBufferBase){
    RangeContext context;
    Core::Alloc::ScratchArena scratch(Name("tests/buffer_range_handoff/operation"));
    Handoff base(context.arena);
    Handoff first(context.arena);
    Handoff second(context.arena);
    Handoff result(context.arena);
    context.addState(base, { 0u, 256u }, Core::ResourceStates::ShaderResource);
    context.addState(first, { 0u, 64u }, Core::ResourceStates::CopyDest);
    context.addState(first, { 64u, 192u }, Core::ResourceStates::ShaderResource);
    context.addState(second, { 0u, 192u }, Core::ResourceStates::ShaderResource);
    context.addState(second, { 192u, 64u }, Core::ResourceStates::CopySource);
    const Handoff* const branches[] = { &first, &second };

    ASSERT_TRUE(result.buildFanIn(base, branches, LengthOf(branches), scratch));
    const auto& states = Access::stateHandoffBuffers(result);
    ASSERT_EQ(states.size(), 3u);
    EXPECT_EQ(states[0u].range, Core::BufferRange(0u, 64u));
    EXPECT_EQ(states[0u].state, Core::ResourceStates::CopyDest);
    EXPECT_EQ(states[1u].range, Core::BufferRange(64u, 128u));
    EXPECT_EQ(states[1u].state, Core::ResourceStates::ShaderResource);
    EXPECT_EQ(states[2u].range, Core::BufferRange(192u, 64u));
    EXPECT_EQ(states[2u].state, Core::ResourceStates::CopySource);
    EXPECT_TRUE(result.coversBufferWithOwnership(context.buffer.get(), s_Owner, s_Owner));

    const ArenaMemoryStats warmedStats = scratch.memoryStats();
    EXPECT_EQ(warmedStats.usedBytes, 0u);
    for(u32 repeat = 0u; repeat < 32u; ++repeat)
        ASSERT_TRUE(result.buildFanIn(base, branches, LengthOf(branches), scratch));
    EXPECT_EQ(scratch.memoryStats().usedBytes, 0u);
    EXPECT_EQ(scratch.memoryStats().reservedBytes, warmedStats.reservedBytes);
}

TEST(BufferRangeHandoff, FanInRejectsConflictingOverlapAndKeepsIdenticalOverlap){
    RangeContext context;
    Core::Alloc::ScratchArena scratch(Name("tests/buffer_range_handoff/operation"));
    Handoff base(context.arena);
    Handoff first(context.arena);
    Handoff second(context.arena);
    Handoff result(context.arena);
    Access::validateStateHandoff(base, s_DeviceGeneration);
    context.addState(first, { 0u, 128u }, Core::ResourceStates::CopyDest);
    context.addState(second, { 64u, 128u }, Core::ResourceStates::CopySource);
    const Handoff* const branches[] = { &first, &second };
    EXPECT_FALSE(result.buildFanIn(base, branches, LengthOf(branches), scratch));
    EXPECT_FALSE(result.valid());

    Access::stateHandoffBuffers(second)[0u].state = Core::ResourceStates::CopyDest;
    ASSERT_TRUE(result.buildFanIn(base, branches, LengthOf(branches), scratch));
    const auto& states = Access::stateHandoffBuffers(result);
    ASSERT_EQ(states.size(), 1u);
    EXPECT_EQ(states[0u].range, Core::BufferRange(0u, 192u));
    EXPECT_FALSE(result.coversBufferWithOwnership(context.buffer.get(), s_Owner, s_Owner));
}

TEST(BufferRangeHandoff, FanInPreservesOwnershipOnDifferentIntervals){
    RangeContext context;
    Core::Alloc::ScratchArena scratch(Name("tests/buffer_range_handoff/operation"));
    Handoff base(context.arena);
    Handoff first(context.arena);
    Handoff second(context.arena);
    Handoff result(context.arena);
    Access::validateStateHandoff(base, s_DeviceGeneration);
    context.addState(first, { 0u, 128u }, Core::ResourceStates::CopyDest, s_Owner, s_Destination);
    context.addState(second, { 128u, 128u }, Core::ResourceStates::CopyDest, s_Destination, {});
    const Handoff* const branches[] = { &first, &second };

    ASSERT_TRUE(result.buildFanIn(base, branches, LengthOf(branches), scratch));
    ASSERT_EQ(Access::stateHandoffBuffers(result).size(), 2u);
    EXPECT_TRUE(result.coversBufferWithOwnership(context.buffer.get(), s_Owner, s_Destination, { 0u, 128u }));
    EXPECT_TRUE(result.coversBufferWithOwnership(context.buffer.get(), s_Destination, s_Destination, { 128u, 128u }));
    EXPECT_FALSE(result.coversBufferWithOwnership(context.buffer.get(), s_Owner, s_Destination));
}

TEST(BufferRangeHandoff, FanInRejectsOverlappingStatesWithinOneSnapshot){
    RangeContext context;
    Core::Alloc::ScratchArena scratch(Name("tests/buffer_range_handoff/operation"));
    Handoff base(context.arena);
    Handoff branch(context.arena);
    Handoff result(context.arena);
    Access::validateStateHandoff(base, s_DeviceGeneration);
    context.addState(branch, { 0u, 128u }, Core::ResourceStates::CopyDest);
    context.addState(branch, { 64u, 128u }, Core::ResourceStates::CopyDest);
    const Handoff* const branches[] = { &branch };
    EXPECT_FALSE(result.buildFanIn(base, branches, LengthOf(branches), scratch));
    EXPECT_FALSE(result.valid());
}

TEST(BufferRangeHandoff, SubsetClipsIntervalsAndSupportsInPlaceSelection){
    RangeContext context;
    Handoff source(context.arena);
    Handoff result(context.arena);
    context.addState(source, { 0u, 64u }, Core::ResourceStates::CopyDest);
    context.addState(source, { 64u, 128u }, Core::ResourceStates::ShaderResource);
    context.addState(source, { 192u, 64u }, Core::ResourceStates::CopySource);

    ASSERT_TRUE(result.buildBufferRangeSubset(source, context.buffer.get(), { 32u, 192u }));
    const auto& states = Access::stateHandoffBuffers(result);
    ASSERT_EQ(states.size(), 3u);
    EXPECT_EQ(states[0u].range, Core::BufferRange(32u, 32u));
    EXPECT_EQ(states[1u].range, Core::BufferRange(64u, 128u));
    EXPECT_EQ(states[2u].range, Core::BufferRange(192u, 32u));
    EXPECT_TRUE(result.coversBufferWithOwnership(context.buffer.get(), s_Owner, s_Owner, { 32u, 192u }));
    EXPECT_FALSE(result.coversBufferWithOwnership(context.buffer.get(), s_Owner, s_Owner));
    ASSERT_TRUE(source.buildBufferRangeSubset(source, context.buffer.get(), { 32u, 192u }));
    EXPECT_TRUE(result.equivalentTo(source));

    Access::stateHandoffBuffers(source)[0u].range.byteOffset = 33u;
    EXPECT_FALSE(result.equivalentTo(source));
}

TEST(BufferRangeHandoff, WholeResourceSubsetRetainsEveryBufferInterval){
    RangeContext context;
    Core::Alloc::ScratchArena scratch(Name("tests/buffer_range_handoff/operation"));
    Handoff source(context.arena);
    Handoff result(context.arena);
    context.addState(source, { 0u, 64u }, Core::ResourceStates::CopyDest);
    context.addState(source, { 192u, 64u }, Core::ResourceStates::CopySource);
    Core::Buffer* const selected[] = { context.buffer.get() };
    ASSERT_TRUE(result.buildResourceSubset(source, nullptr, 0u, selected, LengthOf(selected), scratch));
    EXPECT_TRUE(result.equivalentTo(source));
    EXPECT_FALSE(result.coversBufferWithOwnership(context.buffer.get(), s_Owner, s_Owner));
    EXPECT_TRUE(result.coversBufferWithOwnership(context.buffer.get(), s_Owner, s_Owner, { 192u, 64u }));
}

TEST(BufferRangeHandoff, OwnershipRequiresGapFreeAndNonoverlappingCoverage){
    RangeContext context;
    Handoff source(context.arena);
    context.addState(source, { 128u, 128u }, Core::ResourceStates::CopyDest);
    context.addState(source, { 0u, 128u }, Core::ResourceStates::ShaderResource);
    EXPECT_TRUE(source.coversBufferWithOwnership(context.buffer.get(), s_Owner, s_Owner));

    Access::stateHandoffBuffers(source)[1u].range.byteSize = 127u;
    EXPECT_FALSE(source.coversBufferWithOwnership(context.buffer.get(), s_Owner, s_Owner));
    Access::stateHandoffBuffers(source)[1u].range.byteSize = 129u;
    EXPECT_FALSE(source.coversBufferWithOwnership(context.buffer.get(), s_Owner, s_Owner));
    EXPECT_FALSE(source.coversBufferWithOwnership(context.buffer.get(), s_Owner, s_Owner, { 256u, 1u }));
    EXPECT_FALSE(source.coversBufferWithOwnership(context.buffer.get(), s_Owner, s_Owner, { 0u, 0u }));
}

TEST(BufferRangeHandoff, SymbolicWholeBufferStateIsClippedToRequestedTail){
    RangeContext context;
    Handoff source(context.arena);
    Handoff result(context.arena);
    context.addState(source, Core::s_EntireBuffer, Core::ResourceStates::ShaderResource);
    ASSERT_TRUE(result.buildBufferRangeSubset(source, context.buffer.get(), { 192u, Core::BufferRange::AllBytes }));
    ASSERT_EQ(Access::stateHandoffBuffers(result).size(), 1u);
    EXPECT_EQ(Access::stateHandoffBuffers(result)[0u].range, Core::BufferRange(192u, 64u));
    EXPECT_TRUE(result.coversBufferWithOwnership(context.buffer.get(), s_Owner, s_Owner, { 192u, 64u }));

    EXPECT_FALSE(result.buildBufferRangeSubset(source, context.buffer.get(), { 256u, 1u }));
    EXPECT_EQ(Access::stateHandoffBuffers(result)[0u].range, Core::BufferRange(192u, 64u));
}

TEST(BufferRangeHandoff, PermanentBufferStateRemainsWholeResourceInRangeSubset){
    RangeContext context;
    Handoff source(context.arena);
    Handoff result(context.arena);
    Access::stateHandoffPermanentBuffers(source).push_back({
        .buffer = context.buffer.get(),
        .state = Core::ResourceStates::ShaderResource,
        .ownerQueue = s_Owner,
        .releaseDestinationQueue = s_Destination,
    });
    Access::validateStateHandoff(source, s_DeviceGeneration);
    ASSERT_TRUE(result.buildBufferRangeSubset(source, context.buffer.get(), { 64u, 64u }));
    ASSERT_EQ(Access::stateHandoffPermanentBuffers(result).size(), 1u);
    EXPECT_EQ(Access::stateHandoffPermanentBuffers(result)[0u].range, Core::s_EntireBuffer);
    EXPECT_TRUE(result.coversBufferWithOwnership(context.buffer.get(), s_Owner, s_Destination));
}


TEST(BufferRangeHandoff, PendingReleaseIntervalsRemainDistinctAndCannotBeClipped){
    RangeContext context;
    Core::Alloc::ScratchArena scratch(Name("tests/buffer_range_handoff/operation"));
    Handoff base(context.arena);
    Handoff first(context.arena);
    Handoff second(context.arena);
    Handoff result(context.arena);
    Handoff subset(context.arena);
    Access::validateStateHandoff(base, s_DeviceGeneration);
    context.addState(first, { 0u, 128u }, Core::ResourceStates::CopyDest, s_Owner, s_Destination);
    context.addState(second, { 128u, 128u }, Core::ResourceStates::CopyDest, s_Owner, s_Destination);
    const Handoff* const branches[] = { &first, &second };
    ASSERT_TRUE(result.buildFanIn(base, branches, LengthOf(branches), scratch));
    const auto& states = Access::stateHandoffBuffers(result);
    ASSERT_EQ(states.size(), 2u);
    EXPECT_EQ(states[0u].range, Core::BufferRange(0u, 128u));
    EXPECT_EQ(states[1u].range, Core::BufferRange(128u, 128u));
    EXPECT_FALSE(subset.buildBufferRangeSubset(result, context.buffer.get(), { 64u, 128u }));
    EXPECT_FALSE(result.coversBufferWithOwnership(context.buffer.get(), s_Owner, s_Destination, { 64u, 128u }));
    ASSERT_TRUE(subset.buildBufferRangeSubset(result, context.buffer.get(), { 0u, 128u }));
    EXPECT_TRUE(subset.coversBufferWithOwnership(context.buffer.get(), s_Owner, s_Destination, { 0u, 128u }));

    Access::stateHandoffBuffers(second)[0u].range = { 64u, 64u };
    EXPECT_FALSE(result.buildFanIn(base, branches, LengthOf(branches), scratch));
    EXPECT_FALSE(result.valid());
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

