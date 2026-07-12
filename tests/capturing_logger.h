
#pragma once


#include <global/global.h>
#include <global/core/alloc/general.h>
#include <global/core/common/log.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace Tests{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


inline constexpr Name s_CapturingLoggerArena("tests/capturing_logger");

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
    virtual LogArena& arena()override{ return m_arena; }
    virtual void enqueue(LogString&& str, Core::Common::LogType::Enum type = Core::Common::LogType::Info)override{
        record(str, type);
    }
    virtual void enqueue(const LogString& str, Core::Common::LogType::Enum type = Core::Common::LogType::Info)override{
        record(str, type);
    }

    [[nodiscard]] u32 messageCount()const{ return m_messageCount; }
    [[nodiscard]] u32 errorCount()const{ return m_errorCount; }
    [[nodiscard]] Core::Common::LogType::Enum lastType()const{ return m_lastType; }
    [[nodiscard]] bool sawMessageContaining(const TStringView text)const{
        for(const LogString& message : m_messages){
            if(message.find(text) != LogString::npos)
                return true;
        }
        return false;
    }
    [[nodiscard]] bool sawErrorContaining(const TStringView text)const{
        for(const LogString& error : m_errors){
            if(error.find(text) != LogString::npos)
                return true;
        }
        return false;
    }

private:
    void record(const TStringView str, const Core::Common::LogType::Enum type){
        ++m_messageCount;
        m_lastType = type;
        m_messages.emplace_back(str, m_arena);

        if(type == Core::Common::LogType::Error){
            ++m_errorCount;
            m_errors.emplace_back(str, m_arena);
        }
    }

private:
    LogArena m_arena;
    u32 m_messageCount = 0;
    u32 m_errorCount = 0;
    Core::Common::LogType::Enum m_lastType = Core::Common::LogType::Info;
    Vector<LogString, LogArena> m_messages;
    Vector<LogString, LogArena> m_errors;
};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

