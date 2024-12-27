// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#pragma once


#include "global.h"

#include <thread>
#include <semaphore>
#include <atomic>
#include <chrono>

#include <curl/curl.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_LOG_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


enum class Type : u8{
    Info,
    Warning,
    Error,
    Fatal,
};

using MessageType = std::tuple<std::chrono::system_clock::time_point, Type, std::basic_string<tchar>>;
using MessageQueue = ParallelQueue<MessageType>;


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


static constexpr char g_defaultURL[] = "http://localhost:7117";


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


template <typename T, const tchar* NAME>
class Base{
private:
    static inline bool globalInit(){
        CURLcode ret;

        ret = curl_global_init(CURL_GLOBAL_ALL);
        if(ret != CURLE_OK)
            return false;

        return true;
    }
    static void globalUpdate(T* _this){
        for(;;){
            _this->m_semaphore.acquire();
            if(_this->internalUpdate() && _this->m_exit.load(std::memory_order_acquire))
                break;
        }
    }


public:
    Base()
        : m_curl(nullptr)
        , m_semaphore(0)
        , m_exit(false)
    {}
    virtual ~Base(){
        m_exit.store(true, std::memory_order_release);
        if(m_thread.joinable())
            m_thread.join();

        if(m_curl){
            curl_easy_cleanup(m_curl);
            m_curl = nullptr;
        }
    }


public:
    inline bool init(const char* url){
        if(!m_globalInit){
            if(!globalInit()){
                static_cast<T*>(this)->enqueue(std::format(NWB_TEXT("Failed to global initialization on {}"), NAME));
                return false;
            }
            m_globalInit = true;
        }

        m_curl = curl_easy_init();
        if(!m_curl){
            static_cast<T*>(this)->enqueue(std::format(NWB_TEXT("Failed to initialize CURL on {}"), NAME));
            return false;
        }

        auto ret = static_cast<T*>(this)->internalInit(url);
        if (ret)
            m_thread = std::thread(globalUpdate, static_cast<T*>(this));

        return ret;
    }

public:
    inline bool enqueue(std::basic_string<tchar>&& str, Type type = Type::Info){
        return static_cast<T*>(this)->enqueue(std::make_tuple(std::chrono::system_clock::now(), type, std::move(str)));
    }
    inline bool enqueue(const std::basic_string<tchar>& str, Type type = Type::Info){
        return static_cast<T*>(this)->enqueue(std::make_tuple(std::chrono::system_clock::now(), type, str));
    }


protected:
    bool internalInit(const char* url){ return true; }
    bool internalUpdate(){ return true; }


protected:
    inline bool enqueue(MessageType&& data){
        auto ret = m_msgQueue.enqueue(std::move(data));
        if (ret)
            m_semaphore.release();
        return ret;
    }
    inline bool enqueue(const MessageType& data){
        auto ret = m_msgQueue.enqueue(data);
        if(ret)
            m_semaphore.release();
        return ret;
    }

    inline bool try_dequeue(MessageType& msg){
        return m_msgQueue.try_dequeue(msg);
    }


protected:
    CURL* m_curl;
    MessageQueue m_msgQueue;


private:
    std::thread m_thread;
    std::counting_semaphore<INT_MAX> m_semaphore;
    std::atomic<bool> m_exit;


private:
    static bool m_globalInit;
};
template <typename T, const tchar* NAME>
bool Base<T, NAME>::m_globalInit = false;


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_LOG_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

