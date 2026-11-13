// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#pragma once


#include "api.h"
#include "gpu_timing_accumulator.h"
#include "gpu_timing_frame_transaction.h"
#include "gpu_timing_metric_correlator.h"
#include "gpu_timing_submission_ticket.h"
#include "gpu_timing_types.h"

#include <core/perf/timing.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_CORE_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


class GpuTimingFrameTransaction;
class GpuTimingMeasure;
class GpuTimingRecorder;
class GpuTimingSubmissionTicket;
class GpuGraphSubmissionTransaction;
class GpuRecordedGraph;
class GpuTaskScheduler;


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


class GpuTimingRecorder final : NoCopy{
    friend class GpuTimingAccumulator;
    friend class GpuTimingFrameTransaction;
    friend class GpuTimingMeasure;
    friend class GpuTimingSubmissionTicket;


private:
    class SampleCallbackCompletionScope;

    struct QueueCompletion{
        GpuPhysicalQueueId queue;
        u64 value = 0u;
    };

    struct SampleListenerRecordData{
        Vector<Name, Alloc::GlobalArena> feedbackScopes;
        GpuTimingSampleSubscription subscription;
        GpuTimingSampleListener listener;
        b8 removing = false;
        u32 activeCallbackCount = 0u;


    public:
        explicit SampleListenerRecordData(Alloc::GlobalArena& arena)noexcept
            : feedbackScopes(arena)
        {}
    };

