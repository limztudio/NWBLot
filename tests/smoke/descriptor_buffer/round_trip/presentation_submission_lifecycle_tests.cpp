// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "round_trip_fixture.h"
#include "submission_signals_test_support.h"


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace Tests{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace DescriptorBufferRoundTripDetail{};
namespace __hidden_descriptor_buffer_round_trip_tests = DescriptorBufferRoundTripDetail;


// A presentation-style hook owns its binary semaphore from Queued publication through exact native resolution.
// Cancellation waits on the resolver and lifecycle preparation joins the enclosing submission operation, so neither
// path can destroy queue-visible state during the CPU interval before Queue::submit receives the native handle.
TEST_F(DescriptorBufferRoundTripTest, PresentationSignalResolutionJoinsPreQueueSubmitCancellationAndLifecycleDrain){
    auto& device = DescriptorBufferRoundTripTest::device();
    const GpuPhysicalQueueId graphicsQueue = device.getPrimaryPhysicalQueue(CommandQueue::Graphics);
    ASSERT_TRUE(graphicsQueue.valid());

    {
        __hidden_descriptor_buffer_round_trip_tests::BlockingPresentationSubmissionSignal signal(
            device,
            graphicsQueue
        );
        ASSERT_TRUE(signal.valid());
        QueueSubmissionDesc submissionDesc;
        submissionDesc.forceNativeSubmission = true;
        submissionDesc.setPreSubmitHook(signal.hook());

        QueueSubmissionToken submissionToken;
        Thread submissionThread([&](){
            submissionToken = device.executeCommandLists(nullptr, 0u, graphicsQueue, submissionDesc);
            signal.markExecuteReturned();
        });
        signal.waitUntilQueued();

        AtomicFlag cancellationEntered;
        AtomicFlag cancellationFinished;
        bool cancellationObservedAcceptedToken = false;
        Thread cancellationThread([&](){
            cancellationEntered.test_and_set(MemoryOrder::release);
            cancellationEntered.notify_all();
            signal.waitUntilResolved();
            cancellationObservedAcceptedToken = signal.resolvedToken().valid();
            cancellationFinished.test_and_set(MemoryOrder::release);
            cancellationFinished.notify_all();
        });
        while(!cancellationEntered.test(MemoryOrder::acquire))
            cancellationEntered.wait(false, MemoryOrder::acquire);

        EXPECT_FALSE(signal.isResolved());
        EXPECT_FALSE(signal.executeReturned());
        EXPECT_FALSE(cancellationFinished.test(MemoryOrder::acquire));

        signal.releasePrepare();
        submissionThread.join();
        cancellationThread.join();

        ASSERT_TRUE(submissionToken.valid());
        EXPECT_TRUE(cancellationObservedAcceptedToken);
        EXPECT_TRUE(signal.isResolved());
        EXPECT_TRUE(signal.executeReturned());
        EXPECT_TRUE(signal.resolvedBeforeExecuteReturned());
        EXPECT_EQ(signal.invocationCount(), 1u);
        EXPECT_EQ(signal.resolvedToken().queue, submissionToken.queue);
        EXPECT_EQ(signal.resolvedToken().physicalQueueIndex, submissionToken.physicalQueueIndex);
        EXPECT_EQ(signal.resolvedToken().deviceGeneration, submissionToken.deviceGeneration);
        EXPECT_EQ(signal.resolvedToken().value, submissionToken.value);
        ASSERT_TRUE(device.waitForIdle());
    }

    {
        __hidden_descriptor_buffer_round_trip_tests::BlockingPresentationSubmissionSignal signal(
            device,
            graphicsQueue
        );
        ASSERT_TRUE(signal.valid());
        QueueSubmissionDesc submissionDesc;
        submissionDesc.forceNativeSubmission = true;
        submissionDesc.setPreSubmitHook(signal.hook());

        QueueSubmissionToken submissionToken;
        Thread submissionThread([&](){
            submissionToken = device.executeCommandLists(nullptr, 0u, graphicsQueue, submissionDesc);
            signal.markExecuteReturned();
        });
        signal.waitUntilQueued();

        AtomicFlag cancellationEntered;
        AtomicFlag cancellationFinished;
        bool cancellationObservedAcceptedToken = false;
        Thread cancellationThread([&](){
            cancellationEntered.test_and_set(MemoryOrder::release);
            cancellationEntered.notify_all();
            signal.waitUntilResolved();
            cancellationObservedAcceptedToken = signal.resolvedToken().valid();
            cancellationFinished.test_and_set(MemoryOrder::release);
            cancellationFinished.notify_all();
        });
        while(!cancellationEntered.test(MemoryOrder::acquire))
            cancellationEntered.wait(false, MemoryOrder::acquire);

        AtomicFlag lifecycleEntered;
        AtomicFlag lifecycleFinished;
        bool lifecycleDrainSucceeded = false;
        bool lifecycleIdleSucceeded = false;
        Thread lifecycleThread([&](){
            lifecycleEntered.test_and_set(MemoryOrder::release);
            lifecycleEntered.notify_all();
            lifecycleDrainSucceeded = GraphicsBackend::VulkanTestDispatchAccess::beginLifecycleDrain(device);
            if(lifecycleDrainSucceeded){
                lifecycleIdleSucceeded = device.waitForIdle();
                GraphicsBackend::VulkanTestDispatchAccess::endLifecycleDrain(device);
            }
            lifecycleFinished.test_and_set(MemoryOrder::release);
            lifecycleFinished.notify_all();
        });
        while(!lifecycleEntered.test(MemoryOrder::acquire))
            lifecycleEntered.wait(false, MemoryOrder::acquire);
        while(
            !GraphicsBackend::VulkanTestDispatchAccess::submissionsBlocked(device)
            && !lifecycleFinished.test(MemoryOrder::acquire)
        )
            YieldThread();

        EXPECT_TRUE(GraphicsBackend::VulkanTestDispatchAccess::submissionsBlocked(device));
        EXPECT_FALSE(signal.isResolved());
        EXPECT_FALSE(signal.executeReturned());
        EXPECT_FALSE(cancellationFinished.test(MemoryOrder::acquire));
        EXPECT_FALSE(lifecycleFinished.test(MemoryOrder::acquire));

        signal.releasePrepare();
        submissionThread.join();
        cancellationThread.join();
        lifecycleThread.join();

        EXPECT_EQ(cancellationObservedAcceptedToken, submissionToken.valid());
        EXPECT_TRUE(lifecycleDrainSucceeded);
        EXPECT_TRUE(lifecycleIdleSucceeded);
        EXPECT_TRUE(signal.isResolved());
        EXPECT_TRUE(signal.executeReturned());
        EXPECT_TRUE(signal.resolvedBeforeExecuteReturned());
        EXPECT_EQ(signal.invocationCount(), 1u);
        EXPECT_EQ(signal.resolvedToken().queue, submissionToken.queue);
        EXPECT_EQ(signal.resolvedToken().physicalQueueIndex, submissionToken.physicalQueueIndex);
        EXPECT_EQ(signal.resolvedToken().deviceGeneration, submissionToken.deviceGeneration);
        EXPECT_EQ(signal.resolvedToken().value, submissionToken.value);
        EXPECT_FALSE(GraphicsBackend::VulkanTestDispatchAccess::submissionsBlocked(device));
    }
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

