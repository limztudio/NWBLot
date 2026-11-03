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
    friend class CaptureSessionCaptureScope;


public:
    explicit CaptureSession(TelemetryArena& arena)noexcept
        : m_recorder(arena)
        , m_textLogCapture(m_recorder)
    {}


public:
    void setCaptureOptions(const CaptureOptions& options){ m_recorder.setCaptureOptions(options); }

    void setFrameIndex(const u64 frameIndex)noexcept{
        m_frameIndex.store(frameIndex, MemoryOrder::release);
        m_textLogCapture.setFrameIndex(frameIndex);
    }

    void setStreamId(const u32 streamId)noexcept{
        m_streamId.store(streamId, MemoryOrder::release);
        m_textLogCapture.setStreamId(streamId);
    }

    void clear()noexcept{ m_recorder.clear(); }

    [[nodiscard]] Recorder& recorder()noexcept{ return m_recorder; }

    [[nodiscard]] const Recorder& recorder()const noexcept{ return m_recorder; }

    [[nodiscard]] CaptureOptions captureOptions()const noexcept{ return m_recorder.captureOptions(); }

    [[nodiscard]] b8 enabled()const noexcept{ return m_recorder.enabled(); }

    [[nodiscard]] u64 frameIndex()const noexcept{ return m_frameIndex.load(MemoryOrder::acquire); }

    [[nodiscard]] u32 streamId()const noexcept{ return m_streamId.load(MemoryOrder::acquire); }

    [[nodiscard]] usize eventCount()const noexcept{ return m_recorder.eventCount(); }

    [[nodiscard]] EventView view()const noexcept{ return m_recorder.view(); }

    [[nodiscard]] b8 recordFrameGraph(
        Alloc::ScratchArena& scratchArena,
        const Span<const FrameGraphNodeDesc> nodes,
        const Span<const FrameGraphEdgeDesc> edges,
        const Span<const FrameGraphPhysicalQueueRuntimeStatisticsRecord> physicalQueueRuntimeStatistics,
        const Span<const FrameGraphPacketSubmissionStatisticsRecord> packetSubmissionStatistics
    ){
        return RecordFrameGraph(m_recorder, scratchArena, frameIndex(), nodes, edges, physicalQueueRuntimeStatistics, packetSubmissionStatistics, streamId());
    }

    [[nodiscard]] PerfSessionRecordResult recordPerfReport(const Perf::SessionReport& report, const u32 streamId = 0u){
        return RecordPerfSessionReport(m_recorder, report, streamId);
    }


private:
    Atomic<u64> m_frameIndex{ 0u };
    Recorder m_recorder;
    TextLogCaptureLogger m_textLogCapture;
    Atomic<u32> m_streamId{ 0u };
};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


class CaptureSessionCaptureScope final : NoCopy{
public:
    explicit CaptureSessionCaptureScope(CaptureSession& session)
        : m_session(session)
        , m_diagnostic(
            session.recorder(),
            session.frameIndex(),
            session.streamId(),
            &session.m_frameIndex,
            &session.m_streamId
        )
        , m_logRegistration(
            session.m_textLogCapture,
            &CaptureSessionCaptureScope::BeforePublishLogger,
            &CaptureSessionCaptureScope::AfterUnpublishLogger,
            this
        )
    {}
    CaptureSessionCaptureScope(CaptureSessionCaptureScope&&) = delete;
    ~CaptureSessionCaptureScope()noexcept = default;


private:
    static void BeforePublishLogger(void* const context, Common::ILogger* const previousLogger)noexcept{
        auto& self = *static_cast<CaptureSessionCaptureScope*>(context);
        self.m_previousForwardLogger = self.m_session.m_textLogCapture.forwardLogger();
        Common::ILogger* const forwardLogger = previousLogger == &self.m_session.m_textLogCapture
            ? self.m_previousForwardLogger
            : previousLogger
        ;
        self.m_session.m_textLogCapture.setForwardLogger(forwardLogger);
    }

    static void AfterUnpublishLogger(void* const context, Common::ILogger*)noexcept{
        auto& self = *static_cast<CaptureSessionCaptureScope*>(context);
        self.m_session.m_textLogCapture.setForwardLogger(self.m_previousForwardLogger);
    }


private:
    CaptureSession& m_session;
    Common::ILogger* m_previousForwardLogger = nullptr;
    DiagnosticCaptureGuard m_diagnostic;
    Common::LoggerRegistrationGuard m_logRegistration;
};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_TELEMETRY_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

