// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "telemetry_test_helpers.h"
#include "frame_graph_test_helpers.h"

#include <tests/unit/telemetry/perf_case_types.adhd.h>

#include <core/telemetry/frame_graph_registry.h>

#include <gtest/gtest.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace __hidden_telemetry_frame_graph_registry_tests{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace TestCases = NWB::Tests::TelemetryCases;

static constexpr AStringView s_Target = "target";


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


using namespace TelemetryTestDetail;

class PendingNameFrameGraphContributor final : public Telemetry::IFrameGraphContributor{
public:
    virtual b8 appendFrameGraph(Telemetry::FrameGraphBuilder& builder)override{
        const Telemetry::FrameGraphNodeHandle source = builder.addPass(Name(TestCases::s_TelemetryCaseSource), "Source");
        const Telemetry::FrameGraphNodeHandle target = builder.addResource(Name(s_Target), "Target");
        const Telemetry::FrameGraphNodeHandle duplicateTarget = builder.addResource(Name(s_Target), "Duplicate Target");
        if(!target.valid() || !duplicateTarget.valid())
            return false;

        builder.dependsOnByName(source, Name(s_Target), 7u);
        builder.dependsOnByName(source, Name(TestCases::s_TelemetryCaseMissing), 9u);
        return true;
    }
};

struct ContributionFailure{};

class LocalLabelContributor final : public Telemetry::IFrameGraphContributor{
public:
    virtual b8 appendFrameGraph(Telemetry::FrameGraphBuilder& builder)override{
        char label[] = "local label";
        const auto node = builder.addPass(Name(TestCases::s_TelemetryCaseLocalLabel), AStringView(label));
        label[0u] = 'X';
        return node.valid();
    }
};

class MutatingContributor final : public Telemetry::IFrameGraphContributor{
public:
    explicit MutatingContributor(Telemetry::FrameGraphRegistry& registry)noexcept
        : m_registry(registry)
    {}

    virtual b8 appendFrameGraph(Telemetry::FrameGraphBuilder& builder)override{
        ++calls;
        const auto node = builder.addPass(Name(TestCases::s_TelemetryCaseMutation), "mutation");
        if(clearRegistry)
            m_registry.clear();
        if(removeSelf)
            m_registry.unregisterContributor(*this);
        if(removeOther)
            m_registry.unregisterContributor(*removeOther);
        if(addOther)
            m_registry.registerContributor(*addOther);
        if(throwAfterMutation)
            throw ContributionFailure{};
        return node.valid();
    }

    Telemetry::IFrameGraphContributor* removeOther = nullptr;
    Telemetry::IFrameGraphContributor* addOther = nullptr;
    usize calls = 0u;
    b8 removeSelf = false;
    b8 clearRegistry = false;
    b8 throwAfterMutation = false;

private:
    Telemetry::FrameGraphRegistry& m_registry;
};

class OwnedLabelContributor final : public Telemetry::IFrameGraphContributor{
public:
    OwnedLabelContributor(Telemetry::FrameGraphRegistry& registry, Telemetry::TelemetryArena& arena)
        : m_registry(registry)
        , m_label("label retained after contributor destruction", arena)
    {}

    virtual b8 appendFrameGraph(Telemetry::FrameGraphBuilder& builder)override{
        const auto node = builder.addPass(Name(TestCases::s_TelemetryCaseOwnedLabel), AStringView(m_label));
        m_registry.unregisterContributor(*this);
        return node.valid();
    }

private:
    Telemetry::FrameGraphRegistry& m_registry;
    AString<Telemetry::TelemetryArena> m_label;
};

class DestroyContributor final : public Telemetry::IFrameGraphContributor{
public:
    explicit DestroyContributor(Optional<OwnedLabelContributor>& target)noexcept
        : m_target(target)
    {}

    virtual b8 appendFrameGraph(Telemetry::FrameGraphBuilder& builder)override{
        m_target.reset();
        return builder.addPass(Name(TestCases::s_TelemetryCaseDestroyer), "destroyer").valid();
    }

private:
    Optional<OwnedLabelContributor>& m_target;
};

