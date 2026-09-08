// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include <tests/common/graphics_metadata_test_objects.h>
#include <tests/common/test_context.h>
#include <tests/common/vulkan_test_sync.h>

#include <core/graphics/vulkan/command_buffer_resource_references.h>

#include <global/text_utils.h>
#include <global/timer.h>

#include <gtest/gtest.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace __hidden_command_buffer_resource_references_tests{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace Core = NWB::Core;
namespace Graphics = Core::GraphicsBackend;
using Access = Graphics::VulkanTestDispatchAccess;
using TestArena = NWB::Tests::TestArena<struct CommandBufferResourceReferencesTestsTag>;


struct ReferencesContext{
    TestArena testArena;
    Core::GraphicsAllocator graphicsAllocator{ testArena.arena };
    Core::Alloc::CpuTaskScheduler cpuScheduler{ 0u };
    Graphics::VulkanContext context{ graphicsAllocator, cpuScheduler, 1u };
    Graphics::VulkanAllocator allocator{ context };
    Graphics::CommandBufferResourceReferences references{ testArena.arena };
    Vector<Core::BufferHandle, Core::Alloc::GlobalArena> buffers{ testArena.arena };
    Vector<Core::TextureHandle, Core::Alloc::GlobalArena> textures{ testArena.arena };


    void addBuffers(const usize count){
        buffers.reserve(buffers.size() + count);
        for(usize index = 0u; index < count; ++index){
            Core::Buffer* const buffer = NWB::Tests::NewMetadataOnlyBuffer(
                testArena.arena,
                context,
                allocator,
                Core::BufferDesc{}.setByteSize(256u)
            );
            buffers.emplace_back(buffer, Core::BufferHandle::deleter_type(&testArena.arena), AdoptRef);
        }
    }

    void addTextures(const usize count){
        textures.reserve(textures.size() + count);
        for(usize index = 0u; index < count; ++index){
            Core::Texture* const texture = NWB::Tests::NewMetadataOnlyTexture(testArena.arena, context, allocator, Core::TextureDesc{});
            textures.emplace_back(texture, Core::TextureHandle::deleter_type(&testArena.arena), AdoptRef);
        }
    }

    void recordResources(){
        for(const Core::BufferHandle& buffer : buffers)
            references.trackRetainedBuffer(*buffer);
        for(const Core::TextureHandle& texture : textures)
            references.trackRetainedTexture(*texture);
        for(const Core::BufferHandle& buffer : buffers)
            references.retainBuffer(*buffer);
        for(const Core::TextureHandle& texture : textures)
            references.retainTexture(*texture);
        for(const Core::BufferHandle& buffer : buffers)
            references.appendBufferStateCommit(*buffer);
    }
};


TEST(CommandBufferResourceReferences, PreservesIndependentOwningAndTypedMembershipOrder){
    ReferencesContext context;
    context.addBuffers(2u);
    context.addTextures(2u);
    context.references.retainResource(*context.textures[1u]);
    context.references.trackRetainedBuffer(*context.buffers[1u]);
    context.references.trackRetainedTexture(*context.textures[0u]);
    context.references.retainBuffer(*context.buffers[0u]);
    context.references.retainTexture(*context.textures[0u]);
    context.references.retainBuffer(*context.buffers[1u]);
    context.references.retainTexture(*context.textures[1u]);
    context.recordResources();

    const auto& owners = Access::retainedResources(context.references);
    ASSERT_EQ(owners.size(), 4u);
    EXPECT_EQ(owners[0u].get(), context.textures[1u].get());
    EXPECT_EQ(owners[1u].get(), context.buffers[0u].get());
    EXPECT_EQ(owners[2u].get(), context.textures[0u].get());
    EXPECT_EQ(owners[3u].get(), context.buffers[1u].get());
    const auto& buffers = Access::retainedBuffers(context.references);
    ASSERT_EQ(buffers.size(), 2u);
    EXPECT_EQ(buffers[0u], context.buffers[1u].get());
    EXPECT_EQ(buffers[1u], context.buffers[0u].get());
    const auto& textures = Access::retainedTextures(context.references);
    ASSERT_EQ(textures.size(), 2u);
    EXPECT_EQ(textures[0u], context.textures[0u].get());
    EXPECT_EQ(textures[1u], context.textures[1u].get());
    for(const Core::BufferHandle& buffer : context.buffers)
        EXPECT_EQ(buffer->getReferenceCount(), 2u);
    for(const Core::TextureHandle& texture : context.textures)
        EXPECT_EQ(texture->getReferenceCount(), 2u);
}

