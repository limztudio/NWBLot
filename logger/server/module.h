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


inline constexpr usize s_MaxPendingCrashUploadPathText = 1024u;
inline constexpr f32 s_ServerUpdateIntervalSeconds = 0.1f;

struct PendingCrashUpload{
    char path[s_MaxPendingCrashUploadPathText] = {};
};

using CrashUploadQueue = ParallelQueue<PendingCrashUpload, LogArena>;

class Server final : public IntervalLoggerWorkerBase<Server, s_ServerUpdateIntervalSeconds>{
    template<typename> friend class LoggerWorkerBase;
    template<typename, f32> friend class IntervalLoggerWorkerBase;

    using BaseType = LoggerWorkerBase<Server>;
    using UpdateBaseType = IntervalLoggerWorkerBase<Server, s_ServerUpdateIntervalSeconds>;


private:
    static MHD_Result RequestCallback(void* serverContext, MHD_Connection* connection, const char* url, const char* method, const char* version, const char* uploadData, size_t* uploadDataSizeAddress, void** connectionContextAddress);
    static void CrashIngestUpdate(Server& self);


public:
    Server();
    virtual ~Server()noexcept override;


public:
    using BaseType::enqueue;


protected:
    b8 internalInit(u16 port, BasicStringView<tchar> logFileNameBase = {}, AStringView crashSymbolStoreDirectory = {}, CrashRetentionConfig crashRetentionConfig = CrashRetentionConfig{}, AStringView crashUploadToken = AStringView());
    void internalDestroy();
    b8 internalUpdate();


protected:
    using UpdateBaseType::enqueue;
    using UpdateBaseType::tryDequeue;


private:
    [[nodiscard]] b8 enqueueCrashUpload(const Path& path, Core::Alloc::ScratchArena& scratchArena);
    void stopCrashIngestWorker();
    [[nodiscard]] b8 crashUploadAuthorized(MHD_Connection& connection)const noexcept;
    [[nodiscard]] Expected<PendingCrashUpload> tryDequeueCrashUpload();


private:
    MHD_Daemon* m_daemon;
    ProcessedMessageFile m_processedMessageFile;
    CrashIngestConfig m_crashIngestConfig;
    TelemetryIngestConfig m_telemetryIngestConfig;
    AString<LogArena> m_crashUploadToken;
    CrashUploadQueue m_crashUploads;
    Semaphore<> m_crashIngestSemaphore;
    Atomic<b8> m_crashIngestExit;
    Thread m_crashIngestThread;
};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_LOG_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

