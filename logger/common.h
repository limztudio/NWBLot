// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#pragma once


#include "global.h"

#include <global/binary.h>
#include <global/thread.h>

#include <fstream>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_LOG_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


using MessageType = Tuple<Timer, Core::Common::LogType::Enum, LogString>;
using MessageQueue = ParallelQueue<MessageType, LogArena>;
using LogBytes = Vector<u8, LogArena>;

inline constexpr StringView s_TelemetryUploadEndpoint = "/telemetry";
inline constexpr i32 s_LocalTimeYearBase = 1900;
inline constexpr i32 s_LocalTimeMonthBase = 1;

[[nodiscard]] inline MessageType MakeMessageType(LogArena& arena){
    return MakeTuple(Timer{}, Core::Common::LogType::Info, LogString(arena));
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


inline constexpr TStringView s_UnknownLogLevelName = NWB_TEXT("UNKNOWN");
[[nodiscard]] inline TStringView MessageTypeToString(Core::Common::LogType::Enum type)noexcept{
    switch(type){
    case Core::Common::LogType::Info:
        return NWB_TEXT("INFO");
    case Core::Common::LogType::EssentialInfo:
        return NWB_TEXT("ESSENTIAL INFO");
    case Core::Common::LogType::Warning:
        return NWB_TEXT("WARNING");
    case Core::Common::LogType::CriticalWarning:
        return NWB_TEXT("CRITICAL WARNING");
    case Core::Common::LogType::Assert:
        return NWB_TEXT("ASSERT");
    case Core::Common::LogType::Error:
        return NWB_TEXT("ERROR");
    case Core::Common::LogType::Fatal:
        return NWB_TEXT("FATAL");
    }
    return s_UnknownLogLevelName;
}

[[nodiscard]] inline bool MessageTypeWritesToErrorStream(Core::Common::LogType::Enum type)noexcept{
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

[[nodiscard]] inline bool IsValidMessageType(Core::Common::LogType::Enum type)noexcept{
    switch(type){
    case Core::Common::LogType::Info:
    case Core::Common::LogType::EssentialInfo:
    case Core::Common::LogType::Warning:
    case Core::Common::LogType::CriticalWarning:
    case Core::Common::LogType::Assert:
    case Core::Common::LogType::Error:
    case Core::Common::LogType::Fatal:
        return true;
    }
    return false;
}

[[nodiscard]] inline LogString FormatMessageForProcessing(LogArena& arena, const MessageType& msg){
    const auto& [time, type, str] = msg;
    return StringFormat(arena, NWB_TEXT("{} [{}]:\n{}"), DurationInTimeDelta(time), MessageTypeToString(type), str);
}

template<typename PayloadContainer>
[[nodiscard]] inline bool BuildMessagePayload(const MessageType& msg, PayloadContainer& outPayload){
    const auto& [time, type, str] = msg;
    outPayload.clear();

    if(!IsValidMessageType(type))
        return false;

    usize payloadBytes = 0u;
    if(
        !AddBinaryReserveBytes(payloadBytes, sizeof(time))
        || !AddBinaryReserveBytes(payloadBytes, sizeof(type))
        || !AddBinaryRepeatedReserveBytes(payloadBytes, str.size(), sizeof(tchar))
        || !AddBinaryReserveBytes(payloadBytes, sizeof(tchar))
    )
        return false;

    if constexpr(requires(PayloadContainer& p, usize bytes){ p.reserve(bytes); })
        outPayload.reserve(payloadBytes);

    AppendPOD(outPayload, time);
    AppendPOD(outPayload, type);
    ::BinaryDetail::AppendBytesNoReserveUnchecked(outPayload, str.data(), str.size() * sizeof(tchar));

    constexpr tchar s_NullTerminator = 0;
    AppendPOD(outPayload, s_NullTerminator);

    NWB_ASSERT(outPayload.size() == payloadBytes);
    return outPayload.size() == payloadBytes;
}

[[nodiscard]] inline Expected<MessageType, TStringView> ParseMessagePayload(
    LogArena& arena,
    const void* contents,
    const usize totalSize
){
    if(totalSize < sizeof(Timer) + sizeof(Core::Common::LogType::Enum) + sizeof(tchar)){
        return MakeUnexpected(TStringView(NWB_TEXT("Received a truncated message")));
    }
    if(!contents){
        return MakeUnexpected(TStringView(NWB_TEXT("Received a malformed message payload")));
    }

    const BinaryByteView payload{ static_cast<const u8*>(contents), totalSize };
    usize cursor = 0u;

    const auto time = ReadPOD<Timer>(payload, cursor);
    const auto type = ReadPOD<Core::Common::LogType::Enum>(payload, cursor);
    if(!time || !type){
        return MakeUnexpected(TStringView(NWB_TEXT("Received a truncated message")));
    }

    if(!IsValidMessageType(*type)){
        return MakeUnexpected(TStringView(NWB_TEXT("Received a message with an invalid type")));
    }

    const usize textBytes = totalSize - cursor;
    if(textBytes < sizeof(tchar) || (textBytes % sizeof(tchar)) != 0u){
        return MakeUnexpected(TStringView(NWB_TEXT("Received a malformed message payload")));
    }

    usize terminatorCursor = totalSize - sizeof(tchar);
    const auto terminator = ReadPOD<tchar>(payload, terminatorCursor);
    if(!terminator || *terminator != 0){
        return MakeUnexpected(TStringView(NWB_TEXT("Received a non-null-terminated message")));
    }

    // Serialized text can start at an unaligned byte offset; copy into aligned character storage before reading it.
    LogString message(arena);
    const usize messageBytes = textBytes - sizeof(tchar);
    message.resize(messageBytes / sizeof(tchar));
    if(messageBytes != 0u)
        NWB_MEMCPY(message.data(), messageBytes, payload.data() + cursor, messageBytes);
    return MakeTuple(*time, *type, Move(message));
}


class ProcessedMessageFile{
public:
    using StreamType = BasicOutputFileStream<tchar>;


public:
    explicit ProcessedMessageFile(LogArena& arena)
        : m_arena(arena)
        , m_filePath(arena)
    {}
    ~ProcessedMessageFile(){ close(); }


public:
    bool open(BasicStringView<tchar> fileNameBase){
        close();
        if(fileNameBase.empty())
            return false;

        const auto localTime = GetLocalTime();
        if(!localTime)
            return false;

        const auto executableDirectory = GetExecutableDirectory(m_arena);
        if(!executableDirectory)
            return false;

        const LogString fileName = StringFormat(
            m_arena,
            NWB_TEXT("{}_{:04}{:02}{:02}_{:02}{:02}{:02}.log"),
            fileNameBase,
            localTime->tm_year + s_LocalTimeYearBase,
            localTime->tm_mon + s_LocalTimeMonthBase,
            localTime->tm_mday,
            localTime->tm_hour,
            localTime->tm_min,
            localTime->tm_sec
        );
        m_filePath = *executableDirectory / fileName;

        m_stream.open(m_filePath, s_FileOpenWrite | s_FileOpenAppend);
        return m_stream.is_open();
    }
    bool openByExecutableName(){
        const auto executableName = GetExecutableName(m_arena);
        if(!executableName)
            return false;

        const LogString executableNameString = PathToString<tchar>(m_arena, *executableName);
        return open(executableNameString);
    }

    void close(){
        if(m_stream.is_open())
            m_stream.close();
    }

    [[nodiscard]] bool writeLine(BasicStringView<tchar> line){
        if(!m_stream.is_open())
            return false;

        m_stream << line << static_cast<tchar>('\n');
        m_stream.flush();
        return m_stream.good();
    }


private:
    LogArena& m_arena;
    StreamType m_stream;
    Path m_filePath;
};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


template<typename T, const TStringView& loggerName>
class LoggerWorkerBase{
public:
    explicit LoggerWorkerBase(const AStringView allocationLog)
        : m_arena(Name(allocationLog))
        , m_messageQueue(m_arena)
        , m_exit(false)
    {}
    virtual ~LoggerWorkerBase(){
        stopWorker();
    }


protected:
    inline bool tryDequeue(MessageType& msg){ return m_messageQueue.try_pop(msg); }
    void stopWorker(){
        const bool alreadyStopping = m_exit.exchange(true, MemoryOrder::acq_rel);

        if(!alreadyStopping)
            static_cast<T*>(this)->internalDestroy();
        if(m_thread.joinable())
            m_thread.join();
    }


public:
    inline LogArena& arena()noexcept{ return m_arena; }

public:
    template<typename... Args>
    inline bool init(Args&&... args){
        if constexpr(requires{ T::GlobalInit(); }){
            if(!T::s_GlobalInit){
                if(!T::GlobalInit()){
                    static_cast<T*>(this)->T::enqueue(StringFormat(m_arena, NWB_TEXT("Failed to initialize {} globally"), loggerName), Core::Common::LogType::Fatal);
                    return false;
                }
                T::s_GlobalInit = true;
            }
        }

        const bool initialized = static_cast<T*>(this)->internalInit(Forward<Args>(args)...);
        if(!initialized)
            return false;

        m_thread = Thread(T::GlobalUpdate, static_cast<T*>(this));

        return true;
    }

public:
    inline void enqueue(LogString&& str, Core::Common::LogType::Enum type = Core::Common::LogType::Info){
        LogString message(Move(str), m_arena);
        return static_cast<T*>(this)->T::enqueue(MakeTuple(TimerNow(), type, Move(message)));
    }
    inline void enqueue(const LogString& str, Core::Common::LogType::Enum type = Core::Common::LogType::Info){
        LogString message(str, m_arena);
        return static_cast<T*>(this)->T::enqueue(MakeTuple(TimerNow(), type, Move(message)));
    }
    inline void enqueue(BasicStringView<tchar> str, Core::Common::LogType::Enum type = Core::Common::LogType::Info){
        LogString message(str, m_arena);
        return static_cast<T*>(this)->T::enqueue(MakeTuple(TimerNow(), type, Move(message)));
    }


protected:
    LogArena m_arena;
    MessageQueue m_messageQueue;

protected:
    Thread m_thread;
    Atomic<bool> m_exit;
};

template<typename T, f32 updateIntervalSeconds, const TStringView& loggerName>
class IntervalLoggerWorkerBase : public LoggerWorkerBase<T, loggerName>{
    friend LoggerWorkerBase<T, loggerName>;


private:
    static bool s_GlobalInit;


private:
    static void GlobalUpdate(T* self){
        for(;;){
            const Timer currentTime = TimerNow();
            const f32 elapsedSeconds = DurationInSeconds<f32>(currentTime, self->m_lastUpdateTime);
            if(elapsedSeconds < updateIntervalSeconds){
                // Sleep until the next update deadline; the cadence bounds message and shutdown latency.
                const f32 remainingSeconds = updateIntervalSeconds - elapsedSeconds;
                static constexpr f32 s_MillisecondsPerSecondF = 1000.0f;
                static constexpr u32 s_MinSleepMilliseconds = 1u;
                const u32 sleepMilliseconds = Max<u32>(s_MinSleepMilliseconds, static_cast<u32>(Ceil(remainingSeconds * s_MillisecondsPerSecondF)));
                SleepMS(sleepMilliseconds);
                continue;
            }

            self->m_lastUpdateTime = currentTime;

            if(self->internalUpdate() && self->m_exit.load(MemoryOrder::acquire))
                break;
        }
    }


public:
    explicit IntervalLoggerWorkerBase(const AStringView allocationLog)
        : LoggerWorkerBase<T, loggerName>(allocationLog)
        , m_lastUpdateTime(TimerNow())
    {}


protected:
    inline void enqueue(MessageType&& data){ return LoggerWorkerBase<T, loggerName>::m_messageQueue.emplace(Move(data)); }
    inline void enqueue(const MessageType& data){ return LoggerWorkerBase<T, loggerName>::m_messageQueue.emplace(data); }


private:
    Timer m_lastUpdateTime;
};
template<typename T, f32 updateIntervalSeconds, const TStringView& loggerName>
bool IntervalLoggerWorkerBase<T, updateIntervalSeconds, loggerName>::s_GlobalInit = false;

template<typename T, const TStringView& loggerName>
class QueuedLoggerWorkerBase : public LoggerWorkerBase<T, loggerName>{
    friend LoggerWorkerBase<T, loggerName>;


private:
    static bool s_GlobalInit;


private:
    static void GlobalUpdate(T* self){
        for(;;){
            self->m_semaphore.acquire();

            bool updateSucceeded = self->internalUpdate();

            if(!updateSucceeded){
                self->m_exit.store(true, MemoryOrder::release);
                break;
            }

            if(self->m_exit.load(MemoryOrder::acquire) && self->workerCanExit())
                break;
        }
    }


public:
    explicit QueuedLoggerWorkerBase(const AStringView allocationLog)
        : LoggerWorkerBase<T, loggerName>(allocationLog)
        , m_semaphore(0)
    {}


protected:
    [[nodiscard]] inline bool workerCanExit()const noexcept{ return true; }
    void internalDestroy(){ m_semaphore.release(); }

protected:
    inline void enqueue(MessageType&& data){ LoggerWorkerBase<T, loggerName>::m_messageQueue.emplace(Move(data)); m_semaphore.release(); }
    inline void enqueue(const MessageType& data){ LoggerWorkerBase<T, loggerName>::m_messageQueue.emplace(data); m_semaphore.release(); }


protected:
    Semaphore<> m_semaphore;
};
template<typename T, const TStringView& loggerName>
bool QueuedLoggerWorkerBase<T, loggerName>::s_GlobalInit = false;


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_LOG_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