TEST(CommandBufferResourceReferences, IndirectOwnershipDoesNotPreventLaterOwningUpgrade){
    ReferencesContext context;
    context.addBuffers(1u);
    context.addTextures(1u);
    context.references.trackRetainedBuffer(*context.buffers[0u]);
    context.references.trackRetainedTexture(*context.textures[0u]);
    context.references.trackRetainedBuffer(*context.buffers[0u]);
    context.references.trackRetainedTexture(*context.textures[0u]);
    EXPECT_TRUE(Access::retainedResources(context.references).empty());
    EXPECT_EQ(context.buffers[0u]->getReferenceCount(), 1u);
    EXPECT_EQ(context.textures[0u]->getReferenceCount(), 1u);

    context.references.retainBuffer(*context.buffers[0u]);
    context.references.retainTexture(*context.textures[0u]);
    context.references.retainResource(*context.buffers[0u]);
    context.references.retainResource(*context.textures[0u]);
    EXPECT_EQ(context.buffers[0u]->getReferenceCount(), 2u);
    EXPECT_EQ(context.textures[0u]->getReferenceCount(), 2u);
    Core::Buffer* const retainedBuffer = context.buffers[0u].get();
    context.buffers.clear();
    EXPECT_EQ(retainedBuffer->getReferenceCount(), 1u);
    EXPECT_EQ(retainedBuffer->getCreationDescription().byteSize, 256u);
    context.references.clear();
    EXPECT_EQ(context.textures[0u]->getReferenceCount(), 1u);
    EXPECT_TRUE(Access::retainedResources(context.references).empty());
    EXPECT_TRUE(Access::retainedBuffers(context.references).empty());
    EXPECT_TRUE(Access::retainedTextures(context.references).empty());
}

TEST(CommandBufferResourceReferences, PendingStateDiscardAllowsFreshOrderedJournalWithoutReleasingOwners){
    ReferencesContext context;
    context.addBuffers(3u);
    context.references.appendBufferStateCommit(*context.buffers[2u]);
    context.references.appendBufferStateCommit(*context.buffers[0u]);
    context.references.appendBufferStateCommit(*context.buffers[2u]);
    const auto& first = Access::retainedBufferStateCommits(context.references);
    ASSERT_EQ(first.size(), 2u);
    EXPECT_EQ(first[0u].buffer, context.buffers[2u].get());
    EXPECT_EQ(first[1u].buffer, context.buffers[0u].get());
    context.references.discardBufferStateCommits();
    EXPECT_TRUE(Access::retainedBufferStateCommits(context.references).empty());
    EXPECT_EQ(context.buffers[2u]->getReferenceCount(), 2u);
    EXPECT_EQ(context.buffers[0u]->getReferenceCount(), 2u);
    context.references.appendBufferStateCommit(*context.buffers[0u]);
    context.references.appendBufferStateCommit(*context.buffers[2u]);
    context.references.appendBufferStateCommit(*context.buffers[1u]);
    const auto& second = Access::retainedBufferStateCommits(context.references);
    ASSERT_EQ(second.size(), 3u);
    EXPECT_EQ(second[0u].buffer, context.buffers[0u].get());
    EXPECT_EQ(second[1u].buffer, context.buffers[2u].get());
    EXPECT_EQ(second[2u].buffer, context.buffers[1u].get());
    context.references.clear();
    EXPECT_TRUE(Access::retainedBufferStateCommits(context.references).empty());
    for(const Core::BufferHandle& buffer : context.buffers)
        EXPECT_EQ(buffer->getReferenceCount(), 1u);
}

