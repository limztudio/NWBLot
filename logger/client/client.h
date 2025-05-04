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
    static bool s_sendSwitch;


public:
    Client();
    virtual ~Client()override;


public:
    inline bool enqueue(TString&& str, Type type = Type::Info){ return Base::enqueue(Move(str), type); }
    inline bool enqueue(const TString& str, Type type = Type::Info){ return Base::enqueue(str, type); }


protected:
    bool internalInit(const char* url);
    bool internalUpdate();

protected:
    inline bool enqueue(MessageType&& data){
        auto ret = BaseUpdateIfQueued::enqueue(Move(data));
        if(ret)
            m_msgCount.fetch_add(1, MemoryOrder::memory_order_acq_rel);
        return ret;
    }
    inline bool enqueue(const MessageType& data){
        auto ret = BaseUpdateIfQueued::enqueue(data);
        if(ret)
            m_msgCount.fetch_add(1, MemoryOrder::memory_order_acq_rel);
        return ret;
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

