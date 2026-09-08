// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include <core/task/gpu/persistent_state.h>
#include <core/alloc/scratch.h>

#include <tests/common/graphics_metadata_test_objects.h>
#include <tests/common/vulkan_test_sync.h>

#include <global/arena_memory.h>
#include <global/text_utils.h>
#include <global/timer.h>

#include <gtest/gtest.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace __hidden_persistent_state_subset_tests{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


using namespace NWB;
namespace Core = NWB::Core;
using Access = Core::GraphicsBackend::VulkanTestDispatchAccess;
using Handoff = Core::CommandListResourceStateHandoff;
using Cache = Core::GpuPersistentResourceStateCache;
using BufferVector = Vector<Core::BufferHandle, Core::Alloc::GlobalArena>;

inline constexpr u16 s_DeviceGeneration = 17u;
inline constexpr Core::GpuPhysicalQueueId s_OwnerQueue{ .index = 3u, .deviceGeneration = s_DeviceGeneration };
inline constexpr Core::GpuPhysicalQueueId s_ReleaseQueue{ .index = 7u, .deviceGeneration = s_DeviceGeneration };

struct SubsetContext{
    Core::Alloc::GlobalArena arena{ Name("tests/persistent_state_subset/inputs") };
    Core::GraphicsAllocator graphicsAllocator{ arena };
    Core::CpuTaskScheduler cpuScheduler{ 0u };
    Core::GraphicsBackend::VulkanContext context{ graphicsAllocator, cpuScheduler, s_DeviceGeneration };
    Core::GraphicsBackend::VulkanAllocator allocator{ context };
    BufferVector buffers{ arena };
    Vector<Core::TextureHandle, Core::Alloc::GlobalArena> textures{ arena };
    Handoff source{ arena };

    void addBuffers(const usize count){
        buffers.reserve(count);
        for(usize index = 0u; index < count; ++index){
            Core::Buffer* const buffer = Tests::NewMetadataOnlyBuffer(
                arena, context, allocator, Core::BufferDesc{}.setByteSize(256u)
            );
            buffers.emplace_back(buffer, Core::BufferHandle::deleter_type(&arena), AdoptRef);
        }
    }

    void addTextures(const usize count){
        textures.reserve(count);
        for(usize index = 0u; index < count; ++index){
            Core::Texture* const texture = Tests::NewMetadataOnlyTexture(
                arena, context, allocator, Core::TextureDesc{}.setMipLevels(2u).setArraySize(2u)
            );
            textures.emplace_back(texture, Core::TextureHandle::deleter_type(&arena), AdoptRef);
        }
    }

    void fillBufferStates(){
        auto& states = Access::stateHandoffBuffers(source);
        states.reserve(buffers.size());
        for(const Core::BufferHandle& buffer : buffers){
            states.push_back({
                .buffer = buffer.get(),
                .state = Core::ResourceStates::ShaderResource,
                .queueSharing = Core::ResourceQueueSharing::Exclusive,
                .ownerQueue = s_OwnerQueue,
                .releaseDestinationQueue = s_ReleaseQueue,
            });
        }
        Access::validateStateHandoff(source, s_DeviceGeneration);
    }

    void fillMixedStates(){
        addBuffers(3u);
        addTextures(3u);
        auto& textureStates = Access::stateHandoffTextures(source);
        textureStates.push_back({
            .texture = textures[0u].get(), .mipLevel = 0u, .arraySlice = 1u,
            .state = Core::ResourceStates::ShaderResource,
            .queueSharing = Core::ResourceQueueSharing::Exclusive,
            .ownerQueue = s_OwnerQueue, .releaseDestinationQueue = s_ReleaseQueue,
        });
        textureStates.push_back({
            .texture = textures[2u].get(), .mipLevel = 0u, .arraySlice = 0u,
            .state = Core::ResourceStates::CopyDest,
            .queueSharing = Core::ResourceQueueSharing::Exclusive,
            .ownerQueue = {}, .releaseDestinationQueue = {},
        });
        textureStates.push_back({
            .texture = textures[0u].get(), .mipLevel = 1u, .arraySlice = 0u,
            .state = Core::ResourceStates::CopySource,
            .queueSharing = Core::ResourceQueueSharing::GraphicsAndAsyncCompute,
            .ownerQueue = {}, .releaseDestinationQueue = {},
        });
        Access::stateHandoffPermanentTextures(source).push_back({
            .texture = textures[1u].get(), .state = Core::ResourceStates::ShaderResource,
            .queueSharing = Core::ResourceQueueSharing::Exclusive,
            .ownerQueue = s_OwnerQueue, .releaseDestinationQueue = s_ReleaseQueue,
        });
        Access::stateHandoffBuffers(source).push_back({
            .buffer = buffers[0u].get(), .state = Core::ResourceStates::UnorderedAccess,
            .queueSharing = Core::ResourceQueueSharing::Exclusive,
            .ownerQueue = s_OwnerQueue, .releaseDestinationQueue = s_ReleaseQueue,
        });
        Access::stateHandoffBuffers(source).push_back({
            .buffer = buffers[2u].get(), .state = Core::ResourceStates::CopyDest,
            .queueSharing = Core::ResourceQueueSharing::Exclusive,
            .ownerQueue = {}, .releaseDestinationQueue = {},
        });
        Access::stateHandoffPermanentBuffers(source).push_back({
            .buffer = buffers[1u].get(), .state = Core::ResourceStates::CopySource,
            .queueSharing = Core::ResourceQueueSharing::GraphicsAndAsyncCompute,
            .ownerQueue = {}, .releaseDestinationQueue = {},
        });
        Access::validateStateHandoff(source, s_DeviceGeneration);
    }
};


