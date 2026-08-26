// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


// Transactional ownership and submitted-lifetime coverage for permanent Buffer and Texture state.


#include <gtest/gtest.h>

#include <global/global.h>
#include <global/unique_ptr.h>
#include <core/graphics/vulkan/backend.h>
#include <tests/common/capturing_logger.h>
#include <tests/common/headless_graphics_scope.h>
#include <tests/common/test_context.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace Tests{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


using namespace Core;
using PermanentStateTestArena = ::NWB::Tests::TestArena<struct PermanentStateTestArenaTag>;


class PermanentStateLifetimeTest : public ::testing::Test{
protected:
    static void SetUpTestSuite(){
        s_logger.emplace();
        s_loggerGuard.emplace(*s_logger);

        s_scope = MakeUnique<HeadlessGraphicsScope>();
        if(!s_scope->initialize()){
            GTEST_SKIP() << "Permanent-state lifetime: no usable validation-enabled headless Vulkan device.";
            return;
        }
        s_validationBackedDeviceInitialized = true;
    }

    static void TearDownTestSuite(){
        s_scope.reset();
        if(s_validationBackedDeviceInitialized && s_logger.has_value()){
            EXPECT_FALSE(s_logger->sawMessageContaining(NWB_TEXT("Vulkan debug: [severity=error")))
                << "validation-enabled permanent-state lifetime smoke emitted a Vulkan severity=error message";
        }
        s_loggerGuard.reset();
        s_logger.reset();
        s_validationBackedDeviceInitialized = false;
    }

    [[nodiscard]] static GraphicsBackend::Device& device(){
        return s_scope->graphics().getDevice();
    }

    [[nodiscard]] static Core::Alloc::GlobalArena& arena(){
        return s_scope->arena();
    }

protected:
    static bool s_validationBackedDeviceInitialized;
    static UniquePtr<HeadlessGraphicsScope> s_scope;
    static Optional<CapturingLogger> s_logger;
    static Optional<Common::LoggerRegistrationGuard> s_loggerGuard;
};

bool PermanentStateLifetimeTest::s_validationBackedDeviceInitialized = false;
UniquePtr<HeadlessGraphicsScope> PermanentStateLifetimeTest::s_scope;
Optional<CapturingLogger> PermanentStateLifetimeTest::s_logger;
Optional<Common::LoggerRegistrationGuard> PermanentStateLifetimeTest::s_loggerGuard;


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


