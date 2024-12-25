// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#pragma once


#include <atomic>

#include <logger/common.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_LOG_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


constexpr tchar CLIENT_NAME[] = NWB_TEXT("Client");
class Client : public Base<Client, CLIENT_NAME>{
private:
    static usize sendCallback(void* contents, usize size, usize nmemb, Client* _this);


public:
    Client();


public:
    bool update();


protected:
    bool internalInit(const char* url) override;

protected:
    inline bool enqueue(std::basic_string<tchar>&& str, Type type = Type::Info){
        auto ret = Base::enqueue(std::move(str), type);
        if(ret)
            m_msgCount.fetch_add(1, std::memory_order_acq_rel);
        return ret;
    }
    inline bool enqueue(const std::basic_string<tchar>& str, Type type = Type::Info){
        auto ret = Base::enqueue(str, type);
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

