// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "round_trip_fixture.h"


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace Tests{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace DescriptorBufferRoundTripDetail{};
namespace __hidden_descriptor_buffer_round_trip_tests = DescriptorBufferRoundTripDetail;


namespace DescriptorBufferRoundTripDetail{

// Holds executeCommandLists inside the resolved callback after native acceptance. The caller can then observe state
// that must be published before any externally owned acceptance callback runs.
class BlockingResolvedSubmissionSignal final : NoCopy{
private:
    [[nodiscard]] static bool prepare(
        void* const context,
        const u64 identity,
        const GpuPhysicalQueueId& executionQueue,
        QueueSubmissionNativeSignal& outSignal
    ){
        BlockingResolvedSubmissionSignal* const signal = static_cast<BlockingResolvedSubmissionSignal*>(context);
        if(
            !signal
            || !signal->valid()
            || identity != s_Identity
            || executionQueue != signal->m_expectedQueue
            || signal->m_invocationCount != 0u
        )
            return false;

        outSignal = signal->m_semaphore.nativeSignal();
        ++signal->m_invocationCount;
        return true;
    }
    [[nodiscard]] static bool resolved(
        void* const context,
        const u64 identity,
        const QueueSubmissionToken& submissionToken
    )noexcept{
        BlockingResolvedSubmissionSignal* const signal = static_cast<BlockingResolvedSubmissionSignal*>(context);
        if(!signal || identity != s_Identity)
            return false;

        signal->m_resolvedToken = submissionToken;
        signal->m_resolutionEntered.test_and_set(MemoryOrder::release);
        signal->m_resolutionEntered.notify_all();
        while(!signal->m_releaseResolution.test(MemoryOrder::acquire))
            signal->m_releaseResolution.wait(false, MemoryOrder::acquire);
        return true;
    }


public:
    BlockingResolvedSubmissionSignal(
        GraphicsBackend::Device& device,
        const GpuPhysicalQueueId& expectedQueue
    )
        : m_semaphore(device)
        , m_expectedQueue(expectedQueue)
    {}


public:
    [[nodiscard]] bool valid()const noexcept{ return m_expectedQueue.valid() && m_semaphore.valid(); }
    [[nodiscard]] QueueSubmissionPreSubmitHook hook()noexcept{
        return QueueSubmissionPreSubmitHook{
            .context = this,
            .identity = s_Identity,
            .invoke = &BlockingResolvedSubmissionSignal::prepare,
            .resolved = &BlockingResolvedSubmissionSignal::resolved,
        };
    }
    [[nodiscard]] bool resolutionEntered()const noexcept{ return m_resolutionEntered.test(MemoryOrder::acquire); }
    void releaseResolution()noexcept{
        m_releaseResolution.test_and_set(MemoryOrder::release);
        m_releaseResolution.notify_all();
    }
    [[nodiscard]] const QueueSubmissionToken& resolvedToken()const noexcept{ return m_resolvedToken; }
    [[nodiscard]] u32 invocationCount()const noexcept{ return m_invocationCount; }


private:
    static constexpr u64 s_Identity = 1u;

    VulkanTestBinarySemaphore m_semaphore;
    GpuPhysicalQueueId m_expectedQueue;
    AtomicFlag m_resolutionEntered;
    AtomicFlag m_releaseResolution;
    QueueSubmissionToken m_resolvedToken;
    u32 m_invocationCount = 0u;
};

};


namespace DescriptorBufferRoundTripDetail{

struct NativeBufferAddressQuery{
    VkBuffer buffer = VK_NULL_HANDLE;
    VkDeviceAddress address = 0u;
};

};


namespace DescriptorBufferRoundTripDetail{

struct NativeUploadCopyCommand{
    VkBuffer source = VK_NULL_HANDLE;
    VkDeviceSize sourceOffset = 0u;
};

};


namespace DescriptorBufferRoundTripDetail{

struct NativeUploadReuseCapture{
    NativeBufferAddressQuery addressQueries[2u] = {};
    NativeUploadCopyCommand copyCommands[2u] = {};
    u32 addressQueryCount = 0u;
    u32 copyCommandCount = 0u;
};

};


namespace DescriptorBufferRoundTripDetail{

class ScopedNativeUploadReuseTrace final : NoCopy{
private:
    static thread_local NativeUploadReuseCapture* s_activeCapture;
    static PFN_vkGetBufferDeviceAddress s_forwardGetBufferDeviceAddress;
    static PFN_vkCmdCopyBuffer s_forwardCmdCopyBuffer;

    [[nodiscard]] static VKAPI_ATTR VkDeviceAddress VKAPI_CALL InterceptGetBufferDeviceAddress(
        const VkDevice device,
        const VkBufferDeviceAddressInfo* const addressInfo
    ){
        if(!s_forwardGetBufferDeviceAddress)
            return 0u;

        const VkDeviceAddress address = s_forwardGetBufferDeviceAddress(device, addressInfo);
        NativeUploadReuseCapture* const capture = s_activeCapture;
        if(capture && addressInfo){
            const u32 queryIndex = capture->addressQueryCount;
            if(queryIndex < LengthOf(capture->addressQueries)){
                capture->addressQueries[queryIndex].buffer = addressInfo->buffer;
                capture->addressQueries[queryIndex].address = address;
            }
            ++capture->addressQueryCount;
        }
        return address;
    }
    static VKAPI_ATTR void VKAPI_CALL InterceptCmdCopyBuffer(
        const VkCommandBuffer commandBuffer,
        const VkBuffer source,
        const VkBuffer destination,
        const u32 regionCount,
        const VkBufferCopy* const regions
    ){
        NativeUploadReuseCapture* const capture = s_activeCapture;
        if(capture && regionCount > 0u && regions){
            const u32 commandIndex = capture->copyCommandCount;
            if(commandIndex < LengthOf(capture->copyCommands)){
                capture->copyCommands[commandIndex].source = source;
                capture->copyCommands[commandIndex].sourceOffset = regions[0u].srcOffset;
            }
            ++capture->copyCommandCount;
        }
        if(s_forwardCmdCopyBuffer)
            s_forwardCmdCopyBuffer(commandBuffer, source, destination, regionCount, regions);
    }


public:
    ScopedNativeUploadReuseTrace(GraphicsBackend::Device& device, NativeUploadReuseCapture& capture)
        : m_getBufferDeviceAddressOverride(device, &VolkDeviceTable::vkGetBufferDeviceAddress)
        , m_cmdCopyBufferOverride(device, &VolkDeviceTable::vkCmdCopyBuffer)
    {
        capture = {};
        if(s_activeCapture || !m_getBufferDeviceAddressOverride.valid() || !m_cmdCopyBufferOverride.valid())
            return;

        s_forwardGetBufferDeviceAddress = m_getBufferDeviceAddressOverride.original();
        s_forwardCmdCopyBuffer = m_cmdCopyBufferOverride.original();
        s_activeCapture = &capture;
        if(
            !m_getBufferDeviceAddressOverride.replace(&ScopedNativeUploadReuseTrace::InterceptGetBufferDeviceAddress)
            || !m_cmdCopyBufferOverride.replace(&ScopedNativeUploadReuseTrace::InterceptCmdCopyBuffer)
        ){
            s_activeCapture = nullptr;
            return;
        }
        m_armed = true;
    }
    ~ScopedNativeUploadReuseTrace(){
        if(!m_armed)
            return;

        s_activeCapture = nullptr;
    }


public:
    [[nodiscard]] bool valid()const noexcept{ return m_armed; }


private:
    ScopedVulkanDeviceDispatchOverride<PFN_vkGetBufferDeviceAddress> m_getBufferDeviceAddressOverride;
    ScopedVulkanDeviceDispatchOverride<PFN_vkCmdCopyBuffer> m_cmdCopyBufferOverride;
    bool m_armed = false;
};

};


namespace DescriptorBufferRoundTripDetail{

thread_local NativeUploadReuseCapture* ScopedNativeUploadReuseTrace::s_activeCapture = nullptr;

};


namespace DescriptorBufferRoundTripDetail{

PFN_vkGetBufferDeviceAddress ScopedNativeUploadReuseTrace::s_forwardGetBufferDeviceAddress = nullptr;

};


namespace DescriptorBufferRoundTripDetail{

PFN_vkCmdCopyBuffer ScopedNativeUploadReuseTrace::s_forwardCmdCopyBuffer = nullptr;

};


// Native rejection must leave destination bytes untouched and return the exact upload suballocation for retry. The
// test observes only Vulkan calls at its own boundary; production exposes no allocator identity, counter, or friend.
TEST_F(DescriptorBufferRoundTripTest, RejectedNativeSubmissionReusesUploadSuballocationAtNativeBoundary){
    HeadlessGraphicsScope uploadScope;
    ASSERT_TRUE(uploadScope.initialize());

    GraphicsBackend::Device& uploadDevice = uploadScope.graphics().getDevice();
    static constexpr u32 s_InitialWord = 0x10203040u;
    static constexpr u32 s_RejectedWords[] = { 0xBAD00001u, 0xBAD00002u, 0xBAD00003u, 0xBAD00004u };
    static constexpr u32 s_RetryWords[] = { 0x13579BDFu, 0x2468ACE0u, 0xC001C0DEu, 0x600DF00Du };
    const BufferHandle destination = uploadDevice.createBuffer(
        BufferDesc()
            .setByteSize(sizeof(s_RetryWords))
            .setCanHaveRawViews(true)
            .setInitialState(ResourceStates::Common)
            .setKeepInitialState(true)
    );
    const BufferHandle readback = uploadDevice.createBuffer(
        BufferDesc()
            .setByteSize(sizeof(s_RetryWords))
            .setInitialState(ResourceStates::Common)
            .setKeepInitialState(true)
            .setCpuAccess(CpuAccessMode::Read)
    );
    ASSERT_TRUE(destination);
    ASSERT_TRUE(readback);

    CommandListHandle initialClear = uploadDevice.createCommandList();
    ASSERT_TRUE(initialClear);
    initialClear->open();
    initialClear->clearBufferUInt(destination.get(), s_InitialWord);
    ASSERT_FALSE(initialClear->commandRecordingFailed());
    initialClear->copyBuffer(readback.get(), 0u, destination.get(), 0u, sizeof(s_RetryWords));
    ASSERT_FALSE(initialClear->commandRecordingFailed());
    initialClear->close();
    CommandList* const initialClears[]{ initialClear.get() };
    const QueueSubmissionToken initialToken = uploadDevice.executeCommandLists(
        initialClears,
        LengthOf(initialClears),
        CommandQueue::Graphics,
        QueueSubmissionDesc{}
    );
    ASSERT_TRUE(initialToken.valid());
    ASSERT_TRUE(uploadDevice.waitForIdle());

    const u32* const initialReadback = static_cast<const u32*>(uploadDevice.mapBuffer(readback.get(), CpuAccessMode::Read));
    ASSERT_NE(initialReadback, nullptr);
    for(usize wordIndex = 0u; wordIndex < LengthOf(s_RetryWords); ++wordIndex)
        EXPECT_EQ(initialReadback[wordIndex], s_InitialWord);
    uploadDevice.unmapBuffer(readback.get());

    __hidden_descriptor_buffer_round_trip_tests::NativeUploadReuseCapture rejectedNativeCapture;
    CommandListHandle rejectedUpload = uploadDevice.createCommandList();
    ASSERT_TRUE(rejectedUpload);
    {
        __hidden_descriptor_buffer_round_trip_tests::ScopedNativeUploadReuseTrace nativeTrace(uploadDevice, rejectedNativeCapture);
        ASSERT_TRUE(nativeTrace.valid());

        rejectedUpload->open();
        ASSERT_TRUE(rejectedUpload->tryWriteBuffer(destination.get(), s_RejectedWords, sizeof(s_RejectedWords)));
        rejectedUpload->close();
    }
    ASSERT_EQ(rejectedNativeCapture.addressQueryCount, 1u);
    ASSERT_EQ(rejectedNativeCapture.copyCommandCount, 1u);
    ASSERT_NE(rejectedNativeCapture.addressQueries[0u].buffer, VK_NULL_HANDLE);
    EXPECT_EQ(rejectedNativeCapture.addressQueries[0u].buffer, rejectedNativeCapture.copyCommands[0u].source);
    EXPECT_EQ(rejectedNativeCapture.copyCommands[0u].sourceOffset, 0u);

    CommandList* const rejectedUploads[]{ rejectedUpload.get() };
    QueueSubmissionToken rejectedToken;
    VulkanTestQueueSubmit2Observer submissionObserver(uploadDevice);
    ASSERT_TRUE(submissionObserver.valid());
    const GpuPhysicalQueueId graphicsQueue = uploadDevice.getPrimaryPhysicalQueue(CommandQueue::Graphics);
    ASSERT_TRUE(graphicsQueue.valid());
    const VkQueue nativeGraphicsQueue = static_cast<VkQueue>(
        uploadDevice.getNativeQueue(GraphicsBackend::ObjectTypes::VK_Queue, graphicsQueue).pointer()
    );
    ASSERT_NE(nativeGraphicsQueue, VK_NULL_HANDLE);
    ASSERT_TRUE(submissionObserver.armSubmissionFailures(nativeGraphicsQueue));
    rejectedToken = uploadDevice.executeCommandLists(
        rejectedUploads,
        LengthOf(rejectedUploads),
        CommandQueue::Graphics,
        QueueSubmissionDesc{}
    );
    const bool unexpectedlySubmitted = rejectedToken.valid();
    const bool idleAfterUnexpectedSubmission = !unexpectedlySubmitted || uploadDevice.waitForIdle();
    EXPECT_EQ(submissionObserver.injectedSubmissionFailureCount(), 1u);
    EXPECT_EQ(submissionObserver.pendingSubmissionFailureCount(), 0u);
    ASSERT_FALSE(unexpectedlySubmitted);
    ASSERT_TRUE(idleAfterUnexpectedSubmission);

    __hidden_descriptor_buffer_round_trip_tests::NativeUploadReuseCapture retryNativeCapture;
    CommandListHandle retryUpload = uploadDevice.createCommandList();
    ASSERT_TRUE(retryUpload);
    {
        __hidden_descriptor_buffer_round_trip_tests::ScopedNativeUploadReuseTrace nativeTrace(uploadDevice, retryNativeCapture);
        ASSERT_TRUE(nativeTrace.valid());

        retryUpload->open();
        ASSERT_TRUE(retryUpload->tryWriteBuffer(destination.get(), s_RetryWords, sizeof(s_RetryWords)));
        retryUpload->close();
    }
    EXPECT_EQ(retryNativeCapture.addressQueryCount, 0u);
    ASSERT_EQ(retryNativeCapture.copyCommandCount, 1u);
    EXPECT_EQ(retryNativeCapture.copyCommands[0u].source, rejectedNativeCapture.copyCommands[0u].source);
    EXPECT_EQ(retryNativeCapture.copyCommands[0u].sourceOffset, rejectedNativeCapture.copyCommands[0u].sourceOffset);

    CommandListHandle rejectedReadbackCopy = uploadDevice.createCommandList();
    ASSERT_TRUE(rejectedReadbackCopy);
    rejectedReadbackCopy->open();
    rejectedReadbackCopy->copyBuffer(readback.get(), 0u, destination.get(), 0u, sizeof(s_RetryWords));
    ASSERT_FALSE(rejectedReadbackCopy->commandRecordingFailed());
    rejectedReadbackCopy->close();
    CommandList* const rejectedReadbackCopies[]{ rejectedReadbackCopy.get() };
    const QueueSubmissionToken rejectedReadbackToken = uploadDevice.executeCommandLists(
        rejectedReadbackCopies,
        LengthOf(rejectedReadbackCopies),
        CommandQueue::Graphics,
        QueueSubmissionDesc{}
    );
    ASSERT_TRUE(rejectedReadbackToken.valid());
    ASSERT_TRUE(uploadDevice.waitForIdle());

    const u32* const rejectedReadback = static_cast<const u32*>(uploadDevice.mapBuffer(readback.get(), CpuAccessMode::Read));
    ASSERT_NE(rejectedReadback, nullptr);
    for(usize wordIndex = 0u; wordIndex < LengthOf(s_RetryWords); ++wordIndex)
        EXPECT_EQ(rejectedReadback[wordIndex], s_InitialWord);
    uploadDevice.unmapBuffer(readback.get());

    CommandList* const retryUploads[]{ retryUpload.get() };
    const QueueSubmissionToken acceptedToken = uploadDevice.executeCommandLists(
        retryUploads,
        LengthOf(retryUploads),
        CommandQueue::Graphics,
        QueueSubmissionDesc{}
    );
    ASSERT_TRUE(acceptedToken.valid());
    ASSERT_TRUE(uploadDevice.waitForIdle());

    CommandListHandle retryReadbackCopy = uploadDevice.createCommandList();
    ASSERT_TRUE(retryReadbackCopy);
    retryReadbackCopy->open();
    retryReadbackCopy->copyBuffer(readback.get(), 0u, destination.get(), 0u, sizeof(s_RetryWords));
    ASSERT_FALSE(retryReadbackCopy->commandRecordingFailed());
    retryReadbackCopy->close();
    CommandList* const retryReadbackCopies[]{ retryReadbackCopy.get() };
    const QueueSubmissionToken retryReadbackToken = uploadDevice.executeCommandLists(
        retryReadbackCopies,
        LengthOf(retryReadbackCopies),
        CommandQueue::Graphics,
        QueueSubmissionDesc{}
    );
    ASSERT_TRUE(retryReadbackToken.valid());
    ASSERT_TRUE(uploadDevice.waitForIdle());

    const u32* const retryReadback = static_cast<const u32*>(uploadDevice.mapBuffer(readback.get(), CpuAccessMode::Read));
    ASSERT_NE(retryReadback, nullptr);
    for(usize wordIndex = 0u; wordIndex < LengthOf(s_RetryWords); ++wordIndex)
        EXPECT_EQ(retryReadback[wordIndex], s_RetryWords[wordIndex]);
    uploadDevice.unmapBuffer(readback.get());
}


// Accepted publication for a large owner batch must retain its upload chunk before resolving an external hook. Empty
// peers put the uploading owner after the lookup threshold without creating a staging chunk per command list.
TEST_F(DescriptorBufferRoundTripTest, LargeOwnerSubmissionRecyclesUploadChunkAfterAcceptedBatch){
    HeadlessGraphicsScope uploadScope;
    ASSERT_TRUE(uploadScope.initialize());

    GraphicsBackend::Device& uploadDevice = uploadScope.graphics().getDevice();
    const GpuPhysicalQueueId graphicsQueue = uploadDevice.getPrimaryPhysicalQueue(CommandQueue::Graphics);
    ASSERT_TRUE(graphicsQueue.valid());
    static constexpr usize s_CommandListCount = 9u;
    static constexpr usize s_UploadCommandListIndex = s_CommandListCount - 1u;
    static constexpr u32 s_FirstWords[] = { 0x12345678u, 0x90ABCDEFu, 0x0BADF00Du, 0xCAFEBABEu };
    static constexpr u32 s_ReusedWords[] = { 0xDEADBEEFu, 0x01020304u, 0x55667788u, 0xA5A5A5A5u };
    const BufferHandle destination = uploadDevice.createBuffer(
        BufferDesc()
            .setByteSize(sizeof(s_ReusedWords))
            .setCanHaveRawViews(true)
            .setInitialState(ResourceStates::Common)
            .setKeepInitialState(true)
    );
    const BufferHandle readback = uploadDevice.createBuffer(
        BufferDesc()
            .setByteSize(sizeof(s_ReusedWords))
            .setInitialState(ResourceStates::Common)
            .setKeepInitialState(true)
            .setCpuAccess(CpuAccessMode::Read)
    );
    ASSERT_TRUE(destination);
    ASSERT_TRUE(readback);

    CommandListHandle commandLists[s_CommandListCount];
    CommandList* commandListPointers[s_CommandListCount] = {};
    __hidden_descriptor_buffer_round_trip_tests::NativeUploadReuseCapture firstCapture;
    {
        __hidden_descriptor_buffer_round_trip_tests::ScopedNativeUploadReuseTrace nativeTrace(uploadDevice, firstCapture);
        ASSERT_TRUE(nativeTrace.valid());

        for(usize commandListIndex = 0u; commandListIndex < s_CommandListCount; ++commandListIndex){
            commandLists[commandListIndex] = uploadDevice.createCommandList();
            ASSERT_TRUE(commandLists[commandListIndex]);
            commandLists[commandListIndex]->open();
            if(commandListIndex == s_UploadCommandListIndex){
                ASSERT_TRUE(commandLists[commandListIndex]->tryWriteBuffer(
                    destination.get(),
                    s_FirstWords,
                    sizeof(s_FirstWords)
                ));
            }
            commandLists[commandListIndex]->close();
            ASSERT_FALSE(commandLists[commandListIndex]->commandRecordingFailed());
            commandListPointers[commandListIndex] = commandLists[commandListIndex].get();
        }
    }
    ASSERT_EQ(firstCapture.addressQueryCount, 1u);
    ASSERT_EQ(firstCapture.copyCommandCount, 1u);
    ASSERT_NE(firstCapture.copyCommands[0u].source, VK_NULL_HANDLE);

    __hidden_descriptor_buffer_round_trip_tests::BlockingResolvedSubmissionSignal submissionSignal(
        uploadDevice,
        graphicsQueue
    );
    ASSERT_TRUE(submissionSignal.valid());
    QueueSubmissionDesc firstSubmissionDesc;
    firstSubmissionDesc.setPreSubmitHook(submissionSignal.hook());
    QueueSubmissionToken firstToken;
    AtomicFlag submissionFinished;
    Thread submissionThread([&](){
        firstToken = uploadDevice.executeCommandLists(
            commandListPointers,
            LengthOf(commandListPointers),
            graphicsQueue,
            firstSubmissionDesc
        );
        submissionFinished.test_and_set(MemoryOrder::release);
        submissionFinished.notify_all();
    });

    const Timer resolutionWaitBegin = TimerNow();
    while(
        !submissionSignal.resolutionEntered()
        && !submissionFinished.test(MemoryOrder::acquire)
        && DurationInSeconds<f64>(TimerNow(), resolutionWaitBegin) < 5.0
    )
        YieldThread();
    const bool resolutionEntered = submissionSignal.resolutionEntered();
    if(!resolutionEntered){
        submissionSignal.releaseResolution();
        submissionThread.join();
        EXPECT_TRUE(resolutionEntered);
        return;
    }
    const QueueSubmissionToken resolvedToken = submissionSignal.resolvedToken();

    bool firstSubmissionCompleted = false;
    if(resolvedToken.valid()){
        for(CommandListHandle& commandList : commandLists)
            commandList.reset();

        const Timer completionWaitBegin = TimerNow();
        while(
            uploadDevice.queueGetCompletedInstance(graphicsQueue) < resolvedToken.value
            && DurationInSeconds<f64>(TimerNow(), completionWaitBegin) < 5.0
        )
            YieldThread();
        firstSubmissionCompleted = uploadDevice.queueGetCompletedInstance(graphicsQueue) >= resolvedToken.value;
    }

    CommandListHandle reusedUpload;
    __hidden_descriptor_buffer_round_trip_tests::NativeUploadReuseCapture reusedCapture;
    bool reusedTraceValid = false;
    bool reusedRecorded = false;
    bool reusedRecordingFailed = true;
    if(resolvedToken.valid() && firstSubmissionCompleted){
        reusedUpload = uploadDevice.createCommandList();
        __hidden_descriptor_buffer_round_trip_tests::ScopedNativeUploadReuseTrace nativeTrace(uploadDevice, reusedCapture);
        reusedTraceValid = nativeTrace.valid();
        if(reusedUpload && reusedTraceValid){
            reusedUpload->open();
            reusedRecorded = reusedUpload->tryWriteBuffer(destination.get(), s_ReusedWords, sizeof(s_ReusedWords));
            reusedUpload->close();
            reusedRecordingFailed = reusedUpload->commandRecordingFailed();
        }
    }
    submissionSignal.releaseResolution();
    submissionThread.join();

    ASSERT_TRUE(firstToken.valid());
    ASSERT_TRUE(resolvedToken.valid());
    ASSERT_TRUE(firstSubmissionCompleted);
    EXPECT_EQ(submissionSignal.invocationCount(), 1u);
    EXPECT_EQ(resolvedToken.queue, firstToken.queue);
    EXPECT_EQ(resolvedToken.physicalQueueIndex, firstToken.physicalQueueIndex);
    EXPECT_EQ(resolvedToken.deviceGeneration, firstToken.deviceGeneration);
    EXPECT_EQ(resolvedToken.value, firstToken.value);
    ASSERT_TRUE(reusedUpload);
    ASSERT_TRUE(reusedTraceValid);
    ASSERT_TRUE(reusedRecorded);
    ASSERT_FALSE(reusedRecordingFailed);
    EXPECT_EQ(reusedCapture.addressQueryCount, 0u);
    ASSERT_EQ(reusedCapture.copyCommandCount, 1u);
    EXPECT_EQ(reusedCapture.copyCommands[0u].source, firstCapture.copyCommands[0u].source);
    EXPECT_EQ(reusedCapture.copyCommands[0u].sourceOffset, firstCapture.copyCommands[0u].sourceOffset);

    CommandList* const reusedUploads[]{ reusedUpload.get() };
    const QueueSubmissionToken reusedToken = uploadDevice.executeCommandLists(
        reusedUploads,
        LengthOf(reusedUploads),
        graphicsQueue,
        QueueSubmissionDesc{}
    );
    ASSERT_TRUE(reusedToken.valid());
    ASSERT_TRUE(uploadDevice.waitForIdle());

    CommandListHandle readbackCopy = uploadDevice.createCommandList();
    ASSERT_TRUE(readbackCopy);
    readbackCopy->open();
    readbackCopy->copyBuffer(readback.get(), 0u, destination.get(), 0u, sizeof(s_ReusedWords));
    ASSERT_FALSE(readbackCopy->commandRecordingFailed());
    readbackCopy->close();
    CommandList* const readbackCopies[]{ readbackCopy.get() };
    const QueueSubmissionToken readbackToken = uploadDevice.executeCommandLists(
        readbackCopies,
        LengthOf(readbackCopies),
        graphicsQueue,
        QueueSubmissionDesc{}
    );
    ASSERT_TRUE(readbackToken.valid());
    ASSERT_TRUE(uploadDevice.waitForIdle());

    const u32* const mappedReadback = static_cast<const u32*>(uploadDevice.mapBuffer(readback.get(), CpuAccessMode::Read));
    ASSERT_NE(mappedReadback, nullptr);
    for(usize wordIndex = 0u; wordIndex < LengthOf(s_ReusedWords); ++wordIndex)
        EXPECT_EQ(mappedReadback[wordIndex], s_ReusedWords[wordIndex]);
    uploadDevice.unmapBuffer(readback.get());
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

