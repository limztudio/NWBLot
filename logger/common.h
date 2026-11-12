// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#pragma once


#include "global.h"

#include <global/simd/vector_arithmetic.h>

#include <global/filesystem/executable_paths.h>
#include <global/binary.h>
#include <global/filesystem/operations.h>
#include <global/process.h>
#include <global/thread.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_LOG_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


using MessageType = Tuple<Timer, Core::Common::LogType::Enum, LogString>;
using MessageQueue = ParallelQueue<MessageType, LogArena>;
using LogBytes = Vector<u8, LogArena>;

inline constexpr StringView s_TelemetryUploadEndpoint = "/telemetry";
inline constexpr usize s_MaxLogMessageUploadBytes = 1024u * 1024u;
inline constexpr usize s_MaxTelemetryUploadBytes = 64u * 1024u * 1024u;
inline constexpr StringView s_CrashUploadTokenOption = "--crash-upload-token";
inline constexpr i32 s_LocalTimeYearBase = 1900;
inline constexpr i32 s_LocalTimeMonthBase = 1;


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


// Log server endpoint options shared by log clients and the server.
namespace ArgCommand{
    enum Enum : u8{
        LogAddress,
        LogPort,
    };
};
inline constexpr Array<StringView, 2u> s_ArgCmd = { "-a,--logaddress", "-p,--logport", };
inline const Tuple< AStringView, u16 > s_ArgDefault = { "http://localhost", 7117u, };
inline constexpr Array<StringView, 2u> s_ArgDesc = { "Log server address", "Log server port", };


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


