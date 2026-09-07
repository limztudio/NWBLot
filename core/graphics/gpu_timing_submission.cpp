// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "gpu_timing.h"

#include "backend_selection.h"

#include <global/exception.h>
#include <global/termination.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_CORE_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace __hidden_gpu_timing_submission{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


inline constexpr Name s_SubmissionScratchArena("graphics.gpu_timing.submission_scratch");


class GpuTimingMeasureConstructionUnwindScope final : NoCopy{
public:
    explicit GpuTimingMeasureConstructionUnwindScope(GpuTimingMeasure& measure)noexcept
        : m_measure(measure)
    {}
    ~GpuTimingMeasureConstructionUnwindScope()noexcept{
        if(!m_active)
            return;
        m_measure.abandonTimingWithoutCallbacks();
        m_measure.abandonMarker();
    }


public:
    void release()noexcept{ m_active = false; }


private:
    GpuTimingMeasure& m_measure;
    bool m_active = true;
};


class GpuTimingMeasureFinishTimingUnwindScope final : NoCopy{
public:
    explicit GpuTimingMeasureFinishTimingUnwindScope(GpuTimingMeasure& measure)noexcept
        : m_measure(measure)
    {}
    ~GpuTimingMeasureFinishTimingUnwindScope()noexcept{
        if(m_active)
            m_measure.abandonTimingWithoutCallbacks();
    }


public:
    void release()noexcept{ m_active = false; }


private:
    GpuTimingMeasure& m_measure;
    bool m_active = true;
};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


class GpuTimingSubmissionTicket::PreparedSubmissionUnwindScope final : NoCopy{
public:
    explicit PreparedSubmissionUnwindScope(GpuTimingSubmissionTicket& ticket)noexcept
        : m_ticket(ticket)
    {}
    ~PreparedSubmissionUnwindScope()noexcept{
        if(m_active)
            m_ticket.abandonWithoutCallbacks();
    }


public:
    void release()noexcept{ m_active = false; }


private:
    GpuTimingSubmissionTicket& m_ticket;
    bool m_active = true;
};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


GpuTimingSubmissionTicket::RecordingScope::RecordingScope(GpuTimingSubmissionTicket& ticket)noexcept
    : m_ticket(ticket)
    , m_activated(m_ticket.activateOnCurrentThread(m_previousTicket))
{}

GpuTimingSubmissionTicket::RecordingScope::~RecordingScope()noexcept{
    m_ticket.deactivateOnCurrentThread(m_previousTicket, m_activated);
}


GpuTimingSubmissionTicket::GpuTimingSubmissionTicket(GpuTimingRecorder& recorder)
    : m_recorder(recorder)
    , m_scopePublications(recorder.m_arena)
    , m_submissionPrerequisites(recorder.m_arena)
{}

GpuTimingSubmissionTicket::~GpuTimingSubmissionTicket()noexcept{
    abandonWithoutCallbacks();
}

bool GpuTimingSubmissionTicket::submit(
    Device& device,
    CommandList* const* commandLists,
    const usize commandListCount,
    const CommandQueue::Enum executionQueue
){
    return submit(device, commandLists, commandListCount, executionQueue, QueueSubmissionDesc{}).valid();
}

