// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#pragma once


#include <global/compile.h>
#include <global/platform.h>
#include <global/type.h>
#include <global/call_traits.h>
#include <global/unique_ptr.h>
#include <global/containers.h>
#include <global/generic.h>
#include <global/not_null.h>
#include <global/simplemath.h>
#include <global/atomic.h>
#include <global/sync.h>
#include <global/thread.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#define NWB_ALLOC_BEGIN namespace NWB{ namespace Core{ namespace Alloc{
#define NWB_ALLOC_END }; }; };


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_ALLOC_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


enum class CoreAffinity : u8{
    Any,
    Performance,
    Efficiency,
};


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

    const usize padding = align - 1;
    return AddSize(size, padding) & ~padding;
}


extern usize CachelineSize();


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace __hidden_alloc{


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

template<typename T, typename Arena>
[[nodiscard]] constexpr T* AllocateCacheAligned(Arena& arena, const usize count){
    const usize bytes = SizeOf<sizeof(T)>(count);
    if(!bytes)
        return nullptr;

    const usize alignSize = Max(CachelineSize(), static_cast<usize>(alignof(T)));
    return reinterpret_cast<T*>(arena.allocate(alignSize, bytes));
}

template<typename T>
[[nodiscard]] inline usize MaxCacheAlignedAllocationCount(){
    return (~usize(0) - CachelineSize()) / sizeof(T);
}

template<typename T, typename Arena>
constexpr void DeallocateCacheAligned(Arena& arena, T* const buffer, const usize count)noexcept{
    NWB_ASSERT_MSG((buffer != nullptr || count == 0), NWB_TEXT("null pointer cannot point to a block of non-zero size"));

    const usize bytes = sizeof(T) * count;
    if(!bytes)
        return;

    const usize alignSize = Max(CachelineSize(), static_cast<usize>(alignof(T)));
    arena.deallocate(buffer, alignSize, bytes);
}

template<typename Derived, typename T>
class ArenaAllocatorOperations{
public:
    constexpr void deallocate(T* const buffer, const usize count)noexcept{
        NWB_ASSERT_MSG((buffer != nullptr || count == 0), NWB_TEXT("null pointer cannot point to a block of non-zero size"));

        static_cast<Derived&>(*this).arena().template deallocate<T>(buffer, count);
    }

    constexpr NWB_ALLOCATOR_PREFIX T* allocate(const usize count) NWB_ALLOCATOR_SUFFIX{
        return static_cast<Derived&>(*this).arena().template allocate<T>(count);
    }
#if _HAS_CXX23
    constexpr AllocationResult<T*> allocate_at_least(const usize count){ return { allocate(count), count }; }
#endif
};

template<typename Derived, typename T>
class CacheAlignedArenaAllocatorOperations{
public:
    usize max_size()const noexcept{
        return MaxCacheAlignedAllocationCount<T>();
    }

    constexpr void deallocate(T* const buffer, const usize count)noexcept{
        DeallocateCacheAligned(static_cast<Derived&>(*this).arena(), buffer, count);
    }

    constexpr NWB_ALLOCATOR_PREFIX T* allocate(const usize count) NWB_ALLOCATOR_SUFFIX{
        return AllocateCacheAligned<T>(static_cast<Derived&>(*this).arena(), count);
    }
#if _HAS_CXX23
    constexpr AllocationResult<T*> allocate_at_least(const usize count){ return { allocate(count), count }; }
#endif
};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_ALLOC_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

