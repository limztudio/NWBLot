// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#pragma once


#include "core.h"


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_ALLOC_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


extern usize CachelineSize();


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


template<typename T>
class GeneralAllocator{
public:
    static_assert(!IsConst_V<T>, "NWB::Core::Alloc::GeneralAllocator forbids containers of const elements because allocator<const T> is ill-formed.");
    static_assert(!IsFunction_V<T>, "NWB::Core::Alloc::GeneralAllocator forbids allocators for function elements because of [allocator.requirements].");
    static_assert(!IsReference_V<T>, "NWB::Core::Alloc::GeneralAllocator forbids allocators for reference elements because of [allocator.requirements].");


public:
    using _From_primary = GeneralAllocator;
    using value_type = T;

    using pointer = T*;
    using const_pointer = const T*;

    using void_pointer = void*;
    using const_void_pointer = const void*;

    using reference = T&;
    using const_reference = const T&;

    using size_type = usize;
    using difference_type = isize;

    using propagate_on_container_move_assignment = TrueType;
    using is_always_equal = TrueType;


public:
    template<typename F>
    struct rebind{
        using other = GeneralAllocator<F>;
    };


public:
    constexpr GeneralAllocator()noexcept{}
    constexpr GeneralAllocator(const GeneralAllocator&)noexcept = default;
    template<class F>
    constexpr GeneralAllocator(const GeneralAllocator<F>&)noexcept{}

    constexpr ~GeneralAllocator() = default;
    constexpr GeneralAllocator& operator=(const GeneralAllocator&) = default;


public:
    constexpr void deallocate(T* const buffer, const usize count)noexcept{
        NWB_ASSERT_MSG((buffer != nullptr || count == 0), NWB_TEXT("null pointer cannot point to a block of non-zero size"));

        const usize bytes = sizeof(T) * count;

        if(IsConstantEvaluated()){
            CoreFreeSize(buffer, bytes, "NWB::Core::Alloc::GeneralAllocator::deallocate");
        }
        else
            CoreFreeSizeAligned(buffer, bytes, "NWB::Core::Alloc::GeneralAllocator::deallocate");
    }

    constexpr __declspec(allocator) T* allocate(const usize count){
        static_assert(sizeof(T) > 0, "value_type must be complete before calling allocate.");

        T* output = nullptr;

        const usize bytes = SizeOf<sizeof(T)>(count);

        if(bytes){
            if(IsConstantEvaluated()){
                output = reinterpret_cast<T*>(CoreAlloc(bytes, "NWB::Core::Alloc::GeneralAllocator::allocate"));
            }
            else{
                constexpr usize alignSize = alignof(T);

                output = reinterpret_cast<T*>(CoreAllocAligned(bytes, alignSize, "NWB::Core::Alloc::GeneralAllocator::allocate"));
            }
        }

        return output;
    }
#if _HAS_CXX23
    constexpr AllocationResult<T*> allocate_at_least(const usize count){ return { allocate(count), count }; }
#endif
};
template<typename T, typename F>
inline bool operator==(const GeneralAllocator<T>&, const GeneralAllocator<F>&)noexcept{ return true; }
template<typename T, typename F>
inline bool operator!=(const GeneralAllocator<T>&, const GeneralAllocator<F>&)noexcept{ return false; }


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


template<typename T>
class CacheAlignedAllocator{
public:
    static_assert(!IsConst_V<T>, "NWB::Core::Alloc::CacheAlignedAllocator forbids containers of const elements because allocator<const T> is ill-formed.");
    static_assert(!IsFunction_V<T>, "NWB::Core::Alloc::CacheAlignedAllocator forbids allocators for function elements because of [allocator.requirements].");
    static_assert(!IsReference_V<T>, "NWB::Core::Alloc::CacheAlignedAllocator forbids allocators for reference elements because of [allocator.requirements].");


public:
    using _From_primary = CacheAlignedAllocator;
    using value_type = T;

    using pointer = T*;
    using const_pointer = const T*;

    using void_pointer = void*;
    using const_void_pointer = const void*;

    using reference = T&;
    using const_reference = const T&;

    using size_type = usize;
    using difference_type = isize;

    using propagate_on_container_move_assignment = TrueType;
    using is_always_equal = TrueType;


public:
    template<typename F>
    struct rebind{
        using other = CacheAlignedAllocator<F>;
    };


public:
    constexpr CacheAlignedAllocator()noexcept{}
    constexpr CacheAlignedAllocator(const CacheAlignedAllocator&)noexcept = default;
    template<class F>
    constexpr CacheAlignedAllocator(const CacheAlignedAllocator<F>&)noexcept{}

    constexpr ~CacheAlignedAllocator() = default;
    constexpr CacheAlignedAllocator& operator=(const CacheAlignedAllocator&) = default;


public:
    usize max_size() const noexcept{
        return (~usize(0) - CachelineSize()) / sizeof(value_type);
    }

    constexpr void deallocate(T* const buffer, const usize count)noexcept{
        NWB_ASSERT_MSG((buffer != nullptr || count == 0), NWB_TEXT("null pointer cannot point to a block of non-zero size"));

        const usize bytes = sizeof(T) * count;

        CoreFreeSizeAligned(buffer, bytes, "NWB::Core::Alloc::CacheAlignedAllocator::deallocate");
    }

    constexpr __declspec(allocator) T* allocate(const usize count){
        static_assert(sizeof(T) > 0, "value_type must be complete before calling allocate.");

        const usize bytes = SizeOf<sizeof(T)>(count);
        const usize alignSize = CachelineSize();

        return reinterpret_cast<T*>(CoreAllocAligned(bytes, alignSize, "NWB::Core::Alloc::CacheAlignedAllocator::allocate"));
    }
#if _HAS_CXX23
    constexpr AllocationResult<T*> allocate_at_least(const usize count){ return { allocate(count), count }; }
#endif
};
template<typename T, typename F>
inline bool operator==(const CacheAlignedAllocator<T>&, const CacheAlignedAllocator<F>&)noexcept{ return true; }
template<typename T, typename F>
inline bool operator!=(const CacheAlignedAllocator<T>&, const CacheAlignedAllocator<F>&)noexcept{ return false; }


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_ALLOC_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

