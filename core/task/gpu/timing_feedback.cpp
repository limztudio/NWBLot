// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "timing_feedback.h"

#include <global/scope_exit.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_CORE_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace __hidden_gpu_task_timing_feedback{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


inline constexpr usize s_LinearHistoryCount = 16u;

template<typename NameT>
[[nodiscard]] static usize HashTimingIdentity(const NameT& task, const u32 variant, const u32 resolutionClass)noexcept{
    usize hash = Hasher<NameT>{}(task);
    HashCombine(hash, variant);
    HashCombine(hash, resolutionClass);
    return hash;
}

[[nodiscard]] static usize HashTimingRoute(usize hash, const CommandQueue::Enum queue, const GpuPhysicalQueueId& physicalQueue)noexcept{
    HashCombine(hash, static_cast<u8>(queue));
    HashCombine(hash, physicalQueue.index);
    HashCombine(hash, physicalQueue.deviceGeneration);
    return hash;
}

[[nodiscard]] static GpuTaskTimingHistoryDetail::RouteKey StoredRouteKey(
    const GpuTaskTimingKey& key,
    const GpuPhysicalQueueId& physicalQueue
){
    return { key.task.identityHash(), key.variant, key.resolutionClass, key.queue, physicalQueue };
}

[[nodiscard]] static GpuTaskTimingHistoryDetail::AssignmentKey StoredAssignmentKey(const GpuTaskTimingAssignmentKey& key){
    return { key.task.identityHash(), key.variant, key.resolutionClass };
}

[[nodiscard]] static bool IsFinitePositiveDuration(const f64 value)noexcept{
    return value > 0.0 && value < Limit<f64>::s_Max;
}

