// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#pragma once


#include <microhttpd.h>

#include <logger/common.h>
#include <logger/telemetry/ingest.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_LOG_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


class Server;


class ServerHttpHandler final : NoCopy{
public:
    ServerHttpHandler(Server& server, LogArena& arena);
    ~ServerHttpHandler()noexcept;

    void configure(AStringView crashUploadToken, AStringView readinessToken);
    [[nodiscard]] b8 start(u16 port);
    void stop()noexcept;


private:
    static MHD_Result RequestCallback(void* serverContext, MHD_Connection* connection, const char* url, const char* method, const char* version, const char* uploadData, size_t* uploadDataSizeAddress, void** connectionContextAddress);
    [[nodiscard]] b8 crashUploadAuthorized(MHD_Connection& connection)const noexcept;


private:
    Server& m_server;
    MHD_Daemon* m_daemon = nullptr;
    TelemetryIngestConfig m_telemetryIngestConfig;
    AString<LogArena> m_crashUploadToken;
    AString<LogArena> m_readinessToken;
};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_LOG_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

