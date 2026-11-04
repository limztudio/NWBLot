// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#pragma once


#include "expected.h"
#include "type.h"


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


struct IntegerRectBounds{
    u64 xBegin = 0u;
    u64 xEnd = 0u;
    u64 yBegin = 0u;
    u64 yEnd = 0u;
};

[[nodiscard]] inline Expected<IntegerRectBounds> IntersectIntegerRectBounds(const IntegerRectBounds& lhs, const IntegerRectBounds& rhs)noexcept{
    const IntegerRectBounds result{
        .xBegin = lhs.xBegin > rhs.xBegin ? lhs.xBegin : rhs.xBegin,
        .xEnd = lhs.xEnd < rhs.xEnd ? lhs.xEnd : rhs.xEnd,
        .yBegin = lhs.yBegin > rhs.yBegin ? lhs.yBegin : rhs.yBegin,
        .yEnd = lhs.yEnd < rhs.yEnd ? lhs.yEnd : rhs.yEnd,
    };
    if(result.xBegin >= result.xEnd || result.yBegin >= result.yEnd)
        return MakeUnexpected(Failure{});
    return result;
}

template<typename VectorT>
inline void AppendIntegerRectRemainder(const IntegerRectBounds& outer, const IntegerRectBounds& cut, VectorT& outRanges){
    const auto intersection = IntersectIntegerRectBounds(outer, cut);
    if(!intersection){
        outRanges.push_back(outer);
        return;
    }

    if(outer.xBegin < intersection->xBegin){
        outRanges.push_back(IntegerRectBounds{ .xBegin = outer.xBegin, .xEnd = intersection->xBegin, .yBegin = outer.yBegin, .yEnd = outer.yEnd, });
    }
    if(intersection->xEnd < outer.xEnd){
        outRanges.push_back(IntegerRectBounds{ .xBegin = intersection->xEnd, .xEnd = outer.xEnd, .yBegin = outer.yBegin, .yEnd = outer.yEnd, });
    }
    if(outer.yBegin < intersection->yBegin){
        outRanges.push_back(IntegerRectBounds{ .xBegin = intersection->xBegin, .xEnd = intersection->xEnd, .yBegin = outer.yBegin, .yEnd = intersection->yBegin, });
    }
    if(intersection->yEnd < outer.yEnd){
        outRanges.push_back(IntegerRectBounds{ .xBegin = intersection->xBegin, .xEnd = intersection->xEnd, .yBegin = intersection->yEnd, .yEnd = outer.yEnd, });
    }
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

