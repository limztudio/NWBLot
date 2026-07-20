#pragma once


#include <core/global.h>

#include <global/compile.h>
#include <global/platform.h>
#include <global/type.h>
#include <global/allocation_size.h>
#include <global/call_traits.h>
#include <global/unique_ptr.h>
#include <global/containers.h>
#include <global/generic.h>
#include <global/not_null.h>
#include <global/simplemath.h>
#include <global/atomic.h>
#include <global/sync.h>
#include <global/thread.h>
#include <global/inplace_function.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#define NWB_ALLOC_BEGIN NWB_CORE_BEGIN namespace Alloc{
#define NWB_ALLOC_END }; NWB_CORE_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_ALLOC_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace CoreAffinity{
    enum Enum : u8{
        Any,
        Performance,
        Efficiency,
    };
};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


extern usize CachelineSize();


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace AllocDetail{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


template<typename T, typename Arena>
[[nodiscard]] inline T* AllocateTyped(Arena& arena, const usize count){
    static_assert(sizeof(T) > 0, "value_type must be complete before calling allocate.");
    const usize bytes = SizeOf<sizeof(T)>(count);

    T* output = nullptr;
    if(bytes){
        if(IsConstantEvaluated())
            output = reinterpret_cast<T*>(arena.allocate(1, bytes));
        else{
            constexpr usize alignSize = alignof(T);
            output = reinterpret_cast<T*>(arena.allocate(alignSize, bytes));
        }
    }
    return output;
}

template<typename T, typename Arena>
inline void DeallocateTyped(Arena& arena, void* p, const usize count){
    static_assert(sizeof(T) > 0, "value_type must be complete before calling allocate.");
    const usize bytes = SizeOf<sizeof(T)>(count);

    if(bytes){
        if(IsConstantEvaluated())
            arena.deallocate(p, 1, bytes);
        else{
            constexpr usize alignSize = alignof(T);
            arena.deallocate(p, alignSize, bytes);
        }
    }
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_ALLOC_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