class CacheLifetimeContributor final : public Telemetry::IFrameGraphContributor{
public:
    CacheLifetimeContributor(Telemetry::FrameGraphRegistry& registry, Telemetry::CaptureSession& session, const AString<Telemetry::TelemetryArena>& label)noexcept
        : m_registry(registry)
        , m_session(session)
        , m_label(label)
    {}


public:
    virtual b8 appendFrameGraph(Telemetry::FrameGraphBuilder& builder)override{
        if(!builder.addPass(Name(TestCases::s_TelemetryCaseCacheLifetime), AStringView(m_label)).valid())
            return false;
        if(m_throwNext){
            m_throwNext = false;
            throw ContributionFailure{};
        }
        m_reentryRejected = !m_registry.record(m_session);
        return true;
    }


public:
    b8 m_throwNext = true;
    b8 m_reentryRejected = false;


private:
    Telemetry::FrameGraphRegistry& m_registry;
    Telemetry::CaptureSession& m_session;
    const AString<Telemetry::TelemetryArena>& m_label;
};

TEST(Telemetry, RegistryOwnsLabelsFromContributorCallbacks){
    TestArena testArena;
    Telemetry::CaptureSession session(testArena.arena);
    session.setCaptureOptions(Telemetry::CaptureOptions::FrameGraphOnly());
    Telemetry::FrameGraphRegistry registry(testArena.arena);
    LocalLabelContributor contributor;
    registry.registerContributor(contributor);
    ASSERT_TRUE(registry.record(session));
    const auto* event = session.view().eventAt(0u);
    ASSERT_NE(event, nullptr);
    const auto parsed = Telemetry::ParseFrameGraphPayload(testArena.arena, testArena.scratchArena, event->payload.data(), event->payload.size());
    ASSERT_TRUE(parsed);
    ASSERT_EQ(parsed->nodes.size(), 1u);
    EXPECT_EQ(parsed->nodes[0u].label, "local label");
}

TEST(Telemetry, RegistryDefersAddedContributorsAndSkipsRemovedOnes){
    TestArena testArena;
    Telemetry::CaptureSession session(testArena.arena);
    session.setCaptureOptions(Telemetry::CaptureOptions::FrameGraphOnly());
    Telemetry::FrameGraphRegistry registry(testArena.arena);
    MutatingContributor first(registry), removed(registry), last(registry), added(registry);
    first.removeSelf = true;
    first.removeOther = &removed;
    first.addOther = &added;
    registry.registerContributor(first);
    registry.registerContributor(removed);
    registry.registerContributor(last);

    ASSERT_TRUE(registry.record(session));
    EXPECT_EQ(first.calls, 1u);
    EXPECT_EQ(removed.calls, 0u);
    EXPECT_EQ(last.calls, 1u);
    EXPECT_EQ(added.calls, 0u);
    ASSERT_TRUE(registry.record(session));
    EXPECT_EQ(first.calls, 1u);
    EXPECT_EQ(removed.calls, 0u);
    EXPECT_EQ(last.calls, 2u);
    EXPECT_EQ(added.calls, 1u);
}

TEST(Telemetry, RegistryKeepsLabelsAfterRemovedContributorIsDestroyed){
    TestArena testArena;
    Telemetry::CaptureSession session(testArena.arena);
    session.setCaptureOptions(Telemetry::CaptureOptions::FrameGraphOnly());
    Telemetry::FrameGraphRegistry registry(testArena.arena);
    Optional<OwnedLabelContributor> first;
    first.emplace(registry, testArena.arena);
    DestroyContributor destroyer(first);
    registry.registerContributor(*first);
    registry.registerContributor(destroyer);
    ASSERT_TRUE(registry.record(session));
    EXPECT_FALSE(first.has_value());
    const auto* event = session.view().eventAt(0u);
    ASSERT_NE(event, nullptr);
    const auto parsed = Telemetry::ParseFrameGraphPayload(testArena.arena, testArena.scratchArena, event->payload.data(), event->payload.size());
    ASSERT_TRUE(parsed);
    ASSERT_EQ(parsed->nodes.size(), 2u);
    EXPECT_EQ(parsed->nodes[0u].label, "label retained after contributor destruction");
}

