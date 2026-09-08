// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#pragma once


#include "global.h"
#include "core.h"

#include <global/arena_base.h>
#include <global/arena_object.h>
#include <global/sync.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_ALLOC_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


class PersistentArena : public ::ArenaBaseT<PersistentArena>{
public:
    using Base = ::ArenaBaseT<PersistentArena>;


public:
    using Base::allocate;
    using Base::deallocate;


public:
    [[nodiscard]] static usize StructureAlignedSize(usize byte);
    [[nodiscard]] static usize StructureAlignedSize(usize byte, usize align);


public:
    PersistentArena(const Name& allocationLog, usize maxSize);
    ~PersistentArena();


public:
    [[nodiscard]] void* allocate(usize align, usize size);
    [[nodiscard]] void* reallocate(void* p, usize align, usize size);
    void deallocate(void* p, usize align, usize size);


private:
    [[nodiscard]] void* allocateLocked(usize align, usize size, void*& outBlock)noexcept;
    void deallocateBlockLocked(void* block)noexcept;


private:
    // Individual fixed-pool operations are serialized. The caller owns arena and allocated-object lifetime synchronization.
    MallocMutex m_mutex;
    void* m_bucket = nullptr;
    usize m_maxSize = 0u;
    void* m_freeHead = nullptr;
};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_ALLOC_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_CORE_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


template<typename T>
using PersistentUniquePtr = ::ArenaUniquePtr<T, Alloc::PersistentArena>;

template<typename T, typename... Args>
inline typename EnableIf<!IsArray<T>::value, PersistentUniquePtr<T>>::type MakePersistentUnique(NWB::Core::Alloc::PersistentArena& arena, Args&&... args){
    return ::MakeArenaUnique<T>(arena, Forward<Args>(args)...);
}
template<typename T>
inline typename EnableIf<IsUnboundedArray<T>::value, PersistentUniquePtr<T>>::type MakePersistentUnique(NWB::Core::Alloc::PersistentArena& arena, usize n){
    return ::MakeArenaUnique<T>(arena, n);
}
template<typename T, typename... Args>
typename EnableIf<IsBoundedArray<T>::value>::type
MakePersistentUnique(Args&&...) = delete;


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_CORE_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

