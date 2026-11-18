// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#pragma once


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include <tests/common/memory_sdk.h>

#include <global/arena_c_allocator.h>
#include <global/exception.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_BEGIN
namespace Tests::AllocCases{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


template<typename Storage>
[[nodiscard]] RawAllocation AllocateTypedBytes(Storage& storage, const usize count){
    return { storage.arena.template allocate<u8>(count) };
}


template<typename Storage>
[[nodiscard]] RawAllocation AllocateCBytes(Storage& storage, const usize count){ return { static_cast<u8*>(AllocateArenaCMemory(storage.arena, count)) }; }

template<typename Storage>
[[nodiscard]] RawAllocation ResizeCBytes(Storage& storage, const RawAllocation block, const usize count){ return { static_cast<u8*>(ReallocateArenaCMemory(storage.arena, block.bytes, count)) }; }

template<typename Storage>
void FreeCBytes(Storage& storage, const RawAllocation block){ DeallocateArenaCMemory(storage.arena, block.bytes); }

template<typename Keys>
void SortAllocationKeys(Keys& keys){ Sort(keys.begin(), keys.end()); }

template<typename T>
using PersistentTestArray = Core::PersistentUniquePtr<T[]>;

template<typename T>
[[nodiscard]] auto MakePersistentTestArray(PersistentStorage& storage, const usize count){ return Core::MakePersistentUnique<T[]>(storage.arena, count); }

inline constexpr usize s_MaxAlignment = alignof(MaxAlign);

#if defined(NWB_DEBUG)
inline constexpr b8 s_DebugAllocator = true;
#else
inline constexpr b8 s_DebugAllocator = false;
#endif


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};
NWB_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

