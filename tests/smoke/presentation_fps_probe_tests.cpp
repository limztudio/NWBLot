// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "presentation_fps_probe.h"
#include "presentation_pacing_ring.h"

#include <gtest/gtest.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


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

TEST(PresentationPacingRing, SteadyFramesReportMatchingPercentiles){
    PresentationPacingRing ring;
    const Timer begin{};
    ring.record(0u, begin);
    for(i64 frame = 1; frame <= 100; ++frame)
        ring.record(static_cast<u64>(frame), TimerAddMS(begin, frame * 16));
    const PresentationPacingSummary summary = ring.summarize();
    EXPECT_EQ(summary.samples, 100u);
    EXPECT_DOUBLE_EQ(summary.p50Ms, 16.0);
    EXPECT_DOUBLE_EQ(summary.p95Ms, 16.0);
    EXPECT_DOUBLE_EQ(summary.maxMs, 16.0);
    EXPECT_EQ(summary.stallsOver50Ms, 0u);
}

TEST(PresentationPacingRing, IdleObservationsPreserveFullGapAndCountOneStall){
    PresentationPacingRing ring;
    const Timer begin{};
    ring.record(10u, begin);
    for(i64 step = 1; step <= 3; ++step)
        ring.record(10u, TimerAddMS(begin, step * 60));
    const PresentationPacingSummary idleSummary = ring.summarize();
    EXPECT_EQ(idleSummary.samples, 0u);
    EXPECT_EQ(idleSummary.stallsOver50Ms, 1u);
    ring.record(11u, TimerAddMS(begin, 280));
    const PresentationPacingSummary pacedSummary = ring.summarize();
    EXPECT_EQ(pacedSummary.samples, 1u);
    EXPECT_DOUBLE_EQ(pacedSummary.maxMs, 280.0);
    EXPECT_EQ(pacedSummary.stallsOver50Ms, 1u);
}

TEST(PresentationPacingRing, FrequentIdlePollsCountEachPendingStallOnce){
    PresentationPacingRing ring;
    const Timer begin{};
    ring.record(0u, begin);
    for(i64 milliseconds = 10; milliseconds <= 90; milliseconds += 10)
        ring.record(0u, TimerAddMS(begin, milliseconds));
    EXPECT_EQ(ring.sampleCount(), 0u);
    EXPECT_EQ(ring.stallCount(), 1u);
    ring.record(1u, TimerAddMS(begin, 100));
    ring.record(1u, TimerAddMS(begin, 150));
    ring.record(1u, TimerAddMS(begin, 160));
    EXPECT_EQ(ring.stallCount(), 2u);
    ring.record(2u, TimerAddMS(begin, 200));
    const PresentationPacingSummary summary = ring.summarize();
    EXPECT_EQ(summary.samples, 2u);
    EXPECT_DOUBLE_EQ(summary.p50Ms, 100.0);
    EXPECT_DOUBLE_EQ(summary.maxMs, 100.0);
    EXPECT_EQ(summary.stallsOver50Ms, 2u);
}

TEST(PresentationPacingRing, BatchedAdvanceAveragesTheWholeIntervalAfterIdlePolling){
    PresentationPacingRing ring;
    const Timer begin{};
    ring.record(10u, begin);
    ring.record(10u, TimerAddMS(begin, 25));
    ring.record(14u, TimerAddMS(begin, 80));
    ring.record(14u, TimerAddMS(begin, 88));
    ring.record(15u, TimerAddMS(begin, 96));
    const PresentationPacingSummary summary = ring.summarize();
    EXPECT_EQ(summary.samples, 2u);
    EXPECT_DOUBLE_EQ(summary.maxMs, 20.0);
    EXPECT_EQ(summary.stallsOver50Ms, 0u);
}

TEST(PresentationPacingRing, IdleClockRegressionResetsEvenAfterLastPresentationTime){
    PresentationPacingRing ring;
    const Timer begin{};
    ring.record(10u, begin);
    ring.record(11u, TimerAddMS(begin, 16));
    ring.record(11u, TimerAddMS(begin, 80));
    EXPECT_EQ(ring.stallCount(), 1u);
    ring.record(11u, TimerAddMS(begin, 70));
    EXPECT_EQ(ring.sampleCount(), 0u);
    EXPECT_EQ(ring.stallCount(), 0u);
    ring.record(11u, TimerAddMS(begin, 100));
    ring.record(12u, TimerAddMS(begin, 116));
    const PresentationPacingSummary summary = ring.summarize();
    EXPECT_EQ(summary.samples, 1u);
    EXPECT_DOUBLE_EQ(summary.maxMs, 16.0);
    EXPECT_EQ(summary.stallsOver50Ms, 0u);
}

TEST(PresentationPacingRing, RegressingCountResetsInsteadOfWrapping){
    PresentationPacingRing ring;
    const Timer begin{};
    ring.record(100u, begin);
    ring.record(110u, TimerAddMS(begin, 160));
    EXPECT_EQ(ring.sampleCount(), 1u);
    ring.record(99u, TimerAddMS(begin, 320));
    EXPECT_EQ(ring.sampleCount(), 0u);
    ring.record(99u, TimerAddMS(begin, 320));
    ring.record(100u, TimerAddMS(begin, 336));
    const PresentationPacingSummary summary = ring.summarize();
    EXPECT_EQ(summary.samples, 1u);
    EXPECT_DOUBLE_EQ(summary.maxMs, 16.0);
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


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