template<typename StateVector>
void ExpectSameRecords(const StateVector& actual, const StateVector& expected){
    ASSERT_EQ(actual.size(), expected.size());
    for(usize index = 0u; index < actual.size(); ++index){
        const auto& lhs = actual[index];
        const auto& rhs = expected[index];
        if constexpr(requires { lhs.texture; })
            EXPECT_EQ(lhs.texture, rhs.texture);
        else
            EXPECT_EQ(lhs.buffer, rhs.buffer);
        if constexpr(requires { lhs.mipLevel; }){
            EXPECT_EQ(lhs.mipLevel, rhs.mipLevel);
            EXPECT_EQ(lhs.arraySlice, rhs.arraySlice);
        }
        EXPECT_EQ(lhs.state, rhs.state);
        EXPECT_EQ(lhs.queueSharing, rhs.queueSharing);
        EXPECT_EQ(lhs.ownerQueue, rhs.ownerQueue);
        EXPECT_EQ(lhs.releaseDestinationQueue, rhs.releaseDestinationQueue);
    }
}

void ExpectSameHandoff(const Handoff& actual, const Handoff& expected){
    EXPECT_EQ(actual.valid(), expected.valid());
    EXPECT_EQ(actual.deviceGeneration(), expected.deviceGeneration());
    ExpectSameRecords(Access::stateHandoffTextures(actual), Access::stateHandoffTextures(expected));
    ExpectSameRecords(Access::stateHandoffBuffers(actual), Access::stateHandoffBuffers(expected));
    ExpectSameRecords(Access::stateHandoffPermanentTextures(actual), Access::stateHandoffPermanentTextures(expected));
    ExpectSameRecords(Access::stateHandoffPermanentBuffers(actual), Access::stateHandoffPermanentBuffers(expected));
}


TEST(PersistentStateSubset, PreservesSourceOrderSubresourcesAndAllFourStateCategories){
    Core::Alloc::ScratchArena scratch(Name("tests/persistent_state_subset/operation"));
    SubsetContext context;
    context.fillMixedStates();
    Handoff result(context.arena);
    Core::Texture* const textures[] = { context.textures[1u].get(), nullptr, context.textures[0u].get(), context.textures[1u].get() };
    Core::Buffer* const buffers[] = { context.buffers[1u].get(), context.buffers[0u].get(), nullptr, context.buffers[0u].get() };
    ASSERT_TRUE(result.buildResourceSubset(context.source, textures, LengthOf(textures), buffers, LengthOf(buffers), scratch));
    EXPECT_EQ(result.deviceGeneration(), s_DeviceGeneration);
    const auto& textureStates = Access::stateHandoffTextures(result);
    ASSERT_EQ(textureStates.size(), 2u);
    EXPECT_EQ(textureStates[0u].texture, context.textures[0u].get());
    EXPECT_EQ(textureStates[0u].mipLevel, 0u);
    EXPECT_EQ(textureStates[0u].arraySlice, 1u);
    EXPECT_EQ(textureStates[0u].ownerQueue, s_OwnerQueue);
    EXPECT_EQ(textureStates[0u].releaseDestinationQueue, s_ReleaseQueue);
    EXPECT_EQ(textureStates[1u].texture, context.textures[0u].get());
    EXPECT_EQ(textureStates[1u].mipLevel, 1u);
    EXPECT_EQ(textureStates[1u].arraySlice, 0u);
    EXPECT_EQ(textureStates[1u].state, Core::ResourceStates::CopySource);
    EXPECT_EQ(textureStates[1u].queueSharing, Core::ResourceQueueSharing::GraphicsAndAsyncCompute);
    const auto& bufferStates = Access::stateHandoffBuffers(result);
    ASSERT_EQ(bufferStates.size(), 1u);
    EXPECT_EQ(bufferStates[0u].buffer, context.buffers[0u].get());
    EXPECT_EQ(bufferStates[0u].state, Core::ResourceStates::UnorderedAccess);
    EXPECT_EQ(bufferStates[0u].ownerQueue, s_OwnerQueue);
    EXPECT_EQ(bufferStates[0u].releaseDestinationQueue, s_ReleaseQueue);
    ASSERT_EQ(Access::stateHandoffPermanentTextures(result).size(), 1u);
    EXPECT_EQ(Access::stateHandoffPermanentTextures(result)[0u].texture, context.textures[1u].get());
    EXPECT_EQ(Access::stateHandoffPermanentTextures(result)[0u].ownerQueue, s_OwnerQueue);
    EXPECT_EQ(Access::stateHandoffPermanentTextures(result)[0u].releaseDestinationQueue, s_ReleaseQueue);
    ASSERT_EQ(Access::stateHandoffPermanentBuffers(result).size(), 1u);
    EXPECT_EQ(Access::stateHandoffPermanentBuffers(result)[0u].buffer, context.buffers[1u].get());
    EXPECT_EQ(Access::stateHandoffPermanentBuffers(result)[0u].queueSharing, Core::ResourceQueueSharing::GraphicsAndAsyncCompute);

    Handoff inPlace(context.arena);
    ASSERT_TRUE(inPlace.copyFrom(context.source));
    ASSERT_TRUE(inPlace.buildResourceSubset(inPlace, textures, LengthOf(textures), buffers, LengthOf(buffers), scratch));
    ExpectSameHandoff(inPlace, result);
    ASSERT_TRUE(inPlace.buildResourceSubset(inPlace, nullptr, 0u, nullptr, 0u, scratch));
    EXPECT_TRUE(inPlace.valid());
    EXPECT_TRUE(inPlace.empty());
    EXPECT_EQ(inPlace.deviceGeneration(), s_DeviceGeneration);
}

