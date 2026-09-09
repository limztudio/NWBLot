// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include <impl/ecs_render/reflection/feedback.h>
#include <impl/ecs_render/reflection/feedback_resources.h>

#include <core/alloc/general.h>
#include <core/task/gpu/task_graph.h>

#include <gtest/gtest.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace __hidden_reflection_feedback_tests{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


using namespace NWB;
using namespace NWB::Impl;

[[nodiscard]] ReflectionSceneContentStamp Stamp(){
    return ReflectionSceneContentStamp{1u, 2u, 3u, 4u, true};
}

[[nodiscard]] Core::QueueSubmissionToken Token(const u64 value){
    return Core::QueueSubmissionToken{Core::CommandQueue::Graphics, value, 0u, 1u};
}

void Accept(ReflectionFeedbackState& state, const ReflectionFeedbackPlan& plan, const bool hardwareReady = true){
    ASSERT_TRUE(state.reserve(plan));
    ASSERT_TRUE(state.accept(plan, Token(plan.sequence), hardwareReady));
}

struct LeaseTask{
    struct Payload{
        ReflectionFeedbackReservation reservation;
    };
    static void discarded(Payload& payload){ payload.reservation.discard(); }
};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


TEST(ReflectionFeedback, ExtentAccountsForTwoSurfaceClassesAndPartialTiles){
    const auto native = ComputeReflectionFeedbackExtent(960u, 720u);
    ASSERT_TRUE(native.valid());
    EXPECT_EQ(native.tilesX, 120u);
    EXPECT_EQ(native.tilesY, 90u);
    EXPECT_EQ(native.entryCount, 21600u);
    EXPECT_EQ(native.byteCount, 86416u);
    const auto partial = ComputeReflectionFeedbackExtent(9u, 17u);
    ASSERT_TRUE(partial.valid());
    EXPECT_EQ(partial.tilesX, 2u);
    EXPECT_EQ(partial.tilesY, 3u);
    EXPECT_EQ(partial.entryCount, 12u);
    EXPECT_EQ(partial.byteCount, 64u);
    EXPECT_EQ(ComputeReflectionFeedbackExtent(1u, 1u).byteCount, 24u);
    EXPECT_FALSE(ComputeReflectionFeedbackExtent(0u, 1u).valid());
    EXPECT_FALSE(ComputeReflectionFeedbackExtent(1u, 0u).valid());
    EXPECT_FALSE(ComputeReflectionFeedbackExtent(Limit<u32>::s_Max, 1u).valid());
    EXPECT_FALSE(ComputeReflectionFeedbackExtent(Limit<u32>::s_Max, Limit<u32>::s_Max).valid());
}

TEST(ReflectionFeedback, FirstWriterAndNextCompatibleFrameSelectDifferentBanks){
    ReflectionFeedbackState state(1u);
    const auto first = state.plan(Stamp(), ReflectionSettings{}, true, 100u);
    EXPECT_TRUE(first.eligible);
    EXPECT_TRUE(first.reset);
    EXPECT_FALSE(first.reused);
    EXPECT_EQ(first.probeIndex, 0u);
    EXPECT_EQ(first.startGraphicsFrame, 100u);
    EXPECT_EQ(first.currentBank, 0u);
    EXPECT_EQ(first.resetReason, ReflectionFeedbackResetReason::FirstObservation);
    Accept(state, first);
    const auto next = state.plan(Stamp(), ReflectionSettings{}, true, 101u);
    EXPECT_TRUE(next.reused);
    EXPECT_FALSE(next.reset);
    EXPECT_EQ(next.probeIndex, 1u);
    EXPECT_EQ(next.startGraphicsFrame, 100u);
    EXPECT_EQ(next.currentBank, 1u);
    EXPECT_EQ(next.previousBank, first.currentBank);
    EXPECT_EQ(next.acceptedSequence, first.sequence);
}

