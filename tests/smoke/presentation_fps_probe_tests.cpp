// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include <tests/smoke/smoke_test_cases.adhd.h>

#include <gtest/gtest.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace Tests::Smoke{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


TEST(PresentationFpsProbe, IdleCallbacksNeverBecomePresentations){
    VerifyPresentationFpsProbeIdleCallbacks();
}

TEST(PresentationFpsProbe, BatchedObservationsCountEveryPresentationAndKeepLongStalls){
    VerifyPresentationFpsProbeBatchedObservations();
}

TEST(PresentationFpsProbe, WarmupAndCompletionUseActualWallAnchorsWithoutClamping){
    VerifyPresentationFpsProbeWarmupAndCompletion();
}

TEST(PresentationFpsProbe, ZeroPresentationsCanCompleteButCannotClaimThroughput){
    VerifyPresentationFpsProbeZeroPresentations();
}

TEST(PresentationPacingRing, FrequentIdlePollsCountEachPendingStallOnce){
    VerifyPresentationPacingRingFrequentIdlePolls();
}

TEST(PresentationPacingRing, BatchedAdvanceAveragesTheWholeIntervalAfterIdlePolling){
    VerifyPresentationPacingRingBatchedAdvance();
}

TEST(PresentationPacingRing, IdleClockRegressionResetsEvenAfterLastPresentationTime){
    VerifyPresentationPacingRingIdleClockRegression();
}

TEST(PresentationPacingRing, RegressingCountResetsInsteadOfWrapping){
    VerifyPresentationPacingRingRegressingCount();
}

TEST(PresentationFpsProbe, RegressingCountOrClockInvalidatesInsteadOfWrapping){
    VerifyPresentationFpsProbeRegressingCountOrClock();
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
