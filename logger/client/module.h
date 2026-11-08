// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#pragma once


#include <logger/common.h>
#include "http_log_transport.h"
#include <logger/processed_message_file.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_LOG_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


template<typename T>
class ClientBase : public Core::Common::ILogger, public QueuedLoggerWorkerBase<T>{
protected:
    using BaseType = LoggerWorkerBase<T>;
    using UpdateBaseType = QueuedLoggerWorkerBase<T>;


protected:
    explicit ClientBase(const NameText<char> allocationLog)
        : UpdateBaseType(allocationLog)
    {}


public:
    using BaseType::enqueue;


public:
    virtual LogArena& arena()noexcept override{ return BaseType::arena(); }

    virtual void enqueue(LogString&& str, Core::Common::LogType::Enum type = Core::Common::LogType::Info)override{ BaseType::enqueue(Move(str), type); }

    virtual void enqueue(const LogString& str, Core::Common::LogType::Enum type = Core::Common::LogType::Info)override{ BaseType::enqueue(str, type); }
};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace ClientPayloadKind{
    enum Enum : u8{
        Message,
        Telemetry,
    };
};

class Client final : public ClientBase<Client>{
    template<typename> friend class LoggerWorkerBase;
    template<typename> friend class QueuedLoggerWorkerBase;

    using ClientBaseType = ClientBase<Client>;
    using BaseType = ClientBaseType::BaseType;
    using UpdateBaseType = ClientBaseType::UpdateBaseType;


private:
    static b8 GlobalInit()noexcept;


public:
    Client();
    virtual ~Client()noexcept override;


public:
    using ClientBaseType::enqueue;


public:
    [[nodiscard]] b8 enqueueTelemetry(const void* bytes, usize byteCount);
    [[nodiscard]] usize rejectedPayloadCount()const noexcept{ return m_rejectedPayloadCount.load(MemoryOrder::acquire); }


protected:
    b8 internalInit(AStringView url);
    b8 internalUpdate();
    [[nodiscard]] b8 workerCanExit()const noexcept{
        return !m_hasPendingPayload && !m_messageCount.load(MemoryOrder::acquire) && !m_telemetryCount.load(MemoryOrder::acquire);
    }


protected:
    void enqueue(MessageType&& data);
    b8 tryDequeueMessage(MessageType& msg);


private:
    LogHttpTransport m_transport;
    Vector<u8, LogArena> m_pendingPayload;
    AString<LogArena> m_messageUrl;
    AString<LogArena> m_telemetryUrl;
    ClientPayloadKind::Enum m_pendingPayloadKind;
    b8 m_hasPendingPayload;
    b8 m_preferTelemetry;
    u32 m_retryDelayMs;
    long m_lastHttpFailureStatus;
    i32 m_lastCurlFailureCode;


private:
    Atomic<usize> m_rejectedPayloadCount;
    Atomic<usize> m_messageCount;
    Atomic<usize> m_telemetryCount;
    ParallelQueue<LogBytes, LogArena> m_telemetryQueue;
};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


class ClientStandalone final : public ClientBase<ClientStandalone>{
    template<typename> friend class LoggerWorkerBase;
    template<typename> friend class QueuedLoggerWorkerBase;

    using ClientBaseType = ClientBase<ClientStandalone>;
    using BaseType = ClientBaseType::BaseType;
    using UpdateBaseType = ClientBaseType::UpdateBaseType;


public:
    ClientStandalone();
    virtual ~ClientStandalone()noexcept override;


public:
    using ClientBaseType::enqueue;


protected:
    b8 internalInit(BasicStringView<tchar> logFileNameBase = {});
    b8 internalUpdate();


protected:
    using UpdateBaseType::enqueue;


private:
    ProcessedMessageFile m_processedMessageFile;
};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_LOG_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

