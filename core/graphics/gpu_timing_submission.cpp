// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "gpu_timing.h"

#include "backend_selection.h"


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_CORE_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


GpuTimingSubmissionTicket::RecordingScope::RecordingScope(GpuTimingSubmissionTicket& ticket)
    : m_ticket(ticket)
    , m_previousTicket(m_ticket.activateOnCurrentThread())
{}

GpuTimingSubmissionTicket::RecordingScope::~RecordingScope(){
    m_ticket.deactivateOnCurrentThread(m_previousTicket);
}


GpuTimingSubmissionTicket::GpuTimingSubmissionTicket(GpuTimingRecorder& recorder)
    : m_recorder(recorder)
    , m_scopes(recorder.m_arena)
{}

GpuTimingSubmissionTicket::~GpuTimingSubmissionTicket(){
    discard();
}

bool GpuTimingSubmissionTicket::submit(
    Device& device,
    CommandList* const* commandLists,
    const usize commandListCount,
    const CommandQueue::Enum executionQueue
){
    if(!prepareSubmission(commandLists, commandListCount))
        return false;

    const QueueSubmissionToken token = device.executeCommandLists(
        commandLists,
        commandListCount,
        executionQueue,
        QueueSubmissionDesc{}
    );
    resolveSubmission(token);
    return token.valid();
}

QueueSubmissionToken GpuTimingSubmissionTicket::submit(
    Device& device,
    CommandList* const* commandLists,
    const usize commandListCount,
    const RenderLane::Enum executionLane,
    const QueueSubmissionDesc& submitDesc
){
    if(!prepareSubmission(commandLists, commandListCount))
        return {};

    const QueueSubmissionToken token = device.executeCommandLists(
        commandLists,
        commandListCount,
        executionLane,
        submitDesc
    );
    resolveSubmission(token);
    return token;
}

QueueSubmissionToken GpuTimingSubmissionTicket::submit(
    Device& device,
    CommandList* const* commandLists,
    const usize commandListCount,
    const GpuPhysicalQueueId& executionQueue,
    const QueueSubmissionDesc& submitDesc
){
    if(!prepareSubmission(commandLists, commandListCount))
        return {};

    const QueueSubmissionToken token = device.executeCommandLists(
        commandLists,
        commandListCount,
        executionQueue,
        submitDesc
    );
    resolveSubmission(token);
    return token;
}

QueueSubmissionToken GpuTimingSubmissionTicket::submit(
    Device& device,
    CommandList* const* commandLists,
    const usize commandListCount,
    const CommandQueue::Enum executionQueue,
    const QueueSubmissionDesc& submitDesc
){
    if(!prepareSubmission(commandLists, commandListCount))
        return {};

    const QueueSubmissionToken token = device.executeCommandLists(
        commandLists,
        commandListCount,
        executionQueue,
        submitDesc
    );
    resolveSubmission(token);
    return token;
}

bool GpuTimingSubmissionTicket::prepareSubmission(CommandList* const* commandLists, const usize commandListCount){
    {
        ScopedLock lock(m_mutex);
        NWB_ASSERT_MSG(m_recordingScopeCount == 0u, NWB_TEXT("GPU timing submission ticket submitted while command recording is still active"));
        if(m_resolved || m_recordingScopeCount != 0u)
            return false;
    }

    if(!commandLists || commandListCount == 0u){
        discard();
        return false;
    }

    // Queue::submit omits command lists whose buffer is absent. Reject that condition before submission rather than
    // allowing a producer from a split timing scope to execute without the consumer that contains its end timestamp.
    for(usize i = 0u; i < commandListCount; ++i){
        if(!commandLists[i] || !commandLists[i]->hasCommandBuffer()){
            discard();
            return false;
        }
    }

    return true;
}

void GpuTimingSubmissionTicket::resolveSubmission(const QueueSubmissionToken& token){
    if(token.valid())
        confirm(token);
    else
        discard();
}

void GpuTimingSubmissionTicket::discard(){
    ScopedLock lock(m_mutex);
    if(m_resolved)
        return;

    NWB_ASSERT_MSG(m_recordingScopeCount == 0u, NWB_TEXT("GPU timing submission ticket discarded while command recording is still active"));
    if(m_recordingScopeCount != 0u)
        return;

    for(const GpuTimingScope& scope : m_scopes)
        m_recorder.discardScope(scope);
    m_scopes.clear();
    m_resolved = true;
}

