// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#pragma once


#include "global.h"
#include "core.h"

#include "tlsf.h"


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_ALLOC_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


class MemoryArena : NoCopy{
public:
    inline static usize StructureAlignedSize(usize byte){
        const usize overhead = AddSize(static_cast<usize>(tlsf_size()), 8);
        return AddSize(byte, overhead);
    }


public:
    MemoryArena(usize maxSize)
        : m_bucket(CoreAlloc(maxSize, "NWB::Core::Alloc::MemoryArena::constructor"))
        , m_maxSize(maxSize)
        , m_handle(tlsf_create_with_pool(m_bucket, m_maxSize))
    {
    }
    ~MemoryArena(){
        tlsf_destroy(m_handle);
        m_handle = nullptr;

        CoreFree(m_bucket, "NWB::Core::Alloc::MemoryArena::destructor");
        m_bucket = nullptr;
    }


public:
    inline void* allocate(usize align, usize size){
        size = Alignment(align, size);

        return (align <= 1) ? tlsf_malloc(m_handle, size) : tlsf_memalign(m_handle, align, size);
    }
    inline void* reallocate(void* p, usize align, usize size){
        size = Alignment(align, size);

        static_cast<void>(align);
        return tlsf_realloc(m_handle, p, size);
    }
    template<typename T>
    inline T* allocate(usize count){
        return AllocDetail::AllocateTyped<T>(*this, count);
    }

    inline void deallocate(void* p, usize align, usize size){
        static_cast<void>(align);
        static_cast<void>(size);
        tlsf_free(m_handle, p);
    }
    template<typename T>
    inline void deallocate(void* p, usize count){
        AllocDetail::DeallocateTyped<T>(*this, p, count);
    }


private:
    void* m_bucket;
    usize m_maxSize;

    tlsf_t m_handle;
};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


template<typename T>
class MemoryAllocator : public AllocDetail::ArenaAllocatorOperations<MemoryAllocator<T>, T>{
    template<typename F>
    friend class MemoryAllocator;


public:
    static_assert(!IsConst_V<T>, "NWB::Core::Alloc::MemoryAllocator forbids containers of const elements because allocator<const T> is ill-formed.");
    static_assert(!IsFunction_V<T>, "NWB::Core::Alloc::MemoryAllocator forbids allocators for function elements because of [allocator.requirements].");
    static_assert(!IsReference_V<T>, "NWB::Core::Alloc::MemoryAllocator forbids allocators for reference elements because of [allocator.requirements].");


public:
    using _From_primary = MemoryAllocator;
    using value_type = T;

    using size_type = usize;
    using difference_type = isize;

    using propagate_on_container_move_assignment = TrueType;
    using is_always_equal = TrueType;


public:
    template<typename U>
    struct rebind{
        using other = MemoryAllocator<U>;
    };


public:
    constexpr MemoryAllocator(MemoryArena& arena)noexcept
        : m_arena(arena)
    {}
    constexpr MemoryAllocator(const MemoryAllocator&)noexcept = default;
    template<class F>
    constexpr MemoryAllocator(const MemoryAllocator<F>& rhs)noexcept
        : m_arena(rhs.m_arena)
    {}

    constexpr ~MemoryAllocator() = default;
    constexpr MemoryAllocator& operator=(const MemoryAllocator&) = default;


public:
    [[nodiscard]] constexpr MemoryArena& arena()const noexcept{ return m_arena; }


private:
    MemoryArena& m_arena;
};
template<typename T, typename F>
inline bool operator==(const MemoryAllocator<T>&, const MemoryAllocator<F>&)noexcept{ return true; }
template<typename T, typename F>
inline bool operator!=(const MemoryAllocator<T>&, const MemoryAllocator<F>&)noexcept{ return false; }


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


template<typename T>
class MemoryCacheAlignedAllocator : public AllocDetail::CacheAlignedArenaAllocatorOperations<MemoryCacheAlignedAllocator<T>, T>{
    template<typename F>
    friend class MemoryCacheAlignedAllocator;


public:
    static_assert(!IsConst_V<T>, "NWB::Core::Alloc::MemoryCacheAlignedAllocator forbids containers of const elements because allocator<const T> is ill-formed.");
    static_assert(!IsFunction_V<T>, "NWB::Core::Alloc::MemoryCacheAlignedAllocator forbids allocators for function elements because of [allocator.requirements].");
    static_assert(!IsReference_V<T>, "NWB::Core::Alloc::MemoryCacheAlignedAllocator forbids allocators for reference elements because of [allocator.requirements].");


public:
    using _From_primary = MemoryCacheAlignedAllocator;
    using value_type = T;

    using size_type = usize;
    using difference_type = isize;

    using propagate_on_container_move_assignment = TrueType;
    using is_always_equal = TrueType;


public:
    template<typename U>
    struct rebind{
        using other = MemoryCacheAlignedAllocator<U>;
    };


public:
    constexpr MemoryCacheAlignedAllocator(MemoryArena& arena)noexcept
        : m_arena(arena)
    {}
    constexpr MemoryCacheAlignedAllocator(const MemoryCacheAlignedAllocator&)noexcept = default;
    template<class F>
    constexpr MemoryCacheAlignedAllocator(const MemoryCacheAlignedAllocator<F>& rhs)noexcept
        : m_arena(rhs.m_arena)
    {}

    constexpr ~MemoryCacheAlignedAllocator() = default;
    constexpr MemoryCacheAlignedAllocator& operator=(const MemoryCacheAlignedAllocator&)noexcept{ return *this; }


public:
    [[nodiscard]] constexpr MemoryArena& arena()const noexcept{ return m_arena; }


private:
    MemoryArena& m_arena;
};
template<typename T, typename F>
inline bool operator==(const MemoryCacheAlignedAllocator<T>&, const MemoryCacheAlignedAllocator<F>&)noexcept{ return true; }
template<typename T, typename F>
inline bool operator!=(const MemoryCacheAlignedAllocator<T>&, const MemoryCacheAlignedAllocator<F>&)noexcept{ return false; }


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_ALLOC_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


template<typename T>
using MemoryUniquePtr = UniquePtr<T, ArenaDeleter<T, NWB::Core::Alloc::MemoryArena>>;

template<typename T, typename... Args>
inline typename EnableIf<!IsArray<T>::value, MemoryUniquePtr<T>>::type MakeMemoryUnique(NWB::Core::Alloc::MemoryArena& arena, Args&&... args){
    return MemoryUniquePtr<T>(new(arena.allocate<T>(1)) T(Forward<Args>(args)...), MemoryUniquePtr<T>::deleter_type(arena));
}
template<typename T>
inline typename EnableIf<IsUnboundedArray<T>::value, MemoryUniquePtr<T>>::type MakeMemoryUnique(NWB::Core::Alloc::MemoryArena& arena, usize n){
    typedef typename RemoveExtent<T>::type TBase;
    return MemoryUniquePtr<T>(new(arena.allocate<TBase>(n)) TBase[n], MemoryUniquePtr<T>::deleter_type(arena, n));
}
template<typename T, typename... Args>
typename EnableIf<IsBoundedArray<T>::value>::type
MakeMemoryUnique(Args&&...) = delete;


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