TEST(CommandBufferResourceReferences, ClearAndReusePreserveMembershipAcrossGrowthAndIndependentRecordings){
    ReferencesContext context;
    context.addBuffers(128u);
    context.addTextures(32u);
    context.recordResources();
    context.recordResources();
    EXPECT_EQ(Access::retainedResources(context.references).size(), 160u);
    EXPECT_EQ(Access::retainedBufferStateCommits(context.references).size(), 128u);
    context.references.clear();
    context.references.clear();
    for(const Core::BufferHandle& buffer : context.buffers)
        EXPECT_EQ(buffer->getReferenceCount(), 1u);

    Graphics::CommandBufferResourceReferences independent(context.testArena.arena);
    independent.retainBuffer(*context.buffers[0u]);
    context.addBuffers(64u);
    for(usize index = context.buffers.size(); index > 0u; --index)
        context.references.appendBufferStateCommit(*context.buffers[index - 1u]);
    ASSERT_EQ(Access::retainedBuffers(context.references).size(), 192u);
    EXPECT_EQ(Access::retainedBuffers(context.references).front(), context.buffers.back().get());
    EXPECT_EQ(Access::retainedBuffers(context.references).back(), context.buffers.front().get());
    EXPECT_EQ(Access::retainedBufferStateCommits(context.references).size(), 192u);
    EXPECT_EQ(context.buffers[0u]->getReferenceCount(), 3u);
    context.references.clear();
    EXPECT_EQ(context.buffers[0u]->getReferenceCount(), 2u);
    EXPECT_EQ(Access::retainedResources(independent).size(), 1u);
    independent.clear();
    EXPECT_EQ(context.buffers[0u]->getReferenceCount(), 1u);
}

TEST(CommandBufferResourceReferences, DestructionReleasesOwnedResourcesDuringUnwind){
    ReferencesContext context;
    context.addBuffers(48u);
    const auto retainThenUnwind = [&]{
        Graphics::CommandBufferResourceReferences references(context.testArena.arena);
        for(const Core::BufferHandle& buffer : context.buffers)
            references.appendBufferStateCommit(*buffer);
        throw RuntimeException("test recording unwind");
    };
    EXPECT_THROW(retainThenUnwind(), RuntimeException);
    for(const Core::BufferHandle& buffer : context.buffers)
        EXPECT_EQ(buffer->getReferenceCount(), 1u);
}

TEST(CommandBufferResourceReferences, SmallRepeatedAndRecycledRecordingsNeedNoAdditionalAllocations){
    ReferencesContext context;
    context.addBuffers(4u);
    context.addTextures(1u);
    EXPECT_FALSE(Access::hasResourceReferenceIndex(context.references));
    context.recordResources();
    EXPECT_FALSE(Access::hasResourceReferenceIndex(context.references));
    const ArenaMemoryStats before = context.testArena.arena.memoryStats();
    for(usize iteration = 0u; iteration < 32u; ++iteration){
        context.recordResources();
        context.references.discardBufferStateCommits();
        context.recordResources();
        context.references.clear();
        context.recordResources();
    }
    const ArenaMemoryStats after = context.testArena.arena.memoryStats();
    EXPECT_FALSE(Access::hasResourceReferenceIndex(context.references));
    EXPECT_EQ(after.allocationCount, before.allocationCount);
    EXPECT_EQ(after.reallocationCount, before.reallocationCount);
    EXPECT_EQ(after.usedBytes, before.usedBytes);
    EXPECT_EQ(Access::retainedResources(context.references).size(), 5u);
    EXPECT_EQ(Access::retainedBufferStateCommits(context.references).size(), 4u);
}

