// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include <core/task/gpu/persistent_state.h>
#include <core/alloc/scratch.h>

#include <tests/common/graphics_metadata_test_objects.h>
#include <tests/common/vulkan_test_sync.h>

#include <gtest/gtest.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace __hidden_persistent_state_self_filter_tests{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


using namespace NWB;
namespace Core = NWB::Core;
using Access = Core::GraphicsBackend::VulkanTestDispatchAccess;
using Handoff = Core::CommandListResourceStateHandoff;
using Cache = Core::GpuPersistentResourceStateCache;

constexpr u16 s_DeviceGeneration = 23u;
constexpr Core::GpuPhysicalQueueId s_OwnerQueue{ .index = 2u, .deviceGeneration = s_DeviceGeneration };
constexpr Core::GpuPhysicalQueueId s_ReleaseQueue{ .index = 5u, .deviceGeneration = s_DeviceGeneration };


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


struct FilterContext{
    Core::Alloc::GlobalArena arena{ Name("tests/persistent_state_self_filter/inputs") };
    Core::GraphicsAllocator graphicsAllocator{ arena };
    Core::CpuTaskScheduler cpuScheduler{ 0u };
    Core::GraphicsBackend::VulkanContext context{ graphicsAllocator, cpuScheduler, s_DeviceGeneration };
    Core::GraphicsBackend::VulkanAllocator allocator{ context };
    Core::BufferHandle buffers[4u];
    Core::TextureHandle textures[4u];
    Handoff source{ arena };

    FilterContext(){
        for(Core::BufferHandle& buffer : buffers){
            Core::Buffer* const resource = Tests::NewMetadataOnlyBuffer(
                arena, context, allocator, Core::BufferDesc{}.setByteSize(256u)
            );
            buffer = Core::BufferHandle(resource, Core::BufferHandle::deleter_type(&arena), AdoptRef);
        }
        for(Core::TextureHandle& texture : textures){
            Core::Texture* const resource = Tests::NewMetadataOnlyTexture(
                arena, context, allocator, Core::TextureDesc{}.setMipLevels(2u).setArraySize(2u)
            );
            texture = Core::TextureHandle(resource, Core::TextureHandle::deleter_type(&arena), AdoptRef);
        }
        Access::stateHandoffTextures(source).push_back({
            .texture = textures[0u].get(), .mipLevel = 0u, .arraySlice = 0u,
            .state = Core::ResourceStates::CopyDest,
            .ownerQueue = {}, .releaseDestinationQueue = {},
        });
        Access::stateHandoffTextures(source).push_back({
            .texture = textures[1u].get(), .mipLevel = 1u, .arraySlice = 0u,
            .state = Core::ResourceStates::ShaderResource,
            .queueSharing = Core::ResourceQueueSharing::Exclusive,
            .ownerQueue = s_OwnerQueue, .releaseDestinationQueue = s_ReleaseQueue,
        });
        Access::stateHandoffTextures(source).push_back({
            .texture = textures[1u].get(), .mipLevel = 0u, .arraySlice = 1u,
            .state = Core::ResourceStates::CopySource,
            .queueSharing = Core::ResourceQueueSharing::GraphicsAndAsyncCompute,
            .ownerQueue = {}, .releaseDestinationQueue = {},
        });
        Access::stateHandoffPermanentTextures(source).push_back({
            .texture = textures[2u].get(), .state = Core::ResourceStates::ShaderResource,
            .queueSharing = Core::ResourceQueueSharing::Exclusive,
            .ownerQueue = s_OwnerQueue, .releaseDestinationQueue = s_ReleaseQueue,
        });
        Access::stateHandoffBuffers(source).push_back({
            .buffer = buffers[0u].get(), .state = Core::ResourceStates::CopyDest,
            .ownerQueue = {}, .releaseDestinationQueue = {},
        });
        Access::stateHandoffBuffers(source).push_back({
            .buffer = buffers[1u].get(), .state = Core::ResourceStates::ShaderResource,
            .queueSharing = Core::ResourceQueueSharing::Exclusive,
            .ownerQueue = s_OwnerQueue, .releaseDestinationQueue = s_ReleaseQueue,
            .range = { 16u, 64u },
        });
        Access::stateHandoffBuffers(source).push_back({
            .buffer = buffers[1u].get(), .state = Core::ResourceStates::CopySource,
            .queueSharing = Core::ResourceQueueSharing::GraphicsAndAsyncCompute,
            .ownerQueue = {}, .releaseDestinationQueue = {},
            .range = { 192u, 32u },
        });
        Access::stateHandoffPermanentBuffers(source).push_back({
            .buffer = buffers[2u].get(), .state = Core::ResourceStates::CopySource,
            .queueSharing = Core::ResourceQueueSharing::Exclusive,
            .ownerQueue = s_OwnerQueue, .releaseDestinationQueue = s_ReleaseQueue,
        });
        Access::validateStateHandoff(source, s_DeviceGeneration);
    }
};


