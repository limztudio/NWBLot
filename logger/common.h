// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#pragma once


#include "global.h"


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
    static inline bool globalInit(){ return true; }


public:
    Base()
        :  m_exit(false)
    {}
    virtual ~Base(){
        m_exit.store(true, MemoryOrder::memory_order_release);
        static_cast<T*>(this)->internalDestroy();
        if(m_thread.joinable())
            m_thread.join();
    }


protected:
    template <typename... ARGS>
    inline bool internalInit(ARGS&&... args){ return true; }
    inline void internalDestroy(){}
    inline bool internalUpdate(){ return true; }

protected:
    inline bool tryDequeue(MessageType& msg){ return m_msgQueue.try_pop(msg); }


public:
    template <typename... ARGS>
    inline bool init(ARGS&&... args){
        if(!static_cast<T*>(this)->m_globalInit){
            if(!static_cast<T*>(this)->globalInit()){
                static_cast<T*>(this)->T::enqueue(stringFormat(NWB_TEXT("Failed to global initialization on {}"), NAME), Type::Fatal);
                return false;
            }
            static_cast<T*>(this)->m_globalInit = true;
        }

        auto ret = static_cast<T*>(this)->internalInit(Forward<ARGS>(args)...);
        m_thread = Thread(T::globalUpdate, static_cast<T*>(this));

        return ret;
    }

public:
    inline void enqueue(TString&& str, Type type = Type::Info){ return static_cast<T*>(this)->T::enqueue(MakeTuple(timerNow(), type, Move(str))); }
    inline void enqueue(const TString& str, Type type = Type::Info){ return static_cast<T*>(this)->T::enqueue(MakeTuple(timerNow(), type, str)); }


protected:
    MessageQueue m_msgQueue;

protected:
    Thread m_thread;
    Atomic<bool> m_exit;

private:
    static bool m_globalInit;
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
    inline void enqueue(MessageType&& data){ return Base<T, NAME>::m_msgQueue.emplace(Move(data)); }
    inline void enqueue(const MessageType& data){ return Base<T, NAME>::m_msgQueue.emplace(data); }


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
            bool updateSucceeded;
            {
                ScopedLock _(_this->m_semaphore);
                updateSucceeded = _this->internalUpdate();
            }

            if (!updateSucceeded){
                _this->m_exit.store(true, MemoryOrder::memory_order_release);
                break;
            }

            if(_this->m_exit.load(MemoryOrder::memory_order_acquire))
                break;
        }
    }


public:
    BaseUpdateIfQueued()
        : m_semaphore(1)
    {}


protected:
    void internalDestroy(){ m_semaphore.release(); }

protected:
    inline void enqueue(MessageType&& data){ return Base<T, NAME>::m_msgQueue.emplace(Move(data)); }
    inline void enqueue(const MessageType& data){ return Base<T, NAME>::m_msgQueue.emplace(data); }


protected:
    BinarySemaphore m_semaphore;


protected:
    static bool m_globalInit;
};
template <typename T, const tchar* NAME>
bool BaseUpdateIfQueued<T, NAME>::m_globalInit = false;


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_LOG_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

