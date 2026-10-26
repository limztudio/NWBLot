// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#pragma once


#include "types.h"


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_CORE_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


// Queue routing stays a compiler policy choice. Scores stay visible for inspection, outside correctness and sync planning.
struct GpuQueueAssignmentScore{
    i32 overlap = 0;
    i32 queueLoad = 0;
    i32 incomingCrossings = 0;
    i32 outgoingCrossings = 0;
    i32 ownershipTransfers = 0;


public:
    [[nodiscard]] i32 total()const noexcept;
};


// Accepted assignment and dwell state follow semantic work across queue-class changes.
// Duration histories stay route-specific below, but a Graphics -> Compute -> Graphics sequence must update one shared switch timeline.
struct GpuTaskTimingAssignmentKey{
    Name task = s_NameNone;
    u32 variant = 0u;
    u32 resolutionClass = 0u;


public:
    [[nodiscard]] constexpr bool valid()const noexcept{ return static_cast<bool>(task); }
};
inline constexpr bool operator==(const GpuTaskTimingAssignmentKey& lhs, const GpuTaskTimingAssignmentKey& rhs)noexcept{
    return lhs.task == rhs.task && lhs.variant == rhs.variant && lhs.resolutionClass == rhs.resolutionClass;
}

inline constexpr bool operator!=(const GpuTaskTimingAssignmentKey& lhs, const GpuTaskTimingAssignmentKey& rhs)noexcept{
    return !(lhs == rhs);
}


// Route key for measured work. Class goes in the key; the exact queue stays separate so same-class queues never blend.
struct GpuTaskTimingKey{
    Name task = s_NameNone;
    u32 variant = 0u;
    u32 resolutionClass = 0u;
    CommandQueue::Enum queue = CommandQueue::Graphics;


public:
    [[nodiscard]] constexpr bool valid()const noexcept{ return task && queue < CommandQueue::kCount; }
};
inline constexpr bool operator==(const GpuTaskTimingKey& lhs, const GpuTaskTimingKey& rhs)noexcept{
    return lhs.task == rhs.task && lhs.variant == rhs.variant && lhs.resolutionClass == rhs.resolutionClass && lhs.queue == rhs.queue;
}

inline constexpr bool operator!=(const GpuTaskTimingKey& lhs, const GpuTaskTimingKey& rhs)noexcept{
    return !(lhs == rhs);
}