    struct FeedbackScopeDemand{
        u64 ownerCount = 0u;
        Name scopeName = s_NameNone;
    };


private:
    using AccumulatorPtr = GlobalUniquePtr<GpuTimingAccumulator>;
    using AccumulatorMap = HashMap<Name, AccumulatorPtr, Alloc::GlobalArena, Hasher<Name>, EqualTo<Name>>;
    using SampleListenerRecord = RefCounter<SampleListenerRecordData>;
    using SampleListenerRecordPtr = RefCountPtr< SampleListenerRecord, ::ArenaRefDeleter<SampleListenerRecord, Alloc::GlobalArena> >;
    using SampleListenerVector = Vector<SampleListenerRecordPtr, Alloc::GlobalArena>;
    using FeedbackScopeDemandVector = Vector<FeedbackScopeDemand, Alloc::GlobalArena>;
    using SampleDispatch = GpuTimingAccumulator::SampleDispatch;
    using SampleDispatchVector = GpuTimingAccumulator::SampleDispatchVector;


private:
    static thread_local GpuTimingSubmissionTicket* s_ActiveSubmissionTicket;


public:
    GpuTimingRecorder(Alloc::GlobalArena& arena, Perf::TimingSink& timing);


public:
    void setQueryCollectionEnabled(b8 enabled);
    // Every valid registration receives attributed samples captured in a dispatch batch.
    // New listeners begin with the next batch; removing one registration never replaces or clears another consumer.
    [[nodiscard]] Expected<GpuTimingSampleSubscription> subscribeSampleListener(const GpuTimingSampleListener& listener);
    // Callback-free: attribution retirement waits for collect(), keeping the no-throw boundary while a listener stays subscribed.
    void unsubscribeSampleListener(const GpuTimingSampleSubscription& subscription)noexcept;
    // Atomically replaces one listener's feedback-only query scopes.
    // Names are copied and must be valid and unique.
    // Broad Perf capture still enables every prepared scope; feedback demand only enables its named query scopes.
    [[nodiscard]] b8 setFeedbackCollectionScopes(const GpuTimingSampleSubscription& subscription, Span<const Name> scopeNames);
    [[nodiscard]] b8 clearFeedbackCollectionScopes(const GpuTimingSampleSubscription& subscription);
    // Globally unique attribution identities remain unique across recorder reset, destruction, and recreation.
    // Allocation is lock-free so recording workers issue identities without listener/query locks.
    [[nodiscard]] Expected<GpuTimingSampleAttribution> allocateSampleAttribution()noexcept;
    // True when either normal Perf capture or an adaptive feedback consumer needs query pools to be materialized and reset for the current frame.
    [[nodiscard]] b8 collectionActive()const noexcept;
    [[nodiscard]] GpuTimingRecorderStatistics statistics(const Device& device)const noexcept;
    void resetQueries();
    void collect(Device& device);
    void collect(Device& device, u64 publishFrameIndex);
    void beginFrame(u64 frameIndex)noexcept;
    // Declares the capacity a scope needs.
    // When capture is inactive this only records the request; a later activation materializes pools before its first frame preamble.
    [[nodiscard]] b8 prepareScopeQueries(const Name& scopeName, Device& device, u32 queryCount);
    // Declares the capacity a scope needs without touching the device.
    // Packet recording runs in the render path, so it uses this: pools materialize only in the frame preamble.
    // A newly declared scope skips samples until the next preamble materializes its pool (see QueryCapacityUnavailable).
    [[nodiscard]] b8 requestScopeQueries(const Name& scopeName, u32 queryCount);
    // Publishes the timestamp intersection of an unordered pair of packet envelopes.
    // Reversed exact duplicates are idempotent.
    // An output name is exclusive: not an input or query scope, and an input cannot later become an output.
    // A zero sample means both completed without overlap; no sample publishes when either was rejected.
    [[nodiscard]] b8 prepareOverlapMetric(const Name& firstScope, const Name& secondScope, const Name& outputScope);
    // Correlates one compiler-selected packet envelope by source frame.
    // Inputs and outputs are copied before returning because plans and caller scratch may be rebuilt immediately.
    // Publication needs one accepted comparable range per declared packet; queue idle covers internal holes only.
    [[nodiscard]] b8 preparePacketEnvelopeMetrics(
        u64 sourceFrameIndex,
        Span<const GpuPacketEnvelopeMetricScope> scopeInputs,
        const Name& queueOverlapScope,
        Span<const GpuPacketEnvelopeMetricQueueOutput> queueOutputInputs
    );
    // Materializes every declared scope during Graphics' frame preamble.
    // Capture toggles at runtime, but recording never creates a pool: declare scopes/capacity via requestScopeQueries().
    [[nodiscard]] b8 materializeRequestedQueries(Device& device);
    // Device-timeline reset of available timer pools onto a reset-capable list (frame preamble).
    // Ordered external reset replaces the unorderable host-side reset.
    // confirmFrameReset() only after successful submit.
    void recordFrameReset(CommandList& commandList);
    void confirmFrameReset(const QueueSubmissionToken& token);
    void discardFrameReset()noexcept;


private:
    [[nodiscard]] b8 replaceFeedbackCollectionScopes(const GpuTimingSampleSubscription& subscription, Span<const Name> scopeNames);
    [[nodiscard]] usize findFeedbackScopeDemandLocked(const Name& scopeName)const noexcept;
    [[nodiscard]] b8 feedbackScopeDemandedLocked(const Name& scopeName)const noexcept;
    void addFeedbackScopeDemandsLocked(Span<const Name> scopeNames)noexcept;
    void removeFeedbackScopeDemandsLocked(Span<const Name> scopeNames)noexcept;
    [[nodiscard]] Expected<GpuTimingScope> beginScope(const Name& scopeName, GpuTimingSampleAttribution attribution, b8 requiresComparableTimestamps, Device& device, CommandList& commandList);
    [[nodiscard]] Expected<GpuTimingScope> beginDeferredScope(const Name& scopeName, GpuTimingSampleAttribution attribution, Device& device, CommandList& commandList);
    void endScope(CommandList& commandList, GpuTimingScope& scope);
    void endScopeFromOpeningCommandList(CommandList& commandList, GpuTimingScope& scope)noexcept;
    [[nodiscard]] b8 recordDeferredScopeEnd(CommandList& commandList, const GpuTimingScope& scope);
    [[nodiscard]] b8 validateScopeSubmission(const GpuTimingScope& scope, const QueueSubmissionToken& token)noexcept;
    [[nodiscard]] b8 confirmScope(const GpuTimingScope& scope, const QueueSubmissionToken& token, b8 publishSample)noexcept;
    [[nodiscard]] b8 prepareDeferredScopeForRecovery(const GpuTimingScope& scope)noexcept;
    void discardScope(GpuTimingScope& scope);
    void abandonScopeWithoutCallbacks(GpuTimingScope& scope)noexcept;
    void quarantineScope(const GpuTimingScope& scope)noexcept;
    [[nodiscard]] GpuTimingSubmissionTicket* activeSubmissionTicket()const noexcept;
    [[nodiscard]] GpuTimingAccumulator* findAccumulator(const GpuTimingScope& scope)noexcept;
    // Metric-output scopes have no independent accumulator; successful absence is distinct from creation failure.
    [[nodiscard]] Expected<Optional<NotNull<GpuTimingAccumulator*>>> findOrCreateAccumulator(const Name& scopeName);
    [[nodiscard]] b8 collectLocked(u64 subscriptionIdentityLimit, Device& device, Alloc::ScratchArena& scratchArena, SampleDispatchVector& completedSamples, GpuTimingSinkSampleVector& performanceSamples);
    [[nodiscard]] b8 submissionCompleted(Device& device, const QueueSubmissionToken& token);
    [[nodiscard]] SampleListenerRecordPtr findSampleListenerLocked(const GpuTimingSampleSubscription& subscription)noexcept;
    [[nodiscard]] u64 sampleSubscriptionIdentityLimitLocked()const noexcept;
    void publishSampleSubscriptionIdentityLimitLocked()noexcept;
    void eraseSampleListenerLocked(SampleListenerRecord& record)noexcept;
    void dispatchCompletedSample(const GpuTimingSample& sample, u64 subscriptionIdentityLimit);
    void dispatchCompletedSamples(Span<const SampleDispatch> samples);
    void reservePendingAttributionSamplesLocked(SampleDispatchVector& outSamples)const;
    void retireMarkedPendingAttributionsLocked(SampleDispatchVector& outSamples);
    void discardMarkedPendingAttributionsLocked()noexcept;
    void retirePendingAttributionsLocked(u64 subscriptionIdentityLimit, SampleDispatchVector& outSamples);
    void discardFrameResetLocked()noexcept;
    void noteSkippedScope(GpuTimingScopeSkipReason::Enum reason)noexcept;
    void syncActiveState(u64 subscriptionIdentityLimit)noexcept;
    void syncActiveState()noexcept;
    void advancePerformanceCaptureEpoch()noexcept;
    void advanceEpoch()noexcept;


private:
    Alloc::GlobalArena& m_arena;
    Perf::TimingSink& m_timing;
    GpuTimingMetricCorrelator m_metricCorrelator;
    AccumulatorMap m_accumulators;
    Vector<QueueCompletion, Alloc::GlobalArena> m_queueCompletions;
    SampleListenerVector m_sampleListeners;
    FeedbackScopeDemandVector m_feedbackScopeDemands;
    // Callbacks execute outside this registration lock.
    // Whenever both state locks are needed, acquire this lock first and m_mutex second; no path retains m_mutex while acquiring this lock.
    Futex m_sampleListenerMutex;
    // A separate recursive gate preserves caller-owned context lifetime while allowing a callback to synchronously dispatch or remove registrations.
    // No path acquires it while retaining either recorder state lock.
    RecursiveMutex m_sampleCallbackMutex;
    // A submission ticket protects its own rollback list, while this lock serializes every query-pool mutation and recorder-map access.
    // Worker command-list recordings may share a ticket and begin timing scopes concurrently.
    mutable Futex m_mutex;
    // Serializes collection batches through external sink publication without retaining recorder state locks.
    // The sink may therefore inspect ordinary recorder state, while concurrent collectors preserve sample/frame order.
    Futex m_collectionMutex;
    Atomic<u64> m_sampleSubscriptionIdentityLimit{ 0u };
    u64 m_performanceCaptureEpoch = 1u;
    u64 m_currentFrameIndex = 0u;
    GpuTimingRecorderStatistics m_statistics;
    u32 m_epoch = 1u;
    b8 m_pendingAttributionRetirements = false;
    b8 m_accumulatorsActive = false;
    b8 m_performanceCollectionActive = false;
    b8 m_enabled = false;
};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


