// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "round_trip_fixture.h"


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace Tests{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


// A timeline value is only meaningful for the Device that issued it.  Make the rejection observable at the native
// submission boundary so an imported graph completion cannot accidentally wait on a recycled queue timeline after
// device recreation.
TEST_F(DescriptorBufferRoundTripTest, QueueSubmissionRejectsRetiredDeviceGeneration){
    QueueSubmissionToken retiredToken;
    {
        HeadlessGraphicsScope producerScope;
        ASSERT_TRUE(producerScope.initialize());
        auto& producer = producerScope.graphics().getDevice();

        auto producerCommandList = producer.createCommandList();
        ASSERT_NE(producerCommandList.get(), nullptr);
        producerCommandList->open();
        producerCommandList->close();
        CommandList* const producerCommandLists[] = { producerCommandList.get() };

        retiredToken = producer.executeCommandLists(
            producerCommandLists,
            LengthOf(producerCommandLists),
            CommandQueue::Graphics,
            QueueSubmissionDesc{}
        );
        ASSERT_TRUE(retiredToken.valid());
        ASSERT_TRUE(retiredToken.hasPhysicalQueueIdentity());
        EXPECT_TRUE(producer.matchesPhysicalQueueIdentity(
            retiredToken.queue,
            retiredToken.physicalQueueIndex,
            retiredToken.deviceGeneration
        ));
    }

    HeadlessGraphicsScope consumerScope;
    ASSERT_TRUE(consumerScope.initialize());
    auto& consumer = consumerScope.graphics().getDevice();
    EXPECT_NE(retiredToken.deviceGeneration, consumer.getDeviceGeneration());
    EXPECT_FALSE(consumer.matchesPhysicalQueueIdentity(
        retiredToken.queue,
        retiredToken.physicalQueueIndex,
        retiredToken.deviceGeneration
    ));

    const QueueSubmissionDesc staleWait = QueueSubmissionDesc().setWaitTokens(&retiredToken, 1u);
#if defined(NWB_DEBUG) || defined(NWB_OPTIMIZE)
    EXPECT_DEATH_IF_SUPPORTED({
        EXPECT_FALSE(consumer.executeCommandLists(nullptr, 0u, CommandQueue::Graphics, staleWait).valid());
    }, "");
#else
    EXPECT_FALSE(consumer.executeCommandLists(nullptr, 0u, CommandQueue::Graphics, staleWait).valid());
#endif
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

