// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#pragma once


#include "types.h"


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_CORE_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


// Queue routing remains a compiler policy choice.  This diagnostic keeps the scoring terms independently visible so
// timing feedback can be inspected without becoming part of graph correctness or synchronization planning.
struct GpuQueueAssignmentScore{
    i32 preference = 0;
    i32 overlap = 0;
    i32 queueLoad = 0;
    i32 incomingCrossings = 0;
    i32 outgoingCrossings = 0;
    i32 ownershipTransfers = 0;

    [[nodiscard]] i32 total()const noexcept;
};


// Accepted assignment and dwell state follow semantic work across queue-class changes. Duration histories remain
// route-specific below, but a Graphics -> Compute -> Graphics sequence must update one shared switch timeline.
struct GpuTaskTimingAssignmentKey{
    Name task = NAME_NONE;
    u32 variant = 0u;
    u32 resolutionClass = 0u;

    [[nodiscard]] constexpr bool valid()const noexcept{ return static_cast<bool>(task); }
};
inline constexpr bool operator==(const GpuTaskTimingAssignmentKey& lhs, const GpuTaskTimingAssignmentKey& rhs)noexcept{
    return lhs.task == rhs.task
        && lhs.variant == rhs.variant
        && lhs.resolutionClass == rhs.resolutionClass
    ;
}
inline constexpr bool operator!=(const GpuTaskTimingAssignmentKey& lhs, const GpuTaskTimingAssignmentKey& rhs)noexcept{
    return !(lhs == rhs);
}


// Stable route key for measured task work. The broad queue class belongs in the history key, while the exact
// physical queue is retained separately so devices with several same-class queues do not blend samples.
struct GpuTaskTimingKey{
    Name task = NAME_NONE;
    u32 variant = 0u;
    u32 resolutionClass = 0u;
    CommandQueue::Enum queue = CommandQueue::Graphics;

    [[nodiscard]] constexpr bool valid()const noexcept{ return task && queue < CommandQueue::kCount; }
};
inline constexpr bool operator==(const GpuTaskTimingKey& lhs, const GpuTaskTimingKey& rhs)noexcept{
    return lhs.task == rhs.task
        && lhs.variant == rhs.variant
        && lhs.resolutionClass == rhs.resolutionClass
        && lhs.queue == rhs.queue
    ;
}
inline constexpr bool operator!=(const GpuTaskTimingKey& lhs, const GpuTaskTimingKey& rhs)noexcept{
    return !(lhs == rhs);
}

[[nodiscard]] inline constexpr GpuTaskTimingAssignmentKey GpuTaskTimingAssignmentKeyFromHistoryKey(
    const GpuTaskTimingKey& key
)noexcept{
    return GpuTaskTimingAssignmentKey{
        .task = key.task,
        .variant = key.variant,
        .resolutionClass = key.resolutionClass,
    };
}


struct GpuTaskTimingHistory{
    f64 averageSeconds = 0.0;
    f64 minimumSeconds = 0.0;
    f64 maximumSeconds = 0.0;
    u32 sampleCount = 0u;

    [[nodiscard]] bool valid()const noexcept;
};

struct GpuTaskTimingHistoryEntry{
    GpuTaskTimingKey key;
    GpuPhysicalQueueId physicalQueue;
    GpuTaskTimingHistory history;

    [[nodiscard]] bool valid()const noexcept{
        return key.valid() && physicalQueue.valid() && history.valid();
    }
};

// This state is intentionally separate from duration statistics.  It lets a compiler damp assignment changes even
// when the timing query that established the last accepted route completes several frames later.
struct GpuTaskTimingAssignmentState{
    GpuTaskTimingAssignmentKey key;
    GpuPhysicalQueueId lastAcceptedQueue;
    u64 lastAcceptedFrameIndex = 0u;
    u64 lastSwitchFrameIndex = 0u;
    bool hasAcceptedAssignment = false;

    [[nodiscard]] bool valid()const noexcept{
        return key.valid()
            && hasAcceptedAssignment
            && lastAcceptedQueue.valid()
            && lastSwitchFrameIndex <= lastAcceptedFrameIndex
        ;
    }
};


