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


namespace ClientPayloadKind{
    enum Enum : u8{
        Message,
        Telemetry,
        NameSymbol,
    };
};

inline constexpr tchar CLIENT_NAME[] = NWB_TEXT("Client");
class Client final : public IClient, public BaseUpdateIfQueued<Client, CLIENT_NAME>{
    template<typename, const tchar*> friend class Base;
    template<typename, const tchar*> friend class BaseUpdateIfQueued;

    using BaseType = Base<Client, CLIENT_NAME>;
    using UpdateBaseType = BaseUpdateIfQueued<Client, CLIENT_NAME>;


private:
    static bool globalInit();


public:
    Client();
    virtual ~Client()override;


public:
    using BaseType::enqueue;
    virtual LogArena& arena()override{ return BaseType::arena(); }
    virtual void enqueue(LogString&& str, Type::Enum type = Type::Info)override{ BaseType::enqueue(Move(str), type); }
    virtual void enqueue(const LogString& str, Type::Enum type = Type::Info)override{ BaseType::enqueue(str, type); }
    [[nodiscard]] bool enqueueTelemetry(const void* bytes, usize byteCount);


protected:
    bool internalInit(NotNull<const char*> url);
    bool internalUpdate();

private:
    // Selects the runtime symbol table as the pending upload payload (to /namesym) when it has grown since the last
    // upload, rate-limited. BUILDMODE-only: only a buildmode client holds the full hash->text table; an opt client
    // resolves its runtime names in-message and has nothing the server can't already read, so this is a no-op there.
    [[nodiscard]] bool pickNameSymbolPayload();

protected:
    inline void enqueue(MessageType&& data){
        this->m_msgQueue.emplace(Move(data));
        m_msgCount.fetch_add(1, MemoryOrder::relaxed);
        this->m_semaphore.release();
    }
    inline void enqueue(const MessageType& data){
        this->m_msgQueue.emplace(data);
        m_msgCount.fetch_add(1, MemoryOrder::relaxed);
        this->m_semaphore.release();
    }

    inline bool try_dequeue(MessageType& msg){
        auto ret = this->tryDequeue(msg);
        if(ret)
            m_msgCount.fetch_sub(1, MemoryOrder::relaxed);
        return ret;
    }


private:
    static constexpr f32 s_NameSymbolUploadIntervalSeconds = 2.0f;

private:
    void* m_curl;
    Vector<u8, LogArena> m_pendingPayload;
    AString<LogArena> m_messageUrl;
    AString<LogArena> m_telemetryUrl;
    AString<LogArena> m_namesymUrl;
    bool m_hasPendingPayload;
    ClientPayloadKind::Enum m_pendingPayloadKind;
    usize m_namesymUploadedCount;
    usize m_pendingNamesymCount;
    Timer m_lastNamesymUploadTime;

private:
    Atomic<usize> m_msgCount;
    Atomic<usize> m_telemetryCount;
    ParallelQueue<LogBytes, LogArena> m_telemetryQueue;
};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


inline constexpr tchar CLIENT_STANDALONE_NAME[] = NWB_TEXT("ClientStandalone");
class ClientStandalone final : public IClient, public BaseUpdateIfQueued<ClientStandalone, CLIENT_STANDALONE_NAME>{
    template<typename, const tchar*> friend class Base;
    template<typename, const tchar*> friend class BaseUpdateIfQueued;

    using BaseType = Base<ClientStandalone, CLIENT_STANDALONE_NAME>;
    using UpdateBaseType = BaseUpdateIfQueued<ClientStandalone, CLIENT_STANDALONE_NAME>;


private:
    static bool globalInit();


public:
    ClientStandalone();
    virtual ~ClientStandalone()override;


public:
    using BaseType::enqueue;
    virtual LogArena& arena()override{ return BaseType::arena(); }
    virtual void enqueue(LogString&& str, Type::Enum type = Type::Info)override{ BaseType::enqueue(Move(str), type); }
    virtual void enqueue(const LogString& str, Type::Enum type = Type::Info)override{ BaseType::enqueue(str, type); }


protected:
    bool internalInit(BasicStringView<tchar> logFileNameBase = {});
    bool internalUpdate();

protected:
    using UpdateBaseType::enqueue;


private:
    ProcessedMessageFile m_processedMsgFile;
};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_LOG_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