QueueSubmissionToken GpuTimingSubmissionTicket::submit(
    Device& device,
    CommandList* const* commandLists,
    const usize commandListCount,
    const CommandQueue::Enum executionQueue,
    const QueueSubmissionDesc& submitDesc
){
    Alloc::ScratchArena scratchArena(__hidden_gpu_timing_submission::s_SubmissionScratchArena);
    Vector<QueueSubmissionToken, Alloc::ScratchArena> waitTokens(scratchArena);
    if(!prepareSubmission(commandLists, commandListCount, waitTokens))
        return {};
    if(submitDesc.waitTokenCount > 0u && !submitDesc.waitTokens){
        discardPreparedSubmission();
        return {};
    }
    PreparedSubmissionUnwindScope submissionUnwind(*this);
    waitTokens.reserve(waitTokens.size() + submitDesc.waitTokenCount);
    for(usize waitTokenIndex = 0u; waitTokenIndex < submitDesc.waitTokenCount; ++waitTokenIndex)
        waitTokens.push_back(submitDesc.waitTokens[waitTokenIndex]);

    QueueSubmissionDesc mergedSubmitDesc = submitDesc;
    if(!waitTokens.empty())
        mergedSubmitDesc.setWaitTokens(waitTokens.data(), waitTokens.size());
    const QueueSubmissionToken token = device.executeCommandLists(commandLists, commandListCount, executionQueue, mergedSubmitDesc);
    const bool resolved = resolveSubmission(token);
    submissionUnwind.release();
    if(!resolved)
        NWB_LOGGER_ERROR(NWB_TEXT("GPU timing submission accepted with an invalid query ownership transition; affected queries were quarantined"));
    return token;
}

QueueSubmissionToken GpuTimingSubmissionTicket::submit(
    Device& device,
    CommandList* const* commandLists,
    const usize commandListCount,
    const GpuPhysicalQueueId& executionQueue,
    const QueueSubmissionDesc& submitDesc
){
    Alloc::ScratchArena scratchArena(__hidden_gpu_timing_submission::s_SubmissionScratchArena);
    Vector<QueueSubmissionToken, Alloc::ScratchArena> waitTokens(scratchArena);
    if(!prepareSubmission(commandLists, commandListCount, waitTokens))
        return {};
    if(submitDesc.waitTokenCount > 0u && !submitDesc.waitTokens){
        discardPreparedSubmission();
        return {};
    }
    PreparedSubmissionUnwindScope submissionUnwind(*this);
    waitTokens.reserve(waitTokens.size() + submitDesc.waitTokenCount);
    for(usize waitTokenIndex = 0u; waitTokenIndex < submitDesc.waitTokenCount; ++waitTokenIndex)
        waitTokens.push_back(submitDesc.waitTokens[waitTokenIndex]);

    QueueSubmissionDesc mergedSubmitDesc = submitDesc;
    if(!waitTokens.empty())
        mergedSubmitDesc.setWaitTokens(waitTokens.data(), waitTokens.size());
    const QueueSubmissionToken token = device.executeCommandLists(commandLists, commandListCount, executionQueue, mergedSubmitDesc);
    const bool resolved = resolveSubmission(token);
    submissionUnwind.release();
    if(!resolved)
        NWB_LOGGER_ERROR(NWB_TEXT("GPU timing submission accepted with an invalid query ownership transition; affected queries were quarantined"));
    return token;
}

void GpuTimingSubmissionTicket::discard(){
    ScopedLock lock(m_mutex);
    if(m_resolved)
        return;

    NWB_ASSERT_MSG(m_recordingScopeCount == 0u, NWB_TEXT("GPU timing submission ticket discarded while command recording is still active"));
    NWB_ASSERT_MSG(!m_submissionPrepared, NWB_TEXT("GPU timing submission ticket discarded while native submission is being resolved"));
    if(m_recordingScopeCount != 0u || m_submissionPrepared)
        return;

    for(ScopePublication& publication : m_scopePublications){
        if(publication.state == ScopePublicationState::Cancelled)
            continue;
        publication.state = ScopePublicationState::Cancelled;
        m_recorder.discardScope(publication.scope);
    }
    m_scopePublications.clear();
    m_reservedScopePublicationCount = 0u;
    m_resolved = true;
}