// Disabled by default.  A caller supplies this policy and one immutable history snapshot to the compiler; neither
// missing timing nor an invalid/empty snapshot may change the ordinary deterministic queue assignment.
struct GpuTaskTimingFeedbackPolicy{
    bool enabled = false;
    u32 minimumSampleCount = 8u;
    // While a legal candidate lacks enough samples, one opted-in route is selected every N frames. Every
    // cross-family probe requires the family-routing opt-in, and cross-class probes also require the cross-class
    // timing opt-in. Zero disables calibration. Probes stop after every legal route reaches minimumSampleCount.
    u32 calibrationIntervalFrames = 1u;
    f64 minimumAbsoluteBenefitSeconds = 0.0;
    f64 minimumRelativeBenefit = 0.0;
    u64 minimumFramesBetweenSwitches = 30u;

    [[nodiscard]] bool valid()const noexcept;
};

struct GpuTaskTimingQueueOverride{
    GpuTaskTimingKey key;
    GpuPhysicalQueueId queue;

    [[nodiscard]] bool valid()const noexcept{ return key.valid() && queue.valid(); }
};

namespace GpuTaskTimingQueueOverrideStatus{
    enum Enum : u8{
        Success,
        MissingOverrides,
        InvalidKey,
        InvalidQueue,
        MismatchedDeviceGeneration,
        DuplicateTaskKey,

        kCount,
    };
};

// Validates only identity and device-generation safety.  The compiler remains responsible for capability, queue
// class, resource-ownership, and task-level eligibility checks before honoring a debug force override.
[[nodiscard]] GpuTaskTimingQueueOverrideStatus::Enum ValidateGpuTaskTimingQueueOverrides(
    const GpuTaskTimingQueueOverride* overrides,
    usize overrideCount,
    u16 deviceGeneration
)noexcept;
[[nodiscard]] const GpuTaskTimingQueueOverride* FindGpuTaskTimingQueueOverride(
    const GpuTaskTimingQueueOverride* overrides,
    usize overrideCount,
    const GpuTaskTimingKey& key
)noexcept;

[[nodiscard]] bool GpuTaskTimingHistoryMeetsMinimumSamples(
    const GpuTaskTimingHistory& history,
    const GpuTaskTimingFeedbackPolicy& policy
)noexcept;
[[nodiscard]] bool GpuTaskTimingBenefitExceedsHysteresis(
    const GpuTaskTimingHistory& incumbent,
    const GpuTaskTimingHistory& candidate,
    const GpuTaskTimingFeedbackPolicy& policy
)noexcept;
[[nodiscard]] bool GpuTaskTimingSwitchDwellElapsed(
    const GpuTaskTimingAssignmentState& assignment,
    u64 frameIndex,
    const GpuTaskTimingFeedbackPolicy& policy
)noexcept;
[[nodiscard]] bool GpuTaskTimingFeedbackCanSwitch(
    const GpuTaskTimingHistory& incumbent,
    const GpuTaskTimingHistory& candidate,
    const GpuTaskTimingAssignmentState& assignment,
    const GpuPhysicalQueueId& incumbentQueue,
    const GpuPhysicalQueueId& candidateQueue,
    u64 frameIndex,
    const GpuTaskTimingFeedbackPolicy& policy
)noexcept;


class GpuTaskTimingHistoryStore;

// A snapshot is writeable only by its owning history store.  Compilation consumes these plain values and therefore
// cannot observe query completion or mutable frame state while it makes a deterministic queue decision.
class GpuTaskTimingHistorySnapshot final : NoCopy{
    friend class GpuTaskTimingHistoryStore;

public:
    explicit GpuTaskTimingHistorySnapshot(GraphicsArena& arena)
        : m_histories(arena)
        , m_assignments(arena)
    {}


public:
    [[nodiscard]] bool valid()const noexcept{ return m_valid; }
    [[nodiscard]] u16 deviceGeneration()const noexcept{ return m_deviceGeneration; }
    [[nodiscard]] const GpuTaskTimingHistory* find(
        const GpuTaskTimingKey& key,
        const GpuPhysicalQueueId& physicalQueue
    )const noexcept;
    [[nodiscard]] const GpuTaskTimingAssignmentState* findAssignment(
        const GpuTaskTimingAssignmentKey& key
    )const noexcept;


private:
    void reset()noexcept;


private:
    GraphicsVector<GpuTaskTimingHistoryEntry> m_histories;
    GraphicsVector<GpuTaskTimingAssignmentState> m_assignments;
    u16 m_deviceGeneration = 0u;
    bool m_valid = false;
};


