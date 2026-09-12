// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "gpu_timing.h"

#include "backend_selection.h"

#include <global/exception.h>
#include <global/termination.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_CORE_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace __hidden_gpu_timing_frame_transaction{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


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

    __hidden_gpu_timing_frame_transaction::GpuTimingMeasureConstructionUnwindScope constructionUnwind(*this);
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
    __hidden_gpu_timing_frame_transaction::GpuTimingMeasureFinishTimingUnwindScope finishUnwind(*this);

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

