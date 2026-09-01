// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#pragma once


#include "api.h"
#include "gpu_timing_metric_correlator.h"

#include <core/perf/timing.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_CORE_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


class GpuTimingAccumulator;
class GpuTimingFrameTransaction;
class GpuTimingMeasure;
class GpuTimingRecorder;
class GpuTimingSubmissionTicket;
class GpuTaskGraphSubmitter;

struct GpuTimingScope{
    Name scopeName = NAME_NONE;
    u32 index = Limit<u32>::s_Max;
    // Epoch and reservation distinguish a recreated accumulator and reused query-pool slot from an earlier scope.
    u32 epoch = 0u;
    u64 reservation = 0u;
    TimerQueryRecordingToken timerQueryRecording;
    GpuTimingSubmissionTicket* submissionTicket = nullptr;

    [[nodiscard]] bool valid()const{ return scopeName != NAME_NONE && index != Limit<u32>::s_Max && epoch != 0u && reservation != 0u; }
};

struct GpuTimingScopeDefinition{
    Name identity = NAME_NONE;
    AStringView markerLabel;


    constexpr GpuTimingScopeDefinition() = default;
    constexpr explicit GpuTimingScopeDefinition(const char* const label)
        : identity(label)
        , markerLabel(label)
    {}


    [[nodiscard]] constexpr bool valid()const{ return identity && !markerLabel.empty(); }
};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


// A recorder-issued opaque value retained from recording until the matching accepted GPU query completes. The
// default-invalid value keeps ordinary timing scopes out of the optional completed-sample listener path.
class GpuTimingSampleAttribution final{
    friend class GpuTimingRecorder;
    friend constexpr bool operator==(
        const GpuTimingSampleAttribution& lhs,
        const GpuTimingSampleAttribution& rhs
    )noexcept;


public:
    constexpr GpuTimingSampleAttribution() = default;


public:
    [[nodiscard]] constexpr bool valid()const noexcept{ return m_identity != 0u; }


private:
    constexpr explicit GpuTimingSampleAttribution(const u64 identity)
        : m_identity(identity)
    {}


private:
    u64 m_identity = 0u;
};

inline constexpr bool operator==(
    const GpuTimingSampleAttribution& lhs,
    const GpuTimingSampleAttribution& rhs
)noexcept{
    return lhs.m_identity == rhs.m_identity;
}
inline constexpr bool operator!=(
    const GpuTimingSampleAttribution& lhs,
    const GpuTimingSampleAttribution& rhs
)noexcept{
    return !(lhs == rhs);
}

inline constexpr GpuTimingSampleAttribution s_NoGpuTimingSampleAttribution;

struct GpuTimingSample{
    Name scopeName = NAME_NONE;
    u64 sourceFrameIndex = 0u;
    f64 durationSeconds = 0.0;
    // Exact accepted native queue for every attributed query outcome. This remains valid when the backend exposes
    // duration timestamps but cannot expose an absolute cross-submission comparable range.
    GpuPhysicalQueueId physicalQueue;
    GpuTimingSampleAttribution attribution = s_NoGpuTimingSampleAttribution;
    // False retires a previously attributed query whose value became unavailable for publication, such as after a
    // capture epoch change. durationSeconds is meaningful only when this is true.
    bool published = false;
    // Valid only for a published result whose backend queue exposes absolute comparable timestamps. Unpublished
    // retirement notifications retain physicalQueue but deliberately keep this range invalid even when raw query
    // data existed.
    GpuComparableTimestampRange comparableRange;
};

// The listener context belongs to its caller. Callbacks run without the registration or query-state locks; a
// recursive callback gate serializes their context access. External unsubscription waits for active callbacks
// before returning, so the caller may then release its context. Unsubscription from a thread whose callback stack
// already contains that registration cannot wait for itself; its context must remain alive until that callback stack
// unwinds. Callback exceptions are isolated and counted by the recorder. A false GpuTimingSample::published
// notification only retires caller attribution; it never represents usable timing data.
struct GpuTimingSampleListener{
    void* context = nullptr;
    void (*invoke)(void* context, const GpuTimingSample& sample) = nullptr;