TEST(PersistentStateSubset, InvalidRawInputsPreserveTheDestinationSnapshot){
    Core::Alloc::ScratchArena scratch(Name("tests/persistent_state_subset/operation"));
    SubsetContext context;
    context.fillMixedStates();
    Handoff destination(context.arena);
    ASSERT_TRUE(destination.copyFrom(context.source));
    Handoff invalid(context.arena);
    EXPECT_FALSE(destination.buildResourceSubset(invalid, nullptr, 0u, nullptr, 0u, scratch));
    ExpectSameHandoff(destination, context.source);
    EXPECT_FALSE(destination.buildResourceSubset(context.source, nullptr, 1u, nullptr, 0u, scratch));
    ExpectSameHandoff(destination, context.source);
    EXPECT_FALSE(destination.buildResourceSubset(context.source, nullptr, 0u, nullptr, 1u, scratch));
    ExpectSameHandoff(destination, context.source);
    Access::validateStateHandoff(invalid, 0u);
    EXPECT_FALSE(destination.buildResourceSubset(invalid, nullptr, 0u, nullptr, 0u, scratch));
    ExpectSameHandoff(destination, context.source);
}

TEST(PersistentStateSubset, RetainsRequestedHandlesEvenWhenTheSourceHasNoMatchingState){
    Core::Alloc::ScratchArena scratch(Name("tests/persistent_state_subset/operation"));
    SubsetContext context;
    context.addBuffers(2u);
    context.addTextures(2u);
    Access::validateStateHandoff(context.source, s_DeviceGeneration);
    const Core::BufferHandle buffers[] = { context.buffers[1u], {}, context.buffers[0u], context.buffers[1u] };
    const Core::TextureHandle textures[] = { context.textures[1u], {}, context.textures[1u] };
    const u32 initialBufferReferences = context.buffers[1u]->getReferenceCount();
    const u32 initialTextureReferences = context.textures[1u]->getReferenceCount();
    Cache cache(context.arena);
    Cache::Candidate candidate(cache);
    ASSERT_TRUE(cache.buildFilteredResourceSubset(candidate, context.source, textures, LengthOf(textures), buffers, LengthOf(buffers), scratch));
    EXPECT_TRUE(candidate.valid());
    EXPECT_TRUE(candidate.empty());
    EXPECT_FALSE(cache.valid());
    EXPECT_EQ(context.buffers[1u]->getReferenceCount(), initialBufferReferences + 1u);
    EXPECT_EQ(context.textures[1u]->getReferenceCount(), initialTextureReferences + 1u);
    ASSERT_TRUE(cache.commit(candidate));
    EXPECT_TRUE(cache.valid());
    EXPECT_TRUE(cache.empty());
    EXPECT_EQ(cache.retainedBufferCount(), 2u);
    EXPECT_EQ(cache.retainedTextureCount(), 1u);
    EXPECT_FALSE(candidate.valid());
    EXPECT_FALSE(cache.commit(candidate));
}

TEST(PersistentStateSubset, CommitDefersDisplacedOwnershipUntilTheConsumedCandidateDies){
    Core::Alloc::ScratchArena scratch(Name("tests/persistent_state_subset/operation"));
    SubsetContext context;
    context.addBuffers(2u);
    context.fillBufferStates();
    Cache cache(context.arena);
    ASSERT_TRUE(cache.replaceBufferSubset(context.source, &context.buffers[0u], 1u, scratch));
    EXPECT_EQ(context.buffers[0u]->getReferenceCount(), 2u);
    {
        Cache::Candidate candidate(cache);
        ASSERT_TRUE(cache.buildFilteredBufferSubset(candidate, context.source, &context.buffers[1u], 1u, scratch));
        Cache otherCache(context.arena);
        EXPECT_FALSE(otherCache.commit(candidate));
        EXPECT_TRUE(candidate.valid());
        const ArenaMemoryStats beforeCommit = context.arena.memoryStats();
        ASSERT_TRUE(cache.commit(candidate));
        EXPECT_EQ(context.arena.memoryStats().allocationCount, beforeCommit.allocationCount);
        EXPECT_EQ(context.buffers[0u]->getReferenceCount(), 2u);
        EXPECT_EQ(context.buffers[1u]->getReferenceCount(), 2u);
        ASSERT_EQ(Access::stateHandoffBuffers(*cache.source()).size(), 1u);
        EXPECT_EQ(Access::stateHandoffBuffers(*cache.source())[0u].buffer, context.buffers[1u].get());
    }
    EXPECT_EQ(context.buffers[0u]->getReferenceCount(), 1u);
    Core::Buffer* const retained = context.buffers[1u].get();
    context.buffers.clear();
    EXPECT_EQ(retained->getReferenceCount(), 1u);
    EXPECT_EQ(Access::stateHandoffBuffers(*cache.source())[0u].buffer, retained);
    cache.reset();
    EXPECT_FALSE(cache.valid());
}

