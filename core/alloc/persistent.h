// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#pragma once


#include "base.h"
#include "global.h"
#include "core.h"

#include "tlsf.h"


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_ALLOC_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


class PersistentArena : public ArenaBaseT<PersistentArena>{
public:
    using Base = ArenaBaseT<PersistentArena>;


public:
    using Base::allocate;
    using Base::deallocate;


public:
    inline static usize StructureAlignedSize(usize byte){
        const usize overhead = AddSize(static_cast<usize>(tlsf_size()), 8);
        return AddSize(byte, overhead);
    }


public:
    PersistentArena(usize maxSize, const char* allocationLog = "NWB::Core::Alloc::PersistentArena")
        : Base(allocationLog)
        , m_bucket(CoreAlloc(maxSize, allocationLog))
        , m_maxSize(maxSize)
        , m_handle(tlsf_create_with_pool(m_bucket, m_maxSize))
    {
    }
    ~PersistentArena(){
        tlsf_destroy(m_handle);
        m_handle = nullptr;

        CoreFree(m_bucket, log());
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

    inline void deallocate(void* p, usize align, usize size){
        static_cast<void>(align);
        static_cast<void>(size);
        tlsf_free(m_handle, p);
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
using PersistentUniquePtr = UniquePtr<T, ArenaDeleter<T, NWB::Core::Alloc::PersistentArena>>;

template<typename T, typename... Args>
inline typename EnableIf<!IsArray<T>::value, PersistentUniquePtr<T>>::type MakePersistentUnique(NWB::Core::Alloc::PersistentArena& arena, Args&&... args){
    return PersistentUniquePtr<T>(new(arena.allocate<T>(1)) T(Forward<Args>(args)...), PersistentUniquePtr<T>::deleter_type(arena));
}
template<typename T>
inline typename EnableIf<IsUnboundedArray<T>::value, PersistentUniquePtr<T>>::type MakePersistentUnique(NWB::Core::Alloc::PersistentArena& arena, usize n){
    typedef typename RemoveExtent<T>::type TBase;
    return PersistentUniquePtr<T>(new(arena.allocate<TBase>(n)) TBase[n], PersistentUniquePtr<T>::deleter_type(arena, n));
}
template<typename T, typename... Args>
typename EnableIf<IsBoundedArray<T>::value>::type
MakePersistentUnique(Args&&...) = delete;


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