    [[nodiscard]] constexpr bool valid()const noexcept{ return invoke != nullptr; }
};

// Identifies one independently owned listener registration. Identities are process-unique and are never reused, so
// delayed dispatch snapshots cannot resolve a removed subscription to a replacement context.
class GpuTimingSampleSubscription final{
    friend class GpuTimingRecorder;
    friend constexpr bool operator==(
        const GpuTimingSampleSubscription& lhs,
        const GpuTimingSampleSubscription& rhs
    )noexcept;


public:
    constexpr GpuTimingSampleSubscription() = default;


public:
    [[nodiscard]] constexpr bool valid()const noexcept{ return m_identity != 0u; }


private:
    constexpr explicit GpuTimingSampleSubscription(const u64 identity)
        : m_identity(identity)
    {}


private:
    u64 m_identity = 0u;
};

inline constexpr bool operator==(
    const GpuTimingSampleSubscription& lhs,
    const GpuTimingSampleSubscription& rhs
)noexcept{
    return lhs.m_identity == rhs.m_identity;
}
inline constexpr bool operator!=(
    const GpuTimingSampleSubscription& lhs,
    const GpuTimingSampleSubscription& rhs
)noexcept{
    return !(lhs == rhs);
}


namespace GpuTimingScopeSkipReason{
    enum Enum : u8{
        CollectionInactive,
        QueueTimestampsUnsupported,
        ComparableTimestampsUnsupported,
        ScopeNotPrepared,
        QueryCapacityUnavailable,
        RecordingPositionUnavailable,

        kCount,
    };
};


struct GpuTimingRecorderStatistics{
    u16 deviceGeneration = 0u;
    u64 preparedScopeCount = 0u;
    u64 requestedQueryCount = 0u;
    u64 materializedQueryCount = 0u;
    u64 queryMaterializationFailureCount = 0u;
    u64 scopeAttemptCount = 0u;
    u64 recordedScopeCount = 0u;
    u64 acceptedScopeCount = 0u;
    u64 publishedSampleCount = 0u;
    u64 unpublishedSampleCount = 0u;
    u64 discardedScopeCount = 0u;
    u64 quarantinedScopeCount = 0u;
    u64 beginFailureCount = 0u;
    u64 sampleListenerFailureCount = 0u;
    u64 skippedScopeCountByReason[GpuTimingScopeSkipReason::kCount]{};
    bool queryCollectionEnabled = false;
    bool timingSinkEnabled = false;
    bool feedbackCollectionEnabled = false;
    bool collectionActive = false;
    bool comparableTimestampsSupported = false;


    [[nodiscard]] bool valid()const noexcept{ return deviceGeneration != 0u; }
};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


class GpuTimingAccumulator final : NoCopy{
    friend class GpuTimingRecorder;


private:
    enum class QueryState : u8{
        Available,
        Recording,
        EndedUnaccepted,
        EndFailedUnaccepted,
        PendingAccepted,
        PendingRetirementAccepted,
        Quarantined,
    };

    enum class QueryEndResult : u8{
        Invalid,
        Ended,
        RetirementRequired,
    };

    struct SampleDispatch{
        GpuTimingSample sample;
        u64 subscriptionIdentityLimit = 0u;
    };