TEST(PersistentStateSubset, PreservesFilteredMergedAndReplacementFailureDistinctions){
    SubsetContext context;
    context.addBuffers(2u);
    context.fillBufferStates();
    Core::Alloc::ScratchArena scratch(Name("tests/persistent_state_subset/failure"));
    Cache cache(context.arena);
    Cache otherCache(context.arena);
    ASSERT_TRUE(cache.replaceBufferSubset(context.source, context.buffers.data(), context.buffers.size(), scratch));
    Cache::Candidate candidate(cache);
    ASSERT_TRUE(cache.buildFilteredBufferSubset(candidate, context.source, &context.buffers[0u], 1u, scratch));
    EXPECT_FALSE(otherCache.buildFilteredBufferSubset(candidate, context.source, nullptr, 1u, scratch));
    EXPECT_TRUE(candidate.valid());
    Handoff invalid(context.arena);
    EXPECT_FALSE(cache.buildMergedBufferSubset(candidate, invalid, nullptr, 0u, scratch));
    EXPECT_TRUE(candidate.valid());
    EXPECT_FALSE(cache.buildFilteredBufferSubset(candidate, invalid, nullptr, 0u, scratch));
    EXPECT_FALSE(candidate.valid());
    EXPECT_TRUE(cache.valid());
    EXPECT_EQ(cache.retainedBufferCount(), 2u);

    Handoff foreignGeneration(context.arena);
    ASSERT_TRUE(foreignGeneration.copyFrom(context.source));
    Access::validateStateHandoff(foreignGeneration, s_DeviceGeneration + 1u);
    EXPECT_FALSE(cache.mergeBufferSubset(foreignGeneration, context.buffers.data(), context.buffers.size(), scratch));
    EXPECT_EQ(cache.source()->deviceGeneration(), s_DeviceGeneration);
    EXPECT_EQ(cache.retainedBufferCount(), 2u);
    ASSERT_TRUE(cache.buildMergedBufferSubset(candidate, context.source, &context.buffers[1u], 1u, scratch));
    EXPECT_TRUE(candidate.valid());
    ASSERT_TRUE(cache.commit(candidate));
    EXPECT_EQ(cache.retainedBufferCount(), 1u);
    EXPECT_FALSE(cache.replaceBufferSubset(invalid, nullptr, 0u, scratch));
    EXPECT_FALSE(cache.valid());
    EXPECT_EQ(cache.retainedBufferCount(), 0u);
}

TEST(PersistentStateSubset, RebuildsLargeSelectionsAndMergesUpdatedStatesWithoutStaleMembership){
    SubsetContext context;
    context.addBuffers(96u);
    context.fillBufferStates();
    BufferVector selection(context.arena);
    for(usize index = 64u; index != 0u; --index){
        selection.push_back(context.buffers[index - 1u]);
        selection.push_back(context.buffers[index - 1u]);
    }
    Core::Alloc::ScratchArena scratch(Name("tests/persistent_state_subset/rebuild"));
    Cache cache(context.arena);
    ASSERT_TRUE(cache.replaceBufferSubset(context.source, selection.data(), selection.size(), scratch));
    EXPECT_EQ(cache.retainedBufferCount(), 64u);
    const auto& first = Access::stateHandoffBuffers(*cache.source());
    ASSERT_EQ(first.size(), 64u);
    for(usize index = 0u; index < first.size(); ++index)
        EXPECT_EQ(first[index].buffer, context.buffers[index].get());

    Handoff updated(context.arena);
    ASSERT_TRUE(updated.copyFrom(context.source));
    Access::stateHandoffBuffers(updated)[40u].state = Core::ResourceStates::CopyDest;
    Access::stateHandoffBuffers(updated)[40u].releaseDestinationQueue = { .index = 9u, .deviceGeneration = s_DeviceGeneration };
    selection.clear();
    for(usize index = 96u; index != 32u; --index)
        selection.push_back(context.buffers[index - 1u]);
    Cache::Candidate candidate(cache);
    ASSERT_TRUE(cache.buildMergedBufferSubset(candidate, updated, selection.data(), selection.size(), scratch));
    ASSERT_TRUE(cache.commit(candidate));
    const auto& merged = Access::stateHandoffBuffers(*cache.source());
    ASSERT_EQ(merged.size(), 64u);
    for(usize index = 0u; index < merged.size(); ++index)
        EXPECT_EQ(merged[index].buffer, context.buffers[index + 32u].get());
    EXPECT_EQ(merged[8u].state, Core::ResourceStates::CopyDest);
    EXPECT_EQ(merged[8u].releaseDestinationQueue.index, 9u);
    EXPECT_EQ(merged[8u].releaseDestinationQueue.deviceGeneration, s_DeviceGeneration);
    ASSERT_TRUE(cache.replaceBufferSubset(context.source, &context.buffers[1u], 1u, scratch));
    EXPECT_EQ(cache.retainedBufferCount(), 1u);
    EXPECT_EQ(Access::stateHandoffBuffers(*cache.source())[0u].buffer, context.buffers[1u].get());
}


