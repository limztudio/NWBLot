// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#pragma once


#include "limit.h"
#include "type.h"

#include <new>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


using AllocationException = std::bad_alloc;
using AllocationSizeException = std::bad_array_new_length;
using AlignValT = std::align_val_t;


template<usize size>
constexpr inline usize SizeOf(usize count)noexcept(size <= 1u){
    constexpr auto s_OverflowIsPossible = size > 1;

    if constexpr(s_OverflowIsPossible){
        constexpr auto s_MaxPossible = static_cast<usize>(-1) / size;
        if(count > s_MaxPossible)
            throw AllocationSizeException{};
    }

    return count * size;
}

constexpr inline usize AddSize(usize lhs, usize rhs){
    if(lhs > static_cast<usize>(-1) - rhs)
        throw AllocationSizeException{};

    return lhs + rhs;
}

constexpr inline usize Alignment(usize align, usize size){
    if(align <= 1)
        return size;

    const usize remainder = size % align;
    if(remainder == 0)
        return size;

    return AddSize(size, align - remainder);
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