    struct QueryRecord{
        TimerQueryHandle query;
        GpuPhysicalQueueId physicalQueue;
        QueueSubmissionToken acceptedSubmission;
        QueueSubmissionToken frameResetSubmission;
        GpuPhysicalQueueId frameResetRecordingQueue;
        u64 frameIndex = 0u;
        GpuTimingSampleAttribution attribution = s_NoGpuTimingSampleAttribution;
        u32 epoch = 0u;
        u64 reservation = 0u;
        u64 publicationGeneration = 0u;
        u64 performanceCaptureEpoch = 0u;
        u64 retirementSubscriptionIdentityLimit = 0u;
        CommandQueue::Enum queueClass = CommandQueue::kCount;
        QueryState state = QueryState::Available;
        // A recovery endpoint completes an accepted begin after a later frame packet was rejected. The query must
        // still retire on the device timeline, but it must not publish a partial frame sample as a valid frame time.
        bool publishSample = true;
        // Set while a frame-preamble command list contains a reset for this pool. It becomes deviceReady only after
        // that command list has been submitted successfully.
        bool frameResetRecorded = false;
        // False until this pool has been reset on the DEVICE timeline by recordFrameReset() at a frame open. Recording
        // only consumes pools declared during preparation; render-pass scopes must use pools that have already passed
        // through recordFrameReset().
        bool deviceReady = false;
        bool retirementNotificationPending = false;
    };

    using SampleDispatchVector = Vector<SampleDispatch, Alloc::ScratchArena>;
    using QueryVector = Vector<QueryRecord, Alloc::GlobalArena>;


public:
    explicit GpuTimingAccumulator(
        Alloc::GlobalArena& arena,
        const Name& scopeName,
        const Perf::TimingScopeId timingScope
    )
        : m_queries(arena)
        , m_scopeName(scopeName)
        , m_timingScope(timingScope)
    {}


public:
    [[nodiscard]] bool setCaptureEnabled(bool enabled, u64 subscriptionIdentityLimit)noexcept;
    void collect(
        Device& device,
        GpuTimingRecorder& recorder,
        u32 epoch,
        u64 performanceCaptureEpoch,
        u64 subscriptionIdentityLimit,
        bool publishPerformanceSamples,
        SampleDispatchVector& completedSamples,
        Alloc::ScratchArena& scratchArena
    );
    void recordFrameReset(CommandList& commandList);
    void confirmFrameReset(const QueueSubmissionToken& token);
    void discardFrameReset()noexcept;
    void requestQueries(u32 queryCount);
    [[nodiscard]] bool materializeRequestedQueries(Device& device);
    [[nodiscard]] bool beginQuery(
        CommandList& commandList,
        u64 frameIndex,
        u32 epoch,
        u64 performanceCaptureEpoch,
        GpuTimingSampleAttribution attribution,
        GpuTimingScope& outScope,
        QueueSubmissionToken& outResetSubmission
    );
    [[nodiscard]] QueryEndResult endQuery(CommandList& commandList, const GpuTimingScope& scope);
    [[nodiscard]] bool recordQueryEnd(CommandList& commandList, const GpuTimingScope& scope);
    [[nodiscard]] bool validateQuerySubmission(const GpuTimingScope& scope, const QueueSubmissionToken& token)const;
    [[nodiscard]] bool confirmQuery(
        const GpuTimingScope& scope,
        const QueueSubmissionToken& token,
        bool publishSample
    );
    [[nodiscard]] bool retireQuery(const GpuTimingScope& scope, const QueueSubmissionToken& token);
    [[nodiscard]] bool prepareQueryForRecovery(const GpuTimingScope& scope);
    [[nodiscard]] bool discardQuery(const GpuTimingScope& scope, u64 subscriptionIdentityLimit);
    [[nodiscard]] bool quarantineQuery(const GpuTimingScope& scope, u64 subscriptionIdentityLimit)noexcept;


private:
    [[nodiscard]] bool quarantineRecord(QueryRecord& record, u64 subscriptionIdentityLimit)noexcept;
    [[nodiscard]] bool reserveQueries(Device& device, u32 queryCount);
    [[nodiscard]] u32 findAvailableQuery()const;
    [[nodiscard]] u32 appendQuery(Device& device);
    void releaseQuery(QueryRecord& record);
    void releaseUnacceptedQuery(QueryRecord& record);
    [[nodiscard]] usize pendingAttributionCount()const noexcept;
    [[nodiscard]] bool markAttributionsForRetirement(u64 subscriptionIdentityLimit)noexcept;
    [[nodiscard]] bool retireMarkedAttribution(SampleDispatch& outDispatch)noexcept;
    void discardMarkedAttributions()noexcept;
    void retireAttributions(SampleDispatchVector& outSamples, u64 subscriptionIdentityLimit);
    void appendStatistics(GpuTimingRecorderStatistics& outStatistics)const noexcept;


private:
    QueryVector m_queries;
    Name m_scopeName = NAME_NONE;
    Perf::TimingScopeId m_timingScope;
    u32 m_requestedQueryCount = 0u;
    u64 m_nextReservation = 0u;
    u64 m_publicationGeneration = 1u;
    usize m_pendingAcceptedQueryCount = 0u;
    u64 m_recordedScopeCount = 0u;
    u64 m_acceptedScopeCount = 0u;
    u64 m_publishedSampleCount = 0u;
    u64 m_unpublishedSampleCount = 0u;
    u64 m_discardedScopeCount = 0u;
    u64 m_quarantinedScopeCount = 0u;
    u64 m_skippedScopeCountByReason[GpuTimingScopeSkipReason::kCount]{};
    bool m_captureEnabled = false;
};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


