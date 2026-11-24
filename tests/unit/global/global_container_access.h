// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#pragma once


#include <engine_namespace.h>

#include <global/auto_registration.h>
#include <global/containers.h>
#include <tests/common/gtest_sdk.h>
#include <tests/common/memory_sdk.h>
#include <tests/unit/global/name_case_types.adhd.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_BEGIN
namespace Tests::GlobalCases{

[[nodiscard]] inline HeapStorage& PropagationDefault(){
    static HeapStorage s_Storage(s_GlobalArenaCaseAllocatorPropagationDefault);
    return s_Storage;
}

[[nodiscard]] inline Core::Alloc::GlobalArena& DefaultContainerOwner(){ return PropagationDefault().arena; }

template<typename Container, typename Element>
using ContainerInstance = Container;

using RegularAllocator = ContainerDetail::ArenaAllocator<u32, Core::Alloc::GlobalArena>;
using AlignedAllocator = ContainerDetail::ArenaCacheAlignedAllocator<u32, Core::Alloc::GlobalArena>;
using DefaultAllocator = ContainerDetail::DefaultArenaAllocator<u32, Core::Alloc::GlobalArena, DefaultContainerOwner>;
using RegularVector = ::Vector<u32, Core::Alloc::GlobalArena>;
using ConcurrentVector = ::ParallelVector<u32, Core::Alloc::GlobalArena>;
using DefaultVector = ::DefaultVector<u32, Core::Alloc::GlobalArena, DefaultContainerOwner>;
using RegularText = ::AString<Core::Alloc::GlobalArena>;
using DefaultText = ::DefaultAString<Core::Alloc::GlobalArena, DefaultContainerOwner>;
using OwnerMap = ::Map<u32, u32, Core::Alloc::GlobalArena>;
using OwnerHashMap = ::HashMap<u32, u32, Core::Alloc::GlobalArena>;
using OwnerHashSet = ::HashSet<u32, Core::Alloc::GlobalArena>;
using RegistrationQueue = ::AutoRegistrationQueue<u32, Core::Alloc::GlobalArena>;

[[nodiscard]] inline HeapStorage& SelectStorage(bool foreign, HeapStorage& first, HeapStorage& other)noexcept{ return foreign ? other : first; }
template<typename Allocator>
[[nodiscard]] bool AllocatorUsesStorage(const Allocator& allocator, const HeapStorage& storage){ return allocator.arenaPtr() == &storage.arena; }
template<typename T>
[[nodiscard]] T MakeContainer(HeapStorage& owner){ return T(typename T::allocator_type(owner.arena)); }
template<typename T>
[[nodiscard]] T MoveContainer(T& source){ return T(Move(source)); }
template<typename T>
[[nodiscard]] T MoveContainerTo(T& source, HeapStorage& target){ return T(Move(source), typename T::allocator_type(target.arena)); }
template<typename T, typename... Args>
[[nodiscard]] b8 InsertEntry(T& container, Args... args){ return container.emplace(args...).second; }
template<typename Set>
[[nodiscard]] b8 InsertKey(Set& set, u32 key){ return set.insert(key).second; }
template<typename Map>
[[nodiscard]] const u32& MappedValue(const Map& map, u32 key){ return map.find(key)->second; }
template<typename Set>
[[nodiscard]] const u32& FoundKey(const Set& set, u32 key){ return *set.find(key); }

template<usize N>
inline i32 CopyBounded(Array<char, N>& out, const char* text){ return FOUNDATION_STRCPY(out.data(), N, text); }
template<usize N>
inline i32 AppendBounded(Array<char, N>& out, const char* text){ return FOUNDATION_STRCAT(out.data(), N, text); }

};
NWB_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#if !defined(_MSC_VER)
#define NWB_GLOBAL_PORTABLE_CRT_CASE(Suite, Case, Fn) NWB_GTEST_CASE(Suite, Case, Fn)
#else
#define NWB_GLOBAL_PORTABLE_CRT_CASE(Suite, Case, Fn)
#endif


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

