// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "timing_feedback.h"


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_CORE_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace __hidden_gpu_task_timing_feedback{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


[[nodiscard]] static bool IsFinitePositiveDuration(const f64 value)noexcept{
    return value > 0.0 && value < Limit<f64>::s_Max;
}

[[nodiscard]] static bool IsFiniteNonNegative(const f64 value)noexcept{
    return value >= 0.0 && value < Limit<f64>::s_Max;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


i32 GpuQueueAssignmentScore::total()const noexcept{
    const i64 score = static_cast<i64>(preference)
        + static_cast<i64>(overlap)
        - static_cast<i64>(queueLoad)
        - static_cast<i64>(incomingCrossings)
        - static_cast<i64>(outgoingCrossings)
        - static_cast<i64>(ownershipTransfers)
    ;
    if(score < static_cast<i64>(Limit<i32>::s_Min))
        return Limit<i32>::s_Min;
    if(score > static_cast<i64>(Limit<i32>::s_Max))
        return Limit<i32>::s_Max;
    return static_cast<i32>(score);
}


bool GpuTaskTimingHistory::valid()const noexcept{
    using namespace __hidden_gpu_task_timing_feedback;

    return sampleCount != 0u
        && IsFinitePositiveDuration(averageSeconds)
        && IsFinitePositiveDuration(minimumSeconds)
        && IsFinitePositiveDuration(maximumSeconds)
        && minimumSeconds <= averageSeconds
        && averageSeconds <= maximumSeconds
    ;
}


bool GpuTaskTimingFeedbackPolicy::valid()const noexcept{
    using namespace __hidden_gpu_task_timing_feedback;

    return minimumSampleCount != 0u
        && IsFiniteNonNegative(minimumAbsoluteBenefitSeconds)
        && IsFiniteNonNegative(minimumRelativeBenefit)
        && minimumRelativeBenefit <= 1.0
    ;
}


GpuTaskTimingQueueOverrideStatus::Enum ValidateGpuTaskTimingQueueOverrides(
    const GpuTaskTimingQueueOverride* const overrides,
    const usize overrideCount,
    const u16 deviceGeneration
)noexcept{
    if(overrideCount == 0u)
        return GpuTaskTimingQueueOverrideStatus::Success;
    if(!overrides)
        return GpuTaskTimingQueueOverrideStatus::MissingOverrides;
    if(deviceGeneration == 0u)
        return GpuTaskTimingQueueOverrideStatus::MismatchedDeviceGeneration;

    for(usize overrideIndex = 0u; overrideIndex < overrideCount; ++overrideIndex){
        const GpuTaskTimingQueueOverride& override = overrides[overrideIndex];
        if(!override.key.valid())
            return GpuTaskTimingQueueOverrideStatus::InvalidKey;
        if(!override.queue.valid())
            return GpuTaskTimingQueueOverrideStatus::InvalidQueue;
        if(override.queue.deviceGeneration != deviceGeneration)
            return GpuTaskTimingQueueOverrideStatus::MismatchedDeviceGeneration;

        for(usize previousIndex = 0u; previousIndex < overrideIndex; ++previousIndex){
            if(overrides[previousIndex].key == override.key)
                return GpuTaskTimingQueueOverrideStatus::DuplicateTaskKey;
        }
    }
    return GpuTaskTimingQueueOverrideStatus::Success;
}


const GpuTaskTimingQueueOverride* FindGpuTaskTimingQueueOverride(
    const GpuTaskTimingQueueOverride* const overrides,
    const usize overrideCount,
    const GpuTaskTimingKey& key
)noexcept{
    if(!overrides || !key.valid())
        return nullptr;

    for(usize overrideIndex = 0u; overrideIndex < overrideCount; ++overrideIndex){
        const GpuTaskTimingQueueOverride& override = overrides[overrideIndex];
        if(override.key == key)
            return &override;
    }
    return nullptr;
}


bool GpuTaskTimingHistoryMeetsMinimumSamples(
    const GpuTaskTimingHistory& history,
    const GpuTaskTimingFeedbackPolicy& policy
)noexcept{
    return policy.valid() && history.valid() && history.sampleCount >= policy.minimumSampleCount;
}


bool GpuTaskTimingBenefitExceedsHysteresis(
    const GpuTaskTimingHistory& incumbent,
    const GpuTaskTimingHistory& candidate,
    const GpuTaskTimingFeedbackPolicy& policy
)noexcept{
    if(
        !policy.valid()
        || !incumbent.valid()
        || !candidate.valid()
        || candidate.averageSeconds >= incumbent.averageSeconds
    )
        return false;

    const f64 benefitSeconds = incumbent.averageSeconds - candidate.averageSeconds;
    if(benefitSeconds < policy.minimumAbsoluteBenefitSeconds)
        return false;
    return benefitSeconds / incumbent.averageSeconds >= policy.minimumRelativeBenefit;
}


bool GpuTaskTimingSwitchDwellElapsed(
    const GpuTaskTimingAssignmentState& assignment,
    const u64 frameIndex,
    const GpuTaskTimingFeedbackPolicy& policy
)noexcept{
    if(!policy.valid() || !assignment.valid() || frameIndex < assignment.lastSwitchFrameIndex)
        return false;
    return frameIndex - assignment.lastSwitchFrameIndex >= policy.minimumFramesBetweenSwitches;
}


bool GpuTaskTimingFeedbackCanSwitch(
    const GpuTaskTimingHistory& incumbent,
    const GpuTaskTimingHistory& candidate,
    const GpuTaskTimingAssignmentState& assignment,
    const GpuPhysicalQueueId& incumbentQueue,
    const GpuPhysicalQueueId& candidateQueue,
    const u64 frameIndex,
    const GpuTaskTimingFeedbackPolicy& policy
)noexcept{
    return policy.enabled
        && incumbentQueue.valid()
        && candidateQueue.valid()
        && incumbentQueue != candidateQueue
        && incumbentQueue.deviceGeneration == candidateQueue.deviceGeneration
        && assignment.valid()
        && assignment.lastAcceptedQueue == incumbentQueue
        && GpuTaskTimingHistoryMeetsMinimumSamples(incumbent, policy)
        && GpuTaskTimingHistoryMeetsMinimumSamples(candidate, policy)
        && GpuTaskTimingBenefitExceedsHysteresis(incumbent, candidate, policy)
        && GpuTaskTimingSwitchDwellElapsed(assignment, frameIndex, policy)
    ;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


void GpuTaskTimingHistorySnapshot::reset()noexcept{
    m_histories.clear();
    m_assignments.clear();
    m_deviceGeneration = 0u;
    m_valid = false;
}


const GpuTaskTimingHistory* GpuTaskTimingHistorySnapshot::find(
    const GpuTaskTimingKey& key,
    const GpuPhysicalQueueId& physicalQueue
)const noexcept{
    if(
        !m_valid
        || !key.valid()
        || !physicalQueue.valid()
        || physicalQueue.deviceGeneration != m_deviceGeneration
    )
        return nullptr;

    for(const GpuTaskTimingHistoryEntry& entry : m_histories){
        if(entry.key == key && entry.physicalQueue == physicalQueue)
            return &entry.history;
    }
    return nullptr;
}


const GpuTaskTimingAssignmentState* GpuTaskTimingHistorySnapshot::findAssignment(
    const GpuTaskTimingAssignmentKey& key
)const noexcept{
    if(!m_valid || !key.valid())
        return nullptr;

    for(const GpuTaskTimingAssignmentState& assignment : m_assignments){
        if(assignment.key == key)
            return &assignment;
    }
    return nullptr;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


GpuTaskTimingHistoryStore::GpuTaskTimingHistoryStore(
    GraphicsArena& arena,
    const u32 maximumSamplesPerHistory
)
    : m_arena(arena)
    , m_histories(arena)
    , m_assignments(arena)
    , m_maximumSamplesPerHistory(maximumSamplesPerHistory == 0u ? 1u : maximumSamplesPerHistory)
{
    NWB_ASSERT(maximumSamplesPerHistory != 0u);
}


void GpuTaskTimingHistoryStore::reset()noexcept{
    m_histories.clear();
    m_assignments.clear();
    m_deviceGeneration = 0u;
}

void GpuTaskTimingHistoryStore::reset(GpuTaskTimingHistorySnapshot& outSnapshot)noexcept{
    reset();
    outSnapshot.reset();
}


void GpuTaskTimingHistoryStore::resetForDeviceGeneration(const u16 deviceGeneration)noexcept{
    if(deviceGeneration == 0u){
        reset();
        return;
    }
    if(m_deviceGeneration == deviceGeneration)
        return;

    reset();
    m_deviceGeneration = deviceGeneration;
}


bool GpuTaskTimingHistoryStore::recordSample(
    const GpuTaskTimingKey& key,
    const GpuPhysicalQueueId& physicalQueue,
    const f64 durationSeconds,
    const u64 sourceFrameIndex
){
    using namespace __hidden_gpu_task_timing_feedback;

    if(!IsFinitePositiveDuration(durationSeconds))
        return false;
    if(!noteAcceptedAssignment(key, physicalQueue, sourceFrameIndex))
        return false;

    return recordNonCommittingSample(key, physicalQueue, durationSeconds);
}


bool GpuTaskTimingHistoryStore::recordNonCommittingSample(
    const GpuTaskTimingKey& key,
    const GpuPhysicalQueueId& physicalQueue,
    const f64 durationSeconds
){
    using namespace __hidden_gpu_task_timing_feedback;

    if(!key.valid() || !physicalQueue.valid() || !IsFinitePositiveDuration(durationSeconds))
        return false;
    if(m_deviceGeneration == 0u)
        m_deviceGeneration = physicalQueue.deviceGeneration;
    if(m_deviceGeneration != physicalQueue.deviceGeneration)
        return false;

    HistoryRecord* record = findHistoryRecord(key, physicalQueue);
    if(!record){
        record = &m_histories.emplace_back(m_arena);
        record->entry.key = key;
        record->entry.physicalQueue = physicalQueue;
        record->samples.reserve(m_maximumSamplesPerHistory);
    }
    if(record->samples.size() == m_maximumSamplesPerHistory)
        record->samples.erase(record->samples.begin());
    record->samples.push_back(durationSeconds);
    rebuildHistory(*record);
    return true;
}


bool GpuTaskTimingHistoryStore::noteAcceptedAssignment(
    const GpuTaskTimingKey& key,
    const GpuPhysicalQueueId& physicalQueue,
    const u64 sourceFrameIndex
){
    if(!key.valid() || !physicalQueue.valid())
        return false;
    if(m_deviceGeneration == 0u)
        m_deviceGeneration = physicalQueue.deviceGeneration;
    if(m_deviceGeneration != physicalQueue.deviceGeneration)
        return false;

    const GpuTaskTimingAssignmentKey assignmentKey = GpuTaskTimingAssignmentKeyFromHistoryKey(key);
    GpuTaskTimingAssignmentState* assignment = findAssignmentState(assignmentKey);
    if(!assignment){
        assignment = &m_assignments.emplace_back();
        assignment->key = assignmentKey;
        assignment->lastAcceptedQueue = physicalQueue;
        assignment->lastAcceptedFrameIndex = sourceFrameIndex;
        assignment->lastSwitchFrameIndex = sourceFrameIndex;
        assignment->hasAcceptedAssignment = true;
        return true;
    }
    if(sourceFrameIndex < assignment->lastAcceptedFrameIndex)
        return true;
    if(
        sourceFrameIndex == assignment->lastAcceptedFrameIndex
        && physicalQueue != assignment->lastAcceptedQueue
    )
        return false;

    if(physicalQueue != assignment->lastAcceptedQueue){
        assignment->lastAcceptedQueue = physicalQueue;
        assignment->lastSwitchFrameIndex = sourceFrameIndex;
    }
    assignment->lastAcceptedFrameIndex = sourceFrameIndex;
    assignment->hasAcceptedAssignment = true;
    return true;
}


void GpuTaskTimingHistoryStore::snapshot(GpuTaskTimingHistorySnapshot& outSnapshot)const{
    outSnapshot.reset();
    if(m_deviceGeneration == 0u)
        return;

    outSnapshot.m_histories.reserve(m_histories.size());
    for(const HistoryRecord& record : m_histories)
        outSnapshot.m_histories.push_back(record.entry);

    outSnapshot.m_assignments.reserve(m_assignments.size());
    for(const GpuTaskTimingAssignmentState& assignment : m_assignments)
        outSnapshot.m_assignments.push_back(assignment);

    outSnapshot.m_deviceGeneration = m_deviceGeneration;
    outSnapshot.m_valid = true;
}


const GpuTaskTimingHistory* GpuTaskTimingHistoryStore::find(
    const GpuTaskTimingKey& key,
    const GpuPhysicalQueueId& physicalQueue
)const noexcept{
    const HistoryRecord* const record = findHistoryRecord(key, physicalQueue);
    return record ? &record->entry.history : nullptr;
}


const GpuTaskTimingAssignmentState* GpuTaskTimingHistoryStore::findAssignment(
    const GpuTaskTimingAssignmentKey& key
)const noexcept{
    return findAssignmentState(key);
}


GpuTaskTimingHistoryStore::HistoryRecord* GpuTaskTimingHistoryStore::findHistoryRecord(
    const GpuTaskTimingKey& key,
    const GpuPhysicalQueueId& physicalQueue
)noexcept{
    for(HistoryRecord& record : m_histories){
        if(record.entry.key == key && record.entry.physicalQueue == physicalQueue)
            return &record;
    }
    return nullptr;
}


const GpuTaskTimingHistoryStore::HistoryRecord* GpuTaskTimingHistoryStore::findHistoryRecord(
    const GpuTaskTimingKey& key,
    const GpuPhysicalQueueId& physicalQueue
)const noexcept{
    for(const HistoryRecord& record : m_histories){
        if(record.entry.key == key && record.entry.physicalQueue == physicalQueue)
            return &record;
    }
    return nullptr;
}


GpuTaskTimingAssignmentState* GpuTaskTimingHistoryStore::findAssignmentState(
    const GpuTaskTimingAssignmentKey& key
)noexcept{
    for(GpuTaskTimingAssignmentState& assignment : m_assignments){
        if(assignment.key == key)
            return &assignment;
    }
    return nullptr;
}


const GpuTaskTimingAssignmentState* GpuTaskTimingHistoryStore::findAssignmentState(
    const GpuTaskTimingAssignmentKey& key
)const noexcept{
    for(const GpuTaskTimingAssignmentState& assignment : m_assignments){
        if(assignment.key == key)
            return &assignment;
    }
    return nullptr;
}


void GpuTaskTimingHistoryStore::rebuildHistory(HistoryRecord& record)noexcept{
    NWB_ASSERT(!record.samples.empty());
    if(record.samples.empty()){
        record.entry.history = {};
        return;
    }

    f64 averageSeconds = 0.0;
    f64 minimumSeconds = record.samples.front();
    f64 maximumSeconds = record.samples.front();
    u32 sampleCount = 0u;
    for(const f64 sampleSeconds : record.samples){
        ++sampleCount;
        averageSeconds += (sampleSeconds - averageSeconds) / static_cast<f64>(sampleCount);
        if(sampleSeconds < minimumSeconds)
            minimumSeconds = sampleSeconds;
        if(sampleSeconds > maximumSeconds)
            maximumSeconds = sampleSeconds;
    }
    record.entry.history.averageSeconds = averageSeconds;
    record.entry.history.minimumSeconds = minimumSeconds;
    record.entry.history.maximumSeconds = maximumSeconds;
    record.entry.history.sampleCount = sampleCount;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_CORE_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

