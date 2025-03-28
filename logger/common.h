// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#pragma once


#include "global.h"

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

using MessageType = Tuple<Timer, Type, TString>;
using MessageQueue = ParallelQueue<MessageType>;


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


template <typename T, const tchar* NAME>
class Base{
protected:
    static inline bool globalInit(){
        CURLcode ret;

        ret = curl_global_init(CURL_GLOBAL_ALL);
        if(ret != CURLE_OK)
            return false;

        return true;
    }


public:
    Base()
        : m_curl(nullptr)
        , m_exit(false)
    {}
    virtual ~Base(){
        m_exit.store(true, MemoryOrder::memory_order_release);
        static_cast<T*>(this)->internalDestroy();
        if(m_thread.joinable())
            m_thread.join();

        if(m_curl){
            curl_easy_cleanup(m_curl);
            m_curl = nullptr;
        }
    }


protected:
    bool internalInit(const char* url){ return true; }
    void internalDestroy(){}
    bool internalUpdate(){ return true; }

protected:
    inline bool try_dequeue(MessageType& msg){ return m_msgQueue.try_dequeue(msg); }


public:
    inline bool init(const char* url){
        if(!static_cast<T*>(this)->m_globalInit){
            if(!static_cast<T*>(this)->globalInit()){
                static_cast<T*>(this)->enqueue(stringFormat(NWB_TEXT("Failed to global initialization on {}"), NAME), Type::Fatal);
                return false;
            }
            static_cast<T*>(this)->m_globalInit = true;
        }

        m_curl = curl_easy_init();
        if(!m_curl){
            static_cast<T*>(this)->enqueue(stringFormat(NWB_TEXT("Failed to initialize CURL on {}"), NAME), Type::Fatal);
            return false;
        }

        auto ret = static_cast<T*>(this)->internalInit(url);
        if (ret)
            m_thread = Thread(T::globalUpdate, static_cast<T*>(this));

        return ret;
    }

public:
    inline bool enqueue(TString&& str, Type type = Type::Info){ return static_cast<T*>(this)->enqueue(MakeTuple(timerNow(), type, Move(str))); }
    inline bool enqueue(const TString& str, Type type = Type::Info){ return static_cast<T*>(this)->enqueue(MakeTuple(timerNow(), type, str)); }


protected:
    CURL* m_curl;
    MessageQueue m_msgQueue;

protected:
    Thread m_thread;
    Atomic<bool> m_exit;
};

template <typename T, float UPDATE_INTERVAL, const tchar* NAME>
class BaseUpdateOrdinary : public Base<T, NAME>{
	friend Base<T, NAME>;

private:
    static void globalUpdate(T* _this){
        for(;;){
            auto curTime = timerNow();
            if(durationInSeconds<float>(curTime, _this->m_lastTime) < UPDATE_INTERVAL)
                continue;

            _this->m_lastTime = curTime;

            if(_this->internalUpdate() && _this->m_exit.load(MemoryOrder::memory_order_acquire))
                break;
        }
    }


public:
    BaseUpdateOrdinary()
        : m_lastTime(timerNow())
    {}


protected:
    inline bool enqueue(MessageType&& data){ return Base<T, NAME>::m_msgQueue.enqueue(Move(data)); }
    inline bool enqueue(const MessageType& data){ return Base<T, NAME>::m_msgQueue.enqueue(data); }


private:
    Timer m_lastTime;


protected:
    static bool m_globalInit;
};
template <typename T, float UPDATE_INTERVAL, const tchar* NAME>
bool BaseUpdateOrdinary<T, UPDATE_INTERVAL, NAME>::m_globalInit = false;

template <typename T, const tchar* NAME>
class BaseUpdateIfQueued : public Base<T, NAME>{
    friend Base<T, NAME>;

private:
    static void globalUpdate(T* _this){
        for(;;){
            _this->m_semaphore.acquire();
            if(_this->internalUpdate() && _this->m_exit.load(MemoryOrder::memory_order_acquire))
                break;
        }
    }


public:
    BaseUpdateIfQueued()
        : m_semaphore(0)
    {}


protected:
    void internalDestroy(){ m_semaphore.release(); }

protected:
    inline bool enqueue(MessageType&& data){
        auto ret = Base<T, NAME>::m_msgQueue.enqueue(Move(data));
        if (ret)
            m_semaphore.release();
        return ret;
    }
    inline bool enqueue(const MessageType& data){
        auto ret = Base<T, NAME>::m_msgQueue.enqueue(data);
        if(ret)
            m_semaphore.release();
        return ret;
    }


protected:
    Semaphore<INT_MAX> m_semaphore;


protected:
    static bool m_globalInit;
};
template <typename T, const tchar* NAME>
bool BaseUpdateIfQueued<T, NAME>::m_globalInit = false;


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_LOG_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

