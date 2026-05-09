// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#pragma once


#include <global/global.h>
#include <core/alloc/general.h>
#include <core/common/log.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace Tests{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


class CapturingLogger final : public Core::Common::ILogger{
public:
    virtual void enqueue(TString&& str, Core::Common::LogType::Enum type = Core::Common::LogType::Info)override{
        record(str, type);
    }
    virtual void enqueue(const TString& str, Core::Common::LogType::Enum type = Core::Common::LogType::Info)override{
        record(str, type);
    }

    [[nodiscard]] u32 messageCount()const{ return m_messageCount; }
    [[nodiscard]] u32 errorCount()const{ return m_errorCount; }
    [[nodiscard]] Core::Common::LogType::Enum lastType()const{ return m_lastType; }
    [[nodiscard]] bool sawMessageContaining(const TStringView text)const{
        for(const TString& message : m_messages){
            if(message.find(text) != TString::npos)
                return true;
        }
        return false;
    }
    [[nodiscard]] bool sawErrorContaining(const TStringView text)const{
        for(const TString& error : m_errors){
            if(error.find(text) != TString::npos)
                return true;
        }
        return false;
    }

private:
    void record(const TString& str, const Core::Common::LogType::Enum type){
        ++m_messageCount;
        m_lastType = type;
        m_messages.push_back(str);

        if(type == Core::Common::LogType::Error){
            ++m_errorCount;
            m_errors.push_back(str);
        }
    }

private:
    u32 m_messageCount = 0;
    u32 m_errorCount = 0;
    Core::Common::LogType::Enum m_lastType = Core::Common::LogType::Info;
    Vector<TString> m_messages;
    Vector<TString> m_errors;
};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