TEST(PermanentStateOwnership, StateTrackerValuesOwnSnapshotsTransactionally){
    PermanentStateTestArena testArena;
    GraphicsAllocator graphicsAllocator(testArena.arena);
    Core::Alloc::ThreadPool threadPool(0u);
    GraphicsBackend::VulkanContext context(graphicsAllocator, threadPool, 1u);
    GraphicsBackend::VulkanAllocator allocator(context);

    Buffer* const baselineBufferObject = NewArenaObject<Buffer>(testArena.arena, context, allocator, BufferDesc{});
    Texture* const baselineTextureObject = NewArenaObject<Texture>(
        testArena.arena,
        context,
        allocator,
        TextureDesc{}
    );
    Buffer* const provisionalBufferObject = NewArenaObject<Buffer>(testArena.arena, context, allocator, BufferDesc{});
    Texture* const provisionalTextureObject = NewArenaObject<Texture>(
        testArena.arena,
        context,
        allocator,
        TextureDesc{}
    );
    ASSERT_NE(baselineBufferObject, nullptr);
    ASSERT_NE(baselineTextureObject, nullptr);
    ASSERT_NE(provisionalBufferObject, nullptr);
    ASSERT_NE(provisionalTextureObject, nullptr);

    BufferHandle baselineBuffer(
        baselineBufferObject,
        BufferHandle::deleter_type(&testArena.arena),
        AdoptRef
    );
    TextureHandle baselineTexture(
        baselineTextureObject,
        TextureHandle::deleter_type(&testArena.arena),
        AdoptRef
    );
    BufferHandle provisionalBuffer(
        provisionalBufferObject,
        BufferHandle::deleter_type(&testArena.arena),
        AdoptRef
    );
    TextureHandle provisionalTexture(
        provisionalTextureObject,
        TextureHandle::deleter_type(&testArena.arena),
        AdoptRef
    );

    EXPECT_EQ(baselineBuffer->getReferenceCount(), 1u);
    EXPECT_EQ(baselineTexture->getReferenceCount(), 1u);
    EXPECT_EQ(provisionalBuffer->getReferenceCount(), 1u);
    EXPECT_EQ(provisionalTexture->getReferenceCount(), 1u);

    {
        GraphicsBackend::StateTracker tracker(context);
        tracker.setPermanentBufferState(*baselineBuffer, ResourceStates::Common);
        tracker.setPermanentTextureState(*baselineTexture, ResourceStates::Common);
        EXPECT_EQ(baselineBuffer->getReferenceCount(), 2u);
        EXPECT_EQ(baselineTexture->getReferenceCount(), 2u);

        tracker.setPermanentBufferState(*baselineBuffer, ResourceStates::Common);
        tracker.setPermanentTextureState(*baselineTexture, ResourceStates::Common);
        tracker.setPermanentBufferState(*baselineBuffer, ResourceStates::CopyDest);
        tracker.setPermanentTextureState(*baselineTexture, ResourceStates::CopyDest);
        EXPECT_EQ(tracker.getPermanentBufferState(baselineBuffer.get()), ResourceStates::Common);
        EXPECT_EQ(tracker.getPermanentTextureState(baselineTexture.get()), ResourceStates::Common);
        EXPECT_EQ(baselineBuffer->getReferenceCount(), 2u);
        EXPECT_EQ(baselineTexture->getReferenceCount(), 2u);

        tracker.beginRecordingAttempt();
        EXPECT_EQ(baselineBuffer->getReferenceCount(), 3u);
        EXPECT_EQ(baselineTexture->getReferenceCount(), 3u);
        tracker.setPermanentBufferState(*provisionalBuffer, ResourceStates::Common);
        tracker.setPermanentTextureState(*provisionalTexture, ResourceStates::Common);
        EXPECT_EQ(provisionalBuffer->getReferenceCount(), 2u);
        EXPECT_EQ(provisionalTexture->getReferenceCount(), 2u);

        tracker.rollbackRecordingAttempt();
        EXPECT_EQ(tracker.getPermanentBufferState(baselineBuffer.get()), ResourceStates::Common);
        EXPECT_EQ(tracker.getPermanentTextureState(baselineTexture.get()), ResourceStates::Common);
        EXPECT_EQ(tracker.getPermanentBufferState(provisionalBuffer.get()), ResourceStates::Unknown);
        EXPECT_EQ(tracker.getPermanentTextureState(provisionalTexture.get()), ResourceStates::Unknown);
        EXPECT_EQ(baselineBuffer->getReferenceCount(), 2u);
        EXPECT_EQ(baselineTexture->getReferenceCount(), 2u);
        EXPECT_EQ(provisionalBuffer->getReferenceCount(), 1u);
        EXPECT_EQ(provisionalTexture->getReferenceCount(), 1u);

        tracker.beginRecordingAttempt();
        tracker.setPermanentBufferState(*provisionalBuffer, ResourceStates::Common);
        tracker.setPermanentTextureState(*provisionalTexture, ResourceStates::Common);
        tracker.commitRecordingAttempt();
        EXPECT_EQ(baselineBuffer->getReferenceCount(), 2u);
        EXPECT_EQ(baselineTexture->getReferenceCount(), 2u);
        EXPECT_EQ(provisionalBuffer->getReferenceCount(), 2u);
        EXPECT_EQ(provisionalTexture->getReferenceCount(), 2u);
    }

    EXPECT_EQ(baselineBuffer->getReferenceCount(), 1u);
    EXPECT_EQ(baselineTexture->getReferenceCount(), 1u);
    EXPECT_EQ(provisionalBuffer->getReferenceCount(), 1u);
    EXPECT_EQ(provisionalTexture->getReferenceCount(), 1u);
}