TEST(Telemetry, RegistryClearDuringCallbackSkipsRemainingContributors){
    TestArena testArena;
    Telemetry::CaptureSession session(testArena.arena);
    session.setCaptureOptions(Telemetry::CaptureOptions::FrameGraphOnly());
    Telemetry::FrameGraphRegistry registry(testArena.arena);
    MutatingContributor first(registry), skipped(registry), added(registry);
    first.clearRegistry = true;
    first.addOther = &added;
    registry.registerContributor(first);
    registry.registerContributor(skipped);

    ASSERT_TRUE(registry.record(session));
    EXPECT_EQ(first.calls, 1u);
    EXPECT_EQ(skipped.calls, 0u);
    EXPECT_EQ(added.calls, 0u);
    ASSERT_TRUE(registry.record(session));
    EXPECT_EQ(first.calls, 1u);
    EXPECT_EQ(skipped.calls, 0u);
    EXPECT_EQ(added.calls, 1u);
}

TEST(Telemetry, RegistryCanRemoveAndReregisterTheCurrentContributor){
    TestArena testArena;
    Telemetry::CaptureSession session(testArena.arena);
    session.setCaptureOptions(Telemetry::CaptureOptions::FrameGraphOnly());
    Telemetry::FrameGraphRegistry registry(testArena.arena);
    MutatingContributor contributor(registry);
    contributor.removeSelf = true;
    contributor.addOther = &contributor;
    registry.registerContributor(contributor);
    ASSERT_TRUE(registry.record(session));
    EXPECT_EQ(contributor.calls, 1u);
    ASSERT_TRUE(registry.record(session));
    EXPECT_EQ(contributor.calls, 2u);
}

TEST(Telemetry, RegistryAppliesCallbackMutationsAfterUnwind){
    TestArena testArena;
    Telemetry::CaptureSession session(testArena.arena);
    session.setCaptureOptions(Telemetry::CaptureOptions::FrameGraphOnly());
    Telemetry::FrameGraphRegistry registry(testArena.arena);
    MutatingContributor first(registry), skipped(registry), added(registry);
    first.removeSelf = true;
    first.removeOther = &skipped;
    first.addOther = &added;
    first.throwAfterMutation = true;
    registry.registerContributor(first);
    registry.registerContributor(skipped);
    EXPECT_THROW(EXPECT_TRUE(registry.record(session)), ContributionFailure);
    EXPECT_EQ(session.eventCount(), 0u);
    ASSERT_TRUE(registry.record(session));
    EXPECT_EQ(first.calls, 1u);
    EXPECT_EQ(skipped.calls, 0u);
    EXPECT_EQ(added.calls, 1u);
}

TEST(Telemetry, RegistryClearsBorrowedDescriptorsAfterUnwindAndReusesGraphCapacity){
    TestArena testArena;
    Telemetry::CaptureSession session(testArena.arena);
    session.setCaptureOptions(Telemetry::CaptureOptions::FrameGraphOnly());
    Telemetry::FrameGraphRegistry registry(testArena.arena);
    AString<Telemetry::TelemetryArena> label("first borrowed label", testArena.arena);
    CacheLifetimeContributor contributor(registry, session, label);
    registry.registerContributor(contributor);
    EXPECT_THROW(EXPECT_TRUE(registry.record(session)), ContributionFailure);
    EXPECT_EQ(session.eventCount(), 0u);

    label.assign("replacement label after unwind");
    ASSERT_TRUE(registry.record(session));
    EXPECT_TRUE(contributor.m_reentryRejected);
    const auto* const event = session.view().eventAt(0u);
    ASSERT_NE(event, nullptr);
    const auto parsed = Telemetry::ParseFrameGraphPayload(testArena.arena, testArena.scratchArena, event->payload.data(), event->payload.size());
    ASSERT_TRUE(parsed);
    ASSERT_EQ(parsed->nodes.size(), 1u);
    EXPECT_EQ(AStringView(parsed->nodes[0u].label), AStringView(label));
}

