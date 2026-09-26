// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include <impl/ecs_render/shadow/light_space_capture_history.h>

#include <gtest/gtest.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace __hidden_light_space_capture_history_tests{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


using namespace NWB::Impl;

TEST(LightSpaceCaptureHistory, RequiresRecordedAcceptedCaptureAndAlternatesOneReuse){
    LightSpaceCaptureHistory history;
    const LightSpaceCaptureIdentity identity{ 7u, 11u, 13u, true };
    history.beginFrame();
    const auto first = history.prepare(identity, SoftwareShadowCaptureCadence::ReuseOneFrame);
    ASSERT_FALSE(first.reuse);
    EXPECT_FALSE(history.accept(first));
    history.recordCapture(first);
    ASSERT_TRUE(history.accept(first));
    EXPECT_FALSE(history.accept(first));
    history.beginFrame();
    const auto reused = history.prepare(identity, SoftwareShadowCaptureCadence::ReuseOneFrame);
    ASSERT_TRUE(reused.reuse);
    ASSERT_TRUE(history.accept(reused));
    history.beginFrame();
    const auto refreshed = history.prepare(identity, SoftwareShadowCaptureCadence::ReuseOneFrame);
    EXPECT_FALSE(refreshed.reuse);
    history.recordCapture(refreshed);
    ASSERT_TRUE(history.accept(refreshed));
    EXPECT_EQ(history.acceptedCaptures(), 2u);
    EXPECT_EQ(history.acceptedReuses(), 1u);
}

TEST(LightSpaceCaptureHistory, ReferenceAlwaysCapturesAndUntrustedSceneNeverReuses){
    for(const bool trusted : { false, true }){
        LightSpaceCaptureHistory history;
        const LightSpaceCaptureIdentity identity{ 7u, 11u, 13u, trusted };
        for(u32 frame = 0u; frame < 4u; ++frame){
            history.beginFrame();
            const auto ticket = history.prepare(identity, trusted ? SoftwareShadowCaptureCadence::EveryFrame
                : SoftwareShadowCaptureCadence::ReuseOneFrame);
            EXPECT_FALSE(ticket.reuse);
            history.recordCapture(ticket);
            ASSERT_TRUE(history.accept(ticket));
        }
        EXPECT_EQ(history.acceptedCaptures(), 4u);
        EXPECT_EQ(history.acceptedReuses(), 0u);
    }
}

TEST(LightSpaceCaptureHistory, EachIdentityDomainAndTrustChangeForcesRefresh){
    const LightSpaceCaptureIdentity original{ 7u, 11u, 13u, true };
    const LightSpaceCaptureIdentity changes[] = {
        { 8u, 11u, 13u, true }, { 7u, 12u, 13u, true }, { 7u, 11u, 14u, true }, { 7u, 11u, 13u, false },
    };
    for(const auto& changed : changes){
        LightSpaceCaptureHistory history;
        history.beginFrame();
        const auto captured = history.prepare(original, SoftwareShadowCaptureCadence::ReuseOneFrame);
        history.recordCapture(captured);
        ASSERT_TRUE(history.accept(captured));
        history.beginFrame();
        EXPECT_FALSE(history.prepare(changed, SoftwareShadowCaptureCadence::ReuseOneFrame).reuse);
    }
}

TEST(LightSpaceCaptureHistory, RejectedRefreshCannotResurrectPreviousInPlaceContents){
    LightSpaceCaptureHistory history;
    const LightSpaceCaptureIdentity identity{ 7u, 11u, 13u, true };
    history.beginFrame();
    const auto captured = history.prepare(identity, SoftwareShadowCaptureCadence::ReuseOneFrame);
    history.recordCapture(captured);
    ASSERT_TRUE(history.accept(captured));
    history.beginFrame();
    auto changed = identity;
    ++changed.scene;
    const auto rejected = history.prepare(changed, SoftwareShadowCaptureCadence::ReuseOneFrame);
    EXPECT_FALSE(rejected.reuse);
    history.recordCapture(rejected);
    history.beginFrame();
    const auto next = history.prepare(identity, SoftwareShadowCaptureCadence::ReuseOneFrame);
    EXPECT_FALSE(next.reuse);
    EXPECT_FALSE(history.accept(rejected));
    history.recordCapture(rejected);
    EXPECT_FALSE(history.accept(next));
}

