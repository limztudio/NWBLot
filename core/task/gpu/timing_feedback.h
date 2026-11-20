// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#pragma once


#include "types.h"


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_CORE_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


struct GpuQueueAssignmentScore{
    i32 overlap = 0;
    i32 queueLoad = 0;
    i32 incomingCrossings = 0;
    i32 outgoingCrossings = 0;
    i32 ownershipTransfers = 0;


public:
    [[nodiscard]] i32 total()const noexcept;
};

struct GpuTaskTimingAssignmentKey{
    Name task = s_NameNone;
    u32 variant = 0u;
    u32 resolutionClass = 0u;


public:
    [[nodiscard]] constexpr b8 valid()const noexcept{ return static_cast<b8>(task); }
};
inline constexpr b8 operator==(const GpuTaskTimingAssignmentKey& lhs, const GpuTaskTimingAssignmentKey& rhs)noexcept{
    return lhs.task == rhs.task && lhs.variant == rhs.variant && lhs.resolutionClass == rhs.resolutionClass;
}

inline constexpr b8 operator!=(const GpuTaskTimingAssignmentKey& lhs, const GpuTaskTimingAssignmentKey& rhs)noexcept{
    return !(lhs == rhs);
}

struct GpuTaskTimingKey{
    Name task = s_NameNone;
    u32 variant = 0u;
    u32 resolutionClass = 0u;
    CommandQueue::Enum queue = CommandQueue::Graphics;


public:
    [[nodiscard]] constexpr b8 valid()const noexcept{ return task && queue < CommandQueue::kCount; }
};
inline constexpr b8 operator==(const GpuTaskTimingKey& lhs, const GpuTaskTimingKey& rhs)noexcept{
    return lhs.task == rhs.task && lhs.variant == rhs.variant && lhs.resolutionClass == rhs.resolutionClass && lhs.queue == rhs.queue;
}

inline constexpr b8 operator!=(const GpuTaskTimingKey& lhs, const GpuTaskTimingKey& rhs)noexcept{
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
    GpuPhysicalQueueId physicalQueue;
    u32 variant;
    u32 resolutionClass;
    CommandQueue::Enum queue;


public:
    [[nodiscard]] constexpr b8 operator==(const RouteKey&)const noexcept = default;
};

struct AssignmentKey{
    NameHash task;
    u32 variant;
    u32 resolutionClass;


public:
    [[nodiscard]] constexpr b8 operator==(const AssignmentKey&)const noexcept = default;
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
    [[nodiscard]] b8 operator()(const RouteKey& lhs, const RouteKey& rhs)const noexcept{
        return lhs == rhs;
    }

    [[nodiscard]] b8 operator()(const RouteKey& lhs, const RouteLookup& rhs)const noexcept{
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
    [[nodiscard]] b8 operator()(const AssignmentKey& lhs, const AssignmentKey& rhs)const noexcept{
        return lhs == rhs;
    }

    [[nodiscard]] b8 operator()(const AssignmentKey& lhs, const GpuTaskTimingAssignmentKey& rhs)const noexcept{
        return lhs.task == rhs.task.identityHash() && lhs.variant == rhs.variant && lhs.resolutionClass == rhs.resolutionClass;
    }
};

using RouteIndex = HashMap<RouteKey, usize, ::ArenaBase, RouteHash, RouteEqual>;
using AssignmentIndex = HashMap<AssignmentKey, usize, ::ArenaBase, AssignmentHash, AssignmentEqual>;


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


struct GpuTaskTimingHistory{
    f64 averageSeconds = 0.0;
    f64 minimumSeconds = 0.0;
    f64 maximumSeconds = 0.0;
    u32 sampleCount = 0u;


public:
    [[nodiscard]] b8 valid()const noexcept;
};

struct GpuTaskTimingHistoryEntry{
    GpuTaskTimingKey key;
    GpuTaskTimingHistory history;
    GpuPhysicalQueueId physicalQueue;


public:
    [[nodiscard]] b8 valid()const noexcept{
        return key.valid() && physicalQueue.valid() && history.valid();
    }
};

struct GpuTaskTimingAssignmentState{
    GpuTaskTimingAssignmentKey key;
    u64 lastAcceptedFrameIndex = 0u;
    u64 lastSwitchFrameIndex = 0u;
    GpuPhysicalQueueId lastAcceptedQueue;
    b8 hasAcceptedAssignment = false;


public:
    [[nodiscard]] b8 valid()const noexcept{
        return key.valid() && hasAcceptedAssignment && lastAcceptedQueue.valid() && lastSwitchFrameIndex <= lastAcceptedFrameIndex;
    }
};


namespace GpuTaskTimingFeedbackPolicyDetail{
inline constexpr u64 s_MinFramesBetweenSwitches = 30u;
inline constexpr u32 s_MinSwitchSamples = 8u;
};

struct GpuTaskTimingFeedbackPolicy{
    f64 minimumAbsoluteBenefitSeconds = 0.0;
    f64 minimumRelativeBenefit = 0.0;
    u64 minimumFramesBetweenSwitches = GpuTaskTimingFeedbackPolicyDetail::s_MinFramesBetweenSwitches;
    u32 minimumSampleCount = GpuTaskTimingFeedbackPolicyDetail::s_MinSwitchSamples;
    u32 calibrationIntervalFrames = 1u;
    b8 enabled = false;


public:
    [[nodiscard]] b8 valid()const noexcept;
};



class GpuTaskTimingHistoryStore;

class GpuTaskTimingHistorySnapshot final : NoCopy{
    friend class GpuTaskTimingHistoryStore;


public:
    explicit GpuTaskTimingHistorySnapshot(::ArenaBase& arena)noexcept
        : m_histories(arena)
        , m_assignments(arena)
    {}


public:
    [[nodiscard]] b8 valid()const noexcept{ return m_valid; }

