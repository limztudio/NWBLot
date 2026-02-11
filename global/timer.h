// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#pragma once


#include <chrono>

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
namespace std{
    template<>
    struct formatter<TimerDelta>{
        constexpr auto parse(format_parse_context& ctx){ return ctx.begin(); }
        template<typename FormatContext>
        auto format(const TimerDelta& val, FormatContext& ctx)const{
            auto duration = chrono::duration<i64, ratio<1, 1000>>(static_cast<i64>(val));
            auto h = chrono::duration_cast<chrono::hours>(duration);
            auto m = chrono::duration_cast<chrono::minutes>(duration % chrono::hours(1));
            auto s = chrono::duration_cast<chrono::seconds>(duration % chrono::minutes(1));
            auto ms = chrono::duration_cast<chrono::milliseconds>(duration % chrono::seconds(1));

            return format_to(ctx.out(), "{:02}:{:02}:{:02}.{:03}", h.count(), m.count(), s.count(), ms.count());
        }
    };
    template<>
    struct formatter<TimerDelta, wchar>{
        constexpr auto parse(wformat_parse_context& ctx){ return ctx.begin(); }
        template<typename FormatContext>
        auto format(const TimerDelta& val, FormatContext& ctx)const{
            auto duration = chrono::duration<i64, ratio<1, 1000>>(static_cast<i64>(val));
            auto h = chrono::duration_cast<chrono::hours>(duration);
            auto m = chrono::duration_cast<chrono::minutes>(duration % chrono::hours(1));
            auto s = chrono::duration_cast<chrono::seconds>(duration % chrono::minutes(1));
            auto ms = chrono::duration_cast<chrono::milliseconds>(duration % chrono::seconds(1));

            return format_to(ctx.out(), L"{:02}:{:02}:{:02}.{:03}", h.count(), m.count(), s.count(), ms.count());
        }
    };
};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


inline Timer TimerNow()noexcept{ return std::chrono::steady_clock::now(); }


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace __hidden_timer{
    static inline Timer s_veryBegining = TimerNow();
};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


template<typename T>
inline T DurationInSeconds(const Timer& current, const Timer& late = __hidden_timer::s_veryBegining)noexcept{
    return std::chrono::duration_cast<std::chrono::duration<T, std::ratio<1, 1>>>(current - late).count();
}
template<typename T>
inline T DurationInMS(const Timer& current, const Timer& late = __hidden_timer::s_veryBegining)noexcept{
    return std::chrono::duration_cast<std::chrono::duration<T, std::ratio<1, 1000>>>(current - late).count();
}
template<typename T>
inline T DurationInNS(const Timer& current, const Timer& late = __hidden_timer::s_veryBegining)noexcept{
    return std::chrono::duration_cast<std::chrono::duration<T, std::ratio<1, 1000000000>>>(current - late).count();
}
inline TimerDelta DurationInTimeDelta(const Timer& current, const Timer& late = __hidden_timer::s_veryBegining)noexcept{
    return TimerDelta(DurationInMS<i64>(current, late));
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