TEST(LightSpaceCaptureHistory, RejectedReuseAndSkippedFramesCannotExtendCaptureAge){
    for(const bool prepareReuse : { false, true }){
        LightSpaceCaptureHistory history;
        const LightSpaceCaptureIdentity identity{ 7u, 11u, 13u, true };
        history.beginFrame();
        const auto captured = history.prepare(identity, SoftwareShadowCaptureCadence::ReuseOneFrame);
        history.recordCapture(captured);
        ASSERT_TRUE(history.accept(captured));
        history.beginFrame();
        if(prepareReuse)
            EXPECT_TRUE(history.prepare(identity, SoftwareShadowCaptureCadence::ReuseOneFrame).reuse);
        history.beginFrame();
        EXPECT_FALSE(history.prepare(identity, SoftwareShadowCaptureCadence::ReuseOneFrame).reuse);
    }
}

TEST(LightSpaceCaptureHistory, InvalidatedResourcesRejectPendingTicketsAndRequireRefresh){
    LightSpaceCaptureHistory history;
    const LightSpaceCaptureIdentity identity{ 7u, 11u, 13u, true };
    history.beginFrame();
    const auto captured = history.prepare(identity, SoftwareShadowCaptureCadence::ReuseOneFrame);
    history.recordCapture(captured);
    history.invalidate();
    EXPECT_FALSE(history.accept(captured));
    history.beginFrame();
    const auto current = history.prepare(identity, SoftwareShadowCaptureCadence::ReuseOneFrame);
    EXPECT_FALSE(current.reuse);
    EXPECT_NE(current.sequence, captured.sequence);
    history.recordCapture(captured);
    EXPECT_FALSE(history.accept(current));
}

TEST(LightSpaceCaptureHistory, SettingsRequireExplicitKnownCadence){
    SoftwareShadowSettings settings;
    EXPECT_EQ(settings.captureCadence, SoftwareShadowCaptureCadence::EveryFrame);
    EXPECT_TRUE(ValidateSoftwareShadowSettings(settings));
    settings.captureCadence = SoftwareShadowCaptureCadence::ReuseOneFrame;
    EXPECT_TRUE(ValidateSoftwareShadowSettings(settings));
    settings.captureCadence = static_cast<SoftwareShadowCaptureCadence::Enum>(2u);
    EXPECT_FALSE(ValidateSoftwareShadowSettings(settings));
}

TEST(LightSpaceCaptureHistory, SameFrameGraphRetryAcceptsOnlyItsLatestRecordedCandidate){
    LightSpaceCaptureHistory history;
    const LightSpaceCaptureIdentity identity{ 7u, 11u, 13u, true };
    history.beginFrame();
    const auto firstDeclaration = history.prepare(identity, SoftwareShadowCaptureCadence::ReuseOneFrame);
    const auto retryDeclaration = history.prepare(identity, SoftwareShadowCaptureCadence::ReuseOneFrame);
    EXPECT_FALSE(firstDeclaration.reuse);
    EXPECT_FALSE(retryDeclaration.reuse);
    EXPECT_NE(firstDeclaration.sequence, retryDeclaration.sequence);
    history.recordCapture(firstDeclaration);
    EXPECT_FALSE(history.accept(firstDeclaration));
    EXPECT_FALSE(history.accept(retryDeclaration));
    history.recordCapture(retryDeclaration);
    ASSERT_TRUE(history.accept(retryDeclaration));
    history.beginFrame();
    const auto firstReuse = history.prepare(identity, SoftwareShadowCaptureCadence::ReuseOneFrame);
    const auto retryReuse = history.prepare(identity, SoftwareShadowCaptureCadence::ReuseOneFrame);
    EXPECT_TRUE(firstReuse.reuse);
    EXPECT_TRUE(retryReuse.reuse);
    EXPECT_FALSE(history.accept(firstReuse));
    EXPECT_TRUE(history.accept(retryReuse));
    EXPECT_EQ(history.acceptedCaptures(), 1u);
    EXPECT_EQ(history.acceptedReuses(), 1u);
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

