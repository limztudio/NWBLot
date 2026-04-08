// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#pragma once


#include <logger/common.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_LOG_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


class IClient{
public:
    virtual ~IClient() = default;


public:
    virtual void enqueue(TString&& str, Type type = Type::Info) = 0;
    virtual void enqueue(const TString& str, Type type = Type::Info) = 0;
};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


inline constexpr tchar CLIENT_NAME[] = NWB_TEXT("Client");
class Client : public IClient, public BaseUpdateIfQueued<Client, CLIENT_NAME>{
    template<typename, const tchar*> friend class Base;
    template<typename, const tchar*> friend class BaseUpdateIfQueued;

    using BaseType = Base<Client, CLIENT_NAME>;
    using UpdateBaseType = BaseUpdateIfQueued<Client, CLIENT_NAME>;


private:
    static bool s_SendSwitch;
    static bool globalInit();
    static usize sendCallback(void* contents, usize size, usize nmemb, Client* _this);


public:
    Client();
    virtual ~Client()override;


public:
    virtual void enqueue(TString&& str, Type type = Type::Info)override{ BaseType::enqueue(Move(str), type); }
    virtual void enqueue(const TString& str, Type type = Type::Info)override{ BaseType::enqueue(str, type); }


protected:
    bool internalInit(NotNull<const char*> url);
    bool internalUpdate();

protected:
    inline void enqueue(MessageType&& data){
        this->m_msgQueue.emplace(Move(data));
        m_msgCount.fetch_add(1, std::memory_order_relaxed);
        this->m_semaphore.release();
    }
    inline void enqueue(const MessageType& data){
        this->m_msgQueue.emplace(data);
        m_msgCount.fetch_add(1, std::memory_order_relaxed);
        this->m_semaphore.release();
    }

    inline bool try_dequeue(MessageType& msg){
        auto ret = this->tryDequeue(msg);
        if(ret)
            m_msgCount.fetch_sub(1, std::memory_order_relaxed);
        return ret;
    }


private:
    void* m_curl;

private:
    Atomic<i32> m_msgCount;
};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


inline constexpr tchar CLIENT_STANDALONE_NAME[] = NWB_TEXT("ClientStandalone");
class ClientStandalone : public IClient, public BaseUpdateIfQueued<ClientStandalone, CLIENT_STANDALONE_NAME>{
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
    virtual void enqueue(TString&& str, Type type = Type::Info)override{ BaseType::enqueue(Move(str), type); }
    virtual void enqueue(const TString& str, Type type = Type::Info)override{ BaseType::enqueue(str, type); }


protected:
    bool internalInit(BasicStringView<tchar> logFileNameBase = {});
    bool internalUpdate();

protected:
    inline void enqueue(MessageType&& data){ UpdateBaseType::enqueue(Move(data)); }
    inline void enqueue(const MessageType& data){ UpdateBaseType::enqueue(data); }


private:
    ProcessedMessageFile m_processedMsgFile;
};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_LOG_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