TEST(PersistentStateSubset, MergeCapturesAnAliasedCandidateBeforeResetWhileFilteredSelfSourceRejects){
    SubsetContext context;
    context.addBuffers(2u);
    context.fillBufferStates();
    Access::stateHandoffBuffers(context.source)[1u].state = Core::ResourceStates::CopyDest;
    Core::Alloc::ScratchArena scratch(Name("tests/persistent_state_subset/candidate_alias"));
    Cache cache(context.arena);
    ASSERT_TRUE(cache.replaceBufferSubset(context.source, &context.buffers[0u], 1u, scratch));
    Cache::Candidate candidate(cache);
    ASSERT_TRUE(cache.buildFilteredBufferSubset(candidate, context.source, &context.buffers[1u], 1u, scratch));
    ASSERT_TRUE(cache.buildMergedBufferSubset(
        candidate, *candidate.source(), context.buffers.data(), context.buffers.size(), scratch
    ));
    const auto& merged = Access::stateHandoffBuffers(*candidate.source());
    ASSERT_EQ(merged.size(), 2u);
    EXPECT_EQ(merged[0u].buffer, context.buffers[0u].get());
    EXPECT_EQ(merged[1u].buffer, context.buffers[1u].get());
    EXPECT_EQ(merged[1u].state, Core::ResourceStates::CopyDest);
    EXPECT_EQ(cache.retainedBufferCount(), 1u);
    EXPECT_FALSE(cache.buildFilteredBufferSubset(candidate, *candidate.source(), context.buffers.data(), context.buffers.size(), scratch));
    EXPECT_FALSE(candidate.valid());
    EXPECT_TRUE(cache.valid());
    EXPECT_EQ(cache.retainedBufferCount(), 1u);
    ASSERT_TRUE(cache.buildFilteredBufferSubset(candidate, context.source, context.buffers.data(), context.buffers.size(), scratch));
    EXPECT_TRUE(candidate.valid());
}

TEST(PersistentStateSubset, LargeTextureSelectionsRetainEverySubresourceAndPermanentStateInSourceOrder){
    Core::Alloc::ScratchArena scratch(Name("tests/persistent_state_subset/operation"));
    SubsetContext context;
    context.addTextures(64u);
    context.addBuffers(2u);
    context.fillBufferStates();
    for(usize index = 0u; index < context.textures.size(); ++index){
        if(index % 2u == 0u){
            Access::stateHandoffTextures(context.source).push_back({
                .texture = context.textures[index].get(), .mipLevel = 0u, .arraySlice = 1u,
                .state = Core::ResourceStates::ShaderResource,
                .queueSharing = Core::ResourceQueueSharing::Exclusive,
                .ownerQueue = s_OwnerQueue, .releaseDestinationQueue = s_ReleaseQueue,
            });
            Access::stateHandoffTextures(context.source).push_back({
                .texture = context.textures[index].get(), .mipLevel = 1u, .arraySlice = 0u,
                .state = Core::ResourceStates::CopySource,
                .queueSharing = Core::ResourceQueueSharing::GraphicsAndAsyncCompute,
                .ownerQueue = {}, .releaseDestinationQueue = {},
            });
        }
        else{
            Access::stateHandoffPermanentTextures(context.source).push_back({
                .texture = context.textures[index].get(), .state = Core::ResourceStates::UnorderedAccess,
                .queueSharing = Core::ResourceQueueSharing::Exclusive,
                .ownerQueue = s_OwnerQueue, .releaseDestinationQueue = s_ReleaseQueue,
            });
        }
    }
    Vector<Core::TextureHandle, Core::Alloc::GlobalArena> selection(context.arena);
    Vector<Core::Texture*, Core::Alloc::GlobalArena> pointers(context.arena);
    for(usize index = 56u; index != 8u; --index){
        selection.push_back(context.textures[index - 1u]);
        selection.push_back({});
        selection.push_back(context.textures[index - 1u]);
        pointers.push_back(context.textures[index - 1u].get());
        pointers.push_back(nullptr);
        pointers.push_back(context.textures[index - 1u].get());
    }
    const u32 originalReferences = context.textures[8u]->getReferenceCount();
    Handoff raw(context.arena);
    Core::Buffer* const buffers[] = { context.buffers[1u].get() };
    ASSERT_TRUE(raw.buildResourceSubset(context.source, pointers.data(), pointers.size(), buffers, LengthOf(buffers), scratch));
    const auto& transientStates = Access::stateHandoffTextures(raw);
    const auto& permanentStates = Access::stateHandoffPermanentTextures(raw);
    ASSERT_EQ(transientStates.size(), 48u);
    ASSERT_EQ(permanentStates.size(), 24u);
    for(usize index = 0u; index < 24u; ++index){
        const auto& first = transientStates[index * 2u];
        const auto& second = transientStates[index * 2u + 1u];
        EXPECT_EQ(first.texture, context.textures[8u + index * 2u].get());
        EXPECT_EQ(first.mipLevel, 0u);
        EXPECT_EQ(first.arraySlice, 1u);
        EXPECT_EQ(first.ownerQueue, s_OwnerQueue);
        EXPECT_EQ(first.releaseDestinationQueue, s_ReleaseQueue);
        EXPECT_EQ(second.texture, first.texture);
        EXPECT_EQ(second.mipLevel, 1u);
        EXPECT_EQ(second.arraySlice, 0u);
        EXPECT_EQ(second.queueSharing, Core::ResourceQueueSharing::GraphicsAndAsyncCompute);
        EXPECT_EQ(permanentStates[index].texture, context.textures[9u + index * 2u].get());
        EXPECT_EQ(permanentStates[index].ownerQueue, s_OwnerQueue);
        EXPECT_EQ(permanentStates[index].releaseDestinationQueue, s_ReleaseQueue);
    }
    Cache cache(context.arena);
    Cache::Candidate candidate(cache);
    ASSERT_TRUE(cache.buildFilteredResourceSubset(
        candidate, context.source, selection.data(), selection.size(), &context.buffers[1u], 1u,
        scratch
    ));
    ExpectSameHandoff(*candidate.source(), raw);
    EXPECT_EQ(context.textures[8u]->getReferenceCount(), originalReferences + 1u);
    ASSERT_TRUE(cache.commit(candidate));
    EXPECT_EQ(cache.retainedTextureCount(), 48u);
    EXPECT_EQ(cache.retainedBufferCount(), 1u);
    ASSERT_TRUE(cache.replaceTextureSubset(context.source, context.textures[1u], scratch));
    EXPECT_EQ(cache.retainedTextureCount(), 1u);
    EXPECT_EQ(cache.retainedBufferCount(), 0u);
    EXPECT_TRUE(Access::stateHandoffTextures(*cache.source()).empty());
    ASSERT_EQ(Access::stateHandoffPermanentTextures(*cache.source()).size(), 1u);
    EXPECT_EQ(Access::stateHandoffPermanentTextures(*cache.source())[0u].texture, context.textures[1u].get());
    EXPECT_EQ(context.textures[8u]->getReferenceCount(), originalReferences);
}

