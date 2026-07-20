// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#pragma once


#include "diagnostic.h"
#include "frame_graph.h"
#include "perf.h"
#include "recorder.h"
#include "text_log.h"


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_TELEMETRY_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


class CaptureSessionCaptureScope;


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


class CaptureSession final : NoCopy{
private:
    friend class CaptureSessionCaptureScope;


public:
    explicit CaptureSession(TelemetryArena& arena)
        : m_recorder(arena)
        , m_textLogCapture(m_recorder)
    {}


public:
    void setCaptureOptions(const CaptureOptions& options){ m_recorder.setCaptureOptions(options); }
    void setFrameIndex(const u64 frameIndex){
        m_frameIndex = frameIndex;
        m_textLogCapture.setFrameIndex(frameIndex);
    }
    void setStreamId(const u32 streamId){
        m_streamId = streamId;
        m_textLogCapture.setStreamId(streamId);
    }
    void clear(){ m_recorder.clear(); }

    [[nodiscard]] Recorder& recorder(){ return m_recorder; }
    [[nodiscard]] const Recorder& recorder()const{ return m_recorder; }
    [[nodiscard]] CaptureOptions captureOptions()const{ return m_recorder.captureOptions(); }
    [[nodiscard]] bool enabled()const{ return m_recorder.enabled(); }
    [[nodiscard]] u64 frameIndex()const{ return m_frameIndex; }
    [[nodiscard]] u32 streamId()const{ return m_streamId; }
    [[nodiscard]] usize eventCount()const{ return m_recorder.eventCount(); }
    [[nodiscard]] EventView view()const{ return m_recorder.view(); }

    [[nodiscard]] bool recordFrameGraph(const FrameGraphNodeDescs& nodes, const FrameGraphEdgeDescs& edges){
        return RecordFrameGraph(m_recorder, m_frameIndex, nodes, edges, m_streamId);
    }
    [[nodiscard]] PerfSessionRecordResult recordPerfReport(const Perf::SessionReport& report, const u32 streamId = 0u){
        return RecordPerfSessionReport(m_recorder, report, streamId);
    }

private:
    void setForwardLogger(Common::ILogger* const forwardLogger){ m_textLogCapture.setForwardLogger(forwardLogger); }
    [[nodiscard]] TextLogCaptureLogger& textLogCaptureLogger(){ return m_textLogCapture; }

private:
    u64 m_frameIndex = 0u;
    Recorder m_recorder;
    TextLogCaptureLogger m_textLogCapture;
    u32 m_streamId = 0u;
};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


class CaptureSessionCaptureScope final : NoCopy{
public:
    explicit CaptureSessionCaptureScope(CaptureSession& session)
        : m_session(session)
        , m_previousForwardLogger(session.textLogCaptureLogger().forwardLogger())
        , m_logRegistration(session.textLogCaptureLogger())
        , m_diagnostic(session.recorder())
    {
        Common::ILogger* const forwardLogger = m_logRegistration.previousLogger() == &m_session.textLogCaptureLogger()
            ? m_previousForwardLogger
            : m_logRegistration.previousLogger()
        ;
        m_session.setForwardLogger(forwardLogger);
        m_diagnostic.setFrameIndex(session.frameIndex());
        m_diagnostic.setStreamId(session.streamId());
    }
    CaptureSessionCaptureScope(CaptureSessionCaptureScope&&) = delete;
    ~CaptureSessionCaptureScope(){
        m_session.setForwardLogger(m_previousForwardLogger);
    }

private:
    CaptureSession& m_session;
    Common::ILogger* m_previousForwardLogger = nullptr;
    Common::LoggerRegistrationGuard m_logRegistration;
    DiagnosticCaptureGuard m_diagnostic;
};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_TELEMETRY_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

