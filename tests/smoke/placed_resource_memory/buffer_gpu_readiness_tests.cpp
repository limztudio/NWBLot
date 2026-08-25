// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


// Buffer GPU-readiness, state-ingress atomicity, retention, close defense, and handoff defense coverage.


#include <gtest/gtest.h>

#include <global/global.h>
#include <global/unique_ptr.h>
#include <core/graphics/vulkan/backend.h>
#include <tests/common/capturing_logger.h>
#include <tests/common/headless_graphics_scope.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace Tests{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


using namespace Core;


class BufferGpuReadinessTest : public ::testing::Test{
protected:
    static void SetUpTestSuite(){
        s_logger.emplace();
        s_loggerGuard.emplace(*s_logger);

        s_scope = MakeUnique<HeadlessGraphicsScope>();
        if(!s_scope->setTransferQueueEnabled(true)){
            GTEST_SKIP() << "Buffer GPU readiness: transfer-queue configuration is unavailable.";
            return;
        }
        if(!s_scope->initialize()){
            GTEST_SKIP() << "Buffer GPU readiness: no usable validation-enabled headless Vulkan device.";
            return;
        }
        s_validationBackedDeviceInitialized = true;
    }

    static void TearDownTestSuite(){
        s_scope.reset();
        if(s_validationBackedDeviceInitialized && s_logger.has_value()){
            EXPECT_FALSE(s_logger->sawMessageContaining(NWB_TEXT("Vulkan debug: [severity=error")))
                << "validation-enabled buffer GPU-readiness smoke emitted a Vulkan severity=error message";
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

bool BufferGpuReadinessTest::s_validationBackedDeviceInitialized = false;
UniquePtr<HeadlessGraphicsScope> BufferGpuReadinessTest::s_scope;
Optional<CapturingLogger> BufferGpuReadinessTest::s_logger;
Optional<Common::LoggerRegistrationGuard> BufferGpuReadinessTest::s_loggerGuard;


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


TEST_F(BufferGpuReadinessTest, PredicateAcceptsOrdinaryNativeWrapperAndReadyHandoff){
    auto& device = BufferGpuReadinessTest::device();
    EXPECT_FALSE(device.isBufferReadyForGpuUse(nullptr));

    const BufferDesc desc = BufferDesc()
        .setByteSize(256u)
        .setInitialState(ResourceStates::Common)
    ;
    BufferHandle ordinary = device.createBuffer(desc);
    ASSERT_TRUE(ordinary);
    ASSERT_TRUE(device.isBufferReadyForGpuUse(ordinary.get()));

    const Object nativeBuffer = ordinary->getNativeHandle(GraphicsBackend::ObjectTypes::VK_Buffer);
    ASSERT_NE(nativeBuffer, nullptr);
    const BufferDesc wrapperDesc = BufferDesc()
        .setByteSize(256u)
        .setIsVirtual(true)
        .setInitialState(ResourceStates::Common)
    ;
    BufferHandle nativeWrapper = device.createHandleForNativeBuffer(
        GraphicsBackend::ObjectTypes::VK_Buffer,
        nativeBuffer,
        wrapperDesc
    );
    ASSERT_TRUE(nativeWrapper);
    ASSERT_TRUE(device.isBufferReadyForGpuUse(nativeWrapper.get()));

    CommandListResourceStateHandoff producerState(BufferGpuReadinessTest::arena());
    CommandListHandle producer = device.createCommandList();
    ASSERT_TRUE(producer);
    producer->open();
    producer->beginTrackingBufferState(nativeWrapper.get(), ResourceStates::Common);
    producer->close(&producerState);
    ASSERT_FALSE(producer->commandRecordingFailed());
    ASSERT_TRUE(producer->hasCommandBuffer());
    ASSERT_TRUE(producerState.valid());

    CommandListHandle consumer = device.createCommandList();
    ASSERT_TRUE(consumer);
    consumer->open(&producerState);
    consumer->setBufferState(nativeWrapper.get(), ResourceStates::CopySource);
    consumer->close();
    EXPECT_FALSE(consumer->commandRecordingFailed());
    EXPECT_TRUE(consumer->hasCommandBuffer());
}


TEST_F(BufferGpuReadinessTest, NullUnknownScopeConflictAndRetentionContractsAreExact){
    auto& device = BufferGpuReadinessTest::device();
    CommandListHandle commandList = device.createCommandList();
    ASSERT_TRUE(commandList);

    commandList->beginTrackingBufferState(nullptr, ResourceStates::Unknown);
    commandList->setBufferState(nullptr, ResourceStates::CopyDest);
    commandList->setPermanentBufferState(nullptr, ResourceStates::CopyDest);
    commandList->releaseBufferOwnership(nullptr, GpuPhysicalQueueId{});
    commandList->releaseBufferOwnership(nullptr, static_cast<CommandQueue::Enum>(UINT8_MAX));
    commandList->releaseBufferOwnership(nullptr, static_cast<RenderLane::Enum>(UINT8_MAX));
    EXPECT_FALSE(commandList->commandRecordingFailed());
    EXPECT_FALSE(commandList->hasCommandBuffer());

    BufferHandle tracked = device.createBuffer(
        BufferDesc().setByteSize(256u).setInitialState(ResourceStates::Common)
    );
    ASSERT_TRUE(tracked);
    commandList->beginTrackingBufferState(tracked.get(), ResourceStates::Common);
    EXPECT_TRUE(commandList->commandRecordingFailed());

    commandList->open();
    commandList->beginTrackingBufferState(tracked.get(), ResourceStates::Unknown);
    EXPECT_TRUE(commandList->commandRecordingFailed());
    EXPECT_FALSE(commandList->hasExplicitBufferState(tracked.get()));
    commandList->close();
    EXPECT_FALSE(commandList->hasCommandBuffer());

    commandList->open();
    const u32 trackedReferences = tracked->getReferenceCount();
    commandList->beginTrackingBufferState(tracked.get(), ResourceStates::Common);
    commandList->beginTrackingBufferState(tracked.get(), ResourceStates::Common);
    EXPECT_FALSE(commandList->commandRecordingFailed());
    EXPECT_TRUE(commandList->hasExplicitBufferState(tracked.get()));
    EXPECT_EQ(tracked->getReferenceCount(), trackedReferences + 1u);
    commandList->close();
    EXPECT_TRUE(commandList->hasCommandBuffer());

    BufferHandle permanent = device.createBuffer(
        BufferDesc()
            .setByteSize(256u)
            .setInitialState(ResourceStates::Common)
            .setKeepInitialState(true)
    );
    ASSERT_TRUE(permanent);
    CommandListHandle permanentList = device.createCommandList();
    ASSERT_TRUE(permanentList);
    permanentList->open();
    const u32 permanentReferences = permanent->getReferenceCount();
    permanentList->setPermanentBufferState(permanent.get(), ResourceStates::Common);
    EXPECT_FALSE(permanentList->commandRecordingFailed());
    EXPECT_EQ(permanentList->getPermanentBufferState(permanent.get()), ResourceStates::Common);
    EXPECT_EQ(permanent->getReferenceCount(), permanentReferences + 2u);
    permanentList->close();
    ASSERT_TRUE(permanentList->hasCommandBuffer());

    CommandListHandle conflict = device.createCommandList();
    ASSERT_TRUE(conflict);
    conflict->open();
    conflict->setPermanentBufferState(permanent.get(), ResourceStates::Common);
    ASSERT_FALSE(conflict->commandRecordingFailed());
    const ResourceStates::Mask stateBeforeConflict = conflict->getBufferState(permanent.get());
    const u32 referencesBeforeConflict = permanent->getReferenceCount();
    ASSERT_TRUE(conflict->hasExplicitBufferState(permanent.get()));
    conflict->beginTrackingBufferState(permanent.get(), ResourceStates::CopyDest);
    EXPECT_TRUE(conflict->commandRecordingFailed());
    EXPECT_EQ(conflict->getPermanentBufferState(permanent.get()), ResourceStates::Common);
    EXPECT_EQ(conflict->getBufferState(permanent.get()), stateBeforeConflict);
    EXPECT_TRUE(conflict->hasExplicitBufferState(permanent.get()));
    EXPECT_EQ(permanent->getReferenceCount(), referencesBeforeConflict);
    conflict->close();
    EXPECT_FALSE(conflict->hasCommandBuffer());
    EXPECT_EQ(conflict->getPermanentBufferState(permanent.get()), ResourceStates::Unknown);
}


TEST_F(BufferGpuReadinessTest, UnboundVirtualStateIngressRejectsWithoutPublishing){
    auto& device = BufferGpuReadinessTest::device();
    BufferHandle unbound = device.createBuffer(
        BufferDesc()
            .setByteSize(256u)
            .setIsVirtual(true)
            .setInitialState(ResourceStates::Common)
            .setKeepInitialState(true)
    );
    ASSERT_TRUE(unbound);
    ASSERT_FALSE(device.isBufferReadyForGpuUse(unbound.get()));

    const auto expectRejectedWithoutState = [&](auto&& operation){
        const u32 referencesBefore = unbound->getReferenceCount();
        CommandListResourceStateHandoff rejectedState(BufferGpuReadinessTest::arena());
        CommandListHandle commandList = device.createCommandList();
        ASSERT_TRUE(commandList);
        commandList->open();
        operation(*commandList);
        EXPECT_TRUE(commandList->commandRecordingFailed());
        EXPECT_FALSE(commandList->hasExplicitBufferState(unbound.get()));
        EXPECT_EQ(commandList->getPermanentBufferState(unbound.get()), ResourceStates::Unknown);
        EXPECT_EQ(unbound->getReferenceCount(), referencesBefore);
        commandList->close(&rejectedState);
        EXPECT_FALSE(commandList->hasCommandBuffer());
        EXPECT_FALSE(rejectedState.valid());
    };

    expectRejectedWithoutState([&](CommandList& commandList){
        commandList.beginTrackingBufferState(unbound.get(), ResourceStates::Common);
    });
    expectRejectedWithoutState([&](CommandList& commandList){
        commandList.setBufferState(unbound.get(), ResourceStates::Common);
    });
    expectRejectedWithoutState([&](CommandList& commandList){
        commandList.setPermanentBufferState(unbound.get(), ResourceStates::Common);
    });
    expectRejectedWithoutState([&](CommandList& commandList){
        commandList.releaseBufferOwnership(
            unbound.get(),
            device.getPrimaryPhysicalQueue(CommandQueue::Graphics)
        );
    });
}


TEST_F(BufferGpuReadinessTest, VirtualBufferBindsAfterRejectionAndFreshRecordingSucceeds){
    auto& device = BufferGpuReadinessTest::device();
    BufferHandle placed = device.createBuffer(
        BufferDesc()
            .setByteSize(256u)
            .setIsVirtual(true)
            .setInitialState(ResourceStates::Common)
    );
    ASSERT_TRUE(placed);

    CommandListHandle rejected = device.createCommandList();
    ASSERT_TRUE(rejected);
    rejected->open();
    rejected->beginTrackingBufferState(placed.get(), ResourceStates::Common);
    ASSERT_TRUE(rejected->commandRecordingFailed());
    rejected->close();
    ASSERT_FALSE(rejected->hasCommandBuffer());

    const MemoryRequirements requirements = device.getBufferMemoryRequirements(placed.get());
    ASSERT_GT(requirements.size, 0u);
    HeapHandle heap = device.createHeap(HeapDesc{
        .capacity = requirements.size,
        .type = HeapType::DeviceLocal,
        .debugName = Name("tests/buffer_gpu_readiness/placed_heap"),
    });
    ASSERT_TRUE(heap);
    if(!device.bindBufferMemory(placed.get(), heap.get(), 0u))
        GTEST_SKIP() << "Buffer GPU readiness: DeviceLocal heap is incompatible with virtual buffers.";
    ASSERT_TRUE(device.isBufferReadyForGpuUse(placed.get()));

    CommandListResourceStateHandoff finalState(BufferGpuReadinessTest::arena());
    CommandListHandle retry = device.createCommandList();
    ASSERT_TRUE(retry);
    retry->open();
    retry->beginTrackingBufferState(placed.get(), ResourceStates::Common);
    retry->setBufferState(placed.get(), ResourceStates::CopyDest);
    retry->close(&finalState);
    ASSERT_FALSE(retry->commandRecordingFailed());
    ASSERT_TRUE(retry->hasCommandBuffer());
    ASSERT_TRUE(finalState.valid());

    CommandListHandle consumer = device.createCommandList();
    ASSERT_TRUE(consumer);
    consumer->open(&finalState);
    consumer->close();
    EXPECT_FALSE(consumer->commandRecordingFailed());
    EXPECT_TRUE(consumer->hasCommandBuffer());
}


TEST_F(BufferGpuReadinessTest, ForeignBufferIsRejectedAndOwnerHandoffRecovers){
    auto& device = BufferGpuReadinessTest::device();
    HeadlessGraphicsScope foreignScope;
    if(!foreignScope.initialize())
        GTEST_SKIP() << "Buffer GPU readiness: second validation-backed headless device is unavailable.";
    auto& foreignDevice = foreignScope.graphics().getDevice();

    BufferHandle foreignBuffer = foreignDevice.createBuffer(
        BufferDesc().setByteSize(256u).setInitialState(ResourceStates::Common)
    );
    ASSERT_TRUE(foreignBuffer);
    ASSERT_FALSE(device.isBufferReadyForGpuUse(foreignBuffer.get()));
    ASSERT_TRUE(foreignDevice.isBufferReadyForGpuUse(foreignBuffer.get()));

    CommandListHandle local = device.createCommandList();
    ASSERT_TRUE(local);
    local->open();
    local->beginTrackingBufferState(foreignBuffer.get(), ResourceStates::Common);
    EXPECT_TRUE(local->commandRecordingFailed());
    EXPECT_FALSE(local->hasExplicitBufferState(foreignBuffer.get()));
    local->close();
    EXPECT_FALSE(local->hasCommandBuffer());

    CommandListResourceStateHandoff foreignState(foreignScope.arena());
    CommandListHandle ownerProducer = foreignDevice.createCommandList();
    ASSERT_TRUE(ownerProducer);
    ownerProducer->open();
    ownerProducer->beginTrackingBufferState(foreignBuffer.get(), ResourceStates::Common);
    ownerProducer->close(&foreignState);
    ASSERT_TRUE(foreignState.valid());

    CommandListHandle ownerConsumer = foreignDevice.createCommandList();
    ASSERT_TRUE(ownerConsumer);
    ownerConsumer->open(&foreignState);
    ownerConsumer->close();
    EXPECT_FALSE(ownerConsumer->commandRecordingFailed());
    EXPECT_TRUE(ownerConsumer->hasCommandBuffer());
}


TEST_F(BufferGpuReadinessTest, ConflictingReleaseRejectsAndSameDestinationRetryIsIdempotent){
    auto& device = BufferGpuReadinessTest::device();
    if(!device.getQueue(CommandQueue::Transfer))
        GTEST_SKIP() << "Buffer GPU readiness: adapter exposes no Transfer queue.";

    const GpuPhysicalQueueId graphicsQueue = device.getPrimaryPhysicalQueue(CommandQueue::Graphics);
    const GpuPhysicalQueueId transferQueue = device.getPrimaryPhysicalQueue(CommandQueue::Transfer);
    if(graphicsQueue == transferQueue)
        GTEST_SKIP() << "Buffer GPU readiness: Graphics and Transfer use the same exact queue.";

    BufferHandle buffer = device.createBuffer(
        BufferDesc().setByteSize(256u).setInitialState(ResourceStates::Common)
    );
    ASSERT_TRUE(buffer);

    CommandListHandle conflicting = device.createCommandList();
    ASSERT_TRUE(conflicting);
    conflicting->open();
    conflicting->releaseBufferOwnership(buffer.get(), graphicsQueue);
    ASSERT_FALSE(conflicting->commandRecordingFailed());
    conflicting->releaseBufferOwnership(buffer.get(), transferQueue);
    EXPECT_TRUE(conflicting->commandRecordingFailed());
    conflicting->close();
    EXPECT_FALSE(conflicting->hasCommandBuffer());

    CommandListResourceStateHandoff recoveredState(BufferGpuReadinessTest::arena());
    CommandListHandle recovered = device.createCommandList();
    ASSERT_TRUE(recovered);
    recovered->open();
    recovered->releaseBufferOwnership(buffer.get(), transferQueue);
    recovered->releaseBufferOwnership(buffer.get(), transferQueue);
    recovered->close(&recoveredState);
    EXPECT_FALSE(recovered->commandRecordingFailed());
    EXPECT_TRUE(recovered->hasCommandBuffer());
    EXPECT_TRUE(recoveredState.valid());
}


TEST_F(BufferGpuReadinessTest, CloseAndHandoffDefensesRejectBackingMutationAtomically){
    auto& device = BufferGpuReadinessTest::device();
    BufferHandle first = device.createBuffer(
        BufferDesc().setByteSize(256u).setInitialState(ResourceStates::Common)
    );
    BufferHandle second = device.createBuffer(
        BufferDesc().setByteSize(256u).setInitialState(ResourceStates::Common)
    );
    ASSERT_TRUE(first);
    ASSERT_TRUE(second);

    CommandListHandle closeProbe = device.createCommandList();
    ASSERT_TRUE(closeProbe);
    closeProbe->open();
    closeProbe->beginTrackingBufferState(first.get(), ResourceStates::Common);
    BufferDesc& firstDesc = const_cast<BufferDesc&>(first->getDescription());
    firstDesc.isVirtual = true;
    closeProbe->close();
    firstDesc.isVirtual = false;
    EXPECT_TRUE(closeProbe->commandRecordingFailed());
    EXPECT_FALSE(closeProbe->hasCommandBuffer());

    closeProbe->open();
    closeProbe->beginTrackingBufferState(first.get(), ResourceStates::Common);
    closeProbe->close();
    ASSERT_FALSE(closeProbe->commandRecordingFailed());
    ASSERT_TRUE(closeProbe->hasCommandBuffer());

    CommandListResourceStateHandoff producerState(BufferGpuReadinessTest::arena());
    CommandListHandle producer = device.createCommandList();
    ASSERT_TRUE(producer);
    producer->open();
    producer->beginTrackingBufferState(first.get(), ResourceStates::Common);
    producer->beginTrackingBufferState(second.get(), ResourceStates::Common);
    producer->close(&producerState);
    ASSERT_TRUE(producerState.valid());

    CommandListHandle consumer = device.createCommandList();
    ASSERT_TRUE(consumer);
    const u32 firstReferences = first->getReferenceCount();
    const u32 secondReferences = second->getReferenceCount();
    BufferDesc& secondDesc = const_cast<BufferDesc&>(second->getDescription());
    secondDesc.isVirtual = true;
    consumer->open(&producerState);
    secondDesc.isVirtual = false;
    EXPECT_TRUE(consumer->commandRecordingFailed());
    EXPECT_FALSE(consumer->hasCommandBuffer());
    EXPECT_FALSE(consumer->hasExplicitBufferState(first.get()));
    EXPECT_FALSE(consumer->hasExplicitBufferState(second.get()));
    EXPECT_EQ(first->getReferenceCount(), firstReferences);
    EXPECT_EQ(second->getReferenceCount(), secondReferences);
    consumer->close();

    consumer->open(&producerState);
    consumer->close();
    EXPECT_FALSE(consumer->commandRecordingFailed());
    EXPECT_TRUE(consumer->hasCommandBuffer());
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