TEST(PermanentStateOwnership, StateTrackerUsesEachResourceArenaForLastOwnerDeletion){
    PermanentStateTestArena trackerArena;
    PermanentStateTestArena resourceArena;
    GraphicsAllocator trackerGraphicsAllocator(trackerArena.arena);
    GraphicsAllocator resourceGraphicsAllocator(resourceArena.arena);
    Core::Alloc::ThreadPool trackerThreadPool(0u);
    Core::Alloc::ThreadPool resourceThreadPool(0u);
    GraphicsBackend::VulkanContext trackerContext(trackerGraphicsAllocator, trackerThreadPool, 1u);
    GraphicsBackend::VulkanContext resourceContext(resourceGraphicsAllocator, resourceThreadPool, 2u);
    GraphicsBackend::VulkanAllocator resourceAllocator(resourceContext);
    const u64 trackerUsedBytesBefore = trackerArena.arena.memoryStats().usedBytes;
    const u64 resourceUsedBytesBefore = resourceArena.arena.memoryStats().usedBytes;

    {
        Buffer* const bufferObject = NewArenaObject<Buffer>(
            resourceArena.arena,
            resourceContext,
            resourceAllocator,
            BufferDesc{}
        );
        Texture* const textureObject = NewArenaObject<Texture>(
            resourceArena.arena,
            resourceContext,
            resourceAllocator,
            TextureDesc{}
        );
        ASSERT_NE(bufferObject, nullptr);
        ASSERT_NE(textureObject, nullptr);
        BufferHandle buffer(bufferObject, BufferHandle::deleter_type(&resourceArena.arena), AdoptRef);
        TextureHandle texture(textureObject, TextureHandle::deleter_type(&resourceArena.arena), AdoptRef);

        GraphicsBackend::StateTracker tracker(trackerContext);
        tracker.setPermanentBufferState(*buffer, ResourceStates::Common);
        tracker.setPermanentTextureState(*texture, ResourceStates::Common);
        ASSERT_EQ(buffer->getReferenceCount(), 2u);
        ASSERT_EQ(texture->getReferenceCount(), 2u);
        buffer.reset();
        texture.reset();
        EXPECT_GT(resourceArena.arena.memoryStats().usedBytes, resourceUsedBytesBefore);
    }

    EXPECT_EQ(resourceArena.arena.memoryStats().usedBytes, resourceUsedBytesBefore);
    EXPECT_EQ(trackerArena.arena.memoryStats().usedBytes, trackerUsedBytesBefore);
}


TEST_F(PermanentStateLifetimeTest, SubmittedSameStatePermanentsSurviveCallerDropAndReopen){
    auto& device = PermanentStateLifetimeTest::device();
    BufferHandle buffer = device.createBuffer(
        BufferDesc()
            .setByteSize(256u)
            .setInitialState(ResourceStates::Common)
            .setKeepInitialState(true)
    );
    TextureHandle texture = device.createTexture(
        TextureDesc()
            .setWidth(4u)
            .setHeight(4u)
            .setFormat(Format::RGBA8_UNORM)
            .setInitialState(ResourceStates::Common)
            .setKeepInitialState(true)
    );
    if(!buffer || !texture)
        GTEST_SKIP() << "Permanent-state lifetime: basic Buffer or RGBA8 Texture creation is unavailable.";

    Buffer* const rawBuffer = buffer.get();
    Texture* const rawTexture = texture.get();
    CommandListHandle commandList = device.createCommandList();
    ASSERT_TRUE(commandList);
    const auto submit = [&](){
        CommandList* const commandLists[] = { commandList.get() };
        return device.executeCommandLists(
            commandLists,
            LengthOf(commandLists),
            commandList->getDescription().physicalQueue,
            QueueSubmissionDesc{}
        );
    };
    const auto retirePacketReferences = [&](const u32 expectedReferences){
        for(
            u32 retry = 0u;
            retry < 5000u
                && (
                    rawBuffer->getReferenceCount() != expectedReferences
                    || rawTexture->getReferenceCount() != expectedReferences
                );
            ++retry
        ){
            device.runGarbageCollection();
            SleepMS(1u);
        }
    };

    commandList->open();
    commandList->setPermanentBufferState(rawBuffer, ResourceStates::Common);
    commandList->setPermanentTextureState(rawTexture, ResourceStates::Common);
    commandList->close();
    ASSERT_FALSE(commandList->commandRecordingFailed());
    ASSERT_TRUE(commandList->hasCommandBuffer());
    ASSERT_TRUE(submit().valid());
    ASSERT_TRUE(device.waitForIdle());
    retirePacketReferences(2u);
    ASSERT_EQ(rawBuffer->getReferenceCount(), 2u);
    ASSERT_EQ(rawTexture->getReferenceCount(), 2u);

    buffer.reset();
    texture.reset();
    ASSERT_EQ(rawBuffer->getReferenceCount(), 1u);
    ASSERT_EQ(rawTexture->getReferenceCount(), 1u);

    commandList->open();
    EXPECT_EQ(commandList->getPermanentBufferState(rawBuffer), ResourceStates::Common);
    EXPECT_EQ(commandList->getPermanentTextureState(rawTexture), ResourceStates::Common);
    commandList->close();
    ASSERT_TRUE(submit().valid());
    ASSERT_TRUE(device.waitForIdle());
    retirePacketReferences(1u);
    ASSERT_EQ(rawBuffer->getReferenceCount(), 1u);
    ASSERT_EQ(rawTexture->getReferenceCount(), 1u);

    BufferHandle observedBuffer(rawBuffer, BufferHandle::deleter_type(&PermanentStateLifetimeTest::arena()));
    TextureHandle observedTexture(rawTexture, TextureHandle::deleter_type(&PermanentStateLifetimeTest::arena()));
    EXPECT_EQ(rawBuffer->getReferenceCount(), 2u);
    EXPECT_EQ(rawTexture->getReferenceCount(), 2u);
    commandList.reset();
    EXPECT_EQ(observedBuffer->getReferenceCount(), 1u);
    EXPECT_EQ(observedTexture->getReferenceCount(), 1u);
}


