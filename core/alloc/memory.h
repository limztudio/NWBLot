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
    constexpr static usize StructureAlignedSize(usize byte)noexcept{ return byte + static_cast<usize>(tlsf_size() + 8); }


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
    template <typename T>
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
        (void)align;
        (void)size;
        tlsf_free(m_handle, p);
    }
    template <typename T>
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
    void* m_bucket;
    usize m_maxSize;

    tlsf_t m_handle;
};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


template <typename T>
class MemoryAllocator{
    template <typename F>
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
    constexpr MemoryAllocator(MemoryArena& arena)noexcept : m_arena(arena){}
    constexpr MemoryAllocator(const MemoryAllocator&)noexcept = default;
    template <class F>
    constexpr MemoryAllocator(const MemoryAllocator<F>& rhs)noexcept : m_arena(rhs.m_arena){}

    constexpr ~MemoryAllocator() = default;
    constexpr MemoryAllocator& operator=(const MemoryAllocator&) = default;


public:
    constexpr void deallocate(T* const buffer, const usize count)noexcept{
        NWB_ASSERT((buffer != nullptr || count == 0) && "null pointer cannot point to a block of non-zero size");

        m_arena.deallocate<T>(buffer, count);
    }

    constexpr __declspec(allocator) T* allocate(const usize count){
        return m_arena.allocate<T>(count);
    }
#if _HAS_CXX23
    constexpr AllocationResult<T*> allocate_at_least(const usize count){ return { allocate(count), count }; }
#endif


private:
    MemoryArena& m_arena;
};
template <typename T, typename F>
inline bool operator==(const MemoryAllocator<T>&, const MemoryAllocator<F>&)noexcept{ return true; }


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_ALLOC_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


template <typename T>
using MemoryUniquePtr = UniquePtr<T, ArenaDeleter<T, NWB::Core::Alloc::MemoryArena>>;

template <typename T, typename... Args>
inline typename EnableIf<!IsArray<T>::value, MemoryUniquePtr<T>>::type MakeMemoryUnique(NWB::Core::Alloc::MemoryArena& arena, Args&&... args){
    return MemoryUniquePtr<T>(new(arena.allocate<T>(1)) T(Forward<Args>(args)...), MemoryUniquePtr<T>::deleter_type(arena));
}
template <typename T>
inline typename EnableIf<IsUnboundedArray<T>::value, MemoryUniquePtr<T>>::type MakeMemoryUnique(NWB::Core::Alloc::MemoryArena& arena, size_t n){
    typedef typename RemoveExtent<T>::type TBase;
    return MemoryUniquePtr<T>(new(arena.allocate<TBase>(n)) TBase[n], MemoryUniquePtr<T>::deleter_type(arena, n));
}
template <typename T, typename... Args>
typename EnableIf<IsBoundedArray<T>::value>::type
MakeMemoryUnique(Args&&...) = delete;


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

