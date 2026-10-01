// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#pragma once


#include <logger/common.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_LOG_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


class IClient : public ILogger{
public:
    virtual ~IClient()override = default;
};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


template<typename T, const TStringView& loggerName>
class ClientBase : public IClient, public QueuedLoggerWorkerBase<T, loggerName>{
protected:
    using BaseType = LoggerWorkerBase<T, loggerName>;
    using UpdateBaseType = QueuedLoggerWorkerBase<T, loggerName>;


    explicit ClientBase(const AStringView allocationLog)
        : UpdateBaseType(allocationLog)
    {}


public:
    using BaseType::enqueue;
    virtual LogArena& arena()override{ return BaseType::arena(); }
    virtual void enqueue(LogString&& str, Type::Enum type = Type::Info)override{ BaseType::enqueue(Move(str), type); }
    virtual void enqueue(const LogString& str, Type::Enum type = Type::Info)override{ BaseType::enqueue(str, type); }
};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace ClientPayloadKind{
    enum Enum : u8{
        Message,
        Telemetry,
    };
};

inline constexpr TStringView s_ClientName = NWB_TEXT("Client");
class Client final : public ClientBase<Client, s_ClientName>{
    template<typename, const TStringView&> friend class LoggerWorkerBase;
    template<typename, const TStringView&> friend class QueuedLoggerWorkerBase;

    using ClientBaseType = ClientBase<Client, s_ClientName>;
    using BaseType = ClientBaseType::BaseType;
    using UpdateBaseType = ClientBaseType::UpdateBaseType;


private:
    static bool globalInit();


public:
    Client();
    virtual ~Client()override;


public:
    using ClientBaseType::enqueue;
    [[nodiscard]] bool enqueueTelemetry(const void* bytes, usize byteCount);


protected:
    bool internalInit(AStringView url);
    bool internalUpdate();
    [[nodiscard]] inline bool workerCanExit()const{
        return !m_hasPendingPayload && !m_messageCount.load(MemoryOrder::acquire) && !m_telemetryCount.load(MemoryOrder::acquire);
    }

protected:
    inline void enqueue(MessageType&& data){
        this->m_messageQueue.emplace(Move(data));
        m_messageCount.fetch_add(1, MemoryOrder::relaxed);
        this->m_semaphore.release();
    }
    inline void enqueue(const MessageType& data){
        this->m_messageQueue.emplace(data);
        m_messageCount.fetch_add(1, MemoryOrder::relaxed);
        this->m_semaphore.release();
    }

    inline bool tryDequeueMessage(MessageType& msg){
        auto dequeued = this->tryDequeue(msg);
        if(dequeued)
            m_messageCount.fetch_sub(1, MemoryOrder::relaxed);
        return dequeued;
    }


private:
    void* m_curl;
    Vector<u8, LogArena> m_pendingPayload;
    AString<LogArena> m_messageUrl;
    AString<LogArena> m_telemetryUrl;
    ClientPayloadKind::Enum m_pendingPayloadKind;
    bool m_hasPendingPayload;

private:
    Atomic<usize> m_messageCount;
    Atomic<usize> m_telemetryCount;
    ParallelQueue<LogBytes, LogArena> m_telemetryQueue;
};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


inline constexpr TStringView s_ClientStandaloneName = NWB_TEXT("ClientStandalone");
class ClientStandalone final : public ClientBase<ClientStandalone, s_ClientStandaloneName>{
    template<typename, const TStringView&> friend class LoggerWorkerBase;
    template<typename, const TStringView&> friend class QueuedLoggerWorkerBase;

    using ClientBaseType = ClientBase<ClientStandalone, s_ClientStandaloneName>;
    using BaseType = ClientBaseType::BaseType;
    using UpdateBaseType = ClientBaseType::UpdateBaseType;


private:
    static bool globalInit();


public:
    ClientStandalone();
    virtual ~ClientStandalone()override;


public:
    using ClientBaseType::enqueue;


protected:
    bool internalInit(BasicStringView<tchar> logFileNameBase = {});
    bool internalUpdate();

protected:
    using UpdateBaseType::enqueue;


private:
    ProcessedMessageFile m_processedMessageFile;
};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_LOG_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

