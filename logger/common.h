// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#pragma once


#include "global.h"

#include <fstream>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_LOG_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace Type{
    enum Enum : u8{
        Info,
        EssentialInfo,
        Warning,
        CriticalWarning,
        Error,
        Fatal,
    };
};

using MessageType = Tuple<Timer, Type::Enum, TString>;
using MessageQueue = ParallelQueue<MessageType>;


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


[[nodiscard]] inline const tchar* MessageTypeToString(Type::Enum type){
    switch(type){
    case Type::Info:
        return NWB_TEXT("INFO");
    case Type::EssentialInfo:
        return NWB_TEXT("ESSENTIAL INFO");
    case Type::Warning:
        return NWB_TEXT("WARNING");
    case Type::CriticalWarning:
        return NWB_TEXT("CRITICAL WARNING");
    case Type::Error:
        return NWB_TEXT("ERROR");
    case Type::Fatal:
        return NWB_TEXT("FATAL");
    }
    return NWB_TEXT("UNKNOWN");
}

[[nodiscard]] inline bool MessageTypeWritesToErrorStream(Type::Enum type){
    switch(type){
    case Type::CriticalWarning:
    case Type::Error:
    case Type::Fatal:
        return true;
    default:
        return false;
    }
}

[[nodiscard]] inline TString FormatMessageForProcessing(const MessageType& msg){
    const auto& [time, type, str] = msg;
    return StringFormat(NWB_TEXT("{} [{}]:\n{}"), DurationInTimeDelta(time), MessageTypeToString(type), str);
}


class ProcessedMessageFile{
public:
    using StreamType = std::basic_ofstream<tchar>;


public:
    ProcessedMessageFile() = default;
    ~ProcessedMessageFile(){ close(); }


public:
    bool open(BasicStringView<tchar> fileNameBase){
        close();
        if(fileNameBase.empty())
            return false;

        std::tm localTime = {};
        if(!GetLocalTime(localTime))
            return false;

        Path executableDirectory;
        if(!GetExecutableDirectory(executableDirectory))
            return false;

        const TString fileName = StringFormat(
            NWB_TEXT("{}_{:04}{:02}{:02}_{:02}{:02}{:02}.log"),
            fileNameBase,
            localTime.tm_year + 1900,
            localTime.tm_mon + 1,
            localTime.tm_mday,
            localTime.tm_hour,
            localTime.tm_min,
            localTime.tm_sec
        );
        m_filePath = executableDirectory / fileName;

        m_stream.open(m_filePath, std::ios::out | std::ios::app);
        return m_stream.is_open();
    }
    bool openByExecutableName(){
        Path executableName;
        if(!GetExecutableName(executableName))
            return false;

        const TString executableNameString = PathToString<tchar>(executableName);
        return open(executableNameString);
    }

    void close(){
        if(m_stream.is_open())
            m_stream.close();
    }

    bool writeLine(BasicStringView<tchar> line){
        if(!m_stream.is_open())
            return false;

        m_stream << line << static_cast<tchar>('\n');
        m_stream.flush();
        return m_stream.good();
    }


private:
    StreamType m_stream;
    Path m_filePath;
};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


template<typename T, const tchar* NAME>
class Base{
protected:
    static inline bool globalInit(){ return true; }


public:
    Base()
        : m_exit(false)
    {}
    virtual ~Base(){
        stopWorker();
    }


protected:
    template<typename... ARGS>
    inline bool internalInit(ARGS&&... args){
        ((void)args, ...);
        return true;
    }
    inline void internalDestroy(){}
    inline bool internalUpdate(){ return true; }

protected:
    inline bool tryDequeue(MessageType& msg){ return m_msgQueue.try_pop(msg); }
    void stopWorker(){
        const bool alreadyStopping = m_exit.exchange(true, std::memory_order_acq_rel);

        if(!alreadyStopping)
            static_cast<T*>(this)->internalDestroy();
        if(m_thread.joinable())
            m_thread.join();
    }


public:
    template<typename... ARGS>
    inline bool init(ARGS&&... args){
        if(!static_cast<T*>(this)->s_GlobalInit){
            if(!static_cast<T*>(this)->globalInit()){
                static_cast<T*>(this)->T::enqueue(StringFormat(NWB_TEXT("Failed to global initialization on {}"), NAME), Type::Fatal);
                return false;
            }
            static_cast<T*>(this)->s_GlobalInit = true;
        }

        const bool ret = static_cast<T*>(this)->internalInit(Forward<ARGS>(args)...);
        if(!ret)
            return false;

        m_thread = Thread(T::globalUpdate, static_cast<T*>(this));

        return true;
    }

public:
    inline void enqueue(TString&& str, Type::Enum type = Type::Info){ return static_cast<T*>(this)->T::enqueue(MakeTuple(TimerNow(), type, Move(str))); }
    inline void enqueue(const TString& str, Type::Enum type = Type::Info){ return static_cast<T*>(this)->T::enqueue(MakeTuple(TimerNow(), type, str)); }


protected:
    MessageQueue m_msgQueue;

protected:
    Thread m_thread;
    Atomic<bool> m_exit;
};

template<typename T, f32 UPDATE_INTERVAL, const tchar* NAME>
class BaseUpdateOrdinary : public Base<T, NAME>{
    friend Base<T, NAME>;


private:
    static bool s_GlobalInit;


private:
    static void globalUpdate(T* _this){
        for(;;){
            auto curTime = TimerNow();
            if(DurationInSeconds<f32>(curTime, _this->m_lastTime) < UPDATE_INTERVAL)
                continue;

            _this->m_lastTime = curTime;

            if(_this->internalUpdate() && _this->m_exit.load(std::memory_order_acquire))
                break;
        }
    }


public:
    BaseUpdateOrdinary()
        : m_lastTime(TimerNow())
    {}


protected:
    inline void enqueue(MessageType&& data){ return Base<T, NAME>::m_msgQueue.emplace(Move(data)); }
    inline void enqueue(const MessageType& data){ return Base<T, NAME>::m_msgQueue.emplace(data); }


private:
    Timer m_lastTime;
};
template<typename T, f32 UPDATE_INTERVAL, const tchar* NAME>
bool BaseUpdateOrdinary<T, UPDATE_INTERVAL, NAME>::s_GlobalInit = false;

template<typename T, const tchar* NAME>
class BaseUpdateIfQueued : public Base<T, NAME>{
    friend Base<T, NAME>;


private:
    static bool s_GlobalInit;


private:
    static void globalUpdate(T* _this){
        for(;;){
            _this->m_semaphore.acquire();

            bool updateSucceeded = _this->internalUpdate();

            if(!updateSucceeded){
                _this->m_exit.store(true, std::memory_order_release);
                break;
            }

            if(_this->m_exit.load(std::memory_order_acquire))
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
    inline void enqueue(MessageType&& data){ Base<T, NAME>::m_msgQueue.emplace(Move(data)); m_semaphore.release(); }
    inline void enqueue(const MessageType& data){ Base<T, NAME>::m_msgQueue.emplace(data); m_semaphore.release(); }


protected:
    Semaphore<> m_semaphore;
};
template<typename T, const tchar* NAME>
bool BaseUpdateIfQueued<T, NAME>::s_GlobalInit = false;


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_LOG_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

