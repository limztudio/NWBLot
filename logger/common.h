// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#pragma once


#include "global.h"

#include <global/binary.h>

#include <fstream>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_LOG_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


using MessageType = Tuple<Timer, Type::Enum, LogString>;
using MessageQueue = ParallelQueue<MessageType, LogArena>;
using LogBytes = Vector<u8, LogArena>;

inline constexpr const char* s_TelemetryUploadEndpoint = "/telemetry";
inline constexpr const char* s_NameSymbolUploadEndpoint = "/namesym";
inline constexpr i32 s_LocalTimeYearBase = 1900;
inline constexpr i32 s_LocalTimeMonthBase = 1;

[[nodiscard]] inline MessageType MakeMessageType(LogArena& arena){
    return MakeTuple(Timer{}, Type::Info, LogString(arena));
}


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
    case Type::Assert:
        return NWB_TEXT("ASSERT");
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
    case Type::Assert:
    case Type::Error:
    case Type::Fatal:
        return true;
    default:
        return false;
    }
}

[[nodiscard]] inline bool IsValidMessageType(Type::Enum type){
    switch(type){
    case Type::Info:
    case Type::EssentialInfo:
    case Type::Warning:
    case Type::CriticalWarning:
    case Type::Assert:
    case Type::Error:
    case Type::Fatal:
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
    ::BinaryDetail::AppendBytesNoReserveUnchecked(outPayload, str.c_str(), str.size() * sizeof(tchar));

    constexpr tchar s_NullTerminator = 0;
    AppendPOD(outPayload, s_NullTerminator);

    NWB_ASSERT(outPayload.size() == payloadBytes);
    return outPayload.size() == payloadBytes;
}

[[nodiscard]] inline bool ParseMessagePayload(
    LogArena& arena,
    const void* contents,
    const usize totalSize,
    MessageType& outMessage,
    const tchar*& outError
){
    outMessage = MakeMessageType(arena);
    outError = nullptr;

    if(totalSize < sizeof(Timer) + sizeof(Type::Enum) + sizeof(tchar)){
        outError = NWB_TEXT("Received a truncated message");
        return false;
    }
    if(!contents){
        outError = NWB_TEXT("Received a malformed message payload");
        return false;
    }

    const BinaryByteView payload{ static_cast<const u8*>(contents), totalSize };
    usize cursor = 0u;

    Timer time{};
    Type::Enum type{};
    if(!ReadPOD(payload, cursor, time) || !ReadPOD(payload, cursor, type)){
        outError = NWB_TEXT("Received a truncated message");
        return false;
    }

    if(!IsValidMessageType(type)){
        outError = NWB_TEXT("Received a message with an invalid type");
        return false;
    }

    const usize textBytes = totalSize - cursor;
    if(textBytes < sizeof(tchar) || (textBytes % sizeof(tchar)) != 0u){
        outError = NWB_TEXT("Received a malformed message payload");
        return false;
    }

    const auto* msgText = reinterpret_cast<const tchar*>(payload.data() + cursor);
    const usize msgCharCount = textBytes / sizeof(tchar);
    if(msgText[msgCharCount - 1u] != 0){
        outError = NWB_TEXT("Received a non-null-terminated message");
        return false;
    }

    outMessage = MakeTuple(Move(time), type, LogString(msgText, msgCharCount - 1u, arena));
    return true;
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

        LocalTime localTime = {};
        if(!GetLocalTime(localTime))
            return false;

        Path executableDirectory(m_arena);
        if(!GetExecutableDirectory(executableDirectory))
            return false;

        const LogString fileName = StringFormat(
            m_arena,
            NWB_TEXT("{}_{:04}{:02}{:02}_{:02}{:02}{:02}.log"),
            fileNameBase,
            localTime.tm_year + s_LocalTimeYearBase,
            localTime.tm_mon + s_LocalTimeMonthBase,
            localTime.tm_mday,
            localTime.tm_hour,
            localTime.tm_min,
            localTime.tm_sec
        );
        m_filePath = executableDirectory / fileName;

        m_stream.open(m_filePath, s_FileOpenWrite | s_FileOpenAppend);
        return m_stream.is_open();
    }
    bool openByExecutableName(){
        Path executableName(m_arena);
        if(!GetExecutableName(executableName))
            return false;

        const LogString executableNameString = PathToString<tchar>(m_arena, executableName);
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
    LogArena& m_arena;
    StreamType m_stream;
    Path m_filePath;
};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


template<typename T, const tchar* NAME>
class Base{
protected:
    static inline bool globalInit(){ return true; }


public:
    explicit Base(const char* allocationLog)
        : m_arena(allocationLog)
        , m_msgQueue(m_arena)
        , m_exit(false)
    {}
    virtual ~Base(){
        stopWorker();
    }


protected:
    template<typename... ARGS>
    inline bool internalInit(ARGS&&... args){
        (static_cast<void>(args), ...);
        return true;
    }
    inline void internalDestroy(){}
    inline bool internalUpdate(){ return true; }

protected:
    inline bool tryDequeue(MessageType& msg){ return m_msgQueue.try_pop(msg); }
    void stopWorker(){
        const bool alreadyStopping = m_exit.exchange(true, MemoryOrder::acq_rel);

        if(!alreadyStopping)
            static_cast<T*>(this)->internalDestroy();
        if(m_thread.joinable())
            m_thread.join();
    }


public:
    inline LogArena& arena(){ return m_arena; }

public:
    template<typename... ARGS>
    inline bool init(ARGS&&... args){
        if(!static_cast<T*>(this)->s_GlobalInit){
            if(!static_cast<T*>(this)->globalInit()){
                static_cast<T*>(this)->T::enqueue(StringFormat(m_arena, NWB_TEXT("Failed to global initialization on {}"), NAME), Type::Fatal);
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
    inline void enqueue(LogString&& str, Type::Enum type = Type::Info){
        LogString message(Move(str), m_arena);
        return static_cast<T*>(this)->T::enqueue(MakeTuple(TimerNow(), type, Move(message)));
    }
    inline void enqueue(const LogString& str, Type::Enum type = Type::Info){
        LogString message(str, m_arena);
        return static_cast<T*>(this)->T::enqueue(MakeTuple(TimerNow(), type, Move(message)));
    }
    inline void enqueue(BasicStringView<tchar> str, Type::Enum type = Type::Info){
        LogString message(str, m_arena);
        return static_cast<T*>(this)->T::enqueue(MakeTuple(TimerNow(), type, Move(message)));
    }


protected:
    LogArena m_arena;
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
    static void globalUpdate(T* self){
        for(;;){
            auto curTime = TimerNow();
            if(DurationInSeconds<f32>(curTime, self->m_lastTime) < UPDATE_INTERVAL)
                continue;

            self->m_lastTime = curTime;

            if(self->internalUpdate() && self->m_exit.load(MemoryOrder::acquire))
                break;
        }
    }


public:
    explicit BaseUpdateOrdinary(const char* allocationLog)
        : Base<T, NAME>(allocationLog)
        , m_lastTime(TimerNow())
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
    static void globalUpdate(T* self){
        for(;;){
            self->m_semaphore.acquire();

            bool updateSucceeded = self->internalUpdate();

            if(!updateSucceeded){
                self->m_exit.store(true, MemoryOrder::release);
                break;
            }

            if(self->m_exit.load(MemoryOrder::acquire))
                break;
        }
    }


public:
    explicit BaseUpdateIfQueued(const char* allocationLog)
        : Base<T, NAME>(allocationLog)
        , m_semaphore(0)
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

