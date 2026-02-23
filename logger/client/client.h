// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#pragma once


#include <curl/curl.h>

#include <logger/common.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_LOG_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


constexpr tchar CLIENT_NAME[] = NWB_TEXT("Client");
class Client : public BaseUpdateIfQueued<Client, CLIENT_NAME>{
    friend class Base;
    friend class BaseUpdateIfQueued;


private:
    static bool globalInit();
    static usize sendCallback(void* contents, usize size, usize nmemb, Client* _this);


private:
    static bool s_SendSwitch;


public:
    Client();
    virtual ~Client()override;


public:
    inline void enqueue(TString&& str, Type type = Type::Info){ return Base::enqueue(Move(str), type); }
    inline void enqueue(const TString& str, Type type = Type::Info){ return Base::enqueue(str, type); }


protected:
    bool internalInit(const char* url);
    bool internalUpdate();

protected:
    inline void enqueue(MessageType&& data){
        BaseUpdateIfQueued::enqueue(Move(data));
        m_msgCount.fetch_add(1, MemoryOrder::memory_order_acq_rel);
    }
    inline void enqueue(const MessageType& data){
        BaseUpdateIfQueued::enqueue(data);
        m_msgCount.fetch_add(1, MemoryOrder::memory_order_acq_rel);
    }

    inline bool try_dequeue(MessageType& msg){
        auto ret = BaseUpdateIfQueued::tryDequeue(msg);
        if(ret)
            m_msgCount.fetch_sub(1, MemoryOrder::memory_order_acq_rel);
        return ret;
    }


private:
    CURL* m_curl;

private:
    Atomic<i32> m_msgCount;
};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_LOG_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

