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

