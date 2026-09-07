// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "graph_resources_test_support.h"
#include "round_trip_fixture.h"


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace Tests{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


// The native registry is authoritative: graph packets and direct physical submissions must name a concrete VkQueue
// rather than infer its identity from a CommandQueue ordinal. Keep this on a real device so the registry, command
// pool selection, and submission token all agree even when a device exposes several queues of one broad class.
TEST_F(DescriptorBufferRoundTripTest, NativePhysicalQueueRegistryDrivesExactSubmission){
    auto& nativeDevice = device();
    const GpuPhysicalQueueTopology topology = nativeDevice.getPhysicalQueueTopology();
    ASSERT_NE(topology.queues, nullptr);
    ASSERT_GT(topology.queueCount, 0u);

    const GpuPhysicalQueueId graphicsQueue = nativeDevice.getPrimaryPhysicalQueue(CommandQueue::Graphics);
    ASSERT_TRUE(graphicsQueue.valid());
    ASSERT_TRUE(nativeDevice.matchesPhysicalQueueIdentity(graphicsQueue));
    EXPECT_EQ(BackendQueueId(nativeDevice, CommandQueue::Graphics), graphicsQueue);

    bool foundGraphicsQueue = false;
    for(usize index = 0u; index < topology.queueCount; ++index){
        const GpuPhysicalQueueInfo& queue = topology.queues[index];
        EXPECT_TRUE(queue.id.valid());
        EXPECT_EQ(queue.id.deviceGeneration, nativeDevice.getDeviceGeneration());
        EXPECT_EQ(nativeDevice.getPhysicalQueueInfo(queue.id), &queue);
        EXPECT_TRUE(nativeDevice.matchesPhysicalQueueIdentity(queue.id));
        EXPECT_TRUE(nativeDevice.matchesPhysicalQueueIdentity(
            queue.queueClass,
            queue.id.index,
            queue.id.deviceGeneration
        ));
        EXPECT_TRUE(
            queue.timestampValidBits == 0u
            || (queue.timestampValidBits >= 36u && queue.timestampValidBits <= 64u)
        );
        EXPECT_EQ(
            nativeDevice.supportsComparableGpuTimestamps(queue.id),
            nativeDevice.supportsComparableGpuTimestamps() && queue.timestampValidBits == 64u
        );
        EXPECT_NE(nativeDevice.getNativeQueue(GraphicsBackend::ObjectTypes::VK_Queue, queue.id), Object(nullptr));
        foundGraphicsQueue = foundGraphicsQueue || queue.id == graphicsQueue;
    }
    EXPECT_TRUE(foundGraphicsQueue);

    CommandListParameters parameters;
    parameters.setPhysicalQueue(graphicsQueue);
    auto commandList = nativeDevice.createCommandList(parameters);
    ASSERT_NE(commandList.get(), nullptr);
    EXPECT_EQ(commandList->getDescription().physicalQueue, graphicsQueue);
    commandList->open();
    commandList->close();

    CommandList* const commandLists[] = { commandList.get() };
    const QueueSubmissionToken token = nativeDevice.executeCommandLists(
        commandLists,
        LengthOf(commandLists),
        graphicsQueue,
        QueueSubmissionDesc{}
    );
    ASSERT_TRUE(token.valid());
    EXPECT_TRUE(token.matchesPhysicalQueue(graphicsQueue.index, graphicsQueue.deviceGeneration));
    EXPECT_EQ(token.queue, CommandQueue::Graphics);

    EXPECT_TRUE(nativeDevice.waitForIdle());
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

