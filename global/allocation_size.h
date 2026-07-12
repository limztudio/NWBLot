
#pragma once


#include "limit.h"
#include "type.h"

#include <new>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


template<usize size>
constexpr inline usize SizeOf(usize count){
    constexpr auto overflowIsPossible = size > 1;

    if constexpr(overflowIsPossible){
        constexpr auto maxPossible = static_cast<usize>(-1) / size;
        if(count > maxPossible)
            throw std::bad_array_new_length{};
    }

    return count * size;
}

constexpr inline usize AddSize(usize lhs, usize rhs){
    if(lhs > static_cast<usize>(-1) - rhs)
        throw std::bad_array_new_length{};

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

