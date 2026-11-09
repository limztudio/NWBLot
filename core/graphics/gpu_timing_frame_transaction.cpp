// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "gpu_timing_frame_transaction.h"
#include "gpu_timing.h"
#include "backend_selection.h"

#include <global/scope_exit.h>
#include <global/exception.h>
#include <global/termination.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_CORE_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


GpuTimingFrameTransaction::GpuTimingFrameTransaction(GpuTimingRecorder& recorder)noexcept
    : m_recorder(recorder)
{}

GpuTimingFrameTransaction::~GpuTimingFrameTransaction()noexcept{
    abandonWithoutCallbacks();
}

b8 GpuTimingFrameTransaction::begin(const GpuTimingScopeDefinition& scopeDefinition, Device& device, CommandList& commandList, const GpuTimingSampleAttribution attribution){
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
    const auto scope = m_recorder.beginDeferredScope(scopeDefinition.identity, attribution, device, commandList);
    if(!scope){
        m_scope = {};
        m_state = State::Resolved;
        return false;
    }
    m_scope = *scope;
    m_state = m_scope.valid() ? State::BeginRecorded : State::Inactive;
    return true;
}

b8 GpuTimingFrameTransaction::recordEnd(CommandList& commandList){
    if(m_state == State::Inactive)
        return true;
    if(m_state != State::BeginRecorded && m_state != State::BeginAccepted)
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

b8 GpuTimingFrameTransaction::confirmBeginSubmission(const QueueSubmissionToken& token)noexcept{
    if(m_state == State::Inactive || m_state == State::Resolved)
        return true;

    FOUNDATION_ASSERT(m_state == State::BeginRecorded || m_state == State::EndRecorded);
    if((m_state != State::BeginRecorded && m_state != State::EndRecorded) || !m_recorder.validateScopeSubmission(m_scope, token)){
        m_recorder.quarantineScope(m_scope);
        m_scope = {};
        m_state = State::Resolved;
        return false;
    }

    if(m_beginSubmission.valid()){
        const b8 matches = m_beginSubmission == token;
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

b8 GpuTimingFrameTransaction::confirmEndSubmission(const QueueSubmissionToken& token, const b8 publishSample)noexcept{
    if(m_state == State::Inactive || m_state == State::Resolved)
        return true;

    const b8 orderedSameQueue = m_beginSubmission.valid()
        && m_beginSubmission.hasPhysicalQueueIdentity()
        && token.valid()
        && token.hasPhysicalQueueIdentity()
        && token.queue == m_beginSubmission.queue
        && token.physicalQueueIndex == m_beginSubmission.physicalQueueIndex
        && token.deviceGeneration == m_beginSubmission.deviceGeneration
        && token.value >= m_beginSubmission.value
    ;
    if(m_state != State::EndRecorded || !orderedSameQueue || !m_recorder.confirmScope(m_scope, token, publishSample)){
        m_recorder.quarantineScope(m_scope);
        m_scope = {};
        m_state = State::Resolved;
        return false;
    }

    m_scope = {};
    m_state = State::Resolved;
    return true;
}

b8 GpuTimingFrameTransaction::prepareForRecovery()noexcept{
    if(!needsRetirement())
        return true;

    // The old endpoint lives in an unsubmitted packet, so the recovery list may overwrite the end slot; the begin stays held.
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


GpuTimingMeasure::GpuTimingMeasure(GpuTimingRecorder& recorder, const GpuTimingScopeDefinition& scopeDefinition, Device& device, CommandList& commandList, const GpuTimingSampleAttribution attribution)
    : m_recorder(recorder)
    , m_commandList(commandList)
{
    // For a normal one-command-list scope, the marker brackets the whole timing range.
    // A split scope closes the marker before its producer list closes and stamps the end on the ordered consumer list.
    // Identity stays separate for aggregation; the marker keeps authored text so release builds never see a Name hash.
    if(!scopeDefinition.valid()){
        FOUNDATION_ASSERT(!scopeDefinition.identity && scopeDefinition.markerLabel.empty());
        return;
    }

    ScopeExit constructionUnwind([this]()noexcept{
        abandonTimingWithoutCallbacks();
        abandonMarker();
    });
    m_marker = m_commandList.beginMarkerLease(scopeDefinition.markerLabel);
    const auto scope = m_recorder.beginScope(scopeDefinition.identity, attribution, false, device, commandList);
    m_scope = scope ? *scope : GpuTimingScope{};
    constructionUnwind.release();
}

GpuTimingMeasure::~GpuTimingMeasure()noexcept{
    if(UncaughtExceptionCount() != 0){
        // A split measure may outlive its opening list on unwind; only this stale token relinquishes, never the opener.
        abandonTimingWithoutCallbacks();
        m_marker = {};
        return;
    }
    if(!m_commandList.isRecording() || !m_commandList.hasCommandBuffer() || m_commandList.commandRecordingFailed()){
        abandonTimingWithoutCallbacks();
        abandonMarker();
        return;
    }
    m_recorder.endScopeFromOpeningCommandList(m_commandList, m_scope);
    abandonMarker();
}

b8 GpuTimingMeasure::finishMarker(){
    const b8 finished = !m_marker.valid() || m_commandList.endMarkerLease(m_marker);
    m_marker = {};
    return finished;
}

void GpuTimingMeasure::abandonMarker()noexcept{
    if(m_marker.valid())
        m_commandList.abandonMarkerLease(m_marker);
    m_marker = {};
}

void GpuTimingMeasure::finishTiming(CommandList& commandList){
    ScopeExit finishUnwind([this]()noexcept{ abandonTimingWithoutCallbacks(); });

    m_recorder.endScope(commandList, m_scope);
    finishUnwind.release();
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_CORE_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