[[nodiscard]] inline constexpr GpuTaskTimingAssignmentKey GpuTaskTimingAssignmentKeyFromHistoryKey(const GpuTaskTimingKey& key)noexcept{
    return GpuTaskTimingAssignmentKey{ .task = key.task, .variant = key.variant, .resolutionClass = key.resolutionClass, };
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace GpuTaskTimingHistoryDetail{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


struct RouteKey{
    NameHash task;
    u32 variant;
    u32 resolutionClass;
    CommandQueue::Enum queue;
    GpuPhysicalQueueId physicalQueue;


public:
    [[nodiscard]] constexpr bool operator==(const RouteKey&)const noexcept = default;
};

struct AssignmentKey{
    NameHash task;
    u32 variant;
    u32 resolutionClass;


public:
    [[nodiscard]] constexpr bool operator==(const AssignmentKey&)const noexcept = default;
};

struct RouteLookup{
    const GpuTaskTimingKey& key;
    const GpuPhysicalQueueId& physicalQueue;
};

struct RouteHash{
    [[nodiscard]] usize operator()(const RouteKey& value)const noexcept;
    [[nodiscard]] usize operator()(const RouteLookup& value)const noexcept;
};

struct RouteEqual{
    using is_transparent = void;


public:
    [[nodiscard]] bool operator()(const RouteKey& lhs, const RouteKey& rhs)const noexcept{
        return lhs == rhs;
    }

    [[nodiscard]] bool operator()(const RouteKey& lhs, const RouteLookup& rhs)const noexcept{
        return lhs.task == rhs.key.task.identityHash() && lhs.variant == rhs.key.variant && lhs.resolutionClass == rhs.key.resolutionClass && lhs.queue == rhs.key.queue && lhs.physicalQueue == rhs.physicalQueue;
    }
};

struct AssignmentHash{
    [[nodiscard]] usize operator()(const AssignmentKey& value)const noexcept;
    [[nodiscard]] usize operator()(const GpuTaskTimingAssignmentKey& value)const noexcept;
};

struct AssignmentEqual{
    using is_transparent = void;


public:
    [[nodiscard]] bool operator()(const AssignmentKey& lhs, const AssignmentKey& rhs)const noexcept{
        return lhs == rhs;
    }

    [[nodiscard]] bool operator()(const AssignmentKey& lhs, const GpuTaskTimingAssignmentKey& rhs)const noexcept{
        return lhs.task == rhs.task.identityHash() && lhs.variant == rhs.variant && lhs.resolutionClass == rhs.resolutionClass;
    }
};

using RouteIndex = HashMap<RouteKey, usize, GraphicsArena, RouteHash, RouteEqual>;
using AssignmentIndex = HashMap<AssignmentKey, usize, GraphicsArena, AssignmentHash, AssignmentEqual>;


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


struct GpuTaskTimingHistory{
    f64 averageSeconds = 0.0;
    f64 minimumSeconds = 0.0;
    f64 maximumSeconds = 0.0;
    u32 sampleCount = 0u;


public:
    [[nodiscard]] bool valid()const noexcept;
};

struct GpuTaskTimingHistoryEntry{
    GpuTaskTimingKey key;
    GpuTaskTimingHistory history;
    GpuPhysicalQueueId physicalQueue;


public:
    [[nodiscard]] bool valid()const noexcept{
        return key.valid() && physicalQueue.valid() && history.valid();
    }
};

// This state is intentionally separate from duration statistics.
// It lets a compiler damp assignment changes even when the timing query that established the last accepted route completes several frames later.
struct GpuTaskTimingAssignmentState{
    GpuTaskTimingAssignmentKey key;
    u64 lastAcceptedFrameIndex = 0u;
    u64 lastSwitchFrameIndex = 0u;
    GpuPhysicalQueueId lastAcceptedQueue;
    bool hasAcceptedAssignment = false;


public:
    [[nodiscard]] bool valid()const noexcept{
        return key.valid() && hasAcceptedAssignment && lastAcceptedQueue.valid() && lastSwitchFrameIndex <= lastAcceptedFrameIndex;
    }
};


namespace GpuTaskTimingFeedbackPolicyDetail{
inline constexpr u64 s_MinFramesBetweenSwitches = 30u;
inline constexpr u32 s_MinSwitchSamples = 8u;
};


// Disabled by default. Missing timing or a bad snapshot never changes deterministic assignment.
struct GpuTaskTimingFeedbackPolicy{
    f64 minimumAbsoluteBenefitSeconds = 0.0;
    f64 minimumRelativeBenefit = 0.0;
    u64 minimumFramesBetweenSwitches = GpuTaskTimingFeedbackPolicyDetail::s_MinFramesBetweenSwitches;
    u32 minimumSampleCount = GpuTaskTimingFeedbackPolicyDetail::s_MinSwitchSamples;
    u32 calibrationIntervalFrames = 1u;
    // Short on samples, one opted-in route probes every N frames. Cross-family probes need the family opt-in; cross-class also need timing.
    // Zero disables calibration.
    // Probes stop once every legal route reaches minimumSampleCount.
    bool enabled = false;


public:
    [[nodiscard]] bool valid()const noexcept;
};

[[nodiscard]] bool GpuTaskTimingHistoryMeetsMinimumSamples(const GpuTaskTimingHistory& history, const GpuTaskTimingFeedbackPolicy& policy)noexcept;
[[nodiscard]] bool GpuTaskTimingBenefitExceedsHysteresis(const GpuTaskTimingHistory& incumbent, const GpuTaskTimingHistory& candidate, const GpuTaskTimingFeedbackPolicy& policy)noexcept;
[[nodiscard]] bool GpuTaskTimingSwitchDwellElapsed(const GpuTaskTimingAssignmentState& assignment, u64 frameIndex, const GpuTaskTimingFeedbackPolicy& policy)noexcept;
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

// Only the owning store writes a snapshot. Compile reads plain values, blind to query completion and mutable frame state.
class GpuTaskTimingHistorySnapshot final : NoCopy{
    friend class GpuTaskTimingHistoryStore;


public:
    explicit GpuTaskTimingHistorySnapshot(GraphicsArena& arena)noexcept
        : m_histories(arena)
        , m_assignments(arena)
    {}


public:
    [[nodiscard]] bool valid()const noexcept{ return m_valid; }

    [[nodiscard]] u16 deviceGeneration()const noexcept{ return m_deviceGeneration; }
    [[nodiscard]] const GpuTaskTimingHistory* find(const GpuTaskTimingKey& key, const GpuPhysicalQueueId& physicalQueue)const noexcept;
    [[nodiscard]] const GpuTaskTimingAssignmentState* findAssignment(const GpuTaskTimingAssignmentKey& key)const noexcept;


private:
    void reset()noexcept;


private:
    GraphicsVector<GpuTaskTimingHistoryEntry> m_histories;
    GraphicsVector<GpuTaskTimingAssignmentState> m_assignments;
    Optional<GpuTaskTimingHistoryDetail::RouteIndex> m_historyIndex;
    Optional<GpuTaskTimingHistoryDetail::AssignmentIndex> m_assignmentIndex;
    u16 m_deviceGeneration = 0u;
    bool m_valid = false;
};


inline constexpr u32 s_DefaultGpuTaskTimingHistorySampleCount = 32u;

// Bounded rolling samples for one device generation. Mutation APIs stay out of validation, barriers, and waits.
class GpuTaskTimingHistoryStore final : NoCopy{
private:
    struct HistoryRecord{
        GpuTaskTimingHistoryEntry entry;
        GraphicsVector<f64> samples;


    public:
        explicit HistoryRecord(GraphicsArena& arena)noexcept
            : samples(arena)
        {}
    };


public:
    explicit GpuTaskTimingHistoryStore(GraphicsArena& arena, u32 maximumSamplesPerHistory = s_DefaultGpuTaskTimingHistorySampleCount)noexcept;


public:
    void reset()noexcept;
    void reset(GpuTaskTimingHistorySnapshot& outSnapshot)noexcept;
    void resetForDeviceGeneration(u16 deviceGeneration)noexcept;
    // A collected query is accepted GPU work. Dwell state advances only on newer frames, never backwards on late completion.
    [[nodiscard]] bool recordSample(const GpuTaskTimingKey& key, const GpuPhysicalQueueId& physicalQueue, f64 durationSeconds, u64 sourceFrameIndex);
    // Adds duration statistics without changing adaptive assignment state.
    // This records both bounded calibration observations and accepted samples whose assignment was committed separately at submission resolution.
    [[nodiscard]] bool recordNonCommittingSample(const GpuTaskTimingKey& key, const GpuPhysicalQueueId& physicalQueue, f64 durationSeconds);
    // Packet acceptance reports the selected route even when timestamp capacity was unavailable.
    [[nodiscard]] bool noteAcceptedAssignment(const GpuTaskTimingKey& key, const GpuPhysicalQueueId& physicalQueue, u64 sourceFrameIndex);
    void snapshot(GpuTaskTimingHistorySnapshot& outSnapshot)const;

    [[nodiscard]] u16 deviceGeneration()const noexcept{ return m_deviceGeneration; }

    [[nodiscard]] usize historyCount()const noexcept{ return m_histories.size(); }
    [[nodiscard]] const GpuTaskTimingHistory* find(const GpuTaskTimingKey& key, const GpuPhysicalQueueId& physicalQueue)const noexcept;
    [[nodiscard]] const GpuTaskTimingAssignmentState* findAssignment(const GpuTaskTimingAssignmentKey& key)const noexcept;


private:
    [[nodiscard]] HistoryRecord* findHistoryRecord(const GpuTaskTimingKey& key, const GpuPhysicalQueueId& physicalQueue)noexcept;
    [[nodiscard]] const HistoryRecord* findHistoryRecord(const GpuTaskTimingKey& key, const GpuPhysicalQueueId& physicalQueue)const noexcept;
    [[nodiscard]] GpuTaskTimingAssignmentState* findAssignmentState(const GpuTaskTimingAssignmentKey& key)noexcept;
    [[nodiscard]] const GpuTaskTimingAssignmentState* findAssignmentState(const GpuTaskTimingAssignmentKey& key)const noexcept;
    void rebuildHistory(HistoryRecord& record)noexcept;
    [[nodiscard]] bool promoteHistoryIndex();
    [[nodiscard]] bool promoteAssignmentIndex();


private:
    GraphicsArena& m_arena;
    GraphicsVector<HistoryRecord> m_histories;
    GraphicsVector<GpuTaskTimingAssignmentState> m_assignments;
    Optional<GpuTaskTimingHistoryDetail::RouteIndex> m_historyIndex;
    Optional<GpuTaskTimingHistoryDetail::AssignmentIndex> m_assignmentIndex;
    u32 m_maximumSamplesPerHistory = 1u;
    u16 m_deviceGeneration = 0u;
};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_CORE_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