bool GpuTimingSubmissionTicket::prepareSubmission(
    CommandList* const* commandLists,
    const usize commandListCount,
    Vector<QueueSubmissionToken, Alloc::ScratchArena>& waitTokens
){
    const usize initialWaitTokenCount = waitTokens.size();
    if(!prepareSubmissionState(waitTokens))
        return false;

    if(!commandLists || commandListCount == 0u){
        waitTokens.resize(initialWaitTokenCount);
        discardPreparedSubmission();
        return false;
    }

    // Queue::submit omits command lists whose buffer is absent. Reject that condition before submission rather than
    // allowing a producer from a split timing scope to execute without the consumer that contains its end timestamp.
    for(usize i = 0u; i < commandListCount; ++i){
        if(!commandLists[i] || !commandLists[i]->hasCommandBuffer()){
            waitTokens.resize(initialWaitTokenCount);
            discardPreparedSubmission();
            return false;
        }
    }

    const ScopeEndpointValidationResult endpointValidation = validateScopePublicationEndpoints(commandLists, commandListCount);
    if(endpointValidation == ScopeEndpointValidationResult::Valid)
        return true;

    waitTokens.resize(initialWaitTokenCount);
    if(endpointValidation == ScopeEndpointValidationResult::RetryableBatchMismatch)
        rollbackPreparedSubmission();
    else
        discardPreparedSubmission();
    return false;
}

bool GpuTimingSubmissionTicket::prepareSubmissionAfterCommandListValidation(
    CommandList* const* commandLists,
    const usize commandListCount,
    Vector<QueueSubmissionToken, Alloc::ScratchArena>& waitTokens
){
    const usize initialWaitTokenCount = waitTokens.size();
    if(!prepareSubmissionState(waitTokens))
        return false;

    const ScopeEndpointValidationResult endpointValidation = validateScopePublicationEndpoints(commandLists, commandListCount);
    if(endpointValidation == ScopeEndpointValidationResult::Valid)
        return true;

    waitTokens.resize(initialWaitTokenCount);
    if(endpointValidation == ScopeEndpointValidationResult::RetryableBatchMismatch)
        rollbackPreparedSubmission();
    else
        discardPreparedSubmission();
    return false;
}

bool GpuTimingSubmissionTicket::prepareSubmissionState(Vector<QueueSubmissionToken, Alloc::ScratchArena>& waitTokens){
    ScopedLock lock(m_mutex);
    NWB_ASSERT_MSG(m_recordingScopeCount == 0u, NWB_TEXT("GPU timing submission ticket submitted while command recording is still active"));
    NWB_ASSERT_MSG(m_reservedScopePublicationCount == 0u, NWB_TEXT("GPU timing submission ticket submitted with an unfinished timing scope"));
    if(m_resolved || m_submissionPrepared || m_recordingScopeCount != 0u || m_reservedScopePublicationCount != 0u)
        return false;
    waitTokens.reserve(waitTokens.size() + m_submissionPrerequisites.size());
    for(const QueueSubmissionToken& token : m_submissionPrerequisites)
        waitTokens.push_back(token);
    m_submissionPrepared = true;
    return true;
}

