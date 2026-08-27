// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#pragma once


#include "api.h"

#include <core/perf/timing.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_CORE_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


class GpuTimingAccumulator;
class GpuTimingFrameTransaction;
class GpuTimingMeasure;
class GpuTimingRecorder;
class GpuTimingSubmissionTicket;

struct GpuTimingScope{
    Name scopeName = NAME_NONE;
    u32 index = Limit<u32>::s_Max;
    // Epoch and reservation distinguish a recreated accumulator and reused query-pool slot from an earlier scope.
    u32 epoch = 0u;
    u64 reservation = 0u;
    GpuTimingSubmissionTicket* submissionTicket = nullptr;

    [[nodiscard]] bool valid()const{ return scopeName != NAME_NONE && index != Limit<u32>::s_Max && epoch != 0u && reservation != 0u; }
};

// One absolute device-timestamp range proven comparable across submissions. Queue indices may differ, but every
// range retains its logical-device generation and exact tick period so overlap consumers can reject stale or
// mismatched data before conversion to floating-point seconds.
struct GpuComparableTimestampRange{
    u64 beginTicks = 0u;
    u64 endTicks = 0u;
    f64 secondsPerTick = 0.0;
    GpuPhysicalQueueId physicalQueue;

    [[nodiscard]] bool valid()const{
        return physicalQueue.valid() && beginTicks <= endTicks && secondsPerTick > 0.0;
    }
};

// Returns false when the ranges do not share one calibrated logical-device epoch and exact tick period. Comparable
// disjoint ranges return true with zero overlap. Integer intersection preserves precision beyond f64's exact range.
[[nodiscard]] inline bool TryComputeGpuTimestampOverlap(
    const GpuComparableTimestampRange& first,
    const GpuComparableTimestampRange& second,
    u64& outOverlapTicks
){
    outOverlapTicks = 0u;
    if(
        !first.valid()
        || !second.valid()
        || first.physicalQueue.deviceGeneration != second.physicalQueue.deviceGeneration
        || first.secondsPerTick != second.secondsPerTick
    )
        return false;

    const u64 overlapBegin = Max(first.beginTicks, second.beginTicks);
    const u64 overlapEnd = Min(first.endTicks, second.endTicks);
    if(overlapEnd > overlapBegin)
        outOverlapTicks = overlapEnd - overlapBegin;
    return true;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


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


// A caller-owned, opaque value retained from recording until the matching accepted GPU query completes. Zero keeps
// ordinary timing scopes out of the optional completed-sample listener path.
using GpuTimingSampleAttribution = u64;
inline constexpr GpuTimingSampleAttribution s_NoGpuTimingSampleAttribution = 0u;

struct GpuTimingSample{
    Name scopeName = NAME_NONE;
    u64 sourceFrameIndex = 0u;
    f64 durationSeconds = 0.0;
    GpuTimingSampleAttribution attribution = s_NoGpuTimingSampleAttribution;
    // False retires a previously attributed query whose value became unavailable for publication, such as after a
    // capture epoch change. durationSeconds is meaningful only when this is true.
    bool published = false;
    // Valid only for a published result whose backend queue exposes absolute comparable timestamps. Unpublished
    // retirement notifications deliberately retain the default-invalid range even when raw query data existed.
    GpuComparableTimestampRange comparableRange;
};

// The listener context belongs to its caller. setSampleListener() serializes replacement with any active callback,
// so an external caller may release the previous context after that call returns. A callback may clear or replace
// itself, but must keep its own context alive until that callback returns. A false GpuTimingSample::published
// notification only retires caller attribution; it never represents usable timing data.
struct GpuTimingSampleListener{
    void* context = nullptr;
    void (*invoke)(void* context, const GpuTimingSample& sample) = nullptr;


    [[nodiscard]] constexpr bool valid()const noexcept{ return invoke != nullptr; }
};


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
        PendingAccepted,
        Quarantined,
    };

    struct QueryRecord{
        TimerQueryHandle query;
        GpuPhysicalQueueId physicalQueue;
        QueueSubmissionToken acceptedSubmission;
        u64 frameIndex = 0u;
        GpuTimingSampleAttribution attribution = s_NoGpuTimingSampleAttribution;
        u32 epoch = 0u;
        u64 reservation = 0u;
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
    };

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
    void setEnabled(const bool enabled){
        m_enabled = enabled;
        if(!m_enabled)
            discardFrameReset();
    }

    void collect(
        Device& device,
        GpuTimingRecorder& recorder,
        u32 epoch,
        Vector<GpuTimingSample, Alloc::GlobalArena>* completedSamples
    );
    void recordFrameReset(CommandList& commandList);
    void confirmFrameReset();
    void discardFrameReset();
    void requestQueries(u32 queryCount);
    [[nodiscard]] bool materializeRequestedQueries(Device& device);
    [[nodiscard]] bool reserveQueries(Device& device, u32 queryCount);
    [[nodiscard]] bool beginQuery(
        CommandList& commandList,
        u64 frameIndex,
        u32 epoch,
        GpuTimingSampleAttribution attribution,
        GpuTimingScope& outScope
    );
    [[nodiscard]] bool endQuery(CommandList& commandList, const GpuTimingScope& scope);
    [[nodiscard]] bool recordQueryEnd(CommandList& commandList, const GpuTimingScope& scope);
    [[nodiscard]] bool validateQuerySubmission(const GpuTimingScope& scope, const QueueSubmissionToken& token)const;
    [[nodiscard]] bool confirmQuery(
        const GpuTimingScope& scope,
        const QueueSubmissionToken& token,
        bool publishSample
    );
    [[nodiscard]] bool prepareQueryForRecovery(const GpuTimingScope& scope);
    void discardQuery(const GpuTimingScope& scope);
    void quarantineQuery(const GpuTimingScope& scope);