TEST(ReflectionFeedback, ProbeCadenceCountsAcceptedFeedbackIndependentlyOfTemporalSettings){
    ReflectionFeedbackState state(1u);
    ReflectionSettings settings;
    for(u32 index = 0u; index < 40u; ++index){
        settings.temporalEnabled = (index % 2u) == 0u;
        settings.temporalMaxSamples = index + 1u;
        settings.spatialFilterEnabled = !settings.temporalEnabled;
        settings.spatialRadius = index % 4u;
        settings.diagnosticsEnabled = settings.temporalEnabled;
        settings.debugView = settings.temporalEnabled ? ReflectionDebugView::Confidence : ReflectionDebugView::None;
        const auto plan = state.plan(Stamp(), settings, true, 100u + index);
        EXPECT_EQ(plan.probeIndex, index);
        EXPECT_EQ(plan.reset, index == 0u);
        Accept(state, plan);
    }
}

TEST(ReflectionFeedback, SpeculativePlansAndRejectedWriterDoNotAdvanceCadence){
    ReflectionFeedbackState state(1u);
    const auto first = state.plan(Stamp(), ReflectionSettings{}, true, 1u);
    Accept(state, first);
    for(u32 index = 0u; index < 8u; ++index){
        const auto rejected = state.plan(Stamp(), ReflectionSettings{}, true, 2u + index);
        EXPECT_EQ(rejected.probeIndex, 1u);
        EXPECT_NE(rejected.currentBank, first.currentBank);
        ASSERT_TRUE(state.reserve(rejected));
        state.discard(rejected);
    }
    const auto retry = state.plan(Stamp(), ReflectionSettings{}, true, 10u);
    EXPECT_EQ(retry.probeIndex, 1u);
    EXPECT_EQ(retry.acceptedSequence, first.sequence);
    EXPECT_EQ(retry.previousBank, first.currentBank);
    EXPECT_TRUE(retry.reused);
}

TEST(ReflectionFeedback, EveryTrustedContentDomainInvalidatesPreviousTileState){
    for(u32 domain = 0u; domain < 4u; ++domain){
        ReflectionFeedbackState state(1u);
        Accept(state, state.plan(Stamp(), ReflectionSettings{}, true, 1u));
        auto stamp = Stamp();
        if(domain == 0u)
            ++stamp.geometry;
        else if(domain == 1u)
            ++stamp.material;
        else if(domain == 2u)
            ++stamp.lighting;
        else
            ++stamp.view;
        const auto changed = state.plan(stamp, ReflectionSettings{}, true, 2u);
        EXPECT_TRUE(changed.reset);
        EXPECT_FALSE(changed.reused);
        EXPECT_EQ(changed.probeIndex, 0u);
        EXPECT_EQ(changed.resetReason, domain == 3u ? ReflectionFeedbackResetReason::ViewChanged : ReflectionFeedbackResetReason::SceneChanged);
        Accept(state, changed);
        const auto stable = state.plan(stamp, ReflectionSettings{}, true, 3u);
        EXPECT_FALSE(stable.reset);
        EXPECT_TRUE(stable.reused);
        EXPECT_EQ(stable.probeIndex, 1u);
    }
}

TEST(ReflectionFeedback, ScreenRangeBudgetAndSeedChangesInvalidateFeedback){
    for(u32 field = 0u; field < 10u; ++field){
        ReflectionFeedbackState state(1u);
        ReflectionSettings settings;
        Accept(state, state.plan(Stamp(), settings, true, 1u));
        switch(field){
        case 0u: ++settings.samplingSeed; break;
        case 1u: --settings.maxHardwareRaysPerFrame; break;
        case 2u: --settings.maxOpticalQueries; break;
        case 3u: settings.maxRayDistance += 1.f; break;
        case 4u: settings.distanceFadeStart -= 1.f; break;
        case 5u: settings.roughnessCutoff -= 0.1f; break;
        case 6u: --settings.screenMaxSteps; break;
        case 7u: settings.screenThickness += 0.01f; break;
        case 8u: settings.screenConfidenceThreshold -= 0.1f; break;
        case 9u: settings.screenEdgeFade += 0.01f; break;
        }
        const auto next = state.plan(Stamp(), settings, true, 2u);
        EXPECT_EQ(next.resetReason, ReflectionFeedbackResetReason::SettingsChanged);
        EXPECT_FALSE(next.reused);
        EXPECT_EQ(next.probeIndex, 0u);
    }
}

