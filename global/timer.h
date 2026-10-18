// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#pragma once


#include "basic_string.h"
#include "expected.h"
#include "simd/vector_arithmetic.h"

#include <chrono>
#include <ctime>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


using Timer = std::chrono::steady_clock::time_point;
using LocalTime = std::tm;


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


// Millisecond resolution.
class TimerDelta{
public:
    TimerDelta()noexcept
        : m_value(0)
    {}
    TimerDelta(i64 v)noexcept
        : m_value(v)
    {}
    TimerDelta(const TimerDelta&)noexcept = default;
    TimerDelta(TimerDelta&&)noexcept = default;


public:
    TimerDelta& operator=(const TimerDelta&)noexcept = default;
    TimerDelta& operator=(TimerDelta&&)noexcept = default;


public:
    operator i64()const noexcept{ return m_value; }


private:
    i64 m_value;
};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace TimerDetail{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


struct TimerDeltaParts{
    i64 hours = 0;
    i64 minutes = 0;
    i64 seconds = 0;
    i64 milliseconds = 0;
};

using MillisecondDuration = std::chrono::duration<i64, std::milli>;

[[nodiscard]] inline TimerDeltaParts SplitTimerDelta(const TimerDelta& val)noexcept{
    auto duration = MillisecondDuration(static_cast<i64>(val));
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
    constexpr auto parse(format_parse_context& ctx)noexcept{ return ctx.begin(); }
    template<typename FormatContext>
    auto format(const TimerDelta& val, FormatContext& ctx)const{
        const auto parts = TimerDetail::SplitTimerDelta(val);
        return format_to(ctx.out(), "{:02}:{:02}:{:02}.{:03}", parts.hours, parts.minutes, parts.seconds, parts.milliseconds);
    }
};
template<>
struct formatter<TimerDelta, wchar>{
    constexpr auto parse(wformat_parse_context& ctx)noexcept{ return ctx.begin(); }
    template<typename FormatContext>
    auto format(const TimerDelta& val, FormatContext& ctx)const{
        const auto parts = TimerDetail::SplitTimerDelta(val);
        return format_to(ctx.out(), L"{:02}:{:02}:{:02}.{:03}", parts.hours, parts.minutes, parts.seconds, parts.milliseconds);
    }
};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


inline Timer TimerNow()noexcept{ return std::chrono::steady_clock::now(); }

[[nodiscard]] inline Timer TimerAddMS(const Timer& timer, i64 milliseconds)noexcept{
    return timer + std::chrono::milliseconds(milliseconds);
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace TimerDetail{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


inline Timer g_VeryBegining = TimerNow();


// Same steps as duration_cast to a float count: convert ticks, then scale them in SIMD lanes.
template<typename T, typename Period>
[[nodiscard]] inline T FloatDurationCount(const Timer::duration duration)noexcept{
    using Scale = std::ratio_divide<Timer::period, Period>;
    static_assert(IsSame_V<CommonType_T<T, Timer::rep>, T>);
    if constexpr(IsSame_V<T, f32>){
        SIMDVectorSingle value = VectorReplicate(static_cast<f32>(duration.count()));
        if constexpr(Scale::num != 1)
            value = VectorMultiply(value, VectorReplicate(static_cast<f32>(Scale::num)));
        if constexpr(Scale::den != 1)
            value = VectorDivide(value, VectorReplicate(static_cast<f32>(Scale::den)));
        return VectorGetX(value);
    }
    else{
        SIMDVectorDouble value = VectorDoubleReplicate(static_cast<f64>(duration.count()));
        if constexpr(Scale::num != 1)
            value = VectorDoubleMultiply(value, VectorDoubleReplicate(static_cast<f64>(Scale::num)));
        if constexpr(Scale::den != 1)
            value = VectorDoubleDivide(value, VectorDoubleReplicate(static_cast<f64>(Scale::den)));
        return VectorDoubleGetX(value);
    }
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


[[nodiscard]] inline Expected<LocalTime> GetLocalTime()noexcept{
    const auto now = std::time(nullptr);
    if(now == static_cast<std::time_t>(-1))
        return MakeUnexpected(Failure{});

    LocalTime local = {};
#if defined(NWB_PLATFORM_WINDOWS)
    if(localtime_s(&local, &now) != 0)
#else
    if(localtime_r(&now, &local) == nullptr)
#endif
        return MakeUnexpected(Failure{});
    return local;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


template<typename T>
inline T DurationInSeconds(const Timer& current, const Timer& late = TimerDetail::g_VeryBegining)noexcept(IsArithmetic_V<T>){
    if constexpr(IsSame_V<T, f32> || IsSame_V<T, f64>)
        return TimerDetail::FloatDurationCount<T, std::ratio<1, 1>>(current - late);
    else
        return std::chrono::duration_cast<std::chrono::duration<T, std::ratio<1, 1>>>(current - late).count();
}
template<typename T>
inline T ConsumeTimerDeltaSeconds(Timer& previous)noexcept(IsArithmetic_V<T>){
    const Timer current = TimerNow();
    const T delta = DurationInSeconds<T>(current, previous);
    previous = current;
    return delta;
}
template<typename T>
inline T DurationInMS(const Timer& current, const Timer& late = TimerDetail::g_VeryBegining)noexcept(IsArithmetic_V<T>){
    if constexpr(IsSame_V<T, f32> || IsSame_V<T, f64>)
        return TimerDetail::FloatDurationCount<T, std::milli>(current - late);
    else
        return std::chrono::duration_cast<std::chrono::duration<T, std::milli>>(current - late).count();
}
template<typename T>
inline T DurationInNS(const Timer& current, const Timer& late = TimerDetail::g_VeryBegining)noexcept(IsArithmetic_V<T>){
    if constexpr(IsSame_V<T, f32> || IsSame_V<T, f64>)
        return TimerDetail::FloatDurationCount<T, std::nano>(current - late);
    else
        return std::chrono::duration_cast<std::chrono::duration<T, std::nano>>(current - late).count();
}
inline TimerDelta DurationInTimeDelta(const Timer& current, const Timer& late = TimerDetail::g_VeryBegining)noexcept{
    if(current < late)
        return TimerDelta(0);

    return TimerDelta(DurationInMS<i64>(current, late));
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