void GpuTimingSubmissionTicket::trackScope(const GpuTimingScope& scope){
    if(!scope.valid())
        return;

    ScopedLock lock(m_mutex);
    NWB_ASSERT_MSG(!m_resolved, NWB_TEXT("GPU timing scope ended after its submission ticket was resolved"));
    if(m_resolved){
        m_recorder.discardScope(scope);
        return;
    }

    m_scopes.push_back(scope);
}

GpuTimingSubmissionTicket* GpuTimingSubmissionTicket::activateOnCurrentThread(){
    ScopedLock lock(m_mutex);
    NWB_ASSERT_MSG(!m_resolved, NWB_TEXT("GPU timing submission ticket activated after it was resolved"));
    if(m_resolved)
        return GpuTimingRecorder::s_activeSubmissionTicket;

    GpuTimingSubmissionTicket* previousTicket = GpuTimingRecorder::s_activeSubmissionTicket;
    GpuTimingRecorder::s_activeSubmissionTicket = this;
    ++m_recordingScopeCount;
    return previousTicket;
}

void GpuTimingSubmissionTicket::deactivateOnCurrentThread(GpuTimingSubmissionTicket* previousTicket){
    NWB_ASSERT_MSG(GpuTimingRecorder::s_activeSubmissionTicket == this, NWB_TEXT("GPU timing submission ticket recording scope closed out of order"));
    if(GpuTimingRecorder::s_activeSubmissionTicket != this)
        return;

    GpuTimingRecorder::s_activeSubmissionTicket = previousTicket;
    ScopedLock lock(m_mutex);
    NWB_ASSERT(m_recordingScopeCount > 0u);
    if(m_recordingScopeCount > 0u)
        --m_recordingScopeCount;
}