class GpuTimingRecorder final : NoCopy{
    friend class GpuTimingAccumulator;
    friend class GpuTimingFrameTransaction;
    friend class GpuTimingMeasure;
    friend class GpuTimingSubmissionTicket;

private:
    struct QueueCompletion{
        GpuPhysicalQueueId queue;
        u64 value = 0u;
    };

    struct SampleListenerRecordData{
        Vector<Name, Alloc::GlobalArena> feedbackScopes;
        GpuTimingSampleSubscription subscription;
        GpuTimingSampleListener listener;
        u32 activeCallbackCount = 0u;
        bool removing = false;


        explicit SampleListenerRecordData(Alloc::GlobalArena& arena)
            : feedbackScopes(arena)
        {}
    };

    struct FeedbackScopeDemand{
        Name scopeName = NAME_NONE;
        u64 ownerCount = 0u;
    };

    using AccumulatorPtr = GlobalUniquePtr<GpuTimingAccumulator>;
    using AccumulatorMap = HashMap<Name, AccumulatorPtr, Hasher<Name>, EqualTo<Name>, Alloc::GlobalArena>;
    using SampleListenerRecord = RefCounter<SampleListenerRecordData>;
    using SampleListenerRecordPtr = RefCountPtr<
        SampleListenerRecord,
        ::ArenaRefDeleter<SampleListenerRecord, Alloc::GlobalArena>
    >;
    using SampleListenerVector = Vector<SampleListenerRecordPtr, Alloc::GlobalArena>;
    using FeedbackScopeDemandVector = Vector<FeedbackScopeDemand, Alloc::GlobalArena>;
    using SampleDispatch = GpuTimingAccumulator::SampleDispatch;
    using SampleDispatchVector = GpuTimingAccumulator::SampleDispatchVector;


public:
    GpuTimingRecorder(Alloc::GlobalArena& arena, Perf::TimingSink& timing);


public:
    void setQueryCollectionEnabled(bool enabled);
    // Every valid registration receives attributed samples captured in a dispatch batch. New listeners begin with
    // the next batch; removing one registration never replaces or clears another consumer.
    [[nodiscard]] GpuTimingSampleSubscription subscribeSampleListener(const GpuTimingSampleListener& listener);
    void unsubscribeSampleListener(const GpuTimingSampleSubscription& subscription)noexcept;
    // Atomically replaces one listener's feedback-only query scopes. Names are copied and must be valid and unique.
    // Broad Perf capture still enables every prepared scope; feedback demand only enables its named query scopes.
    [[nodiscard]] bool setFeedbackCollectionScopes(
        const GpuTimingSampleSubscription& subscription,
        NotNull<const Name*> scopeNames,
        usize scopeCount
    );
    [[nodiscard]] bool clearFeedbackCollectionScopes(const GpuTimingSampleSubscription& subscription);
    // Globally unique attribution identities remain unique across recorder reset, destruction, and recreation.
    // Allocation is lock-free so packet-recording workers can issue identities without taking listener/query locks.
    [[nodiscard]] GpuTimingSampleAttribution allocateSampleAttribution()noexcept;
    [[nodiscard]] bool queryCollectionEnabled()const;
    // True when either normal Perf capture or an adaptive feedback consumer needs query pools to be materialized
    // and reset for the current frame.
    [[nodiscard]] bool collectionActive()const;
    [[nodiscard]] GpuTimingRecorderStatistics statistics(const Device& device)const;
    void resetQueries();
    void collect(Device& device);
    void collect(Device& device, u64 publishFrameIndex);
    void beginFrame(u64 frameIndex);
    // Declares the capacity a scope needs. When capture is inactive this records the request without allocating GPU
    // query pools, so a later capture activation can materialize them before its first frame preamble.
    [[nodiscard]] bool prepareScopeQueries(const Name& scopeName, Device& device, u32 queryCount);
    // Publishes the timestamp intersection of an unordered pair of packet envelopes. Reversed exact duplicates are
    // idempotent. An output name is exclusive: it cannot be an input or query scope, and an input cannot later become
    // an output. A zero-valued sample means both packets completed but did not overlap; no sample is published when
    // either packet was rejected.
    [[nodiscard]] bool prepareOverlapMetric(
        const Name& firstScope,
        const Name& secondScope,
        const Name& outputScope
    );
    // Correlates one compiler-selected packet envelope by source frame. Inputs and output mappings are copied before
    // returning because graph plans and caller scratch storage may be rebuilt immediately. Publication requires one
    // accepted comparable range for every declared packet; queue idle covers internal holes only.
    [[nodiscard]] bool preparePacketEnvelopeMetrics(
        u64 sourceFrameIndex,
        NotNull<const GpuPacketEnvelopeMetricScope*> scopeInputs,
        usize scopeCount,
        const Name& queueOverlapScope,
        NotNull<const GpuPacketEnvelopeMetricQueueOutput*> queueOutputInputs,
        usize queueOutputCount
    );
    // Materializes every declared scope during Graphics' frame preamble. Capture can be toggled on at runtime, but
    // recording never creates a query pool: scopes and their capacity must be declared through prepareScopeQueries().
    [[nodiscard]] bool materializeRequestedQueries(Device& device);
    // Record a device-timeline reset of every available timer-query pool onto the command buffer. Graphics emits this
    // in its frame preamble before it invokes any render pass and before any dynamic render pass opens
    // (vkCmdResetQueryPool is illegal inside one), so every pool is defined before this frame's timestamp writes --
    // the validation-correct alternative to a host-side reset the layer cannot order against recorded writes.
    // Call confirmFrameReset() only after that command list submits successfully. discardFrameReset() invalidates
    // prior-frame readiness when a new preamble cannot be submitted.
    void recordFrameReset(CommandList& commandList);
    void confirmFrameReset(const QueueSubmissionToken& token);
    void discardFrameReset();


private:
    [[nodiscard]] bool replaceFeedbackCollectionScopes(
        const GpuTimingSampleSubscription& subscription,
        const Name* scopeNames,
        usize scopeCount
    );
    [[nodiscard]] usize findFeedbackScopeDemandLocked(const Name& scopeName)const noexcept;
    [[nodiscard]] bool feedbackScopeDemandedLocked(const Name& scopeName)const noexcept;
    void addFeedbackScopeDemandsLocked(const Vector<Name, Alloc::GlobalArena>& scopeNames)noexcept;
    void removeFeedbackScopeDemandsLocked(const Vector<Name, Alloc::GlobalArena>& scopeNames)noexcept;
    [[nodiscard]] bool beginScope(
        const Name& scopeName,
        Device& device,
        CommandList& commandList,
        GpuTimingSampleAttribution attribution,
        bool requiresComparableTimestamps,
        GpuTimingScope& outScope
    );
    [[nodiscard]] bool beginDeferredScope(
        const Name& scopeName,
        Device& device,
        CommandList& commandList,
        GpuTimingSampleAttribution attribution,
        GpuTimingScope& outScope
    );
    void endScope(CommandList& commandList, const GpuTimingScope& scope);
    [[nodiscard]] bool recordDeferredScopeEnd(CommandList& commandList, const GpuTimingScope& scope);
    [[nodiscard]] bool validateScopeSubmission(const GpuTimingScope& scope, const QueueSubmissionToken& token);
    [[nodiscard]] bool confirmScope(
        const GpuTimingScope& scope,
        const QueueSubmissionToken& token,
        bool publishSample
    );
    [[nodiscard]] bool prepareDeferredScopeForRecovery(const GpuTimingScope& scope);
    [[nodiscard]] bool retireScope(const GpuTimingScope& scope, const QueueSubmissionToken& token);
    void discardScope(const GpuTimingScope& scope);
    void quarantineScope(const GpuTimingScope& scope);
    [[nodiscard]] GpuTimingSubmissionTicket* activeSubmissionTicket()const;
    [[nodiscard]] GpuTimingAccumulator* findAccumulator(const GpuTimingScope& scope);
    [[nodiscard]] GpuTimingAccumulator* findOrCreateAccumulator(const Name& scopeName);
    void collectLocked(
        Device& device,
        u64 publishFrameIndex,
        u64 subscriptionIdentityLimit,
        SampleDispatchVector& completedSamples,
        Alloc::ScratchArena& scratchArena
    );
    [[nodiscard]] bool submissionCompleted(Device& device, const QueueSubmissionToken& token);
    [[nodiscard]] SampleListenerRecordPtr findSampleListenerLocked(
        const GpuTimingSampleSubscription& subscription
    )noexcept;
    [[nodiscard]] u64 sampleSubscriptionIdentityLimitLocked()const noexcept;
    void publishSampleSubscriptionIdentityLimitLocked()noexcept;
    void eraseSampleListenerLocked(SampleListenerRecord& record)noexcept;
    void dispatchCompletedSample(const GpuTimingSample& sample, u64 subscriptionIdentityLimit)noexcept;
    void dispatchCompletedSamples(const SampleDispatchVector& samples)noexcept;
    void reservePendingAttributionSamplesLocked(SampleDispatchVector& outSamples)const;
    [[nodiscard]] bool retireMarkedPendingAttributionLocked(SampleDispatch& outDispatch)noexcept;
    void retireMarkedPendingAttributionsLocked(SampleDispatchVector& outSamples);
    void discardMarkedPendingAttributionsLocked()noexcept;
    void retirePendingAttributionsLocked(SampleDispatchVector& outSamples, u64 subscriptionIdentityLimit);
    void discardFrameResetLocked();
    void noteSkippedScope(GpuTimingScopeSkipReason::Enum reason);
    void syncActiveState(u64 subscriptionIdentityLimit)noexcept;
    void syncActiveState()noexcept;
    void advancePerformanceCaptureEpoch()noexcept;
    void advanceEpoch()noexcept;


private:
    static thread_local GpuTimingSubmissionTicket* s_activeSubmissionTicket;