TEST(Telemetry, PendingNameEdgesChooseFirstDuplicateAndOmitMissingTargets){
    TestArena testArena;
    Telemetry::CaptureSession session(testArena.arena);
    session.setCaptureOptions(Telemetry::CaptureOptions::FrameGraphOnly());

    Telemetry::FrameGraphRegistry registry(testArena.arena);
    PendingNameFrameGraphContributor contributor;
    registry.registerContributor(contributor);

    EXPECT_TRUE(registry.record(session));
    EXPECT_EQ(session.eventCount(), 1u);

    const Telemetry::EventRecord* event = session.view().eventAt(0u);
    ASSERT_NE(event, nullptr);

    Expected<Telemetry::FrameGraphPayload> parsed = MakeUnexpected(Failure{});
    EXPECT_TRUE((parsed = Telemetry::ParseFrameGraphPayload(testArena.arena, testArena.scratchArena, event->payload.data(), event->payload.size())));
    ASSERT_EQ(parsed->nodes.size(), 3u);
    ASSERT_EQ(parsed->edges.size(), 1u);
    EXPECT_EQ(parsed->edges[0u].fromNodeIndex, 0u);
    EXPECT_EQ(parsed->edges[0u].toNodeIndex, 1u);
}

TEST(Telemetry, PhysicalQueueBuilderOwnsSourceAndRejectsDuplicateOrExcessCounts){
    TestArena testArena;
    Telemetry::FrameGraphNodeDescs nodes(testArena.arena);
    Telemetry::FrameGraphEdgeDescs edges(testArena.arena);
    Telemetry::FrameGraphPendingNameEdges pendingNameEdges(testArena.arena);
    Telemetry::FrameGraphPhysicalQueueRuntimeStatisticsRecords records(testArena.arena);
    Telemetry::FrameGraphBuilder builder(testArena.scratchArena, nodes, edges, pendingNameEdges, records, 915u);

    const Telemetry::FrameGraphNodeHandle owner = builder.addPass(Name(TestCases::s_RuntimeOwner), "Runtime owner", Telemetry::FrameGraphPassMetadata{ .queueAssignment = {}, .compiledTask = {}, .runtimeStatistics = MakeFrameGraphRuntimeStatistics(), });
    Telemetry::FrameGraphPhysicalQueueRuntimeStatistics statistics = MakeFrameGraphPhysicalQueueRuntimeStatistics(1u);
    ASSERT_TRUE(builder.addPhysicalQueueRuntimeStatistics(owner, statistics));
    statistics.compile.taskCount = 0u;

    ASSERT_EQ(records.size(), 1u);
    EXPECT_EQ(records[0u].statistics.compile.taskCount, 50u);
    Telemetry::FrameGraphPhysicalQueueRuntimeStatistics outOfBounds = MakeFrameGraphPhysicalQueueRuntimeStatistics(3u);
    outOfBounds.compile.taskCount = 79u;
    outOfBounds.compile.packetCount = 78u;
    outOfBounds.compile.mergedTaskCount = 1u;
    EXPECT_FALSE(builder.addPhysicalQueueRuntimeStatistics(owner, outOfBounds));
    EXPECT_EQ(records.size(), 1u);

    Telemetry::FrameGraphPhysicalQueueRuntimeStatistics derivedOutOfBounds = MakeFrameGraphPhysicalQueueRuntimeStatistics(3u);
    derivedOutOfBounds.compile.taskCount = 29u;
    derivedOutOfBounds.compile.mergedTaskCount = 2u;
    derivedOutOfBounds.recording.taskCount = 13u;
    EXPECT_TRUE(Telemetry::IsValidFrameGraphPhysicalQueueRuntimeStatistics(derivedOutOfBounds));
    EXPECT_FALSE(Telemetry::IsValidFrameGraphPhysicalQueueRuntimeStatisticsForOwner(derivedOutOfBounds, MakeFrameGraphRuntimeStatistics()));
    EXPECT_FALSE(builder.addPhysicalQueueRuntimeStatistics(owner, derivedOutOfBounds));
    EXPECT_EQ(records.size(), 1u);

    Telemetry::FrameGraphPhysicalQueueRuntimeStatistics reorderedSubmissionDuration = MakeFrameGraphPhysicalQueueRuntimeStatistics(3u);
    reorderedSubmissionDuration.submission.submissionSeconds = 0.021000000000000004;
    EXPECT_TRUE(Telemetry::IsValidFrameGraphPhysicalQueueRuntimeStatisticsForOwner(reorderedSubmissionDuration, MakeFrameGraphRuntimeStatistics()));
    EXPECT_FALSE(builder.addPhysicalQueueRuntimeStatistics(owner, MakeFrameGraphPhysicalQueueRuntimeStatistics(1u)));
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

