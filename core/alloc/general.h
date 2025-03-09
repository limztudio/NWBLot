// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#pragma once


#include "core.h"


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_ALLOC_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


template <typename T>
class GeneralAllocator{
public:
    static_assert(!IsConst_V<T>, "The C++ Standard forbids containers of const elements because allocator<const T> is ill-formed.");
    static_assert(!IsFunction_V<T>, "The C++ Standard forbids allocators for function elements because of [allocator.requirements].");
    static_assert(!IsReference_V<T>, "The C++ Standard forbids allocators for reference elements because of [allocator.requirements].");


public:
    using _From_primary = GeneralAllocator;
    using value_type = T;

    using size_type = usize;
    using difference_type = isize;

    using propagate_on_container_move_assignment = TrueType;
    using is_always_equal = TrueType;


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

        if(IsConstantEvaluated()){
            mi_free(buffer);
        }
        else{
            constexpr usize alignSize = alignof(T);

            coreFreeSizeAligned(buffer, bytes, alignSize, "NWB::Core::Alloc::GeneralAllocator::deallocate");
        }
    }

    constexpr __declspec(allocator) T* allocate(const usize count){
        static_assert(sizeof(T) > 0, "value_type must be complete before calling allocate.");

        T* output = nullptr;

        const usize bytes = getSizeOf<sizeof(T)>(count);

        if(bytes){
            if(IsConstantEvaluated()){
                output = reinterpret_cast<T*>(coreAlloc(bytes, "NWB::Core::Alloc::GeneralAllocator::allocate"));
            }
            else{
                constexpr usize alignSize = alignof(T);

                output = reinterpret_cast<T*>(coreAllocAligned(bytes, alignSize, "NWB::Core::Alloc::GeneralAllocator::allocate"));
            }
        }

        return output;
    }
#if _HAS_CXX23
    constexpr AllocationResult<T*> allocate_at_least(const usize count){ return { allocate(count), count }; }
#endif
};
template <typename T, typename F>
inline bool operator==(const GeneralAllocator<T>&, const GeneralAllocator<F>&)noexcept{ return true; }


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_ALLOC_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

