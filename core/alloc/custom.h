// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#pragma once


#include "global.h"
#include "core.h"


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_ALLOC_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


class CustomArena : NoCopy{
public:
    typedef void* (*AllocFunc)(usize size);
    typedef void (*FreeFunc)(void* ptr);
    typedef void* (*AllocAlignedFunc)(usize size, usize align);
    typedef void (*FreeAlignedFunc)(void* ptr);


public:
    CustomArena(AllocFunc alloc, FreeFunc free, AllocAlignedFunc allocAligned, FreeAlignedFunc freeAligned)
        : m_alloc(alloc)
        , m_free(free)
        , m_allocAligned(allocAligned)
        , m_freeAligned(freeAligned)
    {
        NWB_ASSERT(m_alloc != nullptr);
        NWB_ASSERT(m_free != nullptr);
        NWB_ASSERT(m_allocAligned != nullptr);
        NWB_ASSERT(m_freeAligned != nullptr);
	}
    ~CustomArena(){
        m_alloc = nullptr;
        m_free = nullptr;
        m_allocAligned = nullptr;
        m_freeAligned = nullptr;
    }


public:
    inline void* allocate(usize align, usize size){
        size = Alignment(align, size);

        return (align <= 1) ? m_alloc(size) : m_allocAligned(size, align);
    }
    template<typename T>
    inline T* allocate(usize count){
        static_assert(sizeof(T) > 0, "value_type must be complete before calling allocate.");
        const usize bytes = SizeOf<sizeof(T)>(count);

        T* output = nullptr;
        if(bytes){
            if(IsConstantEvaluated())
                output = reinterpret_cast<T*>(allocate(1, bytes));
            else{
                constexpr usize alignSize = alignof(T);
                output = reinterpret_cast<T*>(allocate(alignSize, bytes));
            }
        }
        return output;
    }

    inline void deallocate(void* p, usize align, usize size){
        (void)size;

        return (align <= 1) ? m_free(p) : m_freeAligned(p);
    }
    template<typename T>
    inline void deallocate(void* p, usize count){
        static_assert(sizeof(T) > 0, "value_type must be complete before calling allocate.");
        const usize bytes = SizeOf<sizeof(T)>(count);

        if(bytes){
            if(IsConstantEvaluated())
                deallocate(p, 1, bytes);
            else{
                constexpr usize alignSize = alignof(T);
                deallocate(p, alignSize, bytes);
            }
        }
    }


private:
    AllocFunc m_alloc;
    FreeFunc m_free;
    AllocAlignedFunc m_allocAligned;
    FreeAlignedFunc m_freeAligned;
};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


template<typename T>
class CustomAllocator{
    template<typename F>
    friend class CustomAllocator;


public:
    static_assert(!IsConst_V<T>, "NWB::Core::Alloc::CustomAllocator forbids containers of const elements because allocator<const T> is ill-formed.");
    static_assert(!IsFunction_V<T>, "NWB::Core::Alloc::CustomAllocator forbids allocators for function elements because of [allocator.requirements].");
    static_assert(!IsReference_V<T>, "NWB::Core::Alloc::CustomAllocator forbids allocators for reference elements because of [allocator.requirements].");


public:
    using _From_primary = CustomAllocator;
    using value_type = T;

    using size_type = usize;
    using difference_type = isize;

    using propagate_on_container_move_assignment = TrueType;
    using is_always_equal = TrueType;


public:
    template<typename U>
    struct rebind{
        using other = CustomAllocator<U>;
    };


public:
    constexpr CustomAllocator(CustomArena& arena)noexcept : m_arena(arena){}
    constexpr CustomAllocator(const CustomAllocator&)noexcept = default;
    template<class F>
    constexpr CustomAllocator(const CustomAllocator<F>& rhs)noexcept : m_arena(rhs.m_arena){}

    constexpr ~CustomAllocator() = default;
    constexpr CustomAllocator& operator=(const CustomAllocator&)noexcept{ return *this; }


public:
    constexpr void deallocate(T* const buffer, const usize count)noexcept{
        NWB_ASSERT_MSG((buffer != nullptr || count == 0), NWB_TEXT("null pointer cannot point to a block of non-zero size"));

        m_arena.deallocate<T>(buffer, count);
    }

    constexpr __declspec(allocator) T* allocate(const usize count){
        return m_arena.allocate<T>(count);
    }
#if _HAS_CXX23
    constexpr AllocationResult<T*> allocate_at_least(const usize count){ return { allocate(count), count }; }
#endif


private:
    CustomArena& m_arena;
};
template<typename T, typename F>
inline bool operator==(const CustomAllocator<T>&, const CustomAllocator<F>&)noexcept{ return true; }
template<typename T, typename F>
inline bool operator!=(const CustomAllocator<T>&, const CustomAllocator<F>&)noexcept{ return false; }


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