    Alloc::GlobalArena& m_arena;
    Perf::TimingSink& m_timing;
    GpuTimingMetricCorrelator m_metricCorrelator;
    AccumulatorMap m_accumulators;
    Vector<QueueCompletion, Alloc::GlobalArena> m_queueCompletions;
    SampleListenerVector m_sampleListeners;
    FeedbackScopeDemandVector m_feedbackScopeDemands;
    // Callbacks execute outside this registration lock. Whenever both state locks are needed, acquire this lock
    // first and m_mutex second; no path retains m_mutex while acquiring this lock.
    Futex m_sampleListenerMutex;
    // A separate recursive gate preserves caller-owned context lifetime while allowing a callback to synchronously
    // dispatch or remove registrations. No path acquires it while retaining either recorder state lock.
    RecursiveMutex m_sampleCallbackMutex;
    // A submission ticket protects its own rollback list, while this lock serializes every query-pool mutation and
    // recorder-map access. Worker command-list recordings may share a ticket and begin timing scopes concurrently.
    mutable Futex m_mutex;
    Atomic<u64> m_sampleSubscriptionIdentityLimit{ 0u };
    u64 m_performanceCaptureEpoch = 1u;
    u64 m_currentFrameIndex = 0u;
    u32 m_epoch = 1u;
    GpuTimingRecorderStatistics m_statistics;
    bool m_pendingAttributionRetirements = false;
    bool m_accumulatorsActive = false;
    bool m_performanceCollectionActive = false;
    bool m_enabled = false;
};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