private:
    [[nodiscard]] u32 findAvailableQuery()const;
    [[nodiscard]] u32 appendQuery(Device& device);
    void releaseQuery(QueryRecord& record);
    void retireAttributions(Vector<GpuTimingSample, Alloc::GlobalArena>& outSamples);
    void appendStatistics(GpuTimingRecorderStatistics& outStatistics)const noexcept;


private:
    QueryVector m_queries;
    Name m_scopeName = NAME_NONE;
    Perf::TimingScopeId m_timingScope;
    u32 m_requestedQueryCount = 0u;
    u64 m_nextReservation = 0u;
    u64 m_recordedScopeCount = 0u;
    u64 m_acceptedScopeCount = 0u;
    u64 m_publishedSampleCount = 0u;
    u64 m_unpublishedSampleCount = 0u;
    u64 m_discardedScopeCount = 0u;
    u64 m_quarantinedScopeCount = 0u;
    u64 m_skippedScopeCountByReason[GpuTimingScopeSkipReason::kCount]{};
    bool m_enabled = false;
};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


class GpuTimingRecorder final : NoCopy{
    friend class GpuTimingAccumulator;
    friend class GpuTimingFrameTransaction;
    friend class GpuTimingMeasure;
    friend class GpuTimingSubmissionTicket;

private:
    using AccumulatorPtr = GlobalUniquePtr<GpuTimingAccumulator>;
    using AccumulatorMap = HashMap<Name, AccumulatorPtr, Hasher<Name>, EqualTo<Name>, Alloc::GlobalArena>;

    struct QueueCompletion{
        GpuPhysicalQueueId queue;
        u64 value = 0u;
    };

    struct PendingOverlapFrame{
        u64 frameIndex = 0u;
        GpuComparableTimestampRange first;
        GpuComparableTimestampRange second;
        bool hasFirst = false;
        bool hasSecond = false;
    };

    struct OverlapRecord{
        Name firstScope = NAME_NONE;
        Name secondScope = NAME_NONE;
        Perf::TimingScopeId outputScope;
        Vector<PendingOverlapFrame, Alloc::GlobalArena> pendingFrames;

        explicit OverlapRecord(Alloc::GlobalArena& arena)
            : pendingFrames(arena)
        {}
    };

