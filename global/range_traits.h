// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#pragma once


#include "type_properties.h"


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace RangeForDetail{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


template<typename IteratorT, typename SentinelT, typename VisitT>
[[nodiscard]] consteval bool IsNothrowIteration()noexcept{
    return
        IsNothrowDestructible_V<IteratorT>
        && IsNothrowDestructible_V<SentinelT>
        && noexcept(static_cast<bool>(DeclVal<IteratorT&>() != DeclVal<SentinelT&>()))
        && noexcept(++DeclVal<IteratorT&>())
        && noexcept(DeclVal<VisitT&>()(*DeclVal<IteratorT&>()))
    ;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


// Match range-for lookup and its auto iterator/sentinel storage, including copies from reference results.
template<typename RangeT, typename VisitT>
[[nodiscard]] consteval bool IsNothrowRangeFor()noexcept{
    if constexpr(IsBoundedArray_V<RangeT>){
        using IteratorT = RemoveExtent_T<RangeT>*;
        return RangeForDetail::IsNothrowIteration<IteratorT, IteratorT, VisitT>();
    }
    else if constexpr(requires(RangeT& range){ range.begin(); range.end(); }){
        using IteratorT = Decay_T<decltype(DeclVal<RangeT&>().begin())>;
        using SentinelT = Decay_T<decltype(DeclVal<RangeT&>().end())>;
        return
            noexcept(IteratorT(DeclVal<RangeT&>().begin()))
            && noexcept(SentinelT(DeclVal<RangeT&>().end()))
            && RangeForDetail::IsNothrowIteration<IteratorT, SentinelT, VisitT>()
        ;
    }
    else{
        using IteratorT = Decay_T<decltype(begin(DeclVal<RangeT&>()))>;
        using SentinelT = Decay_T<decltype(end(DeclVal<RangeT&>()))>;
        return
            noexcept(IteratorT(begin(DeclVal<RangeT&>())))
            && noexcept(SentinelT(end(DeclVal<RangeT&>())))
            && RangeForDetail::IsNothrowIteration<IteratorT, SentinelT, VisitT>()
        ;
    }
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

