// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#pragma once


#include <core/alloc/adhd_arena.h>
#include <tests/common/gtest_sdk.h>
#include <tests/common/memory_sdk.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_BEGIN
namespace Tests::GlobalCases{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


class RejectingVectorArena final : public ArenaBase{
public:
    RejectingVectorArena() : ArenaBase(Name("tests/global/vector_allocation_failure")){}
    void* allocate([[maybe_unused]] usize align, [[maybe_unused]] usize size)override{ return nullptr; }
    void* reallocate([[maybe_unused]] void* memory, [[maybe_unused]] usize align, [[maybe_unused]] usize size)override{ return nullptr; }
    void deallocate([[maybe_unused]] void* memory, [[maybe_unused]] usize align, [[maybe_unused]] usize size)noexcept override{}
};

struct AllocationThrowingCopy{
    AllocationThrowingCopy() = default;
    AllocationThrowingCopy(const AllocationThrowingCopy&){ throw AllocationException(); }
    AllocationThrowingCopy& operator=(const AllocationThrowingCopy&){ throw AllocationException(); }
};

[[nodiscard]] inline b8 CopyVectorPropagatesAllocatorFailure(){
    RejectingVectorArena arena;
    Vector<u32, RejectingVectorArena> destination(arena);
    const Array<u32, 1u> source{ 1u };
    const b8 propagated = static_cast<b8>(ThrowsException<AllocationException>([&](){ return TryCopyVector(destination, Span<const u32>(source)); }));
    return propagated && destination.empty();
}

[[nodiscard]] inline b8 CopyVectorPropagatesAllocationException(){
    HeapStorage storage;
    Vector<AllocationThrowingCopy, Core::Alloc::GlobalArena> destination(storage.arena);
    const Array<AllocationThrowingCopy, 1u> source{};
    return static_cast<b8>(ThrowsException<AllocationException>([&](){ return TryCopyVector(destination, Span<const AllocationThrowingCopy>(source)); }));
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};
NWB_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