GpuTimingSubmissionTicket::ScopeEndpointValidationResult GpuTimingSubmissionTicket::validateScopePublicationEndpoints(
    CommandList* const* commandLists,
    const usize commandListCount
)noexcept{
    NothrowScopedLock lock(m_mutex);
    NWB_ASSERT_MSG(m_submissionPrepared && !m_resolved, NWB_TEXT("GPU timing endpoint validation requires a prepared submission"));
    if(!m_submissionPrepared || m_resolved)
        return ScopeEndpointValidationResult::InvalidEndpoint;

    for(const ScopePublication& publication : m_scopePublications){
        if(publication.state != ScopePublicationState::Published)
            continue;
        if(
            !publication.scope.timerQueryRecording.valid()
            || !publication.beginEndpoint.valid()
            || !publication.endEndpoint.valid()
        )
            return ScopeEndpointValidationResult::InvalidEndpoint;

        usize beginIndex = Limit<usize>::s_Max;
        usize endIndex = Limit<usize>::s_Max;
        for(usize commandListIndex = 0u; commandListIndex < commandListCount; ++commandListIndex){
            const CommandList* const commandList = commandLists[commandListIndex];
            if(commandList == publication.beginEndpoint.commandList && beginIndex == Limit<usize>::s_Max)
                beginIndex = commandListIndex;
            if(commandList == publication.endEndpoint.commandList && endIndex == Limit<usize>::s_Max)
                endIndex = commandListIndex;
        }
        if(beginIndex == Limit<usize>::s_Max || endIndex == Limit<usize>::s_Max || endIndex < beginIndex)
            return ScopeEndpointValidationResult::RetryableBatchMismatch;

        bool beginRecordsBegin = false;
        bool beginRecordsEnd = false;
        if(!commandLists[beginIndex]->inspectExactTimerQueryRecordingEndpoints(
            publication.scope.timerQueryRecording,
            publication.beginEndpoint.recordingLeaseSerial,
            beginRecordsBegin,
            beginRecordsEnd
        ))
            return ScopeEndpointValidationResult::RetryableBatchMismatch;

        bool endRecordsBegin = false;
        bool endRecordsEnd = false;
        if(!commandLists[endIndex]->inspectExactTimerQueryRecordingEndpoints(
            publication.scope.timerQueryRecording,
            publication.endEndpoint.recordingLeaseSerial,
            endRecordsBegin,
            endRecordsEnd
        ))
            return ScopeEndpointValidationResult::RetryableBatchMismatch;
        if(!beginRecordsBegin || !endRecordsEnd)
            return ScopeEndpointValidationResult::InvalidEndpoint;
    }
    return ScopeEndpointValidationResult::Valid;
}

bool GpuTimingSubmissionTicket::resetForRecordingReuse(GpuTimingRecorder& recorder)noexcept{
    NothrowScopedLock lock(m_mutex);
    if(&recorder != &m_recorder || m_recordingScopeCount != 0u)
        return false;

    for(ScopePublication& publication : m_scopePublications){
        if(publication.state != ScopePublicationState::Cancelled)
            m_recorder.abandonScopeWithoutCallbacks(publication.scope);
    }
    m_scopePublications.clear();
    m_submissionPrerequisites.clear();
    m_reservedScopePublicationCount = 0u;
    m_submissionPrepared = false;
    m_resolved = false;
    return true;
}

void GpuTimingSubmissionTicket::rollbackPreparedSubmission()noexcept{
    NothrowScopedLock lock(m_mutex);
    NWB_ASSERT_MSG(m_submissionPrepared && !m_resolved, NWB_TEXT("GPU timing submission preparation rolled back from an invalid state"));
    if(!m_submissionPrepared || m_resolved)
        return;

    m_submissionPrepared = false;
}

void GpuTimingSubmissionTicket::discardPreparedSubmission()noexcept{
    NothrowScopedLock lock(m_mutex);
    NWB_ASSERT_MSG(m_submissionPrepared && !m_resolved, NWB_TEXT("GPU timing submission preparation discarded from an invalid state"));
    NWB_ASSERT_MSG(m_recordingScopeCount == 0u, NWB_TEXT("GPU timing submission preparation discarded while command recording is active"));
    NWB_ASSERT_MSG(m_reservedScopePublicationCount == 0u, NWB_TEXT("GPU timing submission preparation discarded with an unfinished timing scope"));
    if(!m_submissionPrepared || m_resolved || m_recordingScopeCount != 0u || m_reservedScopePublicationCount != 0u)
        return;

    for(ScopePublication& publication : m_scopePublications){
        if(publication.state != ScopePublicationState::Published)
            continue;
        publication.state = ScopePublicationState::Cancelled;
        m_recorder.abandonScopeWithoutCallbacks(publication.scope);
    }
    m_scopePublications.clear();
    m_submissionPrepared = false;
    m_resolved = true;
}

