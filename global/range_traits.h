// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#pragma once


#include "type_properties.h"


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace RangeForDetail{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


template<typename IteratorT, typename SentinelT, typename VisitT>
[[nodiscard]] consteval b8 IsNothrowIteration()noexcept{
    return IsNothrowDestructible_V<IteratorT> && IsNothrowDestructible_V<SentinelT> && noexcept(static_cast<b8>(DeclVal<IteratorT&>() != DeclVal<SentinelT&>())) && noexcept(++DeclVal<IteratorT&>()) && noexcept(DeclVal<VisitT&>()(*DeclVal<IteratorT&>()));
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


// Match range-for lookup and its auto iterator/sentinel storage, including copies from reference results.
template<typename RangeT, typename VisitT>
[[nodiscard]] consteval b8 IsNothrowRangeFor()noexcept{
    if constexpr(IsBoundedArray_V<RangeT>){
        using IteratorT = RemoveExtent_T<RangeT>*;
        return RangeForDetail::IsNothrowIteration<IteratorT, IteratorT, VisitT>();
    }
    else if constexpr(requires(RangeT& range){ range.begin(); range.end(); }){
        using IteratorT = Decay_T<decltype(DeclVal<RangeT&>().begin())>;
        using SentinelT = Decay_T<decltype(DeclVal<RangeT&>().end())>;
        return noexcept(IteratorT(DeclVal<RangeT&>().begin())) && noexcept(SentinelT(DeclVal<RangeT&>().end())) && RangeForDetail::IsNothrowIteration<IteratorT, SentinelT, VisitT>();
    }
    else{
        using IteratorT = Decay_T<decltype(begin(DeclVal<RangeT&>()))>;
        using SentinelT = Decay_T<decltype(end(DeclVal<RangeT&>()))>;
        return noexcept(IteratorT(begin(DeclVal<RangeT&>()))) && noexcept(SentinelT(end(DeclVal<RangeT&>()))) && RangeForDetail::IsNothrowIteration<IteratorT, SentinelT, VisitT>();
    }
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


template<typename RangeT>
struct RangeTraits{
    using Iterator = RemoveCVRef_T<decltype(DeclVal<RangeT&>().begin())>;
    using Sentinel = RemoveCVRef_T<decltype(DeclVal<RangeT&>().end())>;
    using Returned = decltype(*DeclVal<Iterator&>());
    using Value = Decay_T<Returned>;
    using Item = const RemoveReference_T<Returned>&;


public:
    [[nodiscard]] static consteval b8 IsNothrowTraversal(const b8 copyElement)noexcept{
        return
            noexcept(DeclVal<void(*)(Iterator)noexcept>()(DeclVal<RangeT&>().begin()))
            && noexcept(DeclVal<void(*)(Sentinel)noexcept>()(DeclVal<RangeT&>().end()))
            && IsNothrowDestructible_V<Iterator>
            && IsNothrowDestructible_V<Sentinel>
            && noexcept(static_cast<b8>(DeclVal<Iterator&>() != DeclVal<Sentinel&>()))
            && noexcept(++DeclVal<Iterator&>())
            && noexcept(*DeclVal<Iterator&>())
            && (copyElement
                ? ((!IsReference_V<Returned> || IsNothrowConvertible_V<Returned, Value>) && IsNothrowDestructible_V<Value>)
                : noexcept(DeclVal<void(*)(Item)noexcept>()(*DeclVal<Iterator&>())))
        ;
    }
};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