template<ArgCommand::Enum Command, typename CLI, typename T>
inline auto ArgAddOption(CLI& cli, T& arg){
    return cli.add_option(s_ArgCmd[static_cast<usize>(Command)].data(), arg, s_ArgDesc[static_cast<usize>(Command)].data())->default_val(Get<static_cast<usize>(Command)>(s_ArgDefault));
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


[[nodiscard]] inline MessageType MakeMessageType(LogArena& arena)noexcept{
    return MakeTuple(Timer{}, Core::Common::LogType::Info, LogString(arena));
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


inline constexpr TStringView s_UnknownLogLevelName = FOUNDATION_TEXT("UNKNOWN");


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


[[nodiscard]] constexpr TStringView MessageTypeToString(Core::Common::LogType::Enum type)noexcept{
    switch(type){
    case Core::Common::LogType::Info:
        return FOUNDATION_TEXT("INFO");
    case Core::Common::LogType::EssentialInfo:
        return FOUNDATION_TEXT("ESSENTIAL INFO");
    case Core::Common::LogType::Warning:
        return FOUNDATION_TEXT("WARNING");
    case Core::Common::LogType::CriticalWarning:
        return FOUNDATION_TEXT("CRITICAL WARNING");
    case Core::Common::LogType::Assert:
        return FOUNDATION_TEXT("ASSERT");
    case Core::Common::LogType::Error:
        return FOUNDATION_TEXT("ERROR");
    case Core::Common::LogType::Fatal:
        return FOUNDATION_TEXT("FATAL");
    }
    return s_UnknownLogLevelName;
}

[[nodiscard]] constexpr b8 MessageTypeWritesToErrorStream(Core::Common::LogType::Enum type)noexcept{
    switch(type){
    case Core::Common::LogType::CriticalWarning:
    case Core::Common::LogType::Assert:
    case Core::Common::LogType::Error:
    case Core::Common::LogType::Fatal:
        return true;
    default:
        return false;
    }
}

[[nodiscard]] inline LogString FormatMessageForProcessing(LogArena& arena, const MessageType& msg){
    const auto& [time, type, str] = msg;
    return StringFormat(arena, FOUNDATION_TEXT("{} [{}]:\n{}"), DurationInTimeDelta(time), MessageTypeToString(type), str);
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


template<typename T>
class LoggerWorkerBase{
public:
    explicit LoggerWorkerBase(const NameText<char> allocationLog)
        : m_arena(Name(allocationLog))
        , m_messageQueue(m_arena)
        , m_exit(false)
    {}
    virtual ~LoggerWorkerBase()noexcept{
        stopWorker();
    }


public:
    LogArena& arena()noexcept{ return m_arena; }


public:
    template<typename... Args>
    b8 init(Args&&... args){
        if constexpr(requires{ T::GlobalInit(); }){
            if(!T::s_GlobalInit){
                if(!T::GlobalInit())
                    return false;
                T::s_GlobalInit = true;
            }
        }

        const b8 initialized = MakeNotNull(checked_cast<T*>(this))->internalInit(Forward<Args>(args)...);
        if(!initialized)
            return false;

        m_thread = Thread([this](){ T::GlobalUpdate(*MakeNotNull(checked_cast<T*>(this))); });

        return true;
    }


public:
    void enqueue(LogString&& str, Core::Common::LogType::Enum type = Core::Common::LogType::Info){
        LogString message(Move(str), m_arena);
        MakeNotNull(checked_cast<T*>(this))->T::enqueue(MakeTuple(TimerNow(), type, Move(message)));
    }

    void enqueue(const LogString& str, Core::Common::LogType::Enum type = Core::Common::LogType::Info){
        LogString message(str, m_arena);
        MakeNotNull(checked_cast<T*>(this))->T::enqueue(MakeTuple(TimerNow(), type, Move(message)));
    }

    void enqueue(BasicStringView<tchar> str, Core::Common::LogType::Enum type = Core::Common::LogType::Info){
        LogString message(str, m_arena);
        MakeNotNull(checked_cast<T*>(this))->T::enqueue(MakeTuple(TimerNow(), type, Move(message)));
    }


protected:
    b8 tryDequeue(MessageType& msg){ return m_messageQueue.try_pop(msg); }

    void stopWorker(){
        const b8 alreadyStopping = m_exit.exchange(true, MemoryOrder::acq_rel);

        if(!alreadyStopping)
            MakeNotNull(checked_cast<T*>(this))->internalDestroy();
        if(m_thread.joinable())
            m_thread.join();
    }


protected:
    LogArena m_arena;
    MessageQueue m_messageQueue;


protected:
    Thread m_thread;
    Atomic<b8> m_exit;
};

template<typename T, f32 UpdateIntervalSeconds>
class IntervalLoggerWorkerBase : public LoggerWorkerBase<T>{
    friend LoggerWorkerBase<T>;


private:
    static void GlobalUpdate(T& self){
        for(;;){
            const Timer currentTime = TimerNow();
            const f32 elapsedSeconds = DurationInSeconds<f32>(currentTime, self.m_lastUpdateTime);
            if(elapsedSeconds < UpdateIntervalSeconds){
                // Sleep until the next update deadline; the cadence bounds message and shutdown latency.
                const f32 remainingSeconds = VectorGetX(VectorSubtract(VectorReplicate(UpdateIntervalSeconds), VectorReplicate(elapsedSeconds)));
                static constexpr f32 s_MillisecondsPerSecondF = 1000.0f;
                static constexpr u32 s_MinSleepMilliseconds = 1u;
                const u32 sleepMilliseconds = Max<u32>(s_MinSleepMilliseconds, static_cast<u32>(Ceil(VectorGetX(VectorMultiply(VectorReplicate(remainingSeconds), VectorReplicate(s_MillisecondsPerSecondF))))));
                SleepMS(sleepMilliseconds);
                continue;
            }

            self.m_lastUpdateTime = currentTime;

            if(self.internalUpdate() && self.m_exit.load(MemoryOrder::acquire))
                break;
        }
    }


public:
    explicit IntervalLoggerWorkerBase(const NameText<char> allocationLog)
        : LoggerWorkerBase<T>(allocationLog)
        , m_lastUpdateTime(TimerNow())
    {}


protected:
    void enqueue(MessageType&& data){ LoggerWorkerBase<T>::m_messageQueue.emplace(Move(data)); }


private:
    Timer m_lastUpdateTime;
};

template<typename T>
class QueuedLoggerWorkerBase : public LoggerWorkerBase<T>{
    friend LoggerWorkerBase<T>;


private:
    static inline b8 s_GlobalInit = false;


private:
    static void GlobalUpdate(T& self){
        for(;;){
            self.m_semaphore.acquire();

            b8 updateSucceeded = self.internalUpdate();

            if(!updateSucceeded){
                self.m_exit.store(true, MemoryOrder::release);
                break;
            }

            if(self.m_exit.load(MemoryOrder::acquire) && self.workerCanExit())
                break;
        }
    }


public:
    explicit QueuedLoggerWorkerBase(const NameText<char> allocationLog)
        : LoggerWorkerBase<T>(allocationLog)
        , m_semaphore(0)
    {}


protected:
    [[nodiscard]] b8 workerCanExit()const noexcept{ return true; }

    void internalDestroy()noexcept{ m_semaphore.release(); }


protected:
    void enqueue(MessageType&& data){ LoggerWorkerBase<T>::m_messageQueue.emplace(Move(data)); m_semaphore.release(); }


protected:
    Semaphore<> m_semaphore;
};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_LOG_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