// Couples timestamp-query ownership to one complete queue submission. Keep a RecordingScope active while recording
// every primary command buffer that can contain a timing endpoint, then submit that whole ordered batch through this
// ticket. If recording aborts or the submission is rejected, the ticket releases the reserved query slots; a
// successful submission retains them until collect() observes their results. This supports a timing scope whose
// start and end timestamps live in separate primary command buffers.
class GpuTimingSubmissionTicket final : NoCopy{
    friend class GpuTaskGraphSubmitter;

public:
    class RecordingScope final : NoCopy{
    public:
        explicit RecordingScope(GpuTimingSubmissionTicket& ticket);
        ~RecordingScope();


    private:
        GpuTimingSubmissionTicket& m_ticket;
        GpuTimingSubmissionTicket* m_previousTicket = nullptr;
        bool m_activated = false;
    };


public:
    explicit GpuTimingSubmissionTicket(GpuTimingRecorder& recorder);
    ~GpuTimingSubmissionTicket();


public:
    // Validates that every supplied command list still owns a ready command buffer, submits them together in their
    // supplied order, and resolves every timing scope recorded under this ticket. A partial split submission is
    // rejected before reaching Vulkan so a cross-list timing scope can never be left waiting for a missing endpoint.
    [[nodiscard]] bool submit(
        Device& device,
        CommandList* const* commandLists,
        usize commandListCount,
        CommandQueue::Enum executionQueue = CommandQueue::Graphics
    );
    // Confirms the ticket only when Vulkan accepts this exact submission and returns the completion token consumed by
    // a dependent queue. A rejected submission discards its query reservations.
    [[nodiscard]] QueueSubmissionToken submit(
        Device& device,
        CommandList* const* commandLists,
        usize commandListCount,
        CommandQueue::Enum executionQueue,
        const QueueSubmissionDesc& submitDesc
    );
    [[nodiscard]] QueueSubmissionToken submit(
        Device& device,
        CommandList* const* commandLists,
        usize commandListCount,
        const GpuPhysicalQueueId& executionQueue,
        const QueueSubmissionDesc& submitDesc
    );
    // Explicitly releases all timing scopes when a caller elects not to submit its recorded command lists.
    void discard();


private:
    friend class GpuTimingRecorder;