TEST(PersistentStateSubset, SelectionResolvesForcedCollisionsGrowthAndFirstInputOrdinals){
    SubsetContext context;
    context.addBuffers(1024u);
    context.addTextures(1u);
    Vector<usize, Core::Alloc::GlobalArena> bucketCounts(context.arena);
    bucketCounts.resize(Access::initialResourceSelectionBucketCount(), 0u);
    for(const Core::BufferHandle& buffer : context.buffers)
        ++bucketCounts[Access::initialResourceSelectionBucket(buffer.get())];
    usize collisionBucket = 0u;
    for(usize bucket = 1u; bucket < bucketCounts.size(); ++bucket){
        if(bucketCounts[bucket] > bucketCounts[collisionBucket])
            collisionBucket = bucket;
    }
    ASSERT_GE(bucketCounts[collisionBucket], context.buffers.size() / bucketCounts.size());
    Vector<Core::Buffer*, Core::Alloc::GlobalArena> ordered(context.arena);
    ordered.reserve(96u);
    for(const Core::BufferHandle& buffer : context.buffers){
        if(Access::initialResourceSelectionBucket(buffer.get()) == collisionBucket && ordered.size() != 96u)
            ordered.push_back(buffer.get());
    }
    const usize forcedCollisions = ordered.size();
    ASSERT_GT(forcedCollisions, 1u);
    for(const Core::BufferHandle& buffer : context.buffers){
        if(Access::initialResourceSelectionBucket(buffer.get()) != collisionBucket && ordered.size() != 96u)
            ordered.push_back(buffer.get());
    }
    ASSERT_EQ(ordered.size(), 96u);
    Core::Alloc::ScratchArena scratch(Name("tests/persistent_state_subset/collisions"));
    const ArenaMemoryStats initial = scratch.memoryStats();
    {
        Core::CommandListResourceSelection selection(scratch);
        ASSERT_TRUE(selection.addBuffer(context.buffers[0u].get(), 11u));
        ASSERT_TRUE(selection.addBuffer(context.buffers[1u].get(), 12u));
        ASSERT_TRUE(selection.addTexture(context.textures[0u].get(), 21u));
        ASSERT_TRUE(selection.addBuffer(context.buffers[0u].get(), 9999u));
        EXPECT_TRUE(selection.containsBuffer(context.buffers[0u].get()));
        EXPECT_TRUE(selection.containsBuffer(context.buffers[1u].get()));
        EXPECT_TRUE(selection.containsTexture(context.textures[0u].get()));
        EXPECT_FALSE(selection.containsBuffer(reinterpret_cast<Core::Buffer*>(context.textures[0u].get())));
        ASSERT_EQ(selection.size(), 3u);
        EXPECT_EQ(selection.entries()[0u].inputIndex, 11u);
        EXPECT_EQ(selection.entries()[1u].inputIndex, 12u);
        EXPECT_EQ(selection.entries()[2u].inputIndex, 21u);
    }
    EXPECT_EQ(scratch.memoryStats().allocationCount, initial.allocationCount);
    {
        Core::CommandListResourceSelection selection(scratch);
        for(usize index = 0u; index < ordered.size(); ++index)
            ASSERT_TRUE(selection.addBuffer(ordered[index], index * 3u + 17u));
        for(Core::Buffer* const buffer : ordered)
            ASSERT_TRUE(selection.addBuffer(buffer, 9999u));
        ASSERT_TRUE(selection.addBuffer(nullptr, 9999u));
        ASSERT_EQ(selection.size(), ordered.size());
        EXPECT_EQ(selection.bufferCount(), ordered.size());
        for(usize index = 0u; index < ordered.size(); ++index){
            EXPECT_TRUE(selection.containsBuffer(ordered[index]));
            EXPECT_EQ(selection.entries()[index].resource, ordered[index]);
            EXPECT_EQ(selection.entries()[index].inputIndex, index * 3u + 17u);
            EXPECT_FALSE(selection.entries()[index].texture);
        }
        for(const Core::BufferHandle& buffer : context.buffers){
            bool expected = false;
            for(Core::Buffer* const selected : ordered)
                expected = expected || selected == buffer.get();
            EXPECT_EQ(selection.containsBuffer(buffer.get()), expected);
        }
        // Membership uses opaque identities without dereferencing them. A shared address in two kinds stays distinct.
        Core::Texture* const texture = context.textures[0u].get();
        Core::Buffer* const sameAddress = reinterpret_cast<Core::Buffer*>(texture);
        ASSERT_TRUE(selection.addTexture(texture, 31u));
        EXPECT_FALSE(selection.containsBuffer(sameAddress));
        ASSERT_TRUE(selection.addBuffer(sameAddress, 41u));
        EXPECT_TRUE(selection.containsTexture(texture));
        EXPECT_TRUE(selection.containsBuffer(sameAddress));
        EXPECT_EQ(selection.size(), ordered.size() + 2u);
        EXPECT_EQ(selection.textureCount(), 1u);
        EXPECT_EQ(selection.entries()[ordered.size()].inputIndex, 31u);
        EXPECT_EQ(selection.entries()[ordered.size() + 1u].inputIndex, 41u);
    }
    EXPECT_EQ(scratch.memoryStats().usedBytes, initial.usedBytes);
}

