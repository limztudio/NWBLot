// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#pragma once


#include <logger/common.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_LOG_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


constexpr tchar CLIENT_NAME[] = NWB_TEXT("Client");
class Client : public Base<Client, CLIENT_NAME>{
    friend class Base;

private:
    static usize sendCallback(void* contents, usize size, usize nmemb, Client* _this);


public:
    Client();


public:
    inline bool enqueue(std::basic_string<tchar>&& str, Type type = Type::Info){
        return Base::enqueue(std::move(str), type);
    }
    inline bool enqueue(const std::basic_string<tchar>& str, Type type = Type::Info){
        return Base::enqueue(str, type);
    }


protected:
    bool internalInit(const char* url);
    bool internalUpdate();

protected:
    inline bool enqueue(MessageType&& data){
        auto ret = Base::enqueue(std::move(data));
        if(ret)
            m_msgCount.fetch_add(1, std::memory_order_acq_rel);
        return ret;
    }
    inline bool enqueue(const MessageType& data){
        auto ret = Base::enqueue(data);
        if(ret)
            m_msgCount.fetch_add(1, std::memory_order_acq_rel);
        return ret;
    }

    inline bool try_dequeue(MessageType& msg){
        auto ret = Base::try_dequeue(msg);
        if(ret)
            m_msgCount.fetch_sub(1, std::memory_order_acq_rel);
        return ret;
    }


private:
    std::atomic<i32> m_msgCount;
};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_LOG_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

