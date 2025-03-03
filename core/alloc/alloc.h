// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#pragma once


#include "global.h"

#include "scratch.h"


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_ALLOC_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


template <typename T>
class GeneralAllocator{
public:
    static_assert(!std::is_const_v<T>, "The C++ Standard forbids containers of const elements because allocator<const T> is ill-formed.");
    static_assert(!std::is_function_v<T>, "The C++ Standard forbids allocators for function elements because of [allocator.requirements].");
    static_assert(!std::is_reference_v<T>, "The C++ Standard forbids allocators for reference elements because of [allocator.requirements].");


public:
    using _From_primary = GeneralAllocator;
    using value_type = T;

    using size_type = usize;
    using difference_type = isize;

    using propagate_on_container_move_assignment = std::true_type;
    using is_always_equal = std::true_type;


public:
    constexpr GeneralAllocator()noexcept{}
    constexpr GeneralAllocator(const GeneralAllocator&)noexcept = default;
    template <class F>
    constexpr GeneralAllocator(const GeneralAllocator<F>&)noexcept{}

    constexpr ~GeneralAllocator() = default;
    constexpr GeneralAllocator& operator=(const GeneralAllocator&) = default;


public:
    constexpr void deallocate(T* const buffer, const usize count)noexcept{
        assert((buffer != nullptr || count == 0) && "null pointer cannot point to a block of non-zero size");

        const usize bytes = sizeof(T) * count;
        (void)bytes;

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
inline bool operator==(const GeneralAllocator<T>&, const GeneralAllocator<F>&)noexcept{ return true; }


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_ALLOC_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