TEST(ReflectionFeedback, OnlyTrustedEnabledHybridWithAvailableHardwareCanUseFeedback){
    for(u32 reason = 0u; reason < 7u; ++reason){
        ReflectionFeedbackState state(1u);
        auto stamp = Stamp();
        ReflectionSettings settings;
        bool enabled = true;
        bool hardwareReady = true;
        switch(reason){
        case 0u: enabled = false; break;
        case 1u: stamp.trusted = false; break;
        case 2u: settings.traceMode = ReflectionTraceMode::Disabled; break;
        case 3u: settings.traceMode = ReflectionTraceMode::ScreenSpace; break;
        case 4u: settings.traceMode = ReflectionTraceMode::Hardware; break;
        case 5u: settings.maxHardwareRaysPerFrame = 0u; break;
        case 6u: hardwareReady = false; break;
        }
        const auto plan = state.plan(stamp, settings, enabled, 1u);
        const auto outcome = ResolveReflectionFeedbackOutcome(plan, hardwareReady);
        EXPECT_FALSE(outcome.eligible);
        EXPECT_FALSE(outcome.reused);
        Accept(state, plan, hardwareReady);
        const auto next = state.plan(stamp, settings, enabled, 2u);
        EXPECT_EQ(next.probeIndex, 0u);
        EXPECT_FALSE(next.reused);
    }
}

TEST(ReflectionFeedback, RejectedDisablePreservesAcceptedBankButAcceptedDisableInvalidatesReuse){
    ReflectionFeedbackState state(1u);
    const auto first = state.plan(Stamp(), ReflectionSettings{}, true, 1u);
    Accept(state, first);
    const auto disabled = state.plan(Stamp(), ReflectionSettings{}, false, 2u);
    EXPECT_NE(disabled.currentBank, first.currentBank);
    ASSERT_TRUE(state.reserve(disabled));
    state.discard(disabled);
    EXPECT_TRUE(state.plan(Stamp(), ReflectionSettings{}, true, 3u).reused);
    Accept(state, state.plan(Stamp(), ReflectionSettings{}, false, 4u));
    const auto enabled = state.plan(Stamp(), ReflectionSettings{}, true, 5u);
    EXPECT_FALSE(enabled.reused);
    EXPECT_TRUE(enabled.reset);
    EXPECT_EQ(enabled.probeIndex, 0u);
    EXPECT_EQ(enabled.previousBank, first.currentBank);
    EXPECT_NE(enabled.currentBank, first.currentBank);
}

TEST(ReflectionFeedback, RejectedUntrustedPrefixPreservesAcceptedFeedback){
    ReflectionFeedbackState state(1u);
    const auto first = state.plan(Stamp(), ReflectionSettings{}, true, 1u);
    Accept(state, first);
    auto untrusted = Stamp();
    untrusted.trusted = false;
    const auto rejected = state.plan(untrusted, ReflectionSettings{}, true, 2u);
    EXPECT_FALSE(rejected.eligible);
    EXPECT_NE(rejected.currentBank, first.currentBank);
    ASSERT_TRUE(state.reserve(rejected));
    state.discard(rejected);
    EXPECT_TRUE(state.plan(Stamp(), ReflectionSettings{}, true, 3u).reused);
    Accept(state, state.plan(untrusted, ReflectionSettings{}, true, 4u));
    EXPECT_FALSE(state.plan(Stamp(), ReflectionSettings{}, true, 5u).reused);
}

