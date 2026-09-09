// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include <impl/ecs_render/reflection/history.h>
#include <impl/assets/graphics/reflection/frame_constants.h>
#include <impl/ecs_render/reflection/statistics_readback.h>

#include <core/alloc/general.h>
#include <core/task/gpu/task_graph.h>

#include <gtest/gtest.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace __hidden_reflection_history_tests{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


using namespace NWB;
using namespace NWB::Impl;

[[nodiscard]] ReflectionSceneContentStamp Stamp(){
    return ReflectionSceneContentStamp{1u, 2u, 3u, 4u, true};
}

[[nodiscard]] Core::QueueSubmissionToken Token(const u64 value = 1u){
    return Core::QueueSubmissionToken{Core::CommandQueue::Graphics, value, 0u, 1u};
}

void Accept(ReflectionHistoryState& state, const ReflectionHistoryPlan& plan, const bool hardwareReady = true){
    ASSERT_TRUE(state.reserve(plan));
    state.accept(plan, Token(plan.sequence), hardwareReady);
}

struct LeaseTask{
    struct Payload{
        ReflectionHistoryReservation reservation;
    };
    static void discarded(Payload& payload){ payload.reservation.discard(); }
};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


TEST(ReflectionHistory, FirstAcceptedSampleStartsHistoryAndNextFrameSelectsOtherBank){
    ReflectionHistoryState state(1u);
    const auto first = state.plan(Stamp(), ReflectionSettings{}, 100u);
    EXPECT_TRUE(first.eligible);
    EXPECT_TRUE(first.reset);
    EXPECT_FALSE(first.reused);
    EXPECT_EQ(first.sampleIndex, 0u);
    EXPECT_EQ(first.currentBank, 0u);
    const auto outcome = ResolveReflectionHistoryOutcome(first, true);
    EXPECT_EQ(outcome.sampleCount, 1u);
    EXPECT_EQ(outcome.historyStartGraphicsFrame, 100u);
    Accept(state, first);
    const auto next = state.plan(Stamp(), ReflectionSettings{}, 101u);
    EXPECT_TRUE(next.reused);
    EXPECT_FALSE(next.reset);
    EXPECT_EQ(next.previousSampleCount, 1u);
    EXPECT_EQ(next.sampleIndex, 1u);
    EXPECT_EQ(next.currentBank, 1u);
    EXPECT_EQ(next.previousBank, 0u);
    EXPECT_EQ(next.acceptedSequence, first.sequence);
}

TEST(ReflectionHistory, AcceptedSampleCountCapsWhileSamplingIndexContinues){
    ReflectionHistoryState state(1u);
    ReflectionSettings settings;
    settings.temporalMaxSamples = 8u;
    for(u32 index = 0u; index < 40u; ++index){
        const auto plan = state.plan(Stamp(), settings, 100u + index);
        const auto outcome = ResolveReflectionHistoryOutcome(plan, true);
        EXPECT_EQ(outcome.sampleIndex, index);
        EXPECT_EQ(outcome.sampleCount, Min(index + 1u, 8u));
        EXPECT_EQ(outcome.historyStartGraphicsFrame, 100u);
        Accept(state, plan);
    }
}

TEST(ReflectionHistory, DiscardDoesNotAdvanceSampleBankCountOrEpoch){
    ReflectionHistoryState state(1u);
    Accept(state, state.plan(Stamp(), ReflectionSettings{}, 100u));
    const auto rejected = state.plan(Stamp(), ReflectionSettings{}, 101u);
    ASSERT_TRUE(state.reserve(rejected));
    state.discard(rejected);
    const auto retry = state.plan(Stamp(), ReflectionSettings{}, 102u);
    EXPECT_EQ(retry.sampleIndex, rejected.sampleIndex);
    EXPECT_EQ(retry.epoch, rejected.epoch);
    EXPECT_EQ(retry.previousSampleCount, rejected.previousSampleCount);
    EXPECT_EQ(retry.currentBank, rejected.currentBank);
}

TEST(ReflectionHistory, EverySceneDomainAndViewChangeResetsOnce){
    for(u32 domain = 0u; domain < 4u; ++domain){
        ReflectionHistoryState state(1u);
        Accept(state, state.plan(Stamp(), ReflectionSettings{}, 10u));
        auto stamp = Stamp();
        if(domain == 0u)
            ++stamp.geometry;
        else if(domain == 1u)
            ++stamp.material;
        else if(domain == 2u)
            ++stamp.lighting;
        else
            ++stamp.view;
        const auto changed = state.plan(stamp, ReflectionSettings{}, 11u);
        EXPECT_TRUE(changed.reset);
        EXPECT_EQ(changed.sampleIndex, 0u);
        EXPECT_FALSE(changed.reused);
        EXPECT_EQ(changed.resetReason, domain == 3u ? ReflectionHistoryResetReason::ViewChanged : ReflectionHistoryResetReason::SceneChanged);
        Accept(state, changed);
        const auto next = state.plan(stamp, ReflectionSettings{}, 12u);
        EXPECT_FALSE(next.reset);
        EXPECT_EQ(next.sampleIndex, 1u);
        EXPECT_EQ(next.epoch, changed.epoch);
        EXPECT_EQ(ResolveReflectionHistoryOutcome(next, true).historyStartGraphicsFrame, 11u);
    }
}