    // Rejects incomplete batches before they can partially submit a split timing scope. Invalid command-list input
    // releases reservations. Successful preparation atomically blocks recording and any competing submission until
    // the caller either resolves the native attempt or rolls preparation back before reaching the device.
    [[nodiscard]] bool prepareSubmission(
        CommandList* const* commandLists,
        usize commandListCount,
        Vector<QueueSubmissionToken, Alloc::ScratchArena>& waitTokens
    );
    void rollbackPreparedSubmission();
    void discardPreparedSubmission();
    void resolveSubmission(const QueueSubmissionToken& token);
    void trackScope(const GpuTimingScope& scope);
    [[nodiscard]] bool trackSubmissionPrerequisite(const QueueSubmissionToken& token);
    [[nodiscard]] bool activateOnCurrentThread(GpuTimingSubmissionTicket*& outPreviousTicket);
    void deactivateOnCurrentThread(GpuTimingSubmissionTicket* previousTicket, bool activated);
    void confirm(const QueueSubmissionToken& token);


private:
    GpuTimingRecorder& m_recorder;
    Vector<GpuTimingScope, Alloc::GlobalArena> m_scopes;
    Vector<QueueSubmissionToken, Alloc::GlobalArena> m_submissionPrerequisites;
    Futex m_mutex;
    u32 m_recordingScopeCount = 0u;
    bool m_submissionPrepared = false;
    bool m_resolved = false;
};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