TEST(ReflectionFeedback, LateHardwareFailureDisablesBypassAndRecoveryRestartsObservation){
    ReflectionFeedbackState state(1u);
    const auto first = state.plan(Stamp(), ReflectionSettings{}, true, 1u);
    Accept(state, first);
    const auto failed = state.plan(Stamp(), ReflectionSettings{}, true, 2u);
    ASSERT_TRUE(failed.reused);
    const auto outcome = ResolveReflectionFeedbackOutcome(failed, false);
    EXPECT_FALSE(outcome.eligible);
    EXPECT_FALSE(outcome.reused);
    EXPECT_EQ(outcome.resetReason, ReflectionFeedbackResetReason::HardwareChanged);
    EXPECT_EQ(outcome.probeIndex, 0u);
    EXPECT_EQ(outcome.epoch, first.epoch + 1u);
    EXPECT_EQ(outcome.startGraphicsFrame, 2u);
    Accept(state, failed, false);
    const auto recovery = state.plan(Stamp(), ReflectionSettings{}, true, 3u);
    const auto recovered = ResolveReflectionFeedbackOutcome(recovery, true);
    EXPECT_TRUE(recovered.eligible);
    EXPECT_FALSE(recovered.reused);
    EXPECT_EQ(recovered.probeIndex, 0u);
    EXPECT_EQ(recovered.resetReason, ReflectionFeedbackResetReason::HardwareChanged);
    EXPECT_EQ(recovered.startGraphicsFrame, 3u);
    EXPECT_EQ(recovery.previousBank, first.currentBank);
    Accept(state, recovery);
    EXPECT_TRUE(state.plan(Stamp(), ReflectionSettings{}, true, 4u).reused);
}

TEST(ReflectionFeedback, InvalidAcceptedTokensQuarantineBankReuseUntilJoinedOwnerReset){
    for(u32 invalid = 0u; invalid < 6u; ++invalid){
        ReflectionFeedbackState state(1u);
        const auto first = state.plan(Stamp(), ReflectionSettings{}, true, 1u);
        Accept(state, first);
        const auto plan = state.plan(Stamp(), ReflectionSettings{}, true, 2u + invalid);
        ASSERT_TRUE(state.reserve(plan));
        auto token = Token(plan.sequence);
        switch(invalid){
        case 0u: token.value = 0u; break;
        case 1u: token.physicalQueueIndex = Limit<u16>::s_Max; break;
        case 2u: token.deviceGeneration = 2u; break;
        case 3u: token.queue = Core::CommandQueue::Compute; break;
        case 4u: token.value = first.sequence; break;
        case 5u: token.physicalQueueIndex = 1u; break;
        }
        EXPECT_FALSE(state.accept(plan, token, true));
        const auto retry = state.plan(Stamp(), ReflectionSettings{}, true, 20u + invalid);
        EXPECT_EQ(retry.acceptedSequence, first.sequence);
        EXPECT_TRUE(retry.quarantined);
        EXPECT_FALSE(retry.eligible);
        EXPECT_FALSE(retry.reused);
        EXPECT_EQ(retry.resetReason, ReflectionFeedbackResetReason::InvalidAcceptance);
        EXPECT_FALSE(state.reserve(retry));
        state.discard(plan);
        EXPECT_TRUE(state.plan(Stamp(), ReflectionSettings{}, true, 30u + invalid).quarantined);
        // Reset models the renderer's explicit join/discard boundary, not an ordinary rejected frame.
        state.reset(1u);
        const auto reset = state.plan(Stamp(), ReflectionSettings{}, true, 40u + invalid);
        EXPECT_FALSE(reset.quarantined);
        EXPECT_EQ(reset.probeIndex, 0u);
        EXPECT_EQ(reset.resetReason, ReflectionFeedbackResetReason::ResourcesChanged);
        ASSERT_TRUE(state.reserve(reset));
        state.discard(reset);
    }
}