TEST(PersistentStateSelfFilter, PreservesAllStateCategoriesAndRetainsNewLiveHandlesWithoutReplacingStateStorage){
    FilterContext context;
    Core::Alloc::ScratchArena scratch(Name("tests/persistent_state_self_filter/mixed"));
    Cache cache(context.arena);
    ASSERT_TRUE(cache.replaceResourceSubset(context.source, context.textures, 3u, context.buffers, 3u, scratch));
    const Handoff* const previous = cache.source();
    ASSERT_NE(previous, nullptr);
    const auto* const textureStorage = Access::stateHandoffTextures(*previous).data();
    const auto* const bufferStorage = Access::stateHandoffBuffers(*previous).data();
    const auto* const permanentTextureStorage = Access::stateHandoffPermanentTextures(*previous).data();
    const auto* const permanentBufferStorage = Access::stateHandoffPermanentBuffers(*previous).data();
    const Core::TextureHandle textures[] = { context.textures[2u], context.textures[1u], {}, context.textures[3u], context.textures[1u] };
    const Core::BufferHandle buffers[] = { context.buffers[3u], {}, context.buffers[1u], context.buffers[2u], context.buffers[1u] };
    Core::Texture* const texturePointers[] = { context.textures[1u].get(), context.textures[2u].get(), context.textures[3u].get() };
    Core::Buffer* const bufferPointers[] = { context.buffers[1u].get(), context.buffers[2u].get(), context.buffers[3u].get() };
    Handoff expected(context.arena);
    ASSERT_TRUE(expected.buildResourceSubset(
        context.source, texturePointers, LengthOf(texturePointers), bufferPointers, LengthOf(bufferPointers), scratch
    ));
    const u32 removedBufferReferences = context.buffers[0u]->getReferenceCount();
    const u32 removedTextureReferences = context.textures[0u]->getReferenceCount();
    const u32 retainedBufferReferences = context.buffers[1u]->getReferenceCount();
    const u32 retainedTextureReferences = context.textures[1u]->getReferenceCount();
    const u32 newBufferReferences = context.buffers[3u]->getReferenceCount();
    const u32 newTextureReferences = context.textures[3u]->getReferenceCount();

    ASSERT_TRUE(cache.replaceResourceSubset(*previous, textures, LengthOf(textures), buffers, LengthOf(buffers), scratch));
    ASSERT_NE(cache.source(), nullptr);
    EXPECT_EQ(cache.source(), previous);
    EXPECT_TRUE(cache.source()->equivalentTo(expected));
    EXPECT_EQ(cache.retainedBufferCount(), 3u);
    EXPECT_EQ(cache.retainedTextureCount(), 3u);
    EXPECT_EQ(Access::stateHandoffTextures(*cache.source()).data(), textureStorage);
    EXPECT_EQ(Access::stateHandoffBuffers(*cache.source()).data(), bufferStorage);
    EXPECT_EQ(Access::stateHandoffPermanentTextures(*cache.source()).data(), permanentTextureStorage);
    EXPECT_EQ(Access::stateHandoffPermanentBuffers(*cache.source()).data(), permanentBufferStorage);
    EXPECT_EQ(context.buffers[0u]->getReferenceCount(), removedBufferReferences - 1u);
    EXPECT_EQ(context.textures[0u]->getReferenceCount(), removedTextureReferences - 1u);
    EXPECT_EQ(context.buffers[1u]->getReferenceCount(), retainedBufferReferences);
    EXPECT_EQ(context.textures[1u]->getReferenceCount(), retainedTextureReferences);
    EXPECT_EQ(context.buffers[3u]->getReferenceCount(), newBufferReferences + 1u);
    EXPECT_EQ(context.textures[3u]->getReferenceCount(), newTextureReferences + 1u);
}

TEST(PersistentStateSelfFilter, EmptySelectionReleasesRetentionsAndPreservesValidGeneration){
    FilterContext context;
    Core::Alloc::ScratchArena scratch(Name("tests/persistent_state_self_filter/empty"));
    Cache cache(context.arena);
    ASSERT_TRUE(cache.replaceResourceSubset(context.source, context.textures, 3u, context.buffers, 3u, scratch));
    EXPECT_EQ(context.buffers[0u]->getReferenceCount(), 2u);
    EXPECT_EQ(context.textures[0u]->getReferenceCount(), 2u);

    ASSERT_TRUE(cache.replaceResourceSubset(*cache.source(), nullptr, 0u, nullptr, 0u, scratch));
    ASSERT_TRUE(cache.valid());
    EXPECT_TRUE(cache.empty());
    EXPECT_EQ(cache.source()->deviceGeneration(), s_DeviceGeneration);
    EXPECT_EQ(cache.retainedBufferCount(), 0u);
    EXPECT_EQ(cache.retainedTextureCount(), 0u);
    for(const Core::BufferHandle& buffer : context.buffers)
        EXPECT_EQ(buffer->getReferenceCount(), 1u);
    for(const Core::TextureHandle& texture : context.textures)
        EXPECT_EQ(texture->getReferenceCount(), 1u);
    ASSERT_TRUE(cache.replaceResourceSubset(*cache.source(), nullptr, 0u, nullptr, 0u, scratch));
    EXPECT_TRUE(cache.valid());
    EXPECT_TRUE(cache.empty());
}