// Owns a whole-frame timestamp whose endpoints live in different accepted submissions. Unlike a normal submission
// ticket, it does not mark its query pending until the packet containing the ending timestamp has actually been
// accepted. If a later packet is rejected after the producer was accepted, record a recovery end and confirm it with
// publishSample=false so the query retires without reporting a misleading partial frame duration.
class GpuTimingFrameTransaction final : NoCopy{
public:
    explicit GpuTimingFrameTransaction(GpuTimingRecorder& recorder);
    ~GpuTimingFrameTransaction();


public:
    // Must run inside a GpuTimingSubmissionTicket::RecordingScope for the submission that records the begin
    // timestamp. A disabled or under-reserved timing recorder is a valid no-op transaction.
    [[nodiscard]] bool begin(
        const GpuTimingScopeDefinition& scopeDefinition,
        Device& device,
        CommandList& commandList,
        GpuTimingSampleAttribution attribution = s_NoGpuTimingSampleAttribution
    );
    // Records the end timestamp but deliberately keeps the query reservation private until the containing submission
    // is accepted. This permits a replacement recovery endpoint when a pre-recorded final packet is rejected.
    [[nodiscard]] bool recordEnd(CommandList& commandList);
    [[nodiscard]] bool confirmBeginSubmission(const QueueSubmissionToken& token);
    [[nodiscard]] bool confirmEndSubmission(const QueueSubmissionToken& token, bool publishSample);
    [[nodiscard]] bool needsRetirement()const;
    // Discards the endpoint recorded in a packet that will not be submitted, allowing a recovery command list to
    // write the replacement endpoint after the accepted begin.
    [[nodiscard]] bool prepareForRecovery();
    void discard();


private:
    enum class State : u8{
        Idle,
        Inactive,
        BeginRecorded,
        BeginAccepted,
        EndRecorded,
        Resolved,
    };


private:
    GpuTimingRecorder& m_recorder;
    GpuTimingScope m_scope;
    QueueSubmissionToken m_beginSubmission;
    State m_state = State::Idle;
};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


class GpuTimingMeasure final : NoCopy{
public:
    GpuTimingMeasure(GpuTimingMeasure&&) = delete;
    GpuTimingMeasure& operator=(GpuTimingMeasure&&) = delete;

    // Defined out-of-line: the ctor/dtor call CommandList::beginMarker/endMarker, and CommandList is only
    // forward-declared in this header (the marker calls need the complete type, available in the .cpp).
    GpuTimingMeasure(
        GpuTimingRecorder& recorder,
        const GpuTimingScopeDefinition& scopeDefinition,
        Device& device,
        CommandList& commandList,
        GpuTimingSampleAttribution attribution = s_NoGpuTimingSampleAttribution
    );
    ~GpuTimingMeasure();

    // A timing scope may span ordered primary command buffers. Close its debug marker on the command list that
    // opened it before that list is closed, then emit the ending timestamp on the later command list.
    void finishMarker();
    // Recording failure recovery can reset the command list's marker stack before this object unwinds. Relinquish
    // marker ownership in that path so the destructor does not emit an unmatched marker end after recovery.
    void abandonMarker()noexcept{ m_markerOpen = false; }
    void finishTiming(CommandList& commandList);
    // Discards a started scope when its producer command buffer cannot be finalized or submitted.
    void discardTiming();
    [[nodiscard]] bool valid()const noexcept{ return m_scope.valid(); }


private:
    GpuTimingRecorder& m_recorder;
    CommandList& m_commandList;
    GpuTimingScope m_scope;
    bool m_markerOpen = false;
};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_CORE_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

