#pragma once


#include "base.h"
#include "global.h"
#include "core.h"
#include "arena_object.h"

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
    PersistentArena(const Name& allocationLog, usize maxSize)
        : Base(allocationLog)
        , m_bucket(CoreAlloc(maxSize, log()))
        , m_maxSize(maxSize)
        , m_handle(tlsf_create_with_pool(m_bucket, m_maxSize))
    {
        m_memoryStats.reset(static_cast<u64>(m_maxSize));
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

        void* p = (align <= 1) ? tlsf_malloc(m_handle, size) : tlsf_memalign(m_handle, align, size);
        if(p)
            m_memoryStats.recordAllocation(static_cast<u64>(tlsf_block_size(p)));
        return p;
    }

    inline void* reallocate(void* p, usize align, usize size){
        size = Alignment(align, size);

        if(!p)
            return size ? allocate(align, size) : nullptr;

        const usize oldSize = tlsf_block_size(p);
        const u64 oldBytes = static_cast<u64>(oldSize);
        if(size == 0u){
            tlsf_free(m_handle, p);
            m_memoryStats.recordReallocation(oldBytes, 0u);
            return nullptr;
        }

        if(align <= static_cast<usize>(tlsf_align_size()) || size <= oldSize){
            void* next = tlsf_realloc(m_handle, p, size);
            if(next)
                m_memoryStats.recordReallocation(oldBytes, static_cast<u64>(tlsf_block_size(next)));
            return next;
        }

        void* next = tlsf_memalign(m_handle, align, size);
        if(!next)
            return nullptr;

        const usize newSize = tlsf_block_size(next);
        const usize copySize = Min(oldSize, size);
        NWB_MEMCPY(next, newSize, p, copySize);
        tlsf_free(m_handle, p);
        m_memoryStats.recordReallocation(oldBytes, static_cast<u64>(newSize));
        return next;
    }

    inline void deallocate(void* p, usize align, usize size){
        static_cast<void>(align);
        static_cast<void>(size);
        if(p)
            m_memoryStats.recordDeallocation(static_cast<u64>(tlsf_block_size(p)));
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


NWB_CORE_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


template<typename T>
using PersistentUniquePtr = Alloc::ArenaUniquePtr<T, Alloc::PersistentArena>;

template<typename T, typename... Args>
inline typename EnableIf<!IsArray<T>::value, PersistentUniquePtr<T>>::type MakePersistentUnique(NWB::Core::Alloc::PersistentArena& arena, Args&&... args){
    return Alloc::MakeArenaUnique<T>(arena, Forward<Args>(args)...);
}
template<typename T>
inline typename EnableIf<IsUnboundedArray<T>::value, PersistentUniquePtr<T>>::type MakePersistentUnique(NWB::Core::Alloc::PersistentArena& arena, usize n){
    return Alloc::MakeArenaUnique<T>(arena, n);
}
template<typename T, typename... Args>
typename EnableIf<IsBoundedArray<T>::value>::type
MakePersistentUnique(Args&&...) = delete;


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_CORE_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

