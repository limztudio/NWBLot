// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#pragma once


#include <microhttpd.h>

#include <logger/common.h>
#include <logger/telemetry/ingest.h>

#include "crash_ingest.h"


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_LOG_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


inline constexpr TStringView s_ServerName = GLB_TEXT("Server");
inline constexpr usize s_MaxPendingCrashUploadPathText = 1024u;
inline constexpr f32 s_ServerUpdateIntervalSeconds = 0.1f;

struct PendingCrashUpload{
    char path[s_MaxPendingCrashUploadPathText] = {};
};

using CrashUploadQueue = ParallelQueue<PendingCrashUpload, LogArena>;

class Server final : public IntervalLoggerWorkerBase<Server, s_ServerUpdateIntervalSeconds, s_ServerName>{
    template<typename, const TStringView&> friend class LoggerWorkerBase;
    template<typename, f32, const TStringView&> friend class IntervalLoggerWorkerBase;

    using BaseType = LoggerWorkerBase<Server, s_ServerName>;
    using UpdateBaseType = IntervalLoggerWorkerBase<Server, s_ServerUpdateIntervalSeconds, s_ServerName>;


private:
    static MHD_Result RequestCallback(void* serverContext, MHD_Connection* connection, const char* url, const char* method, const char* version, const char* uploadData, size_t* uploadDataSizeAddress, void** connectionContextAddress);
    static void CrashIngestUpdate(Server* self);


public:
    Server();
    virtual ~Server()override;


public:
    using BaseType::enqueue;


protected:
    bool internalInit(
        u16 port,
        BasicStringView<tchar> logFileNameBase = {},
        AStringView crashSymbolStoreDirectory = {},
        CrashRetentionConfig crashRetentionConfig = CrashRetentionConfig{},
        AStringView crashUploadToken = AStringView()
    );
    void internalDestroy();
    bool internalUpdate();

protected:
    using UpdateBaseType::enqueue;
    using UpdateBaseType::tryDequeue;


private:
    [[nodiscard]] bool enqueueCrashUpload(const Path& path);
    void stopCrashIngestWorker();
    [[nodiscard]] bool crashUploadAuthorized(MHD_Connection& connection)const;
    bool tryDequeueCrashUpload(PendingCrashUpload& outUpload);


private:
    MHD_Daemon* m_daemon;
    ProcessedMessageFile m_processedMessageFile;
    CrashIngestConfig m_crashIngestConfig;
    TelemetryIngestConfig m_telemetryIngestConfig;
    AString<LogArena> m_crashUploadToken;
    CrashUploadQueue m_crashUploads;
    Semaphore<> m_crashIngestSemaphore;
    Atomic<bool> m_crashIngestExit;
    Thread m_crashIngestThread;
};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace ServerLoggerDetail{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


extern Server* g_ServerLogger;


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


class ServerLoggerRegistrationGuard final : NoCopy{
public:
    explicit ServerLoggerRegistrationGuard(Server& logger)noexcept
        : m_previous(ServerLoggerDetail::g_ServerLogger)
    {
        ServerLoggerDetail::g_ServerLogger = &logger;
    }
    ServerLoggerRegistrationGuard(ServerLoggerRegistrationGuard&&) = delete;
    ~ServerLoggerRegistrationGuard()noexcept{
        ServerLoggerDetail::g_ServerLogger = m_previous;
    }


private:
    Server* m_previous = nullptr;
};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_LOG_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