[[nodiscard]] static bool IsFiniteNonNegative(const f64 value)noexcept{
    return value >= 0.0 && value < Limit<f64>::s_Max;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


usize GpuTaskTimingHistoryDetail::RouteHash::operator()(const RouteKey& value)const noexcept{
    using namespace __hidden_gpu_task_timing_feedback;
    return HashTimingRoute(HashTimingIdentity(value.task, value.variant, value.resolutionClass), value.queue, value.physicalQueue);
}

usize GpuTaskTimingHistoryDetail::RouteHash::operator()(const RouteLookup& value)const noexcept{
    using namespace __hidden_gpu_task_timing_feedback;
    return HashTimingRoute(HashTimingIdentity(value.key.task, value.key.variant, value.key.resolutionClass), value.key.queue, value.physicalQueue);
}

usize GpuTaskTimingHistoryDetail::AssignmentHash::operator()(const AssignmentKey& value)const noexcept{
    return __hidden_gpu_task_timing_feedback::HashTimingIdentity(value.task, value.variant, value.resolutionClass);
}

usize GpuTaskTimingHistoryDetail::AssignmentHash::operator()(const GpuTaskTimingAssignmentKey& value)const noexcept{
    return __hidden_gpu_task_timing_feedback::HashTimingIdentity(value.task, value.variant, value.resolutionClass);
}


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
    if(m_historyIndex)
        m_historyIndex->clear();
    if(m_assignmentIndex)
        m_assignmentIndex->clear();
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

    if(m_historyIndex){
        const auto found = m_historyIndex->find(GpuTaskTimingHistoryDetail::RouteLookup{ key, physicalQueue });
        return found == m_historyIndex->end() ? nullptr : &m_histories[found->second].history;
    }
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

    if(m_assignmentIndex){
        const auto found = m_assignmentIndex->find(key);
        return found == m_assignmentIndex->end() ? nullptr : &m_assignments[found->second];
    }
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
    if(m_historyIndex)
        m_historyIndex->clear();
    if(m_assignmentIndex)
        m_assignmentIndex->clear();
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
        if(!m_historyIndex && m_histories.size() == s_LinearHistoryCount)
            promoteHistoryIndex();
        record = &m_histories.emplace_back(m_arena);
        ScopeExit discardRecord([&]()noexcept{ m_histories.pop_back(); });

        record->entry.key = key;
        record->entry.physicalQueue = physicalQueue;
        record->samples.reserve(m_maximumSamplesPerHistory);
        if(m_historyIndex)
            m_historyIndex->emplace(StoredRouteKey(key, physicalQueue), m_histories.size() - 1u);
        discardRecord.release();
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
    using namespace __hidden_gpu_task_timing_feedback;

    if(!key.valid() || !physicalQueue.valid())
        return false;
    if(m_deviceGeneration == 0u)
        m_deviceGeneration = physicalQueue.deviceGeneration;
    if(m_deviceGeneration != physicalQueue.deviceGeneration)
        return false;

    const GpuTaskTimingAssignmentKey assignmentKey = GpuTaskTimingAssignmentKeyFromHistoryKey(key);
    GpuTaskTimingAssignmentState* assignment = findAssignmentState(assignmentKey);
    if(!assignment){
        if(!m_assignmentIndex && m_assignments.size() == s_LinearHistoryCount)
            promoteAssignmentIndex();
        assignment = &m_assignments.emplace_back();
        ScopeExit discardAssignment([&]()noexcept{ m_assignments.pop_back(); });

        assignment->key = assignmentKey;
        assignment->lastAcceptedQueue = physicalQueue;
        assignment->lastAcceptedFrameIndex = sourceFrameIndex;
        assignment->lastSwitchFrameIndex = sourceFrameIndex;
        assignment->hasAcceptedAssignment = true;
        if(m_assignmentIndex)
            m_assignmentIndex->emplace(StoredAssignmentKey(assignmentKey), m_assignments.size() - 1u);
        discardAssignment.release();
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

    ScopeExit resetFailedSnapshot([&outSnapshot]()noexcept{
        outSnapshot.m_historyIndex.reset();
        outSnapshot.m_assignmentIndex.reset();
        outSnapshot.reset();
    });

    outSnapshot.m_histories.reserve(m_histories.size());
    for(const HistoryRecord& record : m_histories)
        outSnapshot.m_histories.push_back(record.entry);

    outSnapshot.m_assignments.assign(m_assignments.begin(), m_assignments.end());
    if(m_historyIndex){
        if(!outSnapshot.m_historyIndex)
            outSnapshot.m_historyIndex.emplace(outSnapshot.m_histories.get_allocator().arena());
        *outSnapshot.m_historyIndex = *m_historyIndex;
    }else
        outSnapshot.m_historyIndex.reset();
    if(m_assignmentIndex){
        if(!outSnapshot.m_assignmentIndex)
            outSnapshot.m_assignmentIndex.emplace(outSnapshot.m_assignments.get_allocator().arena());
        *outSnapshot.m_assignmentIndex = *m_assignmentIndex;
    }else
        outSnapshot.m_assignmentIndex.reset();

    outSnapshot.m_deviceGeneration = m_deviceGeneration;
    outSnapshot.m_valid = true;
    resetFailedSnapshot.release();
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
    const GpuTaskTimingHistoryStore& store = *this;
    return const_cast<HistoryRecord*>(store.findHistoryRecord(key, physicalQueue));
}


const GpuTaskTimingHistoryStore::HistoryRecord* GpuTaskTimingHistoryStore::findHistoryRecord(
    const GpuTaskTimingKey& key,
    const GpuPhysicalQueueId& physicalQueue
)const noexcept{
    if(m_historyIndex){
        const auto found = m_historyIndex->find(GpuTaskTimingHistoryDetail::RouteLookup{ key, physicalQueue });
        return found == m_historyIndex->end() ? nullptr : &m_histories[found->second];
    }
    for(const HistoryRecord& record : m_histories){
        if(record.entry.key == key && record.entry.physicalQueue == physicalQueue)
            return &record;
    }
    return nullptr;
}


GpuTaskTimingAssignmentState* GpuTaskTimingHistoryStore::findAssignmentState(
    const GpuTaskTimingAssignmentKey& key
)noexcept{
    const GpuTaskTimingHistoryStore& store = *this;
    return const_cast<GpuTaskTimingAssignmentState*>(store.findAssignmentState(key));
}


const GpuTaskTimingAssignmentState* GpuTaskTimingHistoryStore::findAssignmentState(
    const GpuTaskTimingAssignmentKey& key
)const noexcept{
    if(m_assignmentIndex){
        const auto found = m_assignmentIndex->find(key);
        return found == m_assignmentIndex->end() ? nullptr : &m_assignments[found->second];
    }
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


void GpuTaskTimingHistoryStore::promoteHistoryIndex(){
    using namespace __hidden_gpu_task_timing_feedback;
    using namespace GpuTaskTimingHistoryDetail;

    NWB_ASSERT(!m_historyIndex && m_histories.size() == s_LinearHistoryCount);
    RouteIndex index(s_LinearHistoryCount * 4u, m_arena);
    for(usize recordIndex = 0u; recordIndex < m_histories.size(); ++recordIndex){
        const GpuTaskTimingHistoryEntry& entry = m_histories[recordIndex].entry;
        index.emplace(StoredRouteKey(entry.key, entry.physicalQueue), recordIndex);
    }
    static_assert(IsNothrowMoveConstructible_V<RouteIndex>);
    m_historyIndex.emplace(Move(index));
}

void GpuTaskTimingHistoryStore::promoteAssignmentIndex(){
    using namespace __hidden_gpu_task_timing_feedback;
    using namespace GpuTaskTimingHistoryDetail;

    NWB_ASSERT(!m_assignmentIndex && m_assignments.size() == s_LinearHistoryCount);
    AssignmentIndex index(s_LinearHistoryCount * 4u, m_arena);
    for(usize assignmentIndex = 0u; assignmentIndex < m_assignments.size(); ++assignmentIndex)
        index.emplace(StoredAssignmentKey(m_assignments[assignmentIndex].key), assignmentIndex);
    static_assert(IsNothrowMoveConstructible_V<AssignmentIndex>);
    m_assignmentIndex.emplace(Move(index));
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_CORE_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

