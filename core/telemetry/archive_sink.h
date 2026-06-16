// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#pragma once


#include "session.h"

#include <global/filesystem/path.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_TELEMETRY_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


using TelemetryPath = ::Path<TelemetryArena>;

struct ArchiveSinkOptions{
    bool enabled = false;
    bool clearAfterFlush = false;
};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


class ArchiveSink final : NoCopy{
public:
    explicit ArchiveSink(CaptureSession& session);


public:
    template<typename PathArenaT>
    void setPath(const ::Path<PathArenaT>& path){
        m_path = path;
    }
    void clearPath(){ m_path.clear(); }
    void setOptions(const ArchiveSinkOptions& options){ m_options = options; }

    [[nodiscard]] ArchiveResult flush();
    [[nodiscard]] ArchiveResult flush(bool clearAfterWrite);
    [[nodiscard]] ArchiveResult flushIfEnabled();

    [[nodiscard]] CaptureSession& session(){ return m_session; }
    [[nodiscard]] const CaptureSession& session()const{ return m_session; }
    [[nodiscard]] const TelemetryPath& path()const{ return m_path; }
    [[nodiscard]] bool pathConfigured()const{ return !m_path.empty(); }
    [[nodiscard]] ArchiveSinkOptions options()const{ return m_options; }
    [[nodiscard]] bool enabled()const{ return m_options.enabled; }


private:
    CaptureSession& m_session;
    TelemetryPath m_path;
    ArchiveSinkOptions m_options;
};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_TELEMETRY_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