inline constexpr u32 s_DefaultGpuTaskTimingHistorySampleCount = 32u;

// Owns bounded rolling samples for one current logical-device generation.  Query collection may call recordSample()
// before a later compiler snapshot; none of the mutation APIs participate in graph validation, barriers, or waits.
class GpuTaskTimingHistoryStore final : NoCopy{
private:
    struct HistoryRecord{
        GpuTaskTimingHistoryEntry entry;
        GraphicsVector<f64> samples;

        explicit HistoryRecord(GraphicsArena& arena)
            : samples(arena)
        {}
    };


public:
    explicit GpuTaskTimingHistoryStore(
        GraphicsArena& arena,
        u32 maximumSamplesPerHistory = s_DefaultGpuTaskTimingHistorySampleCount
    );


public:
    void reset()noexcept;
    void resetForDeviceGeneration(u16 deviceGeneration)noexcept;
    // A successfully collected query represents accepted GPU work.  The source frame updates route dwell state only
    // when it is newer than the previous observation, so late completion can never move that state backwards.
    [[nodiscard]] bool recordSample(
        const GpuTaskTimingKey& key,
        const GpuPhysicalQueueId& physicalQueue,
        f64 durationSeconds,
        u64 sourceFrameIndex
    );
    // A bounded calibration observation contributes route statistics but is not a committed adaptive assignment.
    // This keeps a slow or under-sampled route from changing incumbent retention or restarting switch dwell.
    [[nodiscard]] bool recordNonCommittingSample(
        const GpuTaskTimingKey& key,
        const GpuPhysicalQueueId& physicalQueue,
        f64 durationSeconds
    );
    // Future packet-acceptance integrations may report a route even when timestamp capacity was unavailable.
    [[nodiscard]] bool noteAcceptedAssignment(
        const GpuTaskTimingKey& key,
        const GpuPhysicalQueueId& physicalQueue,
        u64 sourceFrameIndex
    );
    void snapshot(GpuTaskTimingHistorySnapshot& outSnapshot)const;

    [[nodiscard]] u16 deviceGeneration()const noexcept{ return m_deviceGeneration; }
    [[nodiscard]] usize historyCount()const noexcept{ return m_histories.size(); }
    [[nodiscard]] const GpuTaskTimingHistory* find(
        const GpuTaskTimingKey& key,
        const GpuPhysicalQueueId& physicalQueue
    )const noexcept;
    [[nodiscard]] const GpuTaskTimingAssignmentState* findAssignment(
        const GpuTaskTimingAssignmentKey& key
    )const noexcept;


private:
    [[nodiscard]] HistoryRecord* findHistoryRecord(
        const GpuTaskTimingKey& key,
        const GpuPhysicalQueueId& physicalQueue
    )noexcept;
    [[nodiscard]] const HistoryRecord* findHistoryRecord(
        const GpuTaskTimingKey& key,
        const GpuPhysicalQueueId& physicalQueue
    )const noexcept;
    [[nodiscard]] GpuTaskTimingAssignmentState* findAssignmentState(
        const GpuTaskTimingAssignmentKey& key
    )noexcept;
    [[nodiscard]] const GpuTaskTimingAssignmentState* findAssignmentState(
        const GpuTaskTimingAssignmentKey& key
    )const noexcept;
    void rebuildHistory(HistoryRecord& record)noexcept;


private:
    GraphicsArena& m_arena;
    GraphicsVector<HistoryRecord> m_histories;
    GraphicsVector<GpuTaskTimingAssignmentState> m_assignments;
    u32 m_maximumSamplesPerHistory = 1u;
    u16 m_deviceGeneration = 0u;
};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_CORE_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

