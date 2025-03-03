// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#pragma once


#include "mimalloc.h"

#include <global/compile.h>
#include <global/platform.h>
#include <global/type.h>
#include <global/generic.h>
#include <global/simplemath.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#define NWB_ALLOC_BEGIN namespace NWB{ namespace Core{ namespace Alloc{
#define NWB_ALLOC_END }; }; };


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_ALLOC_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


template <usize size>
constexpr inline usize getSizeOf(usize count){
    constexpr auto overflowIsPossible = size > 1;

    if constexpr(overflowIsPossible){
        constexpr auto maxPossible = static_cast<usize>(-1) / size;
        if(count > maxPossible)
            throw std::bad_array_new_length{};
    }

    return count * size;
}

constexpr inline usize getAlignment(usize align, usize size){
    return (size + align - 1) & ~(align - 1);
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_ALLOC_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