    using OverlapVector = Vector<OverlapRecord, Alloc::GlobalArena>;


public:
    GpuTimingRecorder(Alloc::GlobalArena& arena, Perf::TimingSink& timing);


public:
    void setQueryCollectionEnabled(bool enabled);
    // A higher-level adaptive policy may collect only the scopes it needs even while general Perf capture is off.
    // This does not enable Perf publication; it only keeps prepared GPU query pools active for an accepted-sample
    // listener.
    void setFeedbackCollectionEnabled(bool enabled);
    // Callbacks run only for scopes that supplied a non-zero attribution. They run after GpuTimingRecorder's query
    // state lock has been released, so listeners may safely interact with higher-level timing consumers.
    void setSampleListener(const GpuTimingSampleListener& listener);
    [[nodiscard]] bool queryCollectionEnabled()const{ return m_enabled; }
    // True when either normal Perf capture or an adaptive feedback consumer needs query pools to be materialized
    // and reset for the current frame.
    [[nodiscard]] bool collectionActive()const{ return (m_enabled && m_timing.enabled()) || m_feedbackCollectionEnabled; }
    [[nodiscard]] GpuTimingRecorderStatistics statistics(const Device& device)const;
    void resetQueries();
    void collect(Device& device);
    void collect(Device& device, u64 publishFrameIndex);
    void beginFrame(u64 frameIndex);
    // Declares the capacity a scope needs. When capture is inactive this records the request without allocating GPU
    // query pools, so a later capture activation can materialize them before its first frame preamble.
    [[nodiscard]] bool prepareScopeQueries(const Name& scopeName, Device& device, u32 queryCount);
    // Publishes the positive timestamp intersection of two packet envelopes. A zero-valued sample means both
    // packets completed but did not overlap; no sample is published when either packet was rejected.
    [[nodiscard]] bool prepareOverlapMetric(
        const Name& firstScope,
        const Name& secondScope,
        const Name& outputScope
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
    void confirmFrameReset();
    void discardFrameReset();
#if !defined(NWB_FINAL)
    // Holds one exact accepted token at the recorder boundary after the native queue may already be complete. This
    // makes query publication/reuse tests deterministic without changing Device timeline state.
    [[nodiscard]] bool holdSubmissionCompletionForTesting(const QueueSubmissionToken& token);
    void releaseSubmissionCompletionForTesting(const QueueSubmissionToken& token);
#endif


private:
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
    void discardScope(const GpuTimingScope& scope);
    void quarantineScope(const GpuTimingScope& scope);
    [[nodiscard]] GpuTimingSubmissionTicket* activeSubmissionTicket()const;
    [[nodiscard]] GpuTimingAccumulator* findAccumulator(const GpuTimingScope& scope);
    [[nodiscard]] GpuTimingAccumulator* findOrCreateAccumulator(const Name& scopeName);
    void collectLocked(
        Device& device,
        u64 publishFrameIndex,
        Vector<GpuTimingSample, Alloc::GlobalArena>* completedSamples
    );
    [[nodiscard]] bool submissionCompleted(Device& device, const QueueSubmissionToken& token);
    void dispatchCompletedSamples(
        const Vector<GpuTimingSample, Alloc::GlobalArena>& samples,
        u64 listenerGeneration
    );
    void retirePendingAttributionsLocked(Vector<GpuTimingSample, Alloc::GlobalArena>& outSamples);
    void recordTimestampRange(const Name& scopeName, u64 frameIndex, const GpuComparableTimestampRange& range);
    void discardFrameResetLocked();
    void noteSkippedScope(GpuTimingScopeSkipReason::Enum reason);
    void syncActiveState();
    void advanceEpoch();


private:
    Alloc::GlobalArena& m_arena;
    Perf::TimingSink& m_timing;
    AccumulatorMap m_accumulators;
    Vector<QueueCompletion, Alloc::GlobalArena> m_queueCompletions;
    OverlapVector m_overlapRecords;
    // A submission ticket protects its own rollback list, while this lock serializes every query-pool mutation and
    // recorder-map access. Worker command-list recordings may share a ticket and begin timing scopes concurrently.
    mutable Futex m_mutex;
    // This intentionally permits callback-driven removal. It serializes listener replacement with each invocation
    // without retaining m_mutex across external code.
    RecursiveMutex m_sampleListenerMutex;
    GpuTimingSampleListener m_sampleListener;
    Atomic<u64> m_sampleListenerGeneration{ 1u };
    Atomic<bool> m_hasSampleListener{ false };
    u64 m_currentFrameIndex = 0u;
    u32 m_epoch = 1u;
    GpuTimingRecorderStatistics m_statistics;
#if !defined(NWB_FINAL)
    QueueSubmissionToken m_heldSubmissionCompletion;
#endif
    bool m_accumulatorsActive = false;
    bool m_enabled = false;
    bool m_feedbackCollectionEnabled = false;

    static thread_local GpuTimingSubmissionTicket* s_activeSubmissionTicket;
};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


// Couples timestamp-query ownership to one complete queue submission. Keep a RecordingScope active while recording
// every primary command buffer that can contain a timing endpoint, then submit that whole ordered batch through this
// ticket. If recording aborts or the submission is rejected, the ticket releases the reserved query slots; a
// successful submission retains them until collect() observes their results. This supports a timing scope whose
// start and end timestamps live in separate primary command buffers.
class GpuTimingSubmissionTicket final : NoCopy{
public:
    class RecordingScope final : NoCopy{
    public:
        explicit RecordingScope(GpuTimingSubmissionTicket& ticket);
        ~RecordingScope();


    private:
        GpuTimingSubmissionTicket& m_ticket;
        GpuTimingSubmissionTicket* m_previousTicket = nullptr;
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
    // The cross-lane form confirms the ticket only when Vulkan accepts this exact submission and returns the
    // completion token consumed by a dependent lane. A rejected submission discards its query reservations.
    [[nodiscard]] QueueSubmissionToken submit(
        Device& device,
        CommandList* const* commandLists,
        usize commandListCount,
        RenderLane::Enum executionLane,
        const QueueSubmissionDesc& submitDesc
    );
    // Graph-owned packets already resolve a physical CommandQueue during compilation.  Keep timing ownership tied to
    // that exact submission instead of translating back through renderer-facing lane intent.
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
    // releases reservations; a ticket that is already resolved or still recording is left unchanged.
    [[nodiscard]] bool prepareSubmission(CommandList* const* commandLists, usize commandListCount);
    void resolveSubmission(const QueueSubmissionToken& token);
    void trackScope(const GpuTimingScope& scope);
    [[nodiscard]] GpuTimingSubmissionTicket* activateOnCurrentThread();
    void deactivateOnCurrentThread(GpuTimingSubmissionTicket* previousTicket);
    void confirm(const QueueSubmissionToken& token);


private:
    GpuTimingRecorder& m_recorder;
    Vector<GpuTimingScope, Alloc::GlobalArena> m_scopes;
    Futex m_mutex;
    u32 m_recordingScopeCount = 0u;
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