TEST(CommandBufferResourceReferences, TexturePromotionPreservesEveryPreexistingMembershipAndJournal){
    ReferencesContext context;
    context.addBuffers(33u);
    context.addTextures(33u);
    for(usize index = 0u; index < 32u; ++index)
        context.references.trackRetainedBuffer(*context.buffers[index]);
    context.references.appendBufferStateCommit(*context.buffers[0u]);
    for(usize index = 0u; index < 32u; ++index)
        context.references.trackRetainedTexture(*context.textures[index]);
    ASSERT_FALSE(Access::hasResourceReferenceIndex(context.references));
    context.references.trackRetainedTexture(*context.textures[32u]);
    ASSERT_TRUE(Access::hasResourceReferenceIndex(context.references));
    EXPECT_EQ(Access::resourceReferenceIndexSize(context.references), 65u);
    EXPECT_EQ(Access::retainedResources(context.references).size(), 1u);
    context.references.appendBufferStateCommit(*context.buffers[0u]);
    EXPECT_EQ(Access::retainedBufferStateCommits(context.references).size(), 1u);
    EXPECT_EQ(context.buffers[0u]->getReferenceCount(), 2u);
    context.references.retainTexture(*context.textures[0u]);
    EXPECT_EQ(Access::retainedTextures(context.references).size(), 33u);
    EXPECT_EQ(context.textures[0u]->getReferenceCount(), 2u);
    context.references.retainBuffer(*context.buffers[32u]);
    EXPECT_EQ(Access::resourceReferenceIndexSize(context.references), 66u);

    context.references.discardBufferStateCommits();
    for(usize index = context.buffers.size(); index > 0u; --index)
        context.references.appendBufferStateCommit(*context.buffers[index - 1u]);
    const auto& commits = Access::retainedBufferStateCommits(context.references);
    ASSERT_EQ(commits.size(), 33u);
    EXPECT_EQ(commits.front().buffer, context.buffers.back().get());
    EXPECT_EQ(commits.back().buffer, context.buffers.front().get());
    for(const Core::BufferHandle& buffer : context.buffers)
        EXPECT_EQ(buffer->getReferenceCount(), 2u);
    context.references.clear();
    EXPECT_EQ(Access::resourceReferenceIndexSize(context.references), 0u);
    context.recordResources();
    EXPECT_EQ(Access::resourceReferenceIndexSize(context.references), 66u);
    EXPECT_EQ(Access::retainedResources(context.references).size(), 66u);
    EXPECT_EQ(Access::retainedBufferStateCommits(context.references).size(), 33u);
}

TEST(CommandBufferResourceReferences, OwningPromotionKeepsUntypedOwnersAliveUntilClear){
    ReferencesContext context;
    context.addBuffers(33u);
    for(const Core::BufferHandle& buffer : context.buffers)
        context.references.retainResource(*buffer);
    ASSERT_TRUE(Access::hasResourceReferenceIndex(context.references));
    ASSERT_EQ(Access::retainedResources(context.references).size(), 33u);
    EXPECT_TRUE(Access::retainedBuffers(context.references).empty());
    EXPECT_EQ(Access::resourceReferenceIndexSize(context.references), 33u);
    context.references.appendBufferStateCommit(*context.buffers[0u]);
    context.references.appendBufferStateCommit(*context.buffers[0u]);
    EXPECT_EQ(Access::retainedBuffers(context.references).size(), 1u);
    EXPECT_EQ(Access::retainedBufferStateCommits(context.references).size(), 1u);
    EXPECT_EQ(context.buffers[0u]->getReferenceCount(), 2u);

    Core::Buffer* const retainedBuffer = context.buffers[0u].get();
    context.buffers.clear();
    EXPECT_EQ(retainedBuffer->getReferenceCount(), 1u);
    EXPECT_EQ(retainedBuffer->getCreationDescription().byteSize, 256u);
    const u64 usedBeforeClear = context.testArena.arena.memoryStats().usedBytes;
    context.references.clear();
    EXPECT_TRUE(Access::retainedResources(context.references).empty());
    EXPECT_TRUE(Access::retainedBufferStateCommits(context.references).empty());
    EXPECT_EQ(Access::resourceReferenceIndexSize(context.references), 0u);
    EXPECT_LT(context.testArena.arena.memoryStats().usedBytes, usedBeforeClear);
}

