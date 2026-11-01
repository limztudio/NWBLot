// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#pragma once


#include <core/alloc/general.h>
#include <core/common/log.h>

#include <global/global.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace Tests{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


inline constexpr Name s_CapturingLoggerArena("tests/common/capturing_logger");

class CapturingLogger final : public Core::Common::ILogger{
    using LogArena = Core::Common::LogArena;
    using LogString = Core::Common::LogString;


public:
    CapturingLogger()
        : m_arena(s_CapturingLoggerArena)
        , m_messages(m_arena)
        , m_errors(m_arena)
    {}


public:
    virtual LogArena& arena()noexcept override{ return m_arena; }

    virtual void enqueue(LogString&& str, Core::Common::LogType::Enum type = Core::Common::LogType::Info)override{
        record(str, type);
    }

    virtual void enqueue(const LogString& str, Core::Common::LogType::Enum type = Core::Common::LogType::Info)override{
        record(str, type);
    }

    [[nodiscard]] u32 messageCount()const noexcept;
    [[nodiscard]] u32 errorCount()const noexcept;
    [[nodiscard]] Core::Common::LogType::Enum lastType()const noexcept;
    [[nodiscard]] b8 sawMessageContaining(TStringView text)const noexcept;
    [[nodiscard]] b8 sawErrorContaining(TStringView text)const noexcept;


private:
    void record(TStringView str, Core::Common::LogType::Enum type);


private:
    mutable Futex m_mutex;
    u32 m_messageCount = 0;
    LogArena m_arena;
    Vector<LogString, LogArena> m_messages;
    Vector<LogString, LogArena> m_errors;
    u32 m_errorCount = 0;
    Core::Common::LogType::Enum m_lastType = Core::Common::LogType::Info;
};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