void GpuTimingSubmissionTicket::abandonWithoutCallbacks()noexcept{
    NothrowScopedLock lock(m_mutex);
    if(m_resolved)
        return;

    for(ScopePublication& publication : m_scopePublications){
        if(publication.state == ScopePublicationState::Cancelled)
            continue;
        publication.state = ScopePublicationState::Cancelled;
        m_recorder.abandonScopeWithoutCallbacks(publication.scope);
    }
    m_scopePublications.clear();
    m_submissionPrerequisites.clear();
    m_reservedScopePublicationCount = 0u;
    m_recordingScopeCount = 0u;
    m_submissionPrepared = false;
    m_resolved = true;
}

bool GpuTimingSubmissionTicket::resolveSubmission(const QueueSubmissionToken& token)noexcept{
    if(token.valid())
        return confirm(token);
    abandonWithoutCallbacks();
    return true;
}

usize GpuTimingSubmissionTicket::reserveScopePublication(){
    ScopedLock lock(m_mutex);
    NWB_ASSERT_MSG(!m_resolved && !m_submissionPrepared, NWB_TEXT("GPU timing scope reserved after its submission ticket stopped recording"));
    if(m_resolved || m_submissionPrepared)
        return Limit<usize>::s_Max;

    m_scopePublications.emplace_back();
    ++m_reservedScopePublicationCount;
    return m_scopePublications.size() - 1u;
}

bool GpuTimingSubmissionTicket::bindScopePublication(
    const usize publicationIndex,
    const GpuTimingScope& scope,
    const CommandList& commandList
)noexcept{
    NothrowScopedLock lock(m_mutex);
    const bool validPublication = publicationIndex < m_scopePublications.size()
        && m_scopePublications[publicationIndex].state == ScopePublicationState::Reserved
        && !m_scopePublications[publicationIndex].scope.valid()
    ;
    NWB_ASSERT_MSG(validPublication, NWB_TEXT("GPU timing scope bound to an invalid submission publication"));
    if(!validPublication || m_resolved || m_submissionPrepared)
        return false;

    ScopePublication& publication = m_scopePublications[publicationIndex];
    publication.scope = scope;
    publication.scope.submissionTicket = nullptr;
    publication.scope.submissionPublicationIndex = Limit<usize>::s_Max;
    publication.beginEndpoint = CommandListRecordingEndpoint{
        .commandList = &commandList,
        .recordingLeaseSerial = commandList.recordingLeaseSerial(),
    };
    return true;
}

void GpuTimingSubmissionTicket::cancelScopePublication(const usize publicationIndex)noexcept{
    if(publicationIndex == Limit<usize>::s_Max)
        return;

    NothrowScopedLock lock(m_mutex);
    const bool validPublication = publicationIndex < m_scopePublications.size()
        && m_scopePublications[publicationIndex].state == ScopePublicationState::Reserved
    ;
    if(!validPublication)
        return;

    ScopePublication& publication = m_scopePublications[publicationIndex];
    publication.scope = {};
    publication.state = ScopePublicationState::Cancelled;
    NWB_ASSERT(m_reservedScopePublicationCount > 0u);
    if(m_reservedScopePublicationCount > 0u)
        --m_reservedScopePublicationCount;
}

bool GpuTimingSubmissionTicket::publishScope(const GpuTimingScope& scope, const CommandList& commandList)noexcept{
    if(
        !scope.valid()
        || scope.submissionTicket != this
        || scope.submissionPublicationIndex == Limit<usize>::s_Max
    )
        return false;

    NothrowScopedLock lock(m_mutex);
    if(scope.submissionPublicationIndex >= m_scopePublications.size())
        return false;

    ScopePublication& publication = m_scopePublications[scope.submissionPublicationIndex];
    const bool matchingPublication = publication.state == ScopePublicationState::Reserved
        && publication.scope.scopeName == scope.scopeName
        && publication.scope.index == scope.index
        && publication.scope.epoch == scope.epoch
        && publication.scope.reservation == scope.reservation
    ;
    NWB_ASSERT_MSG(matchingPublication, NWB_TEXT("GPU timing scope published through a mismatched submission reservation"));
    if(!matchingPublication || m_resolved || m_submissionPrepared)
        return false;

    publication.endEndpoint = CommandListRecordingEndpoint{
        .commandList = &commandList,
        .recordingLeaseSerial = commandList.recordingLeaseSerial(),
    };
    publication.state = ScopePublicationState::Published;
    NWB_ASSERT(m_reservedScopePublicationCount > 0u);
    if(m_reservedScopePublicationCount > 0u)
        --m_reservedScopePublicationCount;
    return true;
}