    [[nodiscard]] u16 deviceGeneration()const noexcept{ return m_deviceGeneration; }
    [[nodiscard]] const GpuTaskTimingHistory* find(const GpuTaskTimingKey& key, const GpuPhysicalQueueId& physicalQueue)const noexcept;
    [[nodiscard]] const GpuTaskTimingAssignmentState* findAssignment(const GpuTaskTimingAssignmentKey& key)const noexcept;


private:
    void reset()noexcept;


private:
    Vector<GpuTaskTimingHistoryEntry, ::ArenaBase> m_histories;
    Vector<GpuTaskTimingAssignmentState, ::ArenaBase> m_assignments;
    Optional<GpuTaskTimingHistoryDetail::RouteIndex> m_historyIndex;
    Optional<GpuTaskTimingHistoryDetail::AssignmentIndex> m_assignmentIndex;
    u64 m_sourceStoreId = 0u;
    u64 m_sourceRevision = 0u;
    u16 m_deviceGeneration = 0u;
    b8 m_valid = false;
};


inline constexpr u32 s_DefaultGpuTaskTimingHistorySampleCount = 32u;

class GpuTaskTimingHistoryStore final : NoCopy{
private:
    struct HistoryRecord{
        GpuTaskTimingHistoryEntry entry;
        Vector<f64, ::ArenaBase> samples;


    public:
        explicit HistoryRecord(::ArenaBase& arena)noexcept
            : samples(arena)
        {}
    };


public:
    explicit GpuTaskTimingHistoryStore(::ArenaBase& arena, u32 maximumSamplesPerHistory = s_DefaultGpuTaskTimingHistorySampleCount)noexcept;


public:
    void reset()noexcept;
    void reset(GpuTaskTimingHistorySnapshot& outSnapshot)noexcept;
    void resetForDeviceGeneration(u16 deviceGeneration)noexcept;
    [[nodiscard]] b8 recordSample(const GpuTaskTimingKey& key, const GpuPhysicalQueueId& physicalQueue, f64 durationSeconds, u64 sourceFrameIndex);
    [[nodiscard]] b8 recordNonCommittingSample(const GpuTaskTimingKey& key, const GpuPhysicalQueueId& physicalQueue, f64 durationSeconds);
    [[nodiscard]] b8 noteAcceptedAssignment(const GpuTaskTimingKey& key, const GpuPhysicalQueueId& physicalQueue, u64 sourceFrameIndex);
    void snapshot(GpuTaskTimingHistorySnapshot& outSnapshot)const;

    [[nodiscard]] u16 deviceGeneration()const noexcept{ return m_deviceGeneration; }

    [[nodiscard]] usize historyCount()const noexcept{ return m_histories.size(); }

    [[nodiscard]] const GpuTaskTimingHistory* find(const GpuTaskTimingKey& key, const GpuPhysicalQueueId& physicalQueue)const noexcept;

    [[nodiscard]] const GpuTaskTimingAssignmentState* findAssignment(const GpuTaskTimingAssignmentKey& key)const noexcept{
        return findAssignmentState(key);
    }


private:
    [[nodiscard]] HistoryRecord* findHistoryRecord(const GpuTaskTimingKey& key, const GpuPhysicalQueueId& physicalQueue)noexcept;
    [[nodiscard]] const HistoryRecord* findHistoryRecord(const GpuTaskTimingKey& key, const GpuPhysicalQueueId& physicalQueue)const noexcept;
    [[nodiscard]] GpuTaskTimingAssignmentState* findAssignmentState(const GpuTaskTimingAssignmentKey& key)noexcept;
    [[nodiscard]] const GpuTaskTimingAssignmentState* findAssignmentState(const GpuTaskTimingAssignmentKey& key)const noexcept;
    void rebuildHistory(HistoryRecord& record)noexcept;
    [[nodiscard]] b8 promoteHistoryIndex();
    [[nodiscard]] b8 promoteAssignmentIndex();


private:
    ::ArenaBase& m_arena;
    Vector<HistoryRecord, ::ArenaBase> m_histories;
    Vector<GpuTaskTimingAssignmentState, ::ArenaBase> m_assignments;
    Optional<GpuTaskTimingHistoryDetail::RouteIndex> m_historyIndex;
    Optional<GpuTaskTimingHistoryDetail::AssignmentIndex> m_assignmentIndex;
    u64 m_storeId = 0u;
    u64 m_mutationRevision = 0u;
    u32 m_maximumSamplesPerHistory = 1u;
    u16 m_deviceGeneration = 0u;
};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_CORE_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

