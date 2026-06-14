// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#pragma once


#include <microhttpd.h>

#include <logger/common.h>

#include "crash_ingest.h"


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_LOG_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


inline constexpr tchar SERVER_NAME[] = NWB_TEXT("Server");
inline constexpr usize s_MaxPendingCrashUploadPathText = 1024u;

struct PendingCrashUpload{
    char path[s_MaxPendingCrashUploadPathText] = {};
};

using CrashUploadQueue = ParallelQueue<PendingCrashUpload, LogArena>;

class Server final : public BaseUpdateOrdinary<Server, 0.1f, SERVER_NAME>{
    template<typename, const tchar*> friend class Base;
    template<typename, f32, const tchar*> friend class BaseUpdateOrdinary;

    using BaseType = Base<Server, SERVER_NAME>;
    using UpdateBaseType = BaseUpdateOrdinary<Server, 0.1f, SERVER_NAME>;


private:
    static MHD_Result requestCallback(void* cls, MHD_Connection* connection, const char* url, const char* method, const char* version, const char* upload_data, size_t* upload_data_size, void** con_cls);
    static void crashIngestUpdate(Server* self);


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
    ProcessedMessageFile m_processedMsgFile;
    CrashIngestConfig m_crashIngestConfig;
    AString<LogArena> m_crashUploadToken;
    CrashUploadQueue m_crashUploads;
    Semaphore<> m_crashIngestSemaphore;
    Thread m_crashIngestThread;
    Atomic<bool> m_crashIngestExit;
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
    explicit ServerLoggerRegistrationGuard(Server& logger)
        : m_previous(ServerLoggerDetail::g_ServerLogger)
    {
        ServerLoggerDetail::g_ServerLogger = &logger;
    }
    ServerLoggerRegistrationGuard(ServerLoggerRegistrationGuard&&) = delete;
    ~ServerLoggerRegistrationGuard(){
        ServerLoggerDetail::g_ServerLogger = m_previous;
    }


private:
    Server* m_previous = nullptr;
};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_LOG_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