TEST(PersistentStateSubset, ReusesOneScratchArenaAcrossLargeSmallAndMixedSelectionGrowth){
    SubsetContext context;
    context.addBuffers(96u);
    context.addTextures(96u);
    Core::Alloc::ScratchArena scratch(Name("tests/persistent_state_subset/repeated_selection"));
    const ArenaMemoryStats initial = scratch.memoryStats();
    const auto collect = [&](const usize count){
        Core::CommandListResourceSelection selection(scratch);
        for(usize index = 0u; index < count; ++index)
            ASSERT_TRUE(selection.addTexture(context.textures[index].get(), index));
        for(usize index = 0u; index < count; ++index)
            ASSERT_TRUE(selection.addBuffer(context.buffers[index].get(), index));
        EXPECT_EQ(selection.textureCount(), count);
        EXPECT_EQ(selection.bufferCount(), count);
        for(usize index = 0u; index < count; ++index){
            EXPECT_TRUE(selection.containsTexture(context.textures[index].get()));
            EXPECT_TRUE(selection.containsBuffer(context.buffers[index].get()));
        }
    };
    collect(96u);
    const ArenaMemoryStats warm = scratch.memoryStats();
    EXPECT_EQ(warm.usedBytes, initial.usedBytes);
    EXPECT_GT(warm.reservedBytes, initial.reservedBytes);
    for(usize iteration = 0u; iteration < 4u; ++iteration){
        const u64 previousAllocations = scratch.memoryStats().allocationCount;
        collect(1u);
        EXPECT_EQ(scratch.memoryStats().allocationCount, previousAllocations);
        collect(96u);
        EXPECT_EQ(scratch.memoryStats().usedBytes, warm.usedBytes);
        EXPECT_EQ(scratch.memoryStats().reservedBytes, warm.reservedBytes);
    }
}

TEST(PersistentStateSubset, ReusesSelectionScratchBeforeMergedStateIndicesAcrossRepeatedCandidates){
    SubsetContext context;
    context.addBuffers(96u);
    context.addTextures(96u);
    context.fillBufferStates();
    for(const Core::TextureHandle& texture : context.textures){
        Access::stateHandoffTextures(context.source).push_back({
            .texture = texture.get(), .mipLevel = 1u, .arraySlice = 1u,
            .state = Core::ResourceStates::ShaderResource,
            .queueSharing = Core::ResourceQueueSharing::Exclusive,
            .ownerQueue = s_OwnerQueue, .releaseDestinationQueue = s_ReleaseQueue,
        });
    }
    Core::Alloc::ScratchArena scratch(Name("tests/persistent_state_subset/repeated_merge"));
    Cache cache(context.arena);
    ASSERT_TRUE(cache.replaceResourceSubset(
        context.source, context.textures.data(), context.textures.size(), context.buffers.data(), context.buffers.size(), scratch
    ));
    const auto build = [&](const usize count){
        Cache::Candidate candidate(cache);
        ASSERT_TRUE(cache.buildMergedResourceSubset(
            candidate, context.source, context.textures.data(), count, context.buffers.data(), count, scratch
        ));
        ASSERT_TRUE(candidate.valid());
        EXPECT_EQ(Access::stateHandoffTextures(*candidate.source()).size(), count);
        EXPECT_EQ(Access::stateHandoffBuffers(*candidate.source()).size(), count);
    };
    build(96u);
    build(1u);
    build(96u);
    const ArenaMemoryStats warm = scratch.memoryStats();
    for(usize iteration = 0u; iteration < 4u; ++iteration){
        build(1u);
        build(96u);
        EXPECT_EQ(scratch.memoryStats().usedBytes, warm.usedBytes);
        EXPECT_EQ(scratch.memoryStats().reservedBytes, warm.reservedBytes);
    }
    EXPECT_EQ(cache.retainedTextureCount(), 96u);
    EXPECT_EQ(cache.retainedBufferCount(), 96u);
}

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


