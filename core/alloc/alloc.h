// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#pragma once


#include "global.h"


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


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


template <typename T>
class allocator{
public:
    static_assert(!std::is_const_v<T>, "The C++ Standard forbids containers of const elements because allocator<const T> is ill-formed.");
    static_assert(!std::is_function_v<T>, "The C++ Standard forbids allocators for function elements because of [allocator.requirements].");
    static_assert(!std::is_reference_v<T>, "The C++ Standard forbids allocators for reference elements because of [allocator.requirements].");


public:
    using _From_primary = allocator;
    using value_type = T;

    using size_type = usize;
    using difference_type = isize;

    using propagate_on_container_move_assignment = std::true_type;
    using is_always_equal = std::true_type;


public:
    constexpr allocator()noexcept{}
    constexpr allocator(const allocator&)noexcept = default;
    template <class F>
    constexpr allocator(const allocator<F>&)noexcept{}

    constexpr ~allocator() = default;
    constexpr allocator& operator=(const allocator&) = default;

    constexpr void deallocate(T* const buffer, const usize count)noexcept{
        assert((buffer != nullptr || count == 0) && "null pointer cannot point to a block of non-zero size");

        const usize bytes = sizeof(T) * count;

        if(std::is_constant_evaluated()){
            mi_free(buffer);
        }
        else{
            constexpr usize alignSize = alignof(T);

            mi_free_aligned(buffer, alignSize);
        }
    }

    constexpr __declspec(allocator) T* allocate(const usize count){
        static_assert(sizeof(T) > 0, "value_type must be complete before calling allocate.");

        T* output = nullptr;

        const usize bytes = getSizeOf<sizeof(T)>(count);

        if(bytes){
            if(std::is_constant_evaluated()){
                output = reinterpret_cast<T*>(mi_malloc(bytes));
            }
            else{
                constexpr usize alignSize = alignof(T);

                output = reinterpret_cast<T*>(mi_aligned_alloc(alignSize, bytes));
            }
        }

        return output;
    }
#if _HAS_CXX23
    constexpr std::allocation_result<T*> allocate_at_least(const usize count){ return { allocate(count), count }; }
#endif
};
template <typename T, typename F>
inline bool operator==(const allocator<T>&, const allocator<F>&)noexcept{ return true; }


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_ALLOC_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

