// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#pragma once


#include <core/global.h>

#include <global/expected.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace UiWidgetTests{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


[[nodiscard]] inline constexpr u64 ArithmeticFixtureRowKey(const u64 count, const u64 removed, const b8 reverse, const u64 index)noexcept{
    const u64 forward = reverse ? count - 1u - index : index;
    const u64 value = forward + 1u;
    return value + (removed != 0u && value >= removed ? 1u : 0u);
}

[[nodiscard]] inline Expected<u64> FindArithmeticFixtureRowIndex(const u64 count, const u64 removed, const b8 reverse, const u64 value)noexcept{
    if(value == 0u || value == removed || value > count + (removed != 0u ? 1u : 0u))
        return MakeUnexpected(Failure{});
    const u64 forward = value - 1u - (removed != 0u && value > removed ? 1u : 0u);
    const u64 index = reverse ? count - 1u - forward : forward;
    return index < count ? Expected<u64>{ index } : MakeUnexpected(Failure{});
}

template<typename KeyLookup>
[[nodiscard]] inline Expected<u64> FindSingleDisabledFixtureRow(const u64 count, const u64 start, const b8 backwards, const u64 disabled, KeyLookup&& rawKey)noexcept(noexcept(static_cast<b8>(rawKey(DeclVal<u64&>()) != disabled))){
    u64 index = 0u;
    if(start >= count)
        return MakeUnexpected(Failure{});
    index = start;
    if(rawKey(index) != disabled)
        return index;
    if(backwards){
        if(index == 0u)
            return MakeUnexpected(Failure{});
        --index;
    }
    else{
        if(index + 1u == count)
            return MakeUnexpected(Failure{});
        ++index;
    }
    return index;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

