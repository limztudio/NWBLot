// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


// Command-list submission provenance, native recording-ID wrap, and upload-ledger lease coverage.


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

#if !defined(NWB_FINAL)
struct LedgerReuseHookContext{
    CommandList* commandList = nullptr;
    Buffer* destination = nullptr;
    u32 value = 0u;
    bool invoked = false;
    bool opened = false;
    bool staged = false;
    bool closed = false;
};


static void RecordReusedLeaseUpload(void* const rawContext){
    LedgerReuseHookContext* const context = static_cast<LedgerReuseHookContext*>(rawContext);
    if(!context || !context->commandList || !context->destination)
        return;

    context->invoked = true;
    context->commandList->open();
    context->opened = context->commandList->isRecording();
    if(!context->opened)
        return;

    context->staged = context->commandList->tryWriteBuffer(
        context->destination,
        &context->value,
        sizeof(context->value)
    );
    context->commandList->close();
    context->closed = context->commandList->hasCommandBuffer()
        && !context->commandList->commandRecordingFailed()
    ;
}
#endif


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
#if !defined(NWB_FINAL)
        if(s_validationBackedDeviceInitialized)
            s_scope->graphics().getDevice().clearSubmissionLedgerFinalizeHookForTesting();
#endif
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


#if !defined(NWB_FINAL)
TEST_F(CommandListSubmissionProvenanceTest, InjectedNativeFailureOnOldLeaseCannotDiscardRecycledLeaseUploadChunks){
    using namespace __hidden_command_list_submission_provenance_tests;

    constexpr u64 s_WorkerDomain = 0x86d731c542af901eull;
    constexpr u32 s_ReusedWorkerIndex = 7u;
    constexpr u32 s_OverwriteWorkerIndex = 8u;
    constexpr u32 s_OldValue = 0x1bd238a4u;
    constexpr u32 s_ReusedValue = 0x67ce941fu;
    constexpr usize s_OverwriteByteCount = GraphicsBackend::s_DefaultUploadSuballocationAlignment + sizeof(u32);
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
        BufferDesc().setByteSize(sizeof(u32)).setInitialState(ResourceStates::Common)
    );
    BufferHandle reusedDestination = device().createBuffer(
        BufferDesc()
            .setByteSize(sizeof(u32))
            .setInitialState(ResourceStates::Common)
            .setCpuAccess(CpuAccessMode::Read)
    );
    BufferHandle overwriteDestination = device().createBuffer(
        BufferDesc().setByteSize(s_OverwriteByteCount).setInitialState(ResourceStates::Common)
    );
    ASSERT_TRUE(oldDestination);
    ASSERT_TRUE(reusedDestination);
    ASSERT_TRUE(overwriteDestination);

    CommandListParameters reusedWorkerParameters;
    reusedWorkerParameters.setPhysicalQueue(queue).setRecordingWorker(s_WorkerDomain, s_ReusedWorkerIndex);
    CommandListParameters overwriteWorkerParameters;
    overwriteWorkerParameters.setPhysicalQueue(queue).setRecordingWorker(s_WorkerDomain, s_OverwriteWorkerIndex);
    CommandListHandle oldLease = device().createCommandList(reusedWorkerParameters);
    CommandListHandle reusedLease = device().createCommandList(reusedWorkerParameters);
    CommandListHandle overwriteLease = device().createCommandList(overwriteWorkerParameters);
    ASSERT_TRUE(oldLease);
    ASSERT_TRUE(reusedLease);
    ASSERT_TRUE(overwriteLease);

    oldLease->open();
    ASSERT_TRUE(oldLease->tryWriteBuffer(oldDestination.get(), &s_OldValue, sizeof(s_OldValue)));
    oldLease->close();
    ASSERT_TRUE(oldLease->hasCommandBuffer());
    const GpuCommandArenaWorkerStatistics workerStatsBefore = device().getCommandArenaWorkerStatistics(
        queue,
        s_WorkerDomain,
        s_ReusedWorkerIndex
    );
    ASSERT_TRUE(workerStatsBefore.valid());

    LedgerReuseHookContext hookContext{
        .commandList = reusedLease.get(),
        .destination = reusedDestination.get(),
        .value = s_ReusedValue,
    };
    ASSERT_TRUE(device().armSubmissionLedgerFinalizeHookForTesting(&hookContext, RecordReusedLeaseUpload));
    ASSERT_TRUE(submissionObserver.armSubmissionFailures(nativeQueue));
    CommandList* const rejectedLists[]{ oldLease.get() };
    const QueueSubmissionToken rejectedToken = device().executeCommandLists(
        rejectedLists,
        LengthOf(rejectedLists),
        queue,
        QueueSubmissionDesc{}
    );
    device().clearSubmissionLedgerFinalizeHookForTesting();
    const GpuCommandArenaWorkerStatistics workerStatsAfter = device().getCommandArenaWorkerStatistics(
        queue,
        s_WorkerDomain,
        s_ReusedWorkerIndex
    );
    ASSERT_FALSE(rejectedToken.valid());
    EXPECT_EQ(submissionObserver.injectedSubmissionFailureCount(), 1u);
    EXPECT_EQ(submissionObserver.pendingSubmissionFailureCount(), 0u);
    ASSERT_TRUE(workerStatsAfter.valid());
    ASSERT_TRUE(hookContext.invoked);
    ASSERT_TRUE(hookContext.opened);
    ASSERT_TRUE(hookContext.staged);
    ASSERT_TRUE(hookContext.closed);
    EXPECT_EQ(workerStatsAfter.growthEventCount, workerStatsBefore.growthEventCount);
    EXPECT_EQ(workerStatsAfter.resetEventCount, workerStatsBefore.resetEventCount + 1u);
    EXPECT_FALSE(oldLease->hasCommandBuffer());

    u8 overwriteBytes[s_OverwriteByteCount];
    NWB_MEMSET(overwriteBytes, 0xa5, sizeof(overwriteBytes));
    overwriteLease->open();
    ASSERT_TRUE(overwriteLease->tryWriteBuffer(
        overwriteDestination.get(),
        overwriteBytes,
        sizeof(overwriteBytes)
    ));
    overwriteLease->close();
    ASSERT_TRUE(overwriteLease->hasCommandBuffer());

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