TEST_F(PermanentStateLifetimeTest, FailedAndAbandonedAttemptsReleaseProvisionalPermanents){
    auto& device = PermanentStateLifetimeTest::device();
    const BufferDesc bufferDesc = BufferDesc()
        .setByteSize(256u)
        .setInitialState(ResourceStates::Common)
        .setKeepInitialState(true)
    ;
    const TextureDesc textureDesc = TextureDesc()
        .setWidth(4u)
        .setHeight(4u)
        .setFormat(Format::RGBA8_UNORM)
        .setInitialState(ResourceStates::Common)
        .setKeepInitialState(true)
    ;
    BufferHandle baselineBuffer = device.createBuffer(bufferDesc);
    TextureHandle baselineTexture = device.createTexture(textureDesc);
    BufferHandle rejectedBuffer = device.createBuffer(bufferDesc);
    TextureHandle rejectedTexture = device.createTexture(textureDesc);
    BufferHandle abandonedBuffer = device.createBuffer(bufferDesc);
    TextureHandle abandonedTexture = device.createTexture(textureDesc);
    if(
        !baselineBuffer
        || !baselineTexture
        || !rejectedBuffer
        || !rejectedTexture
        || !abandonedBuffer
        || !abandonedTexture
    )
        GTEST_SKIP() << "Permanent-state transaction: required basic resources are unavailable.";

    CommandListHandle commandList = device.createCommandList();
    ASSERT_TRUE(commandList);
    const auto submit = [&](){
        CommandList* const commandLists[] = { commandList.get() };
        return device.executeCommandLists(
            commandLists,
            LengthOf(commandLists),
            commandList->getDescription().physicalQueue,
            QueueSubmissionDesc{}
        );
    };

    commandList->open();
    commandList->setPermanentBufferState(baselineBuffer.get(), ResourceStates::Common);
    commandList->setPermanentTextureState(baselineTexture.get(), ResourceStates::Common);
    commandList->close();
    ASSERT_TRUE(submit().valid());
    ASSERT_TRUE(device.waitForIdle());

    commandList->open();
    commandList->setPermanentBufferState(rejectedBuffer.get(), ResourceStates::Common);
    commandList->setPermanentTextureState(rejectedTexture.get(), ResourceStates::Common);
    commandList->setBufferState(baselineBuffer.get(), ResourceStates::CopyDest);
    ASSERT_TRUE(commandList->commandRecordingFailed());
    commandList->close();
    EXPECT_EQ(commandList->getPermanentBufferState(baselineBuffer.get()), ResourceStates::Common);
    EXPECT_EQ(commandList->getPermanentTextureState(baselineTexture.get()), ResourceStates::Common);
    EXPECT_EQ(commandList->getPermanentBufferState(rejectedBuffer.get()), ResourceStates::Unknown);
    EXPECT_EQ(commandList->getPermanentTextureState(rejectedTexture.get()), ResourceStates::Unknown);
    EXPECT_EQ(rejectedBuffer->getReferenceCount(), 1u);
    EXPECT_EQ(rejectedTexture->getReferenceCount(), 1u);

    commandList->open();
    commandList->setPermanentBufferState(abandonedBuffer.get(), ResourceStates::Common);
    commandList->setPermanentTextureState(abandonedTexture.get(), ResourceStates::Common);
    commandList->close();
    ASSERT_TRUE(commandList->hasCommandBuffer());
    commandList->open();
    EXPECT_EQ(commandList->getPermanentBufferState(abandonedBuffer.get()), ResourceStates::Unknown);
    EXPECT_EQ(commandList->getPermanentTextureState(abandonedTexture.get()), ResourceStates::Unknown);
    EXPECT_EQ(abandonedBuffer->getReferenceCount(), 1u);
    EXPECT_EQ(abandonedTexture->getReferenceCount(), 1u);
    EXPECT_EQ(commandList->getPermanentBufferState(baselineBuffer.get()), ResourceStates::Common);
    EXPECT_EQ(commandList->getPermanentTextureState(baselineTexture.get()), ResourceStates::Common);
    commandList->close();
    ASSERT_TRUE(submit().valid());
    ASSERT_TRUE(device.waitForIdle());
}


