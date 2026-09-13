// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "presentation_fps_probe.h"

#include <gtest/gtest.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_BEGIN


namespace Tests::Smoke{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


TEST(PresentationFpsProbe, IdleCallbacksNeverBecomePresentations){
    PresentationFpsProbe probe(0.0);
    const Timer begin{};
    EXPECT_EQ(probe.observe(12u, begin), PresentationFpsStatus::Waiting);
    for(i64 milliseconds = 1; milliseconds < 500; ++milliseconds)
        EXPECT_EQ(probe.observe(12u, TimerAddMS(begin, milliseconds)), PresentationFpsStatus::Waiting);
    ASSERT_EQ(probe.observe(12u, TimerAddMS(begin, 500)), PresentationFpsStatus::Interval);
    EXPECT_EQ(probe.interval().presentations(), 0u);
    EXPECT_DOUBLE_EQ(probe.interval().averageFps(), 0.0);
    EXPECT_DOUBLE_EQ(probe.interval().wallSeconds, 0.5);
}

TEST(PresentationFpsProbe, BatchedObservationsCountEveryPresentationAndKeepLongStalls){
    PresentationFpsProbe probe(0.0);
    const Timer begin{};
    EXPECT_EQ(probe.observe(100u, begin), PresentationFpsStatus::Waiting);
    ASSERT_EQ(probe.observe(115u, TimerAddMS(begin, 500)), PresentationFpsStatus::Interval);
    EXPECT_DOUBLE_EQ(probe.interval().averageFps(), 30.0);
    ASSERT_EQ(probe.observe(116u, TimerAddMS(begin, 2500)), PresentationFpsStatus::Interval);
    EXPECT_EQ(probe.interval().presentations(), 1u);
    EXPECT_DOUBLE_EQ(probe.interval().averageFps(), 0.5);
    EXPECT_EQ(probe.total().presentations(), 16u);
    EXPECT_DOUBLE_EQ(probe.total().averageFps(), 6.4);
}

TEST(PresentationFpsProbe, WarmupAndCompletionUseActualWallAnchorsWithoutClamping){
    PresentationFpsProbe probe(5.0, 30.0);
    const Timer begin{};
    EXPECT_EQ(probe.observe(0u, begin), PresentationFpsStatus::Waiting);
    EXPECT_EQ(probe.observe(90u, TimerAddMS(begin, 4999)), PresentationFpsStatus::Waiting);
    EXPECT_EQ(probe.observe(91u, TimerAddMS(begin, 5100)), PresentationFpsStatus::Waiting);
    EXPECT_EQ(probe.observe(540u, TimerAddMS(begin, 35000)), PresentationFpsStatus::Interval);
    ASSERT_EQ(probe.observe(542u, TimerAddMS(begin, 35200)), PresentationFpsStatus::Complete);
    EXPECT_EQ(probe.total().firstPresentationCount, 91u);
    EXPECT_EQ(probe.total().lastPresentationCount, 542u);
    EXPECT_DOUBLE_EQ(probe.total().wallSeconds, 30.1);
    EXPECT_DOUBLE_EQ(probe.total().averageFps(), 451.0 / 30.1);
    EXPECT_EQ(probe.observe(600u, TimerAddMS(begin, 40000)), PresentationFpsStatus::Waiting);
    EXPECT_EQ(probe.total().lastPresentationCount, 542u);
}

TEST(PresentationFpsProbe, ZeroPresentationsCanCompleteButCannotClaimThroughput){
    PresentationFpsProbe probe(0.0, 1.0);
    const Timer begin{};
    EXPECT_EQ(probe.observe(0u, begin), PresentationFpsStatus::Waiting);
    EXPECT_EQ(probe.observe(0u, TimerAddMS(begin, 1000)), PresentationFpsStatus::Complete);
    EXPECT_EQ(probe.total().presentations(), 0u);
    EXPECT_DOUBLE_EQ(probe.total().averageFps(), 0.0);
}

TEST(PresentationFpsProbe, RegressingCountOrClockInvalidatesInsteadOfWrapping){
    const Timer begin{};
    PresentationFpsProbe countProbe(0.0);
    EXPECT_EQ(countProbe.observe(100u, begin), PresentationFpsStatus::Waiting);
    EXPECT_EQ(countProbe.observe(99u, TimerAddMS(begin, 500)), PresentationFpsStatus::Invalid);
    EXPECT_EQ(countProbe.observe(101u, TimerAddMS(begin, 1000)), PresentationFpsStatus::Invalid);
    PresentationFpsProbe clockProbe(0.0);
    EXPECT_EQ(clockProbe.observe(0u, begin), PresentationFpsStatus::Waiting);
    EXPECT_EQ(clockProbe.observe(1u, TimerAddMS(begin, -1)), PresentationFpsStatus::Invalid);
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


NWB_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