TEST(CommandBufferResourceReferences, IndexedRepeatedAndRecycledRecordingsReuseCapacity){
    ReferencesContext context;
    context.addBuffers(128u);
    context.addTextures(32u);
    context.recordResources();
    ASSERT_TRUE(Access::hasResourceReferenceIndex(context.references));
    context.references.clear();
    context.recordResources();
    const ArenaMemoryStats before = context.testArena.arena.memoryStats();
    for(usize iteration = 0u; iteration < 8u; ++iteration){
        context.recordResources();
        context.references.discardBufferStateCommits();
        context.recordResources();
        context.references.clear();
        context.recordResources();
    }
    const ArenaMemoryStats after = context.testArena.arena.memoryStats();
    EXPECT_EQ(after.allocationCount, before.allocationCount);
    EXPECT_EQ(after.reallocationCount, before.reallocationCount);
    EXPECT_EQ(after.usedBytes, before.usedBytes);
    EXPECT_EQ(Access::resourceReferenceIndexSize(context.references), 160u);
    EXPECT_EQ(Access::retainedResources(context.references).size(), 160u);
    EXPECT_EQ(Access::retainedBufferStateCommits(context.references).size(), 128u);
}

static void BenchmarkReferences(const usize bufferCount, const usize textureCount, const usize repeatCount){
    ReferencesContext context;
    context.addBuffers(bufferCount);
    context.addTextures(textureCount);
    const ArenaMemoryStats initialStats = context.testArena.arena.memoryStats();
    const Timer coldBegin = TimerNow();
    context.recordResources();
    const u64 coldNanoseconds = DurationInNS<u64>(TimerNow(), coldBegin);
    const Timer repeatedBegin = TimerNow();
    for(usize iteration = 0u; iteration < repeatCount; ++iteration)
        context.recordResources();
    const u64 repeatedNanoseconds = DurationInNS<u64>(TimerNow(), repeatedBegin);
    EXPECT_EQ(Access::retainedResources(context.references).size(), bufferCount + textureCount);
    EXPECT_EQ(Access::retainedBuffers(context.references).size(), bufferCount);
    EXPECT_EQ(Access::retainedTextures(context.references).size(), textureCount);
    EXPECT_EQ(Access::retainedBufferStateCommits(context.references).size(), bufferCount);
    const Timer clearBegin = TimerNow();
    context.references.clear();
    const u64 clearNanoseconds = DurationInNS<u64>(TimerNow(), clearBegin);
    const Timer recycleBegin = TimerNow();
    context.recordResources();
    const u64 recycleNanoseconds = DurationInNS<u64>(TimerNow(), recycleBegin);
    EXPECT_EQ(Access::retainedResources(context.references).size(), bufferCount + textureCount);
    EXPECT_EQ(Access::retainedBufferStateCommits(context.references).size(), bufferCount);
    for(const Core::BufferHandle& buffer : context.buffers)
        EXPECT_EQ(buffer->getReferenceCount(), 2u);
    for(const Core::TextureHandle& texture : context.textures)
        EXPECT_EQ(texture->getReferenceCount(), 2u);

    const ArenaMemoryStats stats = context.testArena.arena.memoryStats();
    char coldText[32u] = {};
    char repeatedText[32u] = {};
    char countText[32u] = {};
    char clearText[32u] = {};
    char recycleText[32u] = {};
    char peakText[32u] = {};
    char usedText[32u] = {};
    testing::Test::RecordProperty("references_cold_ns", FormatDecimal(coldNanoseconds, coldText).data());
    testing::Test::RecordProperty("references_repeat_ns", FormatDecimal(repeatedNanoseconds, repeatedText).data());
    testing::Test::RecordProperty("references_repeat_count", FormatDecimal(repeatCount, countText).data());
    testing::Test::RecordProperty("references_clear_ns", FormatDecimal(clearNanoseconds, clearText).data());
    testing::Test::RecordProperty("references_recycle_ns", FormatDecimal(recycleNanoseconds, recycleText).data());
    testing::Test::RecordProperty("references_peak_used_bytes", FormatDecimal(stats.peakUsedBytes - initialStats.usedBytes, peakText).data());
    testing::Test::RecordProperty("references_retained_bytes", FormatDecimal(stats.usedBytes - initialStats.usedBytes, usedText).data());
}

TEST(CommandBufferResourceReferences, DISABLED_BenchmarkLargeRepeatedRecording){
    BenchmarkReferences(1024u, 256u, 8u);
}

TEST(CommandBufferResourceReferences, DISABLED_BenchmarkSmallRepeatedRecording){
    BenchmarkReferences(4u, 1u, 256u);
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

