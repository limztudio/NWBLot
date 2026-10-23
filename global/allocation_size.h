// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#pragma once


#include "algorithm.h"
#include "limit.h"
#include "type.h"

#include <new>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


using AllocationException = std::bad_alloc;
using AllocationSizeException = std::bad_array_new_length;
using AlignValT = std::align_val_t;


template<usize Size>
constexpr inline usize SizeOf(usize count)noexcept(Size <= 1u){
    constexpr auto s_OverflowIsPossible = Size > 1;

    if constexpr(s_OverflowIsPossible){
        constexpr auto s_MaxPossible = static_cast<usize>(-1) / Size;
        if(count > s_MaxPossible)
            throw AllocationSizeException{};
    }

    return count * Size;
}

constexpr inline usize AddSize(usize lhs, usize rhs){
    if(lhs > static_cast<usize>(-1) - rhs)
        throw AllocationSizeException{};

    return lhs + rhs;
}

constexpr inline usize Alignment(usize align, usize size){
    const auto aligned = AlignUpChecked(size, align);
    if(!aligned)
        throw AllocationSizeException{};
    return *aligned;
}

constexpr inline usize AlignmentPowerOfTwo(usize align, usize size){
    const auto aligned = AlignUpPowerOfTwoChecked(size, align);
    if(!aligned)
        throw AllocationSizeException{};
    return *aligned;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

