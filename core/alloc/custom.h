// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#pragma once


#include "global.h"
#include "core.h"


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_ALLOC_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


class CustomArena;


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace __hidden_custom_allocator{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


inline void* DefaultAlloc(usize size){
    return CoreAlloc(size, "CustomAllocator");
}
inline void DefaultFree(void* ptr){
    CoreFree(ptr, "CustomAllocator");
}
inline void* DefaultAllocAligned(usize size, usize align){
    return CoreAllocAligned(size, align, "CustomAllocator");
}
inline void DefaultFreeAligned(void* ptr){
    CoreFreeAligned(ptr, "CustomAllocator");
}

CustomArena& DefaultArena();


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


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
    {}
    ~CustomArena() = default;


public:
    inline void* allocate(usize align, usize size){
        size = Alignment(align, size);

        return (align <= 1) ? m_alloc.get()(size) : m_allocAligned.get()(size, align);
    }
    template<typename T>
    inline T* allocate(usize count){
        return __hidden_alloc::AllocateTyped<T>(*this, count);
    }

    inline void deallocate(void* p, usize align, usize size){
        (void)size;

        return (align <= 1) ? m_free.get()(p) : m_freeAligned.get()(p);
    }
    template<typename T>
    inline void deallocate(void* p, usize count){
        __hidden_alloc::DeallocateTyped<T>(*this, p, count);
    }


private:
    NotNull<AllocFunc> m_alloc;
    NotNull<FreeFunc> m_free;
    NotNull<AllocAlignedFunc> m_allocAligned;
    NotNull<FreeAlignedFunc> m_freeAligned;
};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace __hidden_custom_allocator{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


inline CustomArena& DefaultArena(){
    static CustomArena arena(DefaultAlloc, DefaultFree, DefaultAllocAligned, DefaultFreeAligned);
    return arena;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


template<typename T>
class CustomAllocator : public __hidden_alloc::ArenaAllocatorOperations<CustomAllocator<T>, T>{
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
    using is_always_equal = FalseType;


public:
    template<typename U>
    struct rebind{
        using other = CustomAllocator<U>;
    };


public:
    CustomAllocator()noexcept
        : m_arena(__hidden_custom_allocator::DefaultArena())
    {}
    constexpr CustomAllocator(CustomArena& arena)noexcept : m_arena(arena){}
    constexpr CustomAllocator(const CustomAllocator&)noexcept = default;
    template<class F>
    constexpr CustomAllocator(const CustomAllocator<F>& rhs)noexcept : m_arena(rhs.m_arena){}

    constexpr ~CustomAllocator() = default;
    constexpr CustomAllocator& operator=(const CustomAllocator&)noexcept{ return *this; }


public:
    [[nodiscard]] constexpr CustomArena& arena()const noexcept{ return m_arena; }


private:
    CustomArena& m_arena;
};
template<typename T, typename F>
inline bool operator==(const CustomAllocator<T>& lhs, const CustomAllocator<F>& rhs)noexcept{ return &lhs.arena() == &rhs.arena(); }
template<typename T, typename F>
inline bool operator!=(const CustomAllocator<T>& lhs, const CustomAllocator<F>& rhs)noexcept{ return !(lhs == rhs); }


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


template<typename T>
class CustomCacheAlignedAllocator : public __hidden_alloc::CacheAlignedArenaAllocatorOperations<CustomCacheAlignedAllocator<T>, T>{
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
    using is_always_equal = FalseType;


public:
    template<typename U>
    struct rebind{
        using other = CustomCacheAlignedAllocator<U>;
    };


public:
    CustomCacheAlignedAllocator()noexcept
        : m_arena(__hidden_custom_allocator::DefaultArena())
    {}
    constexpr CustomCacheAlignedAllocator(CustomArena& arena)noexcept : m_arena(arena){}
    constexpr CustomCacheAlignedAllocator(const CustomCacheAlignedAllocator&)noexcept = default;
    template<class F>
    constexpr CustomCacheAlignedAllocator(const CustomCacheAlignedAllocator<F>& rhs)noexcept : m_arena(rhs.m_arena){}

    constexpr ~CustomCacheAlignedAllocator() = default;
    constexpr CustomCacheAlignedAllocator& operator=(const CustomCacheAlignedAllocator&)noexcept{ return *this; }


public:
    [[nodiscard]] constexpr CustomArena& arena()const noexcept{ return m_arena; }


private:
    CustomArena& m_arena;
};
template<typename T, typename F>
inline bool operator==(const CustomCacheAlignedAllocator<T>& lhs, const CustomCacheAlignedAllocator<F>& rhs)noexcept{ return &lhs.arena() == &rhs.arena(); }
template<typename T, typename F>
inline bool operator!=(const CustomCacheAlignedAllocator<T>& lhs, const CustomCacheAlignedAllocator<F>& rhs)noexcept{ return !(lhs == rhs); }


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_ALLOC_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace NWB{ namespace Core{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


template<typename T>
using CustomUniquePtr = UniquePtr<T, ArenaDeleter<T, NWB::Core::Alloc::CustomArena>>;

template<typename T, typename... Args>
inline typename EnableIf<!IsArray<T>::value, CustomUniquePtr<T>>::type MakeCustomUnique(NWB::Core::Alloc::CustomArena& arena, Args&&... args){
    return CustomUniquePtr<T>(new(arena.allocate<T>(1)) T(Forward<Args>(args)...), typename CustomUniquePtr<T>::deleter_type(arena));
}
template<typename T>
inline typename EnableIf<IsUnboundedArray<T>::value, CustomUniquePtr<T>>::type MakeCustomUnique(NWB::Core::Alloc::CustomArena& arena, usize n){
    typedef typename RemoveExtent<T>::type TBase;
    return CustomUniquePtr<T>(new(arena.allocate<TBase>(n)) TBase[n], typename CustomUniquePtr<T>::deleter_type(arena, n));
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


}; };


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

