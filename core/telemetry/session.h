// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#pragma once


#include "archive.h"
#include "diagnostic.h"
#include "frame_graph.h"
#include "perf.h"
#include "recorder.h"
#include "text_log.h"


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_TELEMETRY_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


class CaptureSession final : NoCopy{
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
    void setForwardLogger(Common::ILogger* const forwardLogger){ m_textLogCapture.setForwardLogger(forwardLogger); }
    void clear(){ m_recorder.clear(); }

    [[nodiscard]] Recorder& recorder(){ return m_recorder; }
    [[nodiscard]] const Recorder& recorder()const{ return m_recorder; }
    [[nodiscard]] TextLogCaptureLogger& textLogCaptureLogger(){ return m_textLogCapture; }
    [[nodiscard]] const TextLogCaptureLogger& textLogCaptureLogger()const{ return m_textLogCapture; }
    [[nodiscard]] CaptureOptions captureOptions()const{ return m_recorder.captureOptions(); }
    [[nodiscard]] bool enabled()const{ return m_recorder.enabled(); }
    [[nodiscard]] u64 frameIndex()const{ return m_frameIndex; }
    [[nodiscard]] u32 streamId()const{ return m_streamId; }
    [[nodiscard]] usize eventCount()const{ return m_recorder.eventCount(); }
    [[nodiscard]] EventView view()const{ return m_recorder.view(); }

    [[nodiscard]] bool recordTextLog(const Common::LogType::Enum type, const TStringView message){
        return RecordTextLog(m_recorder, type, message, m_frameIndex, m_streamId);
    }
    [[nodiscard]] bool recordDiagnostic(const DiagnosticEventRecord& record){
        return RecordDiagnostic(m_recorder, record, m_frameIndex, m_streamId);
    }
    [[nodiscard]] bool recordFrameGraph(const FrameGraphNodeDescs& nodes, const FrameGraphEdgeDescs& edges){
        return RecordFrameGraph(m_recorder, m_frameIndex, nodes, edges, m_streamId);
    }
    [[nodiscard]] PerfSessionRecordResult recordPerfReport(const Perf::SessionReport& report, const u32 streamId = 0u){
        return RecordPerfSessionReport(m_recorder, report, streamId);
    }

    template<typename PathArenaT>
    [[nodiscard]] ArchiveResult flushArchive(const ::Path<PathArenaT>& path, const bool clearAfterWrite = false){
        ArchiveResult result = WriteEventStreamArchive(m_recorder.arena(), m_recorder.view(), path);
        if(result.ok() && clearAfterWrite)
            clear();
        return result;
    }


private:
    Recorder m_recorder;
    TextLogCaptureLogger m_textLogCapture;
    u64 m_frameIndex = 0u;
    u32 m_streamId = 0u;
};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


class CaptureSessionForwardLoggerGuard final : NoCopy{
public:
    CaptureSessionForwardLoggerGuard(CaptureSession& session, Common::ILogger* const forwardLogger)
        : m_session(session)
        , m_previousForwardLogger(session.textLogCaptureLogger().forwardLogger())
    {
        m_session.setForwardLogger(forwardLogger);
    }
    CaptureSessionForwardLoggerGuard(CaptureSessionForwardLoggerGuard&&) = delete;
    ~CaptureSessionForwardLoggerGuard(){
        m_session.setForwardLogger(m_previousForwardLogger);
    }


private:
    CaptureSession& m_session;
    Common::ILogger* m_previousForwardLogger = nullptr;
};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


class CaptureSessionLogRegistrationGuard final : NoCopy{
public:
    explicit CaptureSessionLogRegistrationGuard(CaptureSession& session)
        : m_session(session)
        , m_previousForwardLogger(session.textLogCaptureLogger().forwardLogger())
        , m_registration(session.textLogCaptureLogger())
    {
        Common::ILogger* const forwardLogger = m_registration.previousLogger() == &m_session.textLogCaptureLogger()
            ? m_previousForwardLogger
            : m_registration.previousLogger()
        ;
        m_session.setForwardLogger(forwardLogger);
    }
    CaptureSessionLogRegistrationGuard(CaptureSession& session, Common::ILogger* const forwardLogger)
        : m_session(session)
        , m_previousForwardLogger(session.textLogCaptureLogger().forwardLogger())
        , m_registration(session.textLogCaptureLogger())
    {
        m_session.setForwardLogger(forwardLogger);
    }
    CaptureSessionLogRegistrationGuard(CaptureSessionLogRegistrationGuard&&) = delete;
    ~CaptureSessionLogRegistrationGuard(){
        m_session.setForwardLogger(m_previousForwardLogger);
    }


private:
    CaptureSession& m_session;
    Common::ILogger* m_previousForwardLogger = nullptr;
    Common::LoggerRegistrationGuard m_registration;
};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_TELEMETRY_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
