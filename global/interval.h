// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#pragma once


#include "simplemath.h"
#include "span.h"


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


// Empty target ranges are covered without inspecting the inputs.
template<typename ItemT, typename EligibleT, typename BeginT, typename EndT>
[[nodiscard]] inline b8 CoversHalfOpenRange(const u64 begin, const u64 end, const Span<const ItemT> intervals, EligibleT&& eligible, BeginT&& beginOf, EndT&& endOf)noexcept(
    noexcept(static_cast<b8>(eligible(DeclVal<const ItemT&>()) && beginOf(DeclVal<const ItemT&>()) <= DeclVal<u64&>() && endOf(DeclVal<const ItemT&>()) > DeclVal<u64&>()))
    && noexcept(DeclVal<u64&>() = endOf(DeclVal<const ItemT&>()))
){
    u64 coveredEnd = begin;
    while(coveredEnd < end){
        u64 nextEnd = coveredEnd;
        for(const ItemT& interval : intervals){
            if(eligible(interval) && beginOf(interval) <= coveredEnd && endOf(interval) > nextEnd)
                nextEnd = endOf(interval);
        }
        if(nextEnd == coveredEnd)
            return false;
        coveredEnd = nextEnd;
    }
    return true;
}

// Input intervals must be nonempty and sorted by begin.
template<typename ItemT, typename BeginT, typename EndT, typename VisitT>
inline void VisitOverlappingSortedIntervals(const Span<const ItemT> intervals, BeginT&& beginOf, EndT&& endOf, VisitT&& visit)noexcept(
    noexcept(static_cast<b8>(true && beginOf(DeclVal<const ItemT&>()) < DeclVal<u64&>()))
    && noexcept(static_cast<b8>(true && beginOf(DeclVal<const ItemT&>()) < endOf(DeclVal<const ItemT&>())))
    && noexcept(visit(DeclVal<const ItemT&>()))
    && noexcept(DeclVal<u64&>() = Max(DeclVal<u64&>(), endOf(DeclVal<const ItemT&>())))
){
    u64 maximumPreviousEnd = 0u;
    for(usize index = 0u; index < intervals.size(); ++index){
        const ItemT& interval = intervals[index];
        const b8 overlapsPrevious = index != 0u && beginOf(interval) < maximumPreviousEnd;
        const b8 overlapsNext = index + 1u < intervals.size() && beginOf(intervals[index + 1u]) < endOf(interval);
        if(overlapsPrevious || overlapsNext)
            visit(interval);
        maximumPreviousEnd = Max(maximumPreviousEnd, endOf(interval));
    }
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