template<typename T>
class CustomCacheAlignedAllocator{
    template<typename F>
    friend class CustomCacheAlignedAllocator;


public:
    static_assert(!IsConst_V<T>, "NWB::Core::Alloc::CustomCacheAlignedAllocator forbids containers of const elements because allocator<const T> is ill-formed.");
    static_assert(!IsFunction_V<T>, "NWB::Core::Alloc::CustomCacheAlignedAllocator forbids allocators for function elements because of [allocator.requirements].");
    static_assert(!IsReference_V<T>, "NWB::Core::Alloc::CustomCacheAlignedAllocator forbids allocators for reference elements because of [allocator.requirements].");


public:
    using _From_primary = CustomCacheAlignedAllocator;
    using value_type = T;

    using size_type = usize;
    using difference_type = isize;

    using propagate_on_container_move_assignment = TrueType;
    using is_always_equal = TrueType;


public:
    template<typename U>
    struct rebind{
        using other = CustomCacheAlignedAllocator<U>;
    };


public:
    constexpr CustomCacheAlignedAllocator(CustomArena& arena)noexcept : m_arena(arena){}
    constexpr CustomCacheAlignedAllocator(const CustomCacheAlignedAllocator&)noexcept = default;
    template<class F>
    constexpr CustomCacheAlignedAllocator(const CustomCacheAlignedAllocator<F>& rhs)noexcept : m_arena(rhs.m_arena){}

    constexpr ~CustomCacheAlignedAllocator() = default;
    constexpr CustomCacheAlignedAllocator& operator=(const CustomCacheAlignedAllocator&)noexcept{ return *this; }


public:
    usize max_size()const noexcept{
        return (~usize(0) - CachelineSize()) / sizeof(value_type);
    }

    constexpr void deallocate(T* const buffer, const usize count)noexcept{
        NWB_ASSERT_MSG((buffer != nullptr || count == 0), NWB_TEXT("null pointer cannot point to a block of non-zero size"));

        const usize bytes = sizeof(T) * count;
        if(!bytes)
            return;

        const usize alignSize = Max(CachelineSize(), static_cast<usize>(alignof(T)));
        m_arena.deallocate(buffer, alignSize, bytes);
    }

    constexpr __declspec(allocator) T* allocate(const usize count){
        const usize bytes = SizeOf<sizeof(T)>(count);
        if(!bytes)
            return nullptr;

        const usize alignSize = Max(CachelineSize(), static_cast<usize>(alignof(T)));
        return reinterpret_cast<T*>(m_arena.allocate(alignSize, bytes));
    }
#if _HAS_CXX23
    constexpr AllocationResult<T*> allocate_at_least(const usize count){ return { allocate(count), count }; }
#endif


private:
    CustomArena& m_arena;
};
template<typename T, typename F>
inline bool operator==(const CustomCacheAlignedAllocator<T>&, const CustomCacheAlignedAllocator<F>&)noexcept{ return true; }
template<typename T, typename F>
inline bool operator!=(const CustomCacheAlignedAllocator<T>&, const CustomCacheAlignedAllocator<F>&)noexcept{ return false; }


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_ALLOC_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


template<typename T>
using CustomUniquePtr = UniquePtr<T, ArenaDeleter<T, NWB::Core::Alloc::CustomArena>>;

template<typename T, typename... Args>
inline typename EnableIf<!IsArray<T>::value, CustomUniquePtr<T>>::type MakeCustomUnique(NWB::Core::Alloc::CustomArena& arena, Args&&... args){
    return CustomUniquePtr<T>(new(arena.allocate<T>(1)) T(Forward<Args>(args)...), CustomUniquePtr<T>::deleter_type(arena));
}
template<typename T>
inline typename EnableIf<IsUnboundedArray<T>::value, CustomUniquePtr<T>>::type MakeCustomUnique(NWB::Core::Alloc::CustomArena& arena, size_t n){
    typedef typename RemoveExtent<T>::type TBase;
    return CustomUniquePtr<T>(new(arena.allocate<TBase>(n)) TBase[n], CustomUniquePtr<T>::deleter_type(arena, n));
}
template<typename T, typename... Args>
typename EnableIf<IsBoundedArray<T>::value>::type
MakeCustomUnique(Args&&...) = delete;


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


template<typename T>
struct ArenaRefDeleter{
    NWB::Core::Alloc::CustomArena* arena = nullptr;

    constexpr ArenaRefDeleter()noexcept = default;
    constexpr explicit ArenaRefDeleter(NWB::Core::Alloc::CustomArena* a)noexcept : arena(a){}
    template<typename U>
    ArenaRefDeleter(const ArenaRefDeleter<U>& other, typename EnableIf<IsConvertible<U*, T*>::value>::type* = 0)noexcept : arena(other.arena){}

    void operator()(T* p)const noexcept{
        if(p && arena){
            p->~T();
            arena->deallocate(p, alignof(T), sizeof(T));
        }
    }
};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