void GpuTimingSubmissionTicket::confirm(const QueueSubmissionToken& token){
    ScopedLock lock(m_mutex);
    if(m_resolved)
        return;

    NWB_ASSERT_MSG(m_recordingScopeCount == 0u, NWB_TEXT("GPU timing submission ticket confirmed while command recording is still active"));
    if(m_recordingScopeCount != 0u)
        return;

    bool confirmed = true;
    for(const GpuTimingScope& scope : m_scopes){
        if(!m_recorder.confirmScope(scope, token, true)){
            m_recorder.quarantineScope(scope);
            confirmed = false;
        }
    }
    if(!confirmed)
        NWB_LOGGER_ERROR(NWB_TEXT("GPU timing submission accepted with an invalid query ownership transition; affected queries were quarantined"));

    m_scopes.clear();
    m_resolved = true;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


GpuTimingFrameTransaction::GpuTimingFrameTransaction(GpuTimingRecorder& recorder)
    : m_recorder(recorder)
{}

GpuTimingFrameTransaction::~GpuTimingFrameTransaction(){
    discard();
}

bool GpuTimingFrameTransaction::begin(
    const GpuTimingScopeDefinition& scopeDefinition,
    Device& device,
    CommandList& commandList,
    const GpuTimingSampleAttribution attribution
){
    if(m_state != State::Idle)
        return false;
    if(&commandList.getDevice() != &device){
        m_state = State::Resolved;
        return false;
    }
    if(!scopeDefinition.valid()){
        m_state = State::Inactive;
        return true;
    }
    if(!device.supportsComparableGpuTimestamps(commandList.getDescription().physicalQueue)){
        m_state = State::Inactive;
        return true;
    }

    if(!m_recorder.beginDeferredScope(scopeDefinition.identity, device, commandList, attribution, m_scope)){
        m_scope = {};
        m_state = State::Resolved;
        return false;
    }
    m_state = m_scope.valid() ? State::BeginRecorded : State::Inactive;
    return true;
}

bool GpuTimingFrameTransaction::recordEnd(CommandList& commandList){
    if(m_state == State::Inactive)
        return true;
    if(
        m_state != State::BeginRecorded
        && m_state != State::BeginAccepted
    )
        return false;

    if(!m_recorder.recordDeferredScopeEnd(commandList, m_scope)){
        if(m_state == State::BeginRecorded){
            m_recorder.discardScope(m_scope);
            m_scope = {};
            m_state = State::Resolved;
        }
        return false;
    }
    m_state = State::EndRecorded;
    return true;
}

bool GpuTimingFrameTransaction::confirmBeginSubmission(const QueueSubmissionToken& token){
    if(m_state == State::Inactive || m_state == State::Resolved)
        return true;

    NWB_ASSERT(m_state == State::BeginRecorded || m_state == State::EndRecorded);
    if(
        (m_state != State::BeginRecorded && m_state != State::EndRecorded)
        || !m_recorder.validateScopeSubmission(m_scope, token)
    ){
        m_recorder.quarantineScope(m_scope);
        m_scope = {};
        m_state = State::Resolved;
        return false;
    }

    if(m_beginSubmission.valid()){
        const bool matches = m_beginSubmission.queue == token.queue
            && m_beginSubmission.value == token.value
            && m_beginSubmission.physicalQueueIndex == token.physicalQueueIndex
            && m_beginSubmission.deviceGeneration == token.deviceGeneration
        ;
        if(!matches){
            m_recorder.quarantineScope(m_scope);
            m_scope = {};
            m_state = State::Resolved;
        }
        return matches;
    }

    m_beginSubmission = token;
    if(m_state == State::BeginRecorded)
        m_state = State::BeginAccepted;
    return true;
}

bool GpuTimingFrameTransaction::confirmEndSubmission(
    const QueueSubmissionToken& token,
    const bool publishSample
){
    if(m_state == State::Inactive || m_state == State::Resolved)
        return true;

    const bool orderedSameQueue = m_beginSubmission.valid()
        && m_beginSubmission.hasPhysicalQueueIdentity()
        && token.valid()
        && token.hasPhysicalQueueIdentity()
        && token.queue == m_beginSubmission.queue
        && token.physicalQueueIndex == m_beginSubmission.physicalQueueIndex
        && token.deviceGeneration == m_beginSubmission.deviceGeneration
        && token.value >= m_beginSubmission.value
    ;
    if(
        m_state != State::EndRecorded
        || !orderedSameQueue
        || !m_recorder.confirmScope(m_scope, token, publishSample)
    ){
        m_recorder.quarantineScope(m_scope);
        m_scope = {};
        m_state = State::Resolved;
        return false;
    }

    m_scope = {};
    m_state = State::Resolved;
    return true;
}

bool GpuTimingFrameTransaction::needsRetirement()const{
    return m_beginSubmission.valid() && m_scope.valid();
}

bool GpuTimingFrameTransaction::prepareForRecovery(){
    if(!needsRetirement())
        return true;

    // The old endpoint lives only in a packet that will not reach Vulkan, so it is safe for the recovery command
    // list to overwrite the timestamp query's end slot. The begin reservation remains held throughout.
    if(m_state == State::BeginAccepted)
        return true;
    if(m_state == State::EndRecorded && m_recorder.prepareDeferredScopeForRecovery(m_scope)){
        m_state = State::BeginAccepted;
        return true;
    }

    m_recorder.quarantineScope(m_scope);
    m_scope = {};
    m_state = State::Resolved;
    return false;
}

void GpuTimingFrameTransaction::discard(){
    if(m_scope.valid()){
        if(m_beginSubmission.valid())
            m_recorder.quarantineScope(m_scope);
        else
            m_recorder.discardScope(m_scope);
    }
    m_scope = {};
    m_beginSubmission = {};
    if(m_state != State::Inactive)
        m_state = State::Resolved;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


GpuTimingMeasure::GpuTimingMeasure(
    GpuTimingRecorder& recorder,
    const GpuTimingScopeDefinition& scopeDefinition,
    Device& device,
    CommandList& commandList,
    const GpuTimingSampleAttribution attribution
)
    : m_recorder(recorder)
    , m_commandList(commandList)
{
    // For a normal one-command-list scope, the marker brackets the whole timing range. A split scope closes the
    // marker before its producer list closes and writes its ending timestamp on the ordered consumer list. Scope
    // identity is retained separately for timing aggregation; the marker keeps the authored text so release
    // diagnostics never receive a Name hash in place of the original label.
    if(!scopeDefinition.valid()){
        NWB_ASSERT(!scopeDefinition.identity && scopeDefinition.markerLabel.empty());
        return;
    }

    m_commandList.beginMarker(scopeDefinition.markerLabel);
    m_markerOpen = true;
    if(!m_recorder.beginScope(scopeDefinition.identity, device, commandList, attribution, m_scope))
        m_scope = {};
}
GpuTimingMeasure::~GpuTimingMeasure(){
    finishTiming(m_commandList);
    finishMarker();
}

void GpuTimingMeasure::finishMarker(){
    if(m_markerOpen)
        m_commandList.endMarker();
    m_markerOpen = false;
}

void GpuTimingMeasure::finishTiming(CommandList& commandList){
    m_recorder.endScope(commandList, m_scope);
    m_scope = {};
}

void GpuTimingMeasure::discardTiming(){
    m_recorder.discardScope(m_scope);
    m_scope = {};
}

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_CORE_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

