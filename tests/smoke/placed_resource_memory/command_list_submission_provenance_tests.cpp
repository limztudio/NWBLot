// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


// Command-list submission provenance and upload-ledger lease coverage.


#include <gtest/gtest.h>

#include <global/global.h>
#include <global/unique_ptr.h>
#include <core/graphics/vulkan/backend.h>
#include <tests/common/capturing_logger.h>
#include <tests/common/headless_graphics_scope.h>
#include <tests/common/vulkan_test_sync.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace Tests{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


using namespace Core;


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace __hidden_command_list_submission_provenance_tests{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


struct SubmissionHookObserver{
    u32 invocationCount = 0u;
};


[[nodiscard]] static bool RejectObservedSubmissionHook(
    void* const rawContext,
    const GpuPhysicalQueueId& executionQueue,
    QueueSubmissionNativeSignal& outSignal
){
    SubmissionHookObserver* const observer = static_cast<SubmissionHookObserver*>(rawContext);
    if(!observer || !executionQueue.valid())
        return false;

    ++observer->invocationCount;
    outSignal = {};
    return false;
}

[[nodiscard]] static CommandListParameters ForgeExecutionQueue(
    CommandList& commandList,
    const GpuPhysicalQueueInfo& targetQueue
){
    CommandListParameters& publicDescription = const_cast<CommandListParameters&>(commandList.getDescription());
    const CommandListParameters savedDescription = publicDescription;
    publicDescription.physicalQueue = targetQueue.id;
    publicDescription.queueType = targetQueue.queueClass;
    return savedDescription;
}

[[nodiscard]] static const GpuPhysicalQueueInfo* FindDistinctTransferQueue(
    const GraphicsBackend::Device& device,
    const GpuPhysicalQueueId& excludedQueue
){
    const GpuPhysicalQueueTopology topology = device.getPhysicalQueueTopology();
    for(usize queueIndex = 0u; queueIndex < topology.queueCount; ++queueIndex){
        const GpuPhysicalQueueInfo& candidate = topology.queues[queueIndex];
        if(
            candidate.id != excludedQueue
            && (candidate.capabilities & GpuQueueCapability::Transfer) != GpuQueueCapability::None
        )
            return &candidate;
    }
    return nullptr;
}


struct SubmissionMutationHookContext{
    CommandList* commandList = nullptr;
    QueueSubmissionNativeSignal signal;
    CommandListParameters savedDescription;
    u32 invocationCount = 0u;
    bool descriptionMutated = false;
};

[[nodiscard]] static CommandListParameters ForgeRecordingWorker(CommandList& commandList){
    CommandListParameters& publicDescription = const_cast<CommandListParameters&>(commandList.getDescription());
    const CommandListParameters savedDescription = publicDescription;
    publicDescription.recordingWorkerDomain = 0x7c11d9a54183b6e2ull;
    publicDescription.recordingWorkerIndex = savedDescription.recordingWorkerIndex == 1u ? 2u : 1u;
    return savedDescription;
}

[[nodiscard]] static bool ForgeWorkerDuringSubmissionHook(
    void* const rawContext,
    const GpuPhysicalQueueId& executionQueue,
    QueueSubmissionNativeSignal& outSignal
){
    SubmissionMutationHookContext* const context = static_cast<SubmissionMutationHookContext*>(rawContext);
    if(
        !context
        || !context->commandList
        || !executionQueue.valid()
        || !context->signal.valid()
    )
        return false;

    context->savedDescription = ForgeRecordingWorker(*context->commandList);
    context->descriptionMutated = true;
    ++context->invocationCount;
    outSignal = context->signal;
    return true;
}

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


class CommandListSubmissionProvenanceTest : public ::testing::Test{
protected:
    static void SetUpTestSuite(){
        s_logger.emplace();
        s_loggerGuard.emplace(*s_logger);

        s_scope = MakeUnique<HeadlessGraphicsScope>();
        if(
            !s_scope->setTransferQueueEnabled(true)
            || !s_scope->setSameClassMultiQueueEnabled(true)
            || !s_scope->initialize()
        ){
            GTEST_SKIP() << "Command-list submission provenance: no validation-enabled headless Vulkan device.";
            return;
        }
        s_validationBackedDeviceInitialized = true;
    }

    static void TearDownTestSuite(){
        s_scope.reset();
        if(s_validationBackedDeviceInitialized && s_logger.has_value()){
            EXPECT_FALSE(s_logger->sawMessageContaining(NWB_TEXT("Vulkan debug: [severity=error")))
                << "command-list submission provenance tests emitted a Vulkan severity=error message";
        }
        s_loggerGuard.reset();
        s_logger.reset();
        s_validationBackedDeviceInitialized = false;
    }

    [[nodiscard]] static GraphicsBackend::Device& device(){ return s_scope->graphics().getDevice(); }


protected:
    static bool s_validationBackedDeviceInitialized;
    static UniquePtr<HeadlessGraphicsScope> s_scope;
    static Optional<CapturingLogger> s_logger;
    static Optional<Common::LoggerRegistrationGuard> s_loggerGuard;
};

bool CommandListSubmissionProvenanceTest::s_validationBackedDeviceInitialized = false;
UniquePtr<HeadlessGraphicsScope> CommandListSubmissionProvenanceTest::s_scope;
Optional<CapturingLogger> CommandListSubmissionProvenanceTest::s_logger;
Optional<Common::LoggerRegistrationGuard> CommandListSubmissionProvenanceTest::s_loggerGuard;


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


TEST_F(CommandListSubmissionProvenanceTest, SubmissionHookWorkerForgeryIsRejectedBeforeNativeOwnerDetach){
    using namespace __hidden_command_list_submission_provenance_tests;

    const GpuPhysicalQueueId ownerQueue = device().getPrimaryPhysicalQueue(CommandQueue::Graphics);
    ASSERT_TRUE(ownerQueue.valid());
    ASSERT_TRUE(device().waitForIdle());
    const u64 completedBeforeSubmission = device().queueGetCompletedInstance(ownerQueue);
    CommandListParameters parameters;
    parameters.setPhysicalQueue(ownerQueue);
    CommandListHandle commandList = device().createCommandList(parameters);
    ASSERT_TRUE(commandList);
    commandList->open();
    commandList->close();
    ASSERT_TRUE(commandList->hasCommandBuffer());
    const u64 recordingLease = commandList->recordingLeaseSerial();

    VulkanTestBinarySemaphore signal(device());
    ASSERT_TRUE(signal.valid());
    SubmissionMutationHookContext hookContext{
        .commandList = commandList.get(),
        .signal = signal.nativeSignal(),
        .savedDescription = {},
        .invocationCount = 0u,
        .descriptionMutated = false,
    };
    const QueueSubmissionDesc submitDescription{
        .preSubmitHook = QueueSubmissionPreSubmitHook{
            .context = &hookContext,
            .invoke = ForgeWorkerDuringSubmissionHook,
        },
    };
    CommandList* const commandLists[]{ commandList.get() };
    const QueueSubmissionToken rejectedToken = device().executeCommandLists(
        commandLists,
        LengthOf(commandLists),
        ownerQueue,
        submitDescription
    );
    const bool unexpectedlySubmitted = rejectedToken.valid();
    const bool idleAfterUnexpectedSubmission = !unexpectedlySubmitted || device().waitForIdle();
    if(hookContext.descriptionMutated)
        const_cast<CommandListParameters&>(commandList->getDescription()) = hookContext.savedDescription;
    ASSERT_FALSE(unexpectedlySubmitted);
    ASSERT_TRUE(idleAfterUnexpectedSubmission);
    EXPECT_EQ(hookContext.invocationCount, 1u);
    EXPECT_TRUE(hookContext.descriptionMutated);
    EXPECT_TRUE(commandList->hasCommandBuffer());
    EXPECT_EQ(commandList->recordingLeaseSerial(), recordingLease);
    EXPECT_EQ(device().queueGetCompletedInstance(ownerQueue), completedBeforeSubmission);

    ASSERT_TRUE(device().executeCommandLists(
        commandLists,
        LengthOf(commandLists),
        ownerQueue,
        QueueSubmissionDesc{}
    ).valid());
    EXPECT_TRUE(device().waitForIdle());
}


TEST_F(CommandListSubmissionProvenanceTest, UploadLedgerDiscardRequiresExactNativeRecordingIdentity){
    constexpr u64 s_WorkerDomain = 0x9a31b7c542de680full;
    constexpr u32 s_WorkerIndex = 17u;
    constexpr u64 s_OldRecordingID = 0x12345u;
    constexpr u64 s_NewRecordingID = 0x6789au;
    constexpr u64 s_OverwriteRecordingID = 0xbcdefu;
    constexpr u32 s_OldValue = 0x1bd238a4u;
    constexpr u32 s_NewValue = 0x67ce941fu;
    constexpr usize s_ChunkByteCount = GraphicsBackend::s_DefaultUploadSuballocationAlignment + sizeof(u32);
    const GpuPhysicalQueueId queue = device().getPrimaryPhysicalQueue(CommandQueue::Graphics);
    ASSERT_TRUE(queue.valid());
    ASSERT_TRUE(device().waitForIdle());
    GraphicsBackend::Queue* const physicalQueue = device().getQueue(queue);
    ASSERT_NE(physicalQueue, nullptr);
    GraphicsBackend::TrackedCommandBufferPtr owner = physicalQueue->getOrCreateCommandBuffer(
        s_WorkerDomain,
        s_WorkerIndex
    );
    ASSERT_TRUE(owner);
    const u64 completedVersion = device().queueGetCompletedInstance(queue);
    GraphicsBackend::UploadManager uploadManager(device(), s_ChunkByteCount, 0u, false);

    Buffer* oldBuffer = nullptr;
    u64 oldOffset = 0u;
    void* oldCpuVA = nullptr;
    ASSERT_TRUE(uploadManager.suballocateBuffer(
        sizeof(s_OldValue),
        &oldBuffer,
        &oldOffset,
        &oldCpuVA,
        owner.get(),
        s_OldRecordingID,
        queue,
        completedVersion
    ));
    ASSERT_NE(oldBuffer, nullptr);
    ASSERT_NE(oldCpuVA, nullptr);
    EXPECT_EQ(oldOffset, 0u);
    NWB_MEMCPY(oldCpuVA, sizeof(s_OldValue), &s_OldValue, sizeof(s_OldValue));

    Buffer* newBuffer = nullptr;
    u64 newOffset = 0u;
    void* newCpuVA = nullptr;
    ASSERT_TRUE(uploadManager.suballocateBuffer(
        sizeof(s_NewValue),
        &newBuffer,
        &newOffset,
        &newCpuVA,
        owner.get(),
        s_NewRecordingID,
        queue,
        completedVersion
    ));
    ASSERT_NE(newBuffer, nullptr);
    ASSERT_NE(newCpuVA, nullptr);
    EXPECT_NE(newBuffer, oldBuffer);
    EXPECT_EQ(newOffset, 0u);
    NWB_MEMCPY(newCpuVA, sizeof(s_NewValue), &s_NewValue, sizeof(s_NewValue));

    uploadManager.discardChunks(queue, owner.get(), s_OldRecordingID, completedVersion);

    Buffer* firstOverwriteBuffer = nullptr;
    u64 firstOverwriteOffset = 0u;
    void* firstOverwriteCpuVA = nullptr;
    ASSERT_TRUE(uploadManager.suballocateBuffer(
        s_ChunkByteCount,
        &firstOverwriteBuffer,
        &firstOverwriteOffset,
        &firstOverwriteCpuVA,
        owner.get(),
        s_OverwriteRecordingID,
        queue,
        completedVersion
    ));
    ASSERT_NE(firstOverwriteCpuVA, nullptr);
    EXPECT_EQ(firstOverwriteBuffer, oldBuffer);
    EXPECT_EQ(firstOverwriteOffset, 0u);
    NWB_MEMSET(firstOverwriteCpuVA, 0xa5, s_ChunkByteCount);

    Buffer* secondOverwriteBuffer = nullptr;
    u64 secondOverwriteOffset = 0u;
    void* secondOverwriteCpuVA = nullptr;
    ASSERT_TRUE(uploadManager.suballocateBuffer(
        s_ChunkByteCount,
        &secondOverwriteBuffer,
        &secondOverwriteOffset,
        &secondOverwriteCpuVA,
        owner.get(),
        s_OverwriteRecordingID,
        queue,
        completedVersion
    ));
    ASSERT_NE(secondOverwriteCpuVA, nullptr);
    EXPECT_NE(secondOverwriteBuffer, oldBuffer);
    EXPECT_NE(secondOverwriteBuffer, newBuffer);
    EXPECT_EQ(secondOverwriteOffset, 0u);
    NWB_MEMSET(secondOverwriteCpuVA, 0x5a, s_ChunkByteCount);

    Buffer* continuedNewBuffer = nullptr;
    u64 continuedNewOffset = 0u;
    void* continuedNewCpuVA = nullptr;
    ASSERT_TRUE(uploadManager.suballocateBuffer(
        sizeof(s_NewValue),
        &continuedNewBuffer,
        &continuedNewOffset,
        &continuedNewCpuVA,
        owner.get(),
        s_NewRecordingID,
        queue,
        completedVersion
    ));
    ASSERT_NE(continuedNewCpuVA, nullptr);
    EXPECT_EQ(continuedNewBuffer, newBuffer);
    EXPECT_EQ(continuedNewOffset, GraphicsBackend::s_DefaultUploadSuballocationAlignment);

    BufferHandle readback = device().createBuffer(
        BufferDesc()
            .setByteSize(sizeof(s_NewValue))
            .setInitialState(ResourceStates::CopyDest)
            .setCpuAccess(CpuAccessMode::Read)
    );
    ASSERT_TRUE(readback);
    CommandListParameters copyParameters;
    copyParameters.setPhysicalQueue(queue);
    CommandListHandle copyCommandList = device().createCommandList(copyParameters);
    ASSERT_TRUE(copyCommandList);
    copyCommandList->open();
    copyCommandList->copyBuffer(readback.get(), 0u, newBuffer, newOffset, sizeof(s_NewValue));
    copyCommandList->close();
    ASSERT_FALSE(copyCommandList->commandRecordingFailed());
    ASSERT_TRUE(copyCommandList->hasCommandBuffer());
    CommandList* const copyCommandLists[]{ copyCommandList.get() };
    ASSERT_TRUE(device().executeCommandLists(
        copyCommandLists,
        LengthOf(copyCommandLists),
        queue,
        QueueSubmissionDesc{}
    ).valid());
    ASSERT_TRUE(device().waitForIdle());
    const u32* const readbackValue = static_cast<const u32*>(device().mapBuffer(readback.get(), CpuAccessMode::Read));
    ASSERT_NE(readbackValue, nullptr);
    EXPECT_EQ(*readbackValue, s_NewValue);
    device().unmapBuffer(readback.get());
}


TEST_F(CommandListSubmissionProvenanceTest, InjectedNativeFailureRecyclesWorkerOwnerForFollowingUpload){
    constexpr u64 s_WorkerDomain = 0x86d731c542af901eull;
    constexpr u32 s_WorkerIndex = 7u;
    constexpr u32 s_OldValue = 0x1bd238a4u;
    constexpr u32 s_ReusedValue = 0x67ce941fu;
    const GpuPhysicalQueueId queue = device().getPrimaryPhysicalQueue(CommandQueue::Graphics);
    ASSERT_TRUE(queue.valid());
    ASSERT_TRUE(device().waitForIdle());
    const VkQueue nativeQueue = static_cast<VkQueue>(
        device().getNativeQueue(GraphicsBackend::ObjectTypes::VK_Queue, queue).pointer
    );
    ASSERT_NE(nativeQueue, VK_NULL_HANDLE);
    VulkanTestQueueSubmit2Observer submissionObserver;
    ASSERT_TRUE(submissionObserver.valid());

    BufferHandle oldDestination = device().createBuffer(
        BufferDesc().setByteSize(sizeof(s_OldValue)).setInitialState(ResourceStates::Common)
    );
    BufferHandle reusedDestination = device().createBuffer(
        BufferDesc()
            .setByteSize(sizeof(s_ReusedValue))
            .setInitialState(ResourceStates::Common)
            .setCpuAccess(CpuAccessMode::Read)
    );
    ASSERT_TRUE(oldDestination);
    ASSERT_TRUE(reusedDestination);

    CommandListParameters workerParameters;
    workerParameters.setPhysicalQueue(queue).setRecordingWorker(s_WorkerDomain, s_WorkerIndex);
    CommandListHandle oldLease = device().createCommandList(workerParameters);
    CommandListHandle reusedLease = device().createCommandList(workerParameters);
    ASSERT_TRUE(oldLease);
    ASSERT_TRUE(reusedLease);

    oldLease->open();
    ASSERT_TRUE(oldLease->tryWriteBuffer(oldDestination.get(), &s_OldValue, sizeof(s_OldValue)));
    oldLease->close();
    ASSERT_TRUE(oldLease->hasCommandBuffer());
    const GpuCommandArenaWorkerStatistics workerStatsBefore = device().getCommandArenaWorkerStatistics(
        queue,
        s_WorkerDomain,
        s_WorkerIndex
    );
    ASSERT_TRUE(workerStatsBefore.valid());

    ASSERT_TRUE(submissionObserver.armSubmissionFailures(nativeQueue));
    CommandList* const rejectedLists[]{ oldLease.get() };
    const QueueSubmissionToken rejectedToken = device().executeCommandLists(
        rejectedLists,
        LengthOf(rejectedLists),
        queue,
        QueueSubmissionDesc{}
    );
    const GpuCommandArenaWorkerStatistics workerStatsAfterRejection = device().getCommandArenaWorkerStatistics(
        queue,
        s_WorkerDomain,
        s_WorkerIndex
    );
    ASSERT_FALSE(rejectedToken.valid());
    EXPECT_EQ(submissionObserver.injectedSubmissionFailureCount(), 1u);
    EXPECT_EQ(submissionObserver.pendingSubmissionFailureCount(), 0u);
    EXPECT_FALSE(submissionObserver.overflowed());
    ASSERT_TRUE(workerStatsAfterRejection.valid());
    EXPECT_EQ(workerStatsAfterRejection.growthEventCount, workerStatsBefore.growthEventCount);
    EXPECT_EQ(workerStatsAfterRejection.resetEventCount, workerStatsBefore.resetEventCount);
    EXPECT_EQ(workerStatsAfterRejection.leasedCommandBufferCount + 1u, workerStatsBefore.leasedCommandBufferCount);
    EXPECT_EQ(workerStatsAfterRejection.reusableCommandBufferCount, workerStatsBefore.reusableCommandBufferCount + 1u);
    EXPECT_FALSE(oldLease->hasCommandBuffer());

    reusedLease->open();
    ASSERT_TRUE(reusedLease->isRecording());
    ASSERT_TRUE(reusedLease->tryWriteBuffer(reusedDestination.get(), &s_ReusedValue, sizeof(s_ReusedValue)));
    reusedLease->close();
    ASSERT_TRUE(reusedLease->hasCommandBuffer());
    ASSERT_FALSE(reusedLease->commandRecordingFailed());
    const GpuCommandArenaWorkerStatistics workerStatsAfterReuse = device().getCommandArenaWorkerStatistics(
        queue,
        s_WorkerDomain,
        s_WorkerIndex
    );
    ASSERT_TRUE(workerStatsAfterReuse.valid());
    EXPECT_EQ(workerStatsAfterReuse.growthEventCount, workerStatsBefore.growthEventCount);
    EXPECT_EQ(workerStatsAfterReuse.resetEventCount, workerStatsBefore.resetEventCount + 1u);
    EXPECT_EQ(workerStatsAfterReuse.leasedCommandBufferCount, workerStatsBefore.leasedCommandBufferCount);
    EXPECT_EQ(workerStatsAfterReuse.reusableCommandBufferCount, workerStatsBefore.reusableCommandBufferCount);

    CommandList* const reusedLists[]{ reusedLease.get() };
    ASSERT_TRUE(device().executeCommandLists(
        reusedLists,
        LengthOf(reusedLists),
        queue,
        QueueSubmissionDesc{}
    ).valid());
    ASSERT_TRUE(device().waitForIdle());
    const u32* const readback = static_cast<const u32*>(
        device().mapBuffer(reusedDestination.get(), CpuAccessMode::Read)
    );
    ASSERT_NE(readback, nullptr);
    EXPECT_EQ(*readback, s_ReusedValue);
    device().unmapBuffer(reusedDestination.get());
}


TEST_F(CommandListSubmissionProvenanceTest, ClosedQueueForgeryRejectsBeforeHookAndPreservesLeaseForOwnerRetry){
    using namespace __hidden_command_list_submission_provenance_tests;

    const GpuPhysicalQueueId ownerQueue = device().getPrimaryPhysicalQueue(CommandQueue::Graphics);
    const GpuPhysicalQueueInfo* const otherQueue = FindDistinctTransferQueue(device(), ownerQueue);
    if(!otherQueue)
        GTEST_SKIP() << "Command-list submission provenance: no distinct transfer-capable exact queue.";

    CommandListParameters parameters;
    parameters.setPhysicalQueue(ownerQueue);
    CommandListHandle commandList = device().createCommandList(parameters);
    ASSERT_TRUE(commandList);
    commandList->open();
    ASSERT_TRUE(commandList->isRecording());
    commandList->close();
    ASSERT_TRUE(commandList->hasCommandBuffer());
    const u64 recordingLease = commandList->recordingLeaseSerial();

    const CommandListParameters savedDescription = ForgeExecutionQueue(*commandList, *otherQueue);
    EXPECT_FALSE(commandList->hasCommandBuffer());
    SubmissionHookObserver hookObserver;
    const QueueSubmissionDesc submitDescription{
        .preSubmitHook = QueueSubmissionPreSubmitHook{
            .context = &hookObserver,
            .invoke = RejectObservedSubmissionHook,
        },
    };
    CommandList* const commandLists[]{ commandList.get() };
    EXPECT_FALSE(device().executeCommandLists(
        commandLists,
        LengthOf(commandLists),
        otherQueue->id,
        submitDescription
    ).valid());
    EXPECT_EQ(hookObserver.invocationCount, 0u);
    EXPECT_EQ(commandList->recordingLeaseSerial(), recordingLease);

    const_cast<CommandListParameters&>(commandList->getDescription()) = savedDescription;
    ASSERT_TRUE(commandList->hasCommandBuffer());
    ASSERT_TRUE(device().executeCommandLists(
        commandLists,
        LengthOf(commandLists),
        ownerQueue,
        QueueSubmissionDesc{}
    ).valid());
    EXPECT_TRUE(device().waitForIdle());
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

