// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#pragma once


#include "api.h"
#include "gpu_timing_metric_correlator.h"

#include <core/perf/timing.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_CORE_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


struct GpuTimingScope{
    Name scopeName = NAME_NONE;
    u64 reservation = 0u;
    TimerQueryRecordingToken timerQueryRecording;
    GpuTimingSubmissionTicket* submissionTicket = nullptr;
    usize submissionPublicationIndex = Limit<usize>::s_Max;
    u32 index = Limit<u32>::s_Max;
    // Epoch and reservation distinguish a recreated accumulator and reused query-pool slot from an earlier scope.
    u32 epoch = 0u;

    [[nodiscard]] bool valid()const{ return scopeName != NAME_NONE && index != Limit<u32>::s_Max && epoch != 0u && reservation != 0u; }
};

static_assert(IsTriviallyCopyable_V<GpuTimingScope>, "GPU timing publication must remain allocation-free after native recording begins");

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
    friend constexpr bool operator==(const GpuTimingSampleAttribution& lhs, const GpuTimingSampleAttribution& rhs)noexcept;


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
    u64 sourceFrameIndex = 0u;
    f64 durationSeconds = 0.0;
    Name scopeName = NAME_NONE;
    // Accepted native queue per attributed outcome; stays valid when the backend exposes durations
    // but no absolute cross-submission comparable range.
    GpuTimingSampleAttribution attribution = s_NoGpuTimingSampleAttribution;
    GpuComparableTimestampRange comparableRange;
    GpuPhysicalQueueId physicalQueue;
    // False retires an attributed query whose value became unavailable (e.g. capture epoch change).
    // durationSeconds is meaningful only when true.
    bool published = false;
    // Valid only for a published result with absolute comparable timestamps. Unpublished retirements keep
    // physicalQueue but leave this range invalid even when raw query data existed.
};

// Listener context belongs to its caller. Callbacks run unlocked; a recursive gate serializes context access.
// External unsubscription waits for active callbacks, so the caller may then release its context. A callback-stack
// unsubscription cannot wait for itself; its context must outlive that stack. Callback exceptions restore state,
// bump the recorder failure count, and propagate to the application boundary. An unpublished notification only
// retires attribution, never timing data.
struct GpuTimingSampleListener{
    void* context = nullptr;
    void (*invoke)(void* context, const GpuTimingSample& sample) = nullptr;


    [[nodiscard]] constexpr bool valid()const noexcept{ return invoke != nullptr; }
};

// Subscription identities are process-unique and never reused, so delayed snapshots cannot resolve
// a removed subscription to a replacement context.
class GpuTimingSampleSubscription final{
    friend class GpuTimingRecorder;
    friend constexpr bool operator==(const GpuTimingSampleSubscription& lhs, const GpuTimingSampleSubscription& rhs)noexcept;


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
    u16 deviceGeneration = 0u;
    bool queryCollectionEnabled = false;
    bool timingSinkEnabled = false;
    bool feedbackCollectionEnabled = false;
    bool collectionActive = false;
    bool comparableTimestampsSupported = false;


    [[nodiscard]] bool valid()const noexcept{ return deviceGeneration != 0u; }
};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_CORE_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

