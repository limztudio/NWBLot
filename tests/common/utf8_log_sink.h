// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#pragma once

#include <core/alloc/scratch.h>
#include <core/common/logger_registration.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_BEGIN
namespace Tests{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


class Utf8LogSink : public Core::Common::ILogger{
public:
    Core::Common::LogArena& arena()noexcept final{ return m_arena; }
    void enqueue(Core::Common::LogString&& text, const Core::Common::LogType::Enum type = Core::Common::LogType::Info)final{ recordText(text, type); }
    void enqueue(const Core::Common::LogString& text, const Core::Common::LogType::Enum type = Core::Common::LogType::Info)final{ recordText(text, type); }
    virtual void receive(AStringView text, Core::Common::LogType::Enum type) = 0;

private:
    void recordText(const TStringView text, const Core::Common::LogType::Enum type){
#if defined(FOUNDATION_UNICODE)
        Core::Alloc::ScratchArena scratch(m_arena.allocationLog());
        receive(WideToUtf8(scratch, text), type);
#else
        receive(text, type);
#endif
    }
    Core::Common::LogArena m_arena{Name("tests/common/capturing_logger")};
};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};
NWB_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

