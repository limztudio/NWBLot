// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#pragma once


#include <chrono>
#include <ctime>

#include "basic_string.h"


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


using Timer = std::chrono::steady_clock::time_point;


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


// assume that the resolution is miliseconds
class TimerDelta{
public:
    TimerDelta()noexcept : value(0){}
    TimerDelta(i64 v)noexcept : value(v){}
    TimerDelta(const TimerDelta&)noexcept = default;
    TimerDelta(TimerDelta&&)noexcept = default;


public:
    TimerDelta& operator=(const TimerDelta&)noexcept = default;
    TimerDelta& operator=(TimerDelta&&)noexcept = default;

public:
    operator i64()const noexcept{ return value; }


private:
    i64 value;
};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace __hidden_timer{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


struct TimerDeltaParts{
    i64 hours = 0;
    i64 minutes = 0;
    i64 seconds = 0;
    i64 milliseconds = 0;
};

[[nodiscard]] inline TimerDeltaParts SplitTimerDelta(const TimerDelta& val){
    auto duration = std::chrono::duration<i64, std::ratio<1, 1000>>(static_cast<i64>(val));
    auto h = std::chrono::duration_cast<std::chrono::hours>(duration);
    auto m = std::chrono::duration_cast<std::chrono::minutes>(duration % std::chrono::hours(1));
    auto s = std::chrono::duration_cast<std::chrono::seconds>(duration % std::chrono::minutes(1));
    auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(duration % std::chrono::seconds(1));

    TimerDeltaParts parts;
    parts.hours = h.count();
    parts.minutes = m.count();
    parts.seconds = s.count();
    parts.milliseconds = ms.count();
    return parts;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace std{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


template<>
struct formatter<TimerDelta>{
    constexpr auto parse(format_parse_context& ctx){ return ctx.begin(); }
    template<typename FormatContext>
    auto format(const TimerDelta& val, FormatContext& ctx)const{
        const auto parts = __hidden_timer::SplitTimerDelta(val);
        return format_to(ctx.out(), "{:02}:{:02}:{:02}.{:03}", parts.hours, parts.minutes, parts.seconds, parts.milliseconds);
    }
};
template<>
struct formatter<TimerDelta, wchar>{
    constexpr auto parse(wformat_parse_context& ctx){ return ctx.begin(); }
    template<typename FormatContext>
    auto format(const TimerDelta& val, FormatContext& ctx)const{
        const auto parts = __hidden_timer::SplitTimerDelta(val);
        return format_to(ctx.out(), L"{:02}:{:02}:{:02}.{:03}", parts.hours, parts.minutes, parts.seconds, parts.milliseconds);
    }
};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


inline Timer TimerNow()noexcept{ return std::chrono::steady_clock::now(); }


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace __hidden_timer{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


inline Timer s_VeryBegining = TimerNow();


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


[[nodiscard]] inline bool GetLocalTime(std::tm& outTime){
    const auto now = std::time(nullptr);
    if(now == static_cast<std::time_t>(-1))
        return false;

#if defined(NWB_PLATFORM_WINDOWS)
    return localtime_s(&outTime, &now) == 0;
#else
    return localtime_r(&now, &outTime) != nullptr;
#endif
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


template<typename T>
inline T DurationInSeconds(const Timer& current, const Timer& late = __hidden_timer::s_VeryBegining)noexcept{
    return std::chrono::duration_cast<std::chrono::duration<T, std::ratio<1, 1>>>(current - late).count();
}
template<typename T>
inline T ConsumeTimerDeltaSeconds(Timer& previous)noexcept{
    const Timer current = TimerNow();
    const T delta = DurationInSeconds<T>(current, previous);
    previous = current;
    return delta;
}
template<typename T>
inline T DurationInMS(const Timer& current, const Timer& late = __hidden_timer::s_VeryBegining)noexcept{
    return std::chrono::duration_cast<std::chrono::duration<T, std::ratio<1, 1000>>>(current - late).count();
}
template<typename T>
inline T DurationInNS(const Timer& current, const Timer& late = __hidden_timer::s_VeryBegining)noexcept{
    return std::chrono::duration_cast<std::chrono::duration<T, std::ratio<1, 1000000000>>>(current - late).count();
}
inline TimerDelta DurationInTimeDelta(const Timer& current, const Timer& late = __hidden_timer::s_VeryBegining)noexcept{
    if(current < late)
        return TimerDelta(0);

    return TimerDelta(DurationInMS<i64>(current, late));
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