TEST_F(PermanentStateLifetimeTest, ImportedPermanentsOutliveProducerHandoffAndCallerHandles){
    auto& device = PermanentStateLifetimeTest::device();
    BufferHandle buffer = device.createBuffer(
        BufferDesc()
            .setByteSize(256u)
            .setInitialState(ResourceStates::Common)
            .setKeepInitialState(true)
    );
    TextureHandle texture = device.createTexture(
        TextureDesc()
            .setWidth(4u)
            .setHeight(4u)
            .setFormat(Format::RGBA8_UNORM)
            .setInitialState(ResourceStates::Common)
            .setKeepInitialState(true)
    );
    if(!buffer || !texture)
        GTEST_SKIP() << "Permanent-state handoff: basic Buffer or RGBA8 Texture creation is unavailable.";

    Buffer* const rawBuffer = buffer.get();
    Texture* const rawTexture = texture.get();
    CommandListHandle producer = device.createCommandList();
    CommandListHandle consumer = device.createCommandList();
    ASSERT_TRUE(producer);
    ASSERT_TRUE(consumer);
    const auto submit = [&](CommandListHandle& commandList){
        CommandList* const commandLists[] = { commandList.get() };
        return device.executeCommandLists(
            commandLists,
            LengthOf(commandLists),
            commandList->getDescription().physicalQueue,
            QueueSubmissionDesc{}
        );
    };

    CommandListResourceStateHandoff handoff(PermanentStateLifetimeTest::arena());
    producer->open();
    producer->setPermanentBufferState(rawBuffer, ResourceStates::Common);
    producer->setPermanentTextureState(rawTexture, ResourceStates::Common);
    producer->close(&handoff);
    ASSERT_TRUE(handoff.valid());
    ASSERT_TRUE(submit(producer).valid());
    ASSERT_TRUE(device.waitForIdle());

    consumer->open(&handoff);
    ASSERT_FALSE(consumer->commandRecordingFailed());
    EXPECT_EQ(consumer->getPermanentBufferState(rawBuffer), ResourceStates::Common);
    EXPECT_EQ(consumer->getPermanentTextureState(rawTexture), ResourceStates::Common);
    consumer->close();
    ASSERT_TRUE(submit(consumer).valid());
    ASSERT_TRUE(device.waitForIdle());
    for(
        u32 retry = 0u;
        retry < 5000u
            && (rawBuffer->getReferenceCount() != 3u || rawTexture->getReferenceCount() != 3u);
        ++retry
    ){
        device.runGarbageCollection();
        SleepMS(1u);
    }
    ASSERT_EQ(rawBuffer->getReferenceCount(), 3u);
    ASSERT_EQ(rawTexture->getReferenceCount(), 3u);

    handoff.reset();
    buffer.reset();
    texture.reset();
    producer.reset();
    ASSERT_EQ(rawBuffer->getReferenceCount(), 1u);
    ASSERT_EQ(rawTexture->getReferenceCount(), 1u);
    EXPECT_EQ(consumer->getPermanentBufferState(rawBuffer), ResourceStates::Common);
    EXPECT_EQ(consumer->getPermanentTextureState(rawTexture), ResourceStates::Common);

    BufferHandle observedBuffer(rawBuffer, BufferHandle::deleter_type(&PermanentStateLifetimeTest::arena()));
    TextureHandle observedTexture(rawTexture, TextureHandle::deleter_type(&PermanentStateLifetimeTest::arena()));
    consumer.reset();
    EXPECT_EQ(observedBuffer->getReferenceCount(), 1u);
    EXPECT_EQ(observedTexture->getReferenceCount(), 1u);
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

