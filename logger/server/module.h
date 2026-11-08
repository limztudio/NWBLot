// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#pragma once


#include "crash_ingest_worker.h"
#include "http_handler.h"

#include <logger/common.h>
#include <logger/processed_message_file.h>
#include <logger/server/server_values.adhd.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_LOG_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


inline constexpr f32 s_ServerUpdateIntervalSeconds = ::NWB::Log::s_LogServerValueModuleServerUpdateIntervalSeconds;

class Server final : public IntervalLoggerWorkerBase<Server, s_ServerUpdateIntervalSeconds>, private ICrashIngestSink{
    friend class ServerHttpHandler;
    template<typename> friend class LoggerWorkerBase;
    template<typename, f32> friend class IntervalLoggerWorkerBase;

    using BaseType = LoggerWorkerBase<Server>;
    using UpdateBaseType = IntervalLoggerWorkerBase<Server, s_ServerUpdateIntervalSeconds>;


public:
    Server();
    virtual ~Server()noexcept override;


public:
    using BaseType::enqueue;


protected:
    b8 internalInit(u16 port, BasicStringView<tchar> logFileNameBase = {}, AStringView crashSymbolStoreDirectory = {}, CrashRetentionConfig crashRetentionConfig = CrashRetentionConfig{}, AStringView crashUploadToken = AStringView(), AStringView readinessToken = AStringView());
    void internalDestroy(){ m_crashIngestWorker.stop(); }
    b8 internalUpdate();


protected:
    using UpdateBaseType::enqueue;
    using UpdateBaseType::tryDequeue;


private:
    [[nodiscard]] b8 enqueueCrashUpload(const Path& path, Core::Alloc::ScratchArena& scratchArena);
    virtual void deliverCrashIngestMessage(LogString&& message, Core::Common::LogType::Enum type)override;


private:
    ProcessedMessageFile m_processedMessageFile;
    CrashIngestWorker m_crashIngestWorker;
    ServerHttpHandler m_httpHandler;
};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_LOG_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

