// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#pragma once


#include "archive.h"
#include "perf.h"
#include "recorder.h"


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_TELEMETRY_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


class CaptureSession final : NoCopy{
public:
    explicit CaptureSession(TelemetryArena& arena)
        : m_recorder(arena)
    {}


public:
    void setCaptureOptions(const CaptureOptions& options){ m_recorder.setCaptureOptions(options); }
    void clear(){ m_recorder.clear(); }

    [[nodiscard]] Recorder& recorder(){ return m_recorder; }
    [[nodiscard]] const Recorder& recorder()const{ return m_recorder; }
    [[nodiscard]] CaptureOptions captureOptions()const{ return m_recorder.captureOptions(); }
    [[nodiscard]] bool enabled()const{ return m_recorder.enabled(); }
    [[nodiscard]] usize eventCount()const{ return m_recorder.eventCount(); }
    [[nodiscard]] EventView view()const{ return m_recorder.view(); }

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
};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_TELEMETRY_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