TEST(ReflectionHistory, SeedAndBudgetChangesRestartTheSamplingSequence){
    ReflectionHistoryState state(1u);
    ReflectionSettings settings;
    Accept(state, state.plan(Stamp(), settings, 1u));
    ++settings.samplingSeed;
    const auto seed = state.plan(Stamp(), settings, 2u);
    EXPECT_EQ(seed.resetReason, ReflectionHistoryResetReason::SettingsChanged);
    EXPECT_EQ(seed.sampleIndex, 0u);
    Accept(state, seed);
    settings.maxHardwareRaysPerFrame = 0u;
    const auto budget = state.plan(Stamp(), settings, 3u);
    EXPECT_EQ(budget.resetReason, ReflectionHistoryResetReason::SettingsChanged);
    EXPECT_EQ(budget.sampleIndex, 0u);
    Accept(state, budget);
    --settings.maxOpticalQueries;
    const auto queries = state.plan(Stamp(), settings, 4u);
    EXPECT_EQ(queries.resetReason, ReflectionHistoryResetReason::SettingsChanged);
    EXPECT_EQ(queries.sampleIndex, 0u);
}

TEST(ReflectionHistory, SpatialAndDiagnosticSettingsDoNotResetUnfilteredHistory){
    ReflectionHistoryState state(1u);
    ReflectionSettings settings;
    Accept(state, state.plan(Stamp(), settings, 1u));
    settings.spatialFilterEnabled = false;
    settings.spatialRadius = 3u;
    settings.diagnosticsEnabled = true;
    settings.debugView = ReflectionDebugView::TraceSource;
    const auto next = state.plan(Stamp(), settings, 2u);
    EXPECT_TRUE(next.reused);
    EXPECT_FALSE(next.reset);
    EXPECT_EQ(next.resetReason, ReflectionHistoryResetReason::None);
}

TEST(ReflectionHistory, RejectedDisabledTransitionCannotOverwriteAcceptedHistoryBank){
    ReflectionHistoryState state(1u);
    ReflectionSettings settings;
    const auto first = state.plan(Stamp(), settings, 1u);
    Accept(state, first);
    settings.temporalEnabled = false;
    const auto disabled = state.plan(Stamp(), settings, 2u);
    EXPECT_FALSE(disabled.eligible);
    EXPECT_NE(disabled.currentBank, first.currentBank);
    ASSERT_TRUE(state.reserve(disabled));
    state.discard(disabled);
    settings.temporalEnabled = true;
    const auto retry = state.plan(Stamp(), settings, 3u);
    EXPECT_TRUE(retry.reused);
    EXPECT_EQ(retry.previousBank, first.currentBank);
    EXPECT_EQ(retry.previousSampleCount, 1u);
}

TEST(ReflectionHistory, RejectedUntrustedTransitionCannotOverwriteAcceptedHistoryBank){
    ReflectionHistoryState state(1u);
    const auto first = state.plan(Stamp(), ReflectionSettings{}, 1u);
    Accept(state, first);
    auto stamp = Stamp();
    stamp.trusted = false;
    const auto untrusted = state.plan(stamp, ReflectionSettings{}, 2u);
    EXPECT_FALSE(untrusted.eligible);
    EXPECT_NE(untrusted.currentBank, first.currentBank);
    ASSERT_TRUE(state.reserve(untrusted));
    state.discard(untrusted);
    EXPECT_TRUE(state.plan(Stamp(), ReflectionSettings{}, 3u).reused);
}

TEST(ReflectionHistory, StableUntrustedSceneNeverAccumulatesButDoesAdvanceAcceptedSampling){
    ReflectionHistoryState state(1u);
    auto stamp = Stamp();
    stamp.trusted = false;
    const auto first = state.plan(stamp, ReflectionSettings{}, 1u);
    EXPECT_EQ(first.resetReason, ReflectionHistoryResetReason::UntrustedScene);
    EXPECT_EQ(ResolveReflectionHistoryOutcome(first, false).sampleCount, 0u);
    Accept(state, first, false);
    const auto next = state.plan(stamp, ReflectionSettings{}, 2u);
    EXPECT_FALSE(next.reset);
    EXPECT_FALSE(next.eligible);
    EXPECT_EQ(next.resetReason, ReflectionHistoryResetReason::None);
    EXPECT_EQ(next.sampleIndex, 1u);
    EXPECT_EQ(ResolveReflectionHistoryOutcome(next, false).historyStartGraphicsFrame, 0u);
}