class GpuTimingMeasure final : NoCopy{
public:
    // Out-of-line: ctor/dtor own a marker lease; CommandList is forward-declared here and completed in the .cpp.
    GpuTimingMeasure(GpuTimingRecorder& recorder, const GpuTimingScopeDefinition& scopeDefinition, Device& device, CommandList& commandList, GpuTimingSampleAttribution attribution = s_NoGpuTimingSampleAttribution);
    ~GpuTimingMeasure()noexcept;


public:
    // A scope may span ordered primaries. Close the marker lease on the opener before it closes, then stamp the end on the later list.
    [[nodiscard]] b8 finishMarker();
    // Recording failure recovery can reset the command list's marker stack before this object unwinds.
    // Relinquish marker ownership in that path so the destructor does not emit an unmatched marker end after recovery.
    void abandonMarker()noexcept;
    void finishTiming(CommandList& commandList);
    // Discards a started scope when its producer command buffer cannot be finalized or submitted.
    void discardTiming(){ m_recorder.discardScope(m_scope); }
    void abandonTimingWithoutCallbacks()noexcept{ m_recorder.abandonScopeWithoutCallbacks(m_scope); }

    [[nodiscard]] b8 valid()const noexcept{ return m_scope.valid(); }


private:
    GpuTimingRecorder& m_recorder;
    CommandList& m_commandList;
    GpuTimingScope m_scope;
    CommandMarkerRecordingToken m_marker;
};

void DiscardGpuTimingMeasure(Optional<GpuTimingMeasure>* timing);
void DiscardGpuTimingMeasure(Optional<GpuTimingMeasure>& timing);

// Closes only the marker half of a split scope before its ending timestamp is published.
// A marker failure discards that unpublished timing scope.
// After finishTiming(), callers must instead roll back through the owning ticket.
[[nodiscard]] b8 FinishSplitGpuTimingMarker(Optional<GpuTimingMeasure>* timing);
[[nodiscard]] b8 FinishSplitGpuTimingMarker(Optional<GpuTimingMeasure>& timing);


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_CORE_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