TEST(ReflectionFeedback, StalePlanCannotReserveAfterAnIndependentPlanAccepts){
    ReflectionFeedbackState state(1u);
    const auto stale = state.plan(Stamp(), ReflectionSettings{}, true, 1u);
    const auto current = state.plan(Stamp(), ReflectionSettings{}, true, 2u);
    Accept(state, current);
    EXPECT_FALSE(state.reserve(stale));
    EXPECT_FALSE(state.accept(stale, Token(99u), true));
    state.discard(stale);
    EXPECT_EQ(state.plan(Stamp(), ReflectionSettings{}, true, 3u).acceptedSequence, current.sequence);
}

TEST(ReflectionFeedback, ResourceGenerationBlocksOldCallbacksWithoutDiscardingTheNewLease){
    ReflectionFeedbackState state(1u);
    const auto old = state.plan(Stamp(), ReflectionSettings{}, true, 1u);
    ASSERT_TRUE(state.reserve(old));
    state.reset(2u);
    const auto next = state.plan(Stamp(), ReflectionSettings{}, true, 2u);
    EXPECT_GT(next.generation, old.generation);
    EXPECT_EQ(next.resetReason, ReflectionFeedbackResetReason::ResourcesChanged);
    ASSERT_TRUE(state.reserve(next));
    state.discard(old);
    EXPECT_FALSE(state.accept(old, Token(1u), true));
    auto token = Token(1u);
    token.deviceGeneration = 2u;
    EXPECT_TRUE(state.accept(next, token, true));
    EXPECT_TRUE(state.plan(Stamp(), ReflectionSettings{}, true, 3u).reused);
}

TEST(ReflectionFeedback, RejectedGraphAndResetReleaseTheMoveOnlyReservation){
    Core::Alloc::GlobalArena arena{Name("tests/reflection/feedback_graph")};
    auto control = CreateReflectionFeedbackControl(arena, 1u);
    ASSERT_TRUE(control);
    Core::GpuTaskGraph graph(arena);
    const auto rejected = control->plan(Stamp(), ReflectionSettings{}, true, 1u);
    EXPECT_FALSE(graph.addTask<LeaseTask>(Core::GpuTaskDesc{}, LeaseTask::Payload{ReflectionFeedbackReservation(control, rejected)}).valid());
    Core::GpuTaskDesc desc;
    desc.setIdentity(Name("tests.feedback.lease")).setMarkerLabel("Feedback Lease");
    const auto pending = control->plan(Stamp(), ReflectionSettings{}, true, 2u);
    ASSERT_TRUE(graph.addTask<LeaseTask>(desc, LeaseTask::Payload{ReflectionFeedbackReservation(control, pending)}).valid());
    graph.reset();
    const auto retry = control->plan(Stamp(), ReflectionSettings{}, true, 3u);
    ASSERT_TRUE(control->reserve(retry));
    EXPECT_EQ(retry.probeIndex, 0u);
    control->discard(retry);
}

TEST(ReflectionFeedback, MovedLeasePublishesOnceAndInvalidAcceptanceRetainsQuarantineAfterDestruction){
    Core::Alloc::GlobalArena arena{Name("tests/reflection/feedback_move")};
    auto control = CreateReflectionFeedbackControl(arena, 1u);
    const auto first = control->plan(Stamp(), ReflectionSettings{}, true, 1u);
    ReflectionFeedbackReservation original(control, first);
    ASSERT_TRUE(original.valid());
    ReflectionFeedbackReservation moved(Move(original));
    EXPECT_FALSE(original.valid());
    EXPECT_TRUE(moved.valid());
    moved.accept(Token(first.sequence), true);
    EXPECT_FALSE(moved.valid());
    moved.accept(Token(99u), true);
    const auto retry = control->plan(Stamp(), ReflectionSettings{}, true, 2u);
    EXPECT_EQ(retry.probeIndex, 1u);
    {
        ReflectionFeedbackReservation accepted(control, retry);
        ASSERT_TRUE(accepted.valid());
        accepted.accept({}, true);
    }
    const auto quarantined = control->plan(Stamp(), ReflectionSettings{}, true, 3u);
    EXPECT_TRUE(quarantined.quarantined);
    ReflectionFeedbackReservation blocked(control, quarantined);
    EXPECT_FALSE(blocked.valid());
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