bool GpuTimingSubmissionTicket::trackSubmissionPrerequisite(const QueueSubmissionToken& token){
    if(!token.valid() || !token.hasPhysicalQueueIdentity())
        return false;

    ScopedLock lock(m_mutex);
    NWB_ASSERT_MSG(!m_resolved && !m_submissionPrepared, NWB_TEXT("GPU timing submission prerequisite added after recording closed"));
    if(m_resolved || m_submissionPrepared)
        return false;

    for(QueueSubmissionToken& prerequisite : m_submissionPrerequisites){
        if(
            prerequisite.physicalQueueIndex != token.physicalQueueIndex
            || prerequisite.deviceGeneration != token.deviceGeneration
        )
            continue;
        if(prerequisite.queue != token.queue)
            return false;

        prerequisite.value = Max(prerequisite.value, token.value);
        return true;
    }

    m_submissionPrerequisites.push_back(token);
    return true;
}

bool GpuTimingSubmissionTicket::activateOnCurrentThread(GpuTimingSubmissionTicket*& outPreviousTicket)noexcept{
    NothrowScopedLock lock(m_mutex);
    outPreviousTicket = GpuTimingRecorder::s_activeSubmissionTicket;
    NWB_ASSERT_MSG(!m_resolved && !m_submissionPrepared, NWB_TEXT("GPU timing submission ticket activated after recording closed"));
    if(m_resolved || m_submissionPrepared){
        GpuTimingRecorder::s_activeSubmissionTicket = nullptr;
        return false;
    }
    const bool countAvailable = m_recordingScopeCount != Limit<u32>::s_Max;
    NWB_FATAL_ASSERT_MSG(countAvailable, "GPU timing submission ticket recording-scope count exhausted");
    if(!countAvailable)
        TerminateInvariant();

    GpuTimingRecorder::s_activeSubmissionTicket = this;
    ++m_recordingScopeCount;
    return true;
}

void GpuTimingSubmissionTicket::deactivateOnCurrentThread(
    GpuTimingSubmissionTicket* const previousTicket,
    const bool activated
)noexcept{
    GpuTimingSubmissionTicket* const expectedTicket = activated ? this : nullptr;
    const bool tlsValid = GpuTimingRecorder::s_activeSubmissionTicket == expectedTicket;
    NWB_FATAL_ASSERT_MSG(tlsValid, "GPU timing submission ticket recording scope closed out of order");
    if(!tlsValid)
        TerminateInvariant();
    if(!activated){
        GpuTimingRecorder::s_activeSubmissionTicket = previousTicket;
        return;
    }
    NothrowScopedLock lock(m_mutex);
    const bool countValid = m_recordingScopeCount != 0u;
    NWB_FATAL_ASSERT_MSG(countValid, "GPU timing submission ticket recording-scope count underflow");
    if(!countValid)
        TerminateInvariant();
    --m_recordingScopeCount;
    GpuTimingRecorder::s_activeSubmissionTicket = previousTicket;
}