TEST(ReflectionHistory, ActualHardwareReadinessResetMatchesNextAcceptedState){
    ReflectionHistoryState state(1u);
    Accept(state, state.plan(Stamp(), ReflectionSettings{}, 10u), true);
    const auto next = state.plan(Stamp(), ReflectionSettings{}, 11u);
    const auto outcome = ResolveReflectionHistoryOutcome(next, false);
    EXPECT_EQ(outcome.resetReason, ReflectionHistoryResetReason::HardwareChanged);
    EXPECT_EQ(outcome.sampleIndex, 0u);
    EXPECT_EQ(outcome.sampleCount, 1u);
    EXPECT_FALSE(outcome.reused);
    Accept(state, next, false);
    const auto stable = state.plan(Stamp(), ReflectionSettings{}, 12u);
    const auto stableOutcome = ResolveReflectionHistoryOutcome(stable, false);
    EXPECT_EQ(stableOutcome.epoch, outcome.epoch);
    EXPECT_FALSE(stableOutcome.reset);
    EXPECT_EQ(stableOutcome.sampleIndex, 1u);
    EXPECT_EQ(stableOutcome.sampleCount, 2u);
}

TEST(ReflectionHistory, StalePlanCannotReserveAfterAnotherFrameAccepts){
    ReflectionHistoryState state(1u);
    const auto old = state.plan(Stamp(), ReflectionSettings{}, 1u);
    const auto accepted = state.plan(Stamp(), ReflectionSettings{}, 2u);
    Accept(state, accepted);
    EXPECT_FALSE(state.reserve(old));
    state.discard(old);
    state.accept(old, Token(99u), true);
    EXPECT_EQ(state.plan(Stamp(), ReflectionSettings{}, 3u).acceptedSequence, accepted.sequence);
}

TEST(ReflectionHistory, ResourceGenerationRejectsOldAcceptanceAndResetsEpoch){
    ReflectionHistoryState state(1u);
    const auto old = state.plan(Stamp(), ReflectionSettings{}, 1u);
    ASSERT_TRUE(state.reserve(old));
    state.reset(2u);
    state.accept(old, Token(), true);
    const auto next = state.plan(Stamp(), ReflectionSettings{}, 2u);
    EXPECT_GT(next.generation, old.generation);
    EXPECT_EQ(next.resetReason, ReflectionHistoryResetReason::ResourcesChanged);
    EXPECT_EQ(next.sampleIndex, 0u);
    ASSERT_TRUE(state.reserve(next));
    Core::QueueSubmissionToken token = Token();
    token.deviceGeneration = 2u;
    state.accept(next, token, false);
    EXPECT_EQ(state.plan(Stamp(), ReflectionSettings{}, 3u).previousSampleCount, 1u);
}

TEST(ReflectionHistory, GraphRejectionAndResetReleaseUnacceptedLease){
    Core::Alloc::GlobalArena arena{Name("tests/reflection/history_lease")};
    auto control = CreateReflectionHistoryControl(arena, 1u);
    ASSERT_TRUE(control);
    Core::GpuTaskGraph graph(arena);
    const auto rejected = control->plan(Stamp(), ReflectionSettings{}, 1u);
    EXPECT_FALSE(graph.addTask<LeaseTask>(Core::GpuTaskDesc{}, LeaseTask::Payload{ReflectionHistoryReservation(control, rejected)}).valid());
    Core::GpuTaskDesc desc;
    desc.setIdentity(Name("tests.history.lease")).setMarkerLabel("History Lease");
    const auto pending = control->plan(Stamp(), ReflectionSettings{}, 2u);
    ASSERT_TRUE(graph.addTask<LeaseTask>(desc, LeaseTask::Payload{ReflectionHistoryReservation(control, pending)}).valid());
    graph.reset();
    const auto retry = control->plan(Stamp(), ReflectionSettings{}, 3u);
    EXPECT_TRUE(control->reserve(retry));
    EXPECT_EQ(retry.sampleIndex, 0u);
}

TEST(ReflectionHistory, CompletedStatisticsRetainEpochStartAndResolvedSamplingMetadata){
    ReflectionHistoryState history(1u);
    const auto plan = history.plan(Stamp(), ReflectionSettings{}, 500u);
    const auto outcome = ResolveReflectionHistoryOutcome(plan, true);
    ReflectionStatisticsState statistics(1u);
    ReflectionStatistics metadata;
    metadata.graphicsFrameIndex = 500u;
    metadata.samplingSeed = 77u;
    const auto key = statistics.reserve(metadata);
    statistics.accept(key, Token(), true, &outcome);
    const u32 counters[NWB_REFLECTION_COUNTER_SIZE / sizeof(u32)] = {};
    statistics.complete(key, Token(), counters);
    ReflectionStatistics completed;
    ASSERT_TRUE(statistics.tryGetLatestStatistics(completed));
    EXPECT_EQ(completed.graphicsFrameIndex, 500u);
    EXPECT_EQ(completed.historyStartGraphicsFrame, 500u);
    EXPECT_EQ(completed.historyEpoch, outcome.epoch);
    EXPECT_EQ(completed.historySampleCount, 1u);
    EXPECT_EQ(completed.sampleIndex, 0u);
    EXPECT_EQ(completed.samplingSeed, 77u);
    EXPECT_TRUE(completed.historyEligible);
    EXPECT_TRUE(completed.historyReset);
    EXPECT_FALSE(completed.historyReused);
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