TEST(PersistentStateSelfFilter, MissingStatesStillRetainNewRequestedResourcesOnceAcrossRepeatedSelections){
    FilterContext context;
    Core::Alloc::ScratchArena scratch(Name("tests/persistent_state_self_filter/missing"));
    Cache cache(context.arena);
    ASSERT_TRUE(cache.replaceResourceSubset(context.source, context.textures, 1u, context.buffers, 1u, scratch));
    const Core::TextureHandle textures[] = { {}, context.textures[3u], context.textures[3u] };
    const Core::BufferHandle buffers[] = { context.buffers[3u], {}, context.buffers[3u] };
    const u32 newBufferReferences = context.buffers[3u]->getReferenceCount();
    const u32 newTextureReferences = context.textures[3u]->getReferenceCount();
    for(usize iteration = 0u; iteration < 3u; ++iteration){
        ASSERT_TRUE(cache.replaceResourceSubset(*cache.source(), textures, LengthOf(textures), buffers, LengthOf(buffers), scratch));
        EXPECT_TRUE(cache.valid());
        EXPECT_TRUE(cache.empty());
        EXPECT_EQ(cache.source()->deviceGeneration(), s_DeviceGeneration);
        EXPECT_EQ(cache.retainedBufferCount(), 1u);
        EXPECT_EQ(cache.retainedTextureCount(), 1u);
        EXPECT_EQ(context.buffers[0u]->getReferenceCount(), 1u);
        EXPECT_EQ(context.textures[0u]->getReferenceCount(), 1u);
        EXPECT_EQ(context.buffers[3u]->getReferenceCount(), newBufferReferences + 1u);
        EXPECT_EQ(context.textures[3u]->getReferenceCount(), newTextureReferences + 1u);
    }
    cache.reset();
    EXPECT_EQ(context.buffers[3u]->getReferenceCount(), newBufferReferences);
    EXPECT_EQ(context.textures[3u]->getReferenceCount(), newTextureReferences);
}

TEST(PersistentStateSelfFilter, InvalidSelfInputsKeepReplacementResetBehavior){
    FilterContext context;
    Core::Alloc::ScratchArena scratch(Name("tests/persistent_state_self_filter/invalid"));
    Cache cache(context.arena);
    ASSERT_TRUE(cache.replaceBufferSubset(context.source, context.buffers, 3u, scratch));
    EXPECT_FALSE(cache.replaceBufferSubset(*cache.source(), nullptr, 1u, scratch));
    EXPECT_FALSE(cache.valid());
    EXPECT_EQ(cache.retainedBufferCount(), 0u);
    for(const Core::BufferHandle& buffer : context.buffers)
        EXPECT_EQ(buffer->getReferenceCount(), 1u);
}

TEST(PersistentStateSelfFilter, ForeignSourceStillReplacesRatherThanMergingAcceptedStatesOrGeneration){
    FilterContext context;
    Core::Alloc::ScratchArena scratch(Name("tests/persistent_state_self_filter/foreign"));
    Cache cache(context.arena);
    ASSERT_TRUE(cache.replaceResourceSubset(context.source, context.textures, 3u, context.buffers, 3u, scratch));
    Handoff foreign(context.arena);
    constexpr u16 s_ForeignGeneration = s_DeviceGeneration + 1u;
    Access::stateHandoffBuffers(foreign).push_back({
        .buffer = context.buffers[3u].get(), .state = Core::ResourceStates::CopyDest,
        .queueSharing = Core::ResourceQueueSharing::Exclusive,
        .ownerQueue = { .index = 1u, .deviceGeneration = s_ForeignGeneration },
        .releaseDestinationQueue = {},
        .range = { 32u, 96u },
    });
    Access::validateStateHandoff(foreign, s_ForeignGeneration);
    const Core::BufferHandle selected[] = { context.buffers[0u], context.buffers[3u], {}, context.buffers[3u] };

    ASSERT_TRUE(cache.replaceBufferSubset(foreign, selected, LengthOf(selected), scratch));
    ASSERT_NE(cache.source(), nullptr);
    EXPECT_TRUE(cache.source()->equivalentTo(foreign));
    EXPECT_EQ(cache.source()->deviceGeneration(), s_ForeignGeneration);
    EXPECT_EQ(cache.retainedTextureCount(), 0u);
    EXPECT_EQ(cache.retainedBufferCount(), 2u);
    for(const Core::TextureHandle& texture : context.textures)
        EXPECT_EQ(texture->getReferenceCount(), 1u);
    EXPECT_EQ(context.buffers[1u]->getReferenceCount(), 1u);
    EXPECT_EQ(context.buffers[2u]->getReferenceCount(), 1u);
    EXPECT_EQ(context.source.deviceGeneration(), s_DeviceGeneration);
    EXPECT_EQ(Access::stateHandoffBuffers(context.source).size(), 3u);
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