bool GpuTimingSubmissionTicket::confirm(const QueueSubmissionToken& token)noexcept{
    NothrowScopedLock lock(m_mutex);
    if(m_resolved)
        return false;

    NWB_ASSERT_MSG(m_recordingScopeCount == 0u, NWB_TEXT("GPU timing submission ticket confirmed while command recording is still active"));
    NWB_ASSERT_MSG(m_submissionPrepared, NWB_TEXT("GPU timing submission ticket confirmed without preparation"));
    NWB_ASSERT_MSG(m_reservedScopePublicationCount == 0u, NWB_TEXT("GPU timing submission ticket confirmed with an unfinished timing scope"));
    if(m_recordingScopeCount != 0u || !m_submissionPrepared || m_reservedScopePublicationCount != 0u)
        return false;

    bool confirmed = true;
    for(ScopePublication& publication : m_scopePublications){
        if(publication.state != ScopePublicationState::Published)
            continue;
        publication.state = ScopePublicationState::Cancelled;
        if(!m_recorder.confirmScope(publication.scope, token, true)){
            m_recorder.quarantineScope(publication.scope);
            confirmed = false;
        }
    }
    m_scopePublications.clear();
    m_submissionPrepared = false;
    m_resolved = true;
    return confirmed;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


GpuTimingFrameTransaction::GpuTimingFrameTransaction(GpuTimingRecorder& recorder)
    : m_recorder(recorder)
{}

GpuTimingFrameTransaction::~GpuTimingFrameTransaction()noexcept{
    abandonWithoutCallbacks();
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
            m_state = State::Resolved;
            m_recorder.discardScope(m_scope);
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
    const QueueSubmissionToken beginSubmission = m_beginSubmission;
    m_beginSubmission = {};
    if(m_state != State::Inactive)
        m_state = State::Resolved;

    if(m_scope.valid()){
        if(beginSubmission.valid())
            m_recorder.quarantineScope(m_scope);
        else
            m_recorder.discardScope(m_scope);
    }
    m_scope = {};
}

void GpuTimingFrameTransaction::abandonWithoutCallbacks()noexcept{
    if(m_scope.valid()){
        if(m_beginSubmission.valid())
            m_recorder.quarantineScope(m_scope);
        else
            m_recorder.abandonScopeWithoutCallbacks(m_scope);
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

    __hidden_gpu_timing_submission::GpuTimingMeasureConstructionUnwindScope constructionUnwind(*this);
    m_marker = m_commandList.beginMarkerLease(scopeDefinition.markerLabel);
    if(!m_recorder.beginScope(scopeDefinition.identity, device, commandList, attribution, false, m_scope))
        m_scope = {};
    constructionUnwind.release();
}

GpuTimingMeasure::~GpuTimingMeasure()noexcept{
    if(UncaughtExceptionCount() != 0){
        // A split measure may be owned by the caller of a task-graph recording operation and therefore outlive the
        // opening command list during exception unwind. The command-list recording owner recovers its complete marker
        // stack while that list is still alive; relinquish only this stale token instead of dereferencing the opener.
        abandonTimingWithoutCallbacks();
        m_marker = {};
        return;
    }
    if(
        !m_commandList.isRecording()
        || !m_commandList.hasCommandBuffer()
        || m_commandList.commandRecordingFailed()
    ){
        abandonTimingWithoutCallbacks();
        abandonMarker();
        return;
    }
    m_recorder.endScopeFromOpeningCommandList(m_commandList, m_scope);
    abandonMarker();
}

bool GpuTimingMeasure::finishMarker(){
    const bool finished = !m_marker.valid() || m_commandList.endMarkerLease(m_marker);
    m_marker = {};
    return finished;
}

void GpuTimingMeasure::abandonMarker()noexcept{
    if(m_marker.valid())
        m_commandList.abandonMarkerLease(m_marker);
    m_marker = {};
}

void GpuTimingMeasure::finishTiming(CommandList& commandList){
    __hidden_gpu_timing_submission::GpuTimingMeasureFinishTimingUnwindScope finishUnwind(*this);

    m_recorder.endScope(commandList, m_scope);
    finishUnwind.release();
}

void GpuTimingMeasure::discardTiming(){
    m_recorder.discardScope(m_scope);
}

void GpuTimingMeasure::abandonTimingWithoutCallbacks()noexcept{
    m_recorder.abandonScopeWithoutCallbacks(m_scope);
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_CORE_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

