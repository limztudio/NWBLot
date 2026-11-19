// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#pragma once


#include <logger/common.h>
#include <logger/processed_message_file.h>
#include <logger/server/crash_ingest.h>
#include <logger/server/server_values.adhd.h>
#include <logger/telemetry/ingest.h>

#include <core/common/frame_data.h>

#include <microhttpd.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_LOG_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


class Frame{
#if defined(FOUNDATION_PLATFORM_WINDOWS)
    using NativeFrameData = Core::Common::WinFrame;
#else
    using NativeFrameData = Core::Common::FrameData;
#endif


public:
    static void Print(BasicStringView<tchar> str, Core::Common::LogType::Enum type);


public:
    Frame(void* inst)noexcept(IsSame_V<NativeFrameData, Core::Common::FrameData>);
    ~Frame()noexcept;


public:
    b8 init()noexcept(IsSame_V<NativeFrameData, Core::Common::FrameData>);
    void showFrame();
    b8 mainLoop()noexcept(IsSame_V<NativeFrameData, Core::Common::FrameData>);


public:
    template<typename T>
    inline T& data()noexcept{ return static_cast<T&>(m_data); }


private:
    NativeFrameData m_data;
};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


inline constexpr usize s_MaxPendingCrashUploadPathText = ::NWB::Log::s_LogServerValueModuleMaxPendingCrashUploadPathText;

struct PendingCrashUpload{
    Array<char, s_MaxPendingCrashUploadPathText> path = {};
};

using CrashUploadQueue = ParallelQueue<PendingCrashUpload, LogArena>;

interface ICrashIngestSink{
    virtual ~ICrashIngestSink()noexcept = default;
    virtual void deliverCrashIngestMessage(LogString&& message, Core::Common::LogType::Enum type) = 0;
};

class CrashIngestWorker final : NoCopy{
public:
    CrashIngestWorker(LogArena& arena, ICrashIngestSink& sink);
    ~CrashIngestWorker()noexcept;


public:
    void configure(AStringView symbolStoreDirectory, CrashRetentionConfig retention);
    void start();
    void stop();
    [[nodiscard]] b8 enqueue(const Path& path, Core::Alloc::ScratchArena& scratchArena);


private:
    void run();
    [[nodiscard]] Expected<PendingCrashUpload> tryDequeue();


private:
    ICrashIngestSink& m_sink;
    CrashIngestConfig m_config;
    CrashUploadQueue m_uploads;
    Semaphore<> m_semaphore;
    Atomic<b8> m_exit;
    Thread m_thread;
};


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