TEST_F(CommandListSubmissionProvenanceTest, NativeRecordingIDWrapSkipsReservedZeroLease){
    const GpuPhysicalQueueId queue = device().getPrimaryPhysicalQueue(CommandQueue::Graphics);
    ASSERT_TRUE(queue.valid());
    ASSERT_TRUE(device().waitForIdle());
    ASSERT_TRUE(device().armRecordingIDWrapForTesting(queue));

    CommandListParameters parameters;
    parameters.setPhysicalQueue(queue).setRecordingWorker(0x4973cbe8126adf05ull, 9u);
    CommandListHandle maximumIDLease = device().createCommandList(parameters);
    CommandListHandle wrappedIDLease = device().createCommandList(parameters);
    ASSERT_TRUE(maximumIDLease);
    ASSERT_TRUE(wrappedIDLease);
    maximumIDLease->open();
    wrappedIDLease->open();
    ASSERT_TRUE(maximumIDLease->isRecording());
    ASSERT_TRUE(wrappedIDLease->isRecording());
    maximumIDLease->close();
    wrappedIDLease->close();
    ASSERT_TRUE(maximumIDLease->hasCommandBuffer());
    ASSERT_TRUE(wrappedIDLease->hasCommandBuffer());

    CommandList* const commandLists[]{ maximumIDLease.get(), wrappedIDLease.get() };
    ASSERT_TRUE(device().executeCommandLists(
        commandLists,
        LengthOf(commandLists),
        queue,
        QueueSubmissionDesc{}
    ).valid());
    EXPECT_TRUE(device().waitForIdle());
}
#endif


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