void RecordUnsignedProperty(const NotNull<const char*> key, const u64 value){
    char text[32u] = {};
    const AStringView formatted = FormatDecimal(value, text);
    text[formatted.size()] = '\0';
    testing::Test::RecordProperty(key.get(), text);
}

void BenchmarkSubset(
    const usize sourceCount,
    const usize selectionCount,
    const usize uniqueCount,
    const usize iterations,
    const bool rawSubset,
    const bool merge
){
    SubsetContext context;
    context.addBuffers(sourceCount);
    context.fillBufferStates();
    BufferVector selection(context.arena);
    Vector<Core::Buffer*, Core::Alloc::GlobalArena> pointers(context.arena);
    selection.reserve(selectionCount);
    pointers.reserve(selectionCount);
    for(usize index = 0u; index < selectionCount; ++index){
        const usize selected = (uniqueCount - 1u - index % uniqueCount) * (sourceCount / uniqueCount);
        selection.push_back(context.buffers[selected]);
        pointers.push_back(context.buffers[selected].get());
    }
    Core::Alloc::ScratchArena setupScratch(Name("tests/persistent_state_subset/benchmark_setup"));
    Cache cache(context.arena);
    ASSERT_TRUE(cache.replaceBufferSubset(context.source, selection.data(), selection.size(), setupScratch));
    Handoff subset(context.arena);
    ASSERT_TRUE(subset.buildResourceSubset(context.source, nullptr, 0u, pointers.data(), pointers.size(), setupScratch));
    const ArenaMemoryStats before = HeapBackingMemoryStats();
    usize completed = 0u;
    usize resultCount = 0u;
    u64 scratchPeak = 0u;
    u64 scratchReserved = 0u;
    const Timer begin = TimerNow();
    for(usize iteration = 0u; iteration < iterations; ++iteration){
        Core::Alloc::ScratchArena scratch(Name("tests/persistent_state_subset/benchmark"));
        if(rawSubset){
            if(subset.buildResourceSubset(context.source, nullptr, 0u, pointers.data(), pointers.size(), scratch))
                ++completed;
            resultCount = Access::stateHandoffBuffers(subset).size();
        }
        else{
            Cache::Candidate candidate(cache);
            const bool built = merge
                ? cache.buildMergedBufferSubset(candidate, context.source, selection.data(), selection.size(), scratch)
                : cache.buildFilteredBufferSubset(candidate, context.source, selection.data(), selection.size(), scratch)
            ;
            if(built){
                ++completed;
                resultCount = Access::stateHandoffBuffers(*candidate.source()).size();
            }
        }
        scratchPeak = Max(scratchPeak, scratch.memoryStats().peakUsedBytes);
        scratchReserved = Max(scratchReserved, scratch.memoryStats().reservedBytes);
    }
    const u64 elapsed = DurationInNS<u64>(TimerNow(), begin);
    const ArenaMemoryStats after = HeapBackingMemoryStats();
    ASSERT_EQ(completed, iterations);
    ASSERT_EQ(resultCount, uniqueCount);
    EXPECT_EQ(cache.retainedBufferCount(), uniqueCount);
    RecordUnsignedProperty(MakeNotNull("persistent_subset_ns"), elapsed);
    RecordUnsignedProperty(MakeNotNull("persistent_subset_iterations"), iterations);
    RecordUnsignedProperty(MakeNotNull("persistent_subset_source_count"), sourceCount);
    RecordUnsignedProperty(MakeNotNull("persistent_subset_selection_count"), selectionCount);
    RecordUnsignedProperty(MakeNotNull("persistent_subset_unique_count"), uniqueCount);
    RecordUnsignedProperty(MakeNotNull("persistent_subset_heap_allocations"), after.allocationCount - before.allocationCount);
    RecordUnsignedProperty(MakeNotNull("persistent_subset_scratch_peak_bytes"), scratchPeak);
    RecordUnsignedProperty(MakeNotNull("persistent_subset_scratch_reserved_bytes"), scratchReserved);
}

// Opt in with --gtest_also_run_disabled_tests and the PersistentStateSubsetBenchmark.* filter.
TEST(PersistentStateSubsetBenchmark, DISABLED_RawUnique4096){
    BenchmarkSubset(4096u, 4096u, 4096u, 1u, true, false);
}

TEST(PersistentStateSubsetBenchmark, DISABLED_FilteredUnique4096){
    BenchmarkSubset(4096u, 4096u, 4096u, 1u, false, false);
}

TEST(PersistentStateSubsetBenchmark, DISABLED_MergedUnique4096){
    BenchmarkSubset(4096u, 4096u, 4096u, 1u, false, true);
}

TEST(PersistentStateSubsetBenchmark, DISABLED_FilteredSingleton){
    BenchmarkSubset(1u, 1u, 1u, 1024u, false, false);
}

TEST(PersistentStateSubsetBenchmark, DISABLED_FilteredRepeated4096){
    BenchmarkSubset(32u, 4096u, 32u, 4u, false, false);
}

TEST(PersistentStateSubsetBenchmark, DISABLED_MergedSparse4096){
    BenchmarkSubset(4096u, 32u, 32u, 8u, false, true);
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

