// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#pragma once


#include "gpu_timing_types.h"


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_CORE_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


class GpuTimingRecorder;


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


class GpuTimingAccumulator final : NoCopy{
    friend class GpuTimingRecorder;


private:
    struct QueryState{
        enum Enum : u8{
            Available,
            Recording,
            EndedUnaccepted,
            EndFailedUnaccepted,
            PendingAccepted,
            PendingRetirementAccepted,
            Quarantined,
        };
    };

    struct QueryEndResult{
        enum Enum : u8{
            Invalid,
            Ended,
            RetirementRequired,
        };
    };

    struct SampleDispatch{
        GpuTimingSample sample;
        u64 subscriptionIdentityLimit = 0u;
    };

    struct QueryBegin{
        GpuTimingScope scope;
        QueueSubmissionToken resetSubmission;
    };

    struct QueryRecord{
        TimerQueryHandle query;
        QueueSubmissionToken acceptedSubmission;
        QueueSubmissionToken frameResetSubmission;
        u64 frameIndex = 0u;
        u64 reservation = 0u;
        u64 publicationGeneration = 0u;
        u64 performanceCaptureEpoch = 0u;
        u64 retirementSubscriptionIdentityLimit = 0u;
        GpuTimingSampleAttribution attribution = s_NoGpuTimingSampleAttribution;
        GpuPhysicalQueueId physicalQueue;
        GpuPhysicalQueueId frameResetRecordingQueue;
        u32 epoch = 0u;
        CommandQueue::Enum queueClass = CommandQueue::kCount;
        QueryState::Enum state = QueryState::Available;
        // A recovery endpoint finishes an accepted begin after a later packet rejects. It retires on the device timeline but publishes no frame sample.
        b8 publishSample = true;
        // Set while a frame-preamble command list contains a reset for this pool.
        // It becomes deviceReady only after that command list has been submitted successfully.
        b8 frameResetRecorded = false;
        // False until this pool has been reset on the DEVICE timeline by recordFrameReset() at a frame open.
        // Recording takes only pools declared during preparation; positions with no native reset need a pool past recordFrameReset().
        b8 deviceReady = false;
        b8 retirementNotificationPending = false;
    };


private:
    using SampleDispatchVector = Vector<SampleDispatch, Alloc::ScratchArena>;
    using QueryVector = Vector<QueryRecord, Alloc::GlobalArena>;


public:
    explicit GpuTimingAccumulator(Alloc::GlobalArena& arena, const Name& scopeName, const Perf::TimingScopeId timingScope)noexcept
        : m_queries(arena)
        , m_scopeName(scopeName)
        , m_timingScope(timingScope)
    {}


public:
    [[nodiscard]] b8 setCaptureEnabled(b8 enabled, u64 subscriptionIdentityLimit)noexcept;
    void collect(
        Device& device,
        GpuTimingRecorder& recorder,
        Alloc::ScratchArena& scratchArena,
        SampleDispatchVector& completedSamples,
        GpuTimingSinkSampleVector& performanceSamples,
        u32 epoch,
        u64 performanceCaptureEpoch,
        u64 subscriptionIdentityLimit,
        b8 publishPerformanceSamples
    );
    void recordFrameReset(CommandList& commandList);
    void confirmFrameReset(const QueueSubmissionToken& token);
    void discardFrameReset()noexcept;
    void requestQueries(const u32 queryCount)noexcept{ m_requestedQueryCount = Max(m_requestedQueryCount, queryCount); }
    [[nodiscard]] b8 materializeRequestedQueries(Device& device);
    [[nodiscard]] Expected<QueryBegin> beginQuery(CommandList& commandList, u64 frameIndex, u32 epoch, u64 performanceCaptureEpoch, GpuTimingSampleAttribution attribution);
    [[nodiscard]] QueryEndResult::Enum endQuery(CommandList& commandList, const GpuTimingScope& scope);
    [[nodiscard]] QueryEndResult::Enum endQueryFromExistingClaim(CommandList& commandList, const GpuTimingScope& scope)noexcept;
    [[nodiscard]] b8 recordQueryEnd(CommandList& commandList, const GpuTimingScope& scope);
    [[nodiscard]] b8 validateQuerySubmission(const GpuTimingScope& scope, const QueueSubmissionToken& token)const noexcept;
    [[nodiscard]] b8 confirmQuery(const GpuTimingScope& scope, const QueueSubmissionToken& token, b8 publishSample)noexcept;
    [[nodiscard]] b8 prepareQueryForRecovery(const GpuTimingScope& scope)noexcept;
    [[nodiscard]] b8 discardQuery(const GpuTimingScope& scope, u64 subscriptionIdentityLimit);
    [[nodiscard]] b8 abandonQuery(const GpuTimingScope& scope, u64 subscriptionIdentityLimit)noexcept;
    [[nodiscard]] b8 quarantineQuery(const GpuTimingScope& scope, u64 subscriptionIdentityLimit)noexcept;


private:
    [[nodiscard]] b8 quarantineRecord(QueryRecord& record, u64 subscriptionIdentityLimit)noexcept;
    [[nodiscard]] b8 reserveQueries(Device& device, u32 queryCount);
    [[nodiscard]] u32 findAvailableQuery()const noexcept;
    [[nodiscard]] b8 appendQuery(Device& device);
    void releaseQuery(QueryRecord& record)noexcept;
    void releaseUnacceptedQuery(QueryRecord& record)noexcept;
    [[nodiscard]] usize pendingAttributionCount()const noexcept;
    [[nodiscard]] b8 markAttributionsForRetirement(u64 subscriptionIdentityLimit)noexcept;
    void retireMarkedAttributions(SampleDispatchVector& outSamples);
    void discardMarkedAttributions()noexcept;
    void retireAttributions(u64 subscriptionIdentityLimit, SampleDispatchVector& outSamples);
    void appendStatistics(GpuTimingRecorderStatistics& outStatistics)const noexcept;


private:
    QueryVector m_queries;
    u64 m_nextReservation = 0u;
    u64 m_publicationGeneration = 1u;
    usize m_pendingAcceptedQueryCount = 0u;
    u64 m_recordedScopeCount = 0u;
    u64 m_acceptedScopeCount = 0u;
    u64 m_publishedSampleCount = 0u;
    u64 m_unpublishedSampleCount = 0u;
    u64 m_discardedScopeCount = 0u;
    u64 m_quarantinedScopeCount = 0u;
    Array<u64, GpuTimingScopeSkipReason::Count> m_skippedScopeCountByReason{};
    Name m_scopeName = s_NameNone;
    u32 m_requestedQueryCount = 0u;
    b8 m_captureEnabled = false;
    Perf::TimingScopeId m_timingScope;
};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_CORE_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

