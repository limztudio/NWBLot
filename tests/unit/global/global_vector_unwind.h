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
    void rejectAllocations()noexcept{ m_reject = true; }
    void* allocate(const usize align, const usize size)override{
        return m_reject ? nullptr : m_storage.arena.allocate(align, size);
    }
    void* reallocate(void* const memory, const usize align, const usize size)override{
        return m_reject ? nullptr : m_storage.arena.reallocate(memory, align, size);
    }
    void deallocate(void* const memory, const usize align, const usize size)noexcept override{
        m_storage.arena.deallocate(memory, align, size);
    }


private:
    HeapStorage m_storage;
    b8 m_reject = false;
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
    arena.rejectAllocations();
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

