// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#pragma once


#include <impl/ecs_ui/toolkit/input/context_staging.adhd.h>
#include <impl/ecs_ui/toolkit/widgets/edit_box_snapshot_builder.adhd.h>
#include <tests/common/gtest_sdk.h>
#include <tests/common/memory_sdk.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_BEGIN
namespace Tests::UiRootFailure{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


class ToggleRootArena final : public ArenaBase{
public:
    ToggleRootArena() : ArenaBase(Name("tests/ui/snapshot_allocation_failure")), m_backing(Name("tests/ui/snapshot_backing")){}
    void rejectAllocations()noexcept{ m_reject = true; }
    void* allocate(usize align, usize size)override{ return m_reject ? nullptr : m_backing.allocate(align, size); }
    void* reallocate(void* memory, usize align, usize size)override{ return m_reject ? nullptr : m_backing.reallocate(memory, align, size); }
    void deallocate(void* memory, usize align, usize size)noexcept override{ m_backing.deallocate(memory, align, size); }


private:
    Core::Alloc::GlobalArena m_backing;
    b8 m_reject = false;
};

[[nodiscard]] inline b8 AllocationFailurePreservesEditSnapshot(){
    HeapStorage storage;
    ToggleRootArena arena;
    AString<Core::Alloc::GlobalArena> display(storage.arena);
    display.assign("old");
    Vector<Impl::Ui::EditCaretMapping, ToggleRootArena> mapping(arena);
    const Array<usize, 4u> boundaries{ 0u, 1u, 2u, 3u };
    Impl::Ui::EditCompositionView composition;
    Impl::Ui::EditBoxRange selection;
    Impl::Ui::EditBoxRange preedit;
    Impl::Ui::EditBoxRange replacement;
    usize caret = 7u;
    usize committedBytes = 8u;
    u64 revision = 9u;
    Impl::Ui::EditTextMode::Enum mode = Impl::Ui::EditTextMode::SingleLine;
    b8 composing = false;
    b8 ready = true;
    arena.rejectAllocations();
    const b8 propagated = static_cast<b8>(ThrowsException<AllocationException>([&](){
        return Impl::Ui::EditBoxSnapshotBuilder().commit("abc", Span<const usize>(boundaries), composition, mode, 0u, 0u, 0u, 1u,
            display, mapping, selection, preedit, replacement, caret, committedBytes, revision, mode, composing, ready);
    }));
    return propagated && display == "old" && mapping.empty() && caret == 7u && committedBytes == 8u && revision == 9u && ready;
}

[[nodiscard]] inline b8 DisplayAllocationFailurePreservesEditSnapshot(){
    HeapStorage storage;
    ToggleRootArena arena;
    AString<ToggleRootArena> display(arena);
    display.assign("old");
    display.reserve(64u);
    AString<Core::Alloc::GlobalArena> input(storage.arena);
    input.assign(display.capacity() + 1u, 'a');
    const Array<usize, 2u> boundaries{ 0u, input.size() };
    Vector<Impl::Ui::EditCaretMapping, Core::Alloc::GlobalArena> mapping(storage.arena);
    arena.rejectAllocations();
    Impl::Ui::EditCompositionView composition;
    Impl::Ui::EditBoxRange selection;
    Impl::Ui::EditBoxRange preedit;
    Impl::Ui::EditBoxRange replacement;
    usize caret = 7u;
    usize committedBytes = 8u;
    u64 revision = 9u;
    Impl::Ui::EditTextMode::Enum mode = Impl::Ui::EditTextMode::SingleLine;
    b8 composing = false;
    b8 ready = true;
    const b8 propagated = static_cast<b8>(ThrowsException<AllocationException>([&](){
        return Impl::Ui::EditBoxSnapshotBuilder().commit(AStringView(input), Span<const usize>(boundaries), composition, mode, 0u, 0u, 0u, 1u,
            display, mapping, selection, preedit, replacement, caret, committedBytes, revision, mode, composing, ready);
    }));
    return propagated && display == "old" && mapping.empty() && caret == 7u && committedBytes == 8u && revision == 9u && ready;
}

[[nodiscard]] inline b8 AllocationFailurePreservesRootState(){
    ToggleRootArena arena;
    Vector<Impl::Ui::WidgetId, ToggleRootArena> scopes(arena);
    Impl::Ui::WidgetRoot requested;
    requested.value = 12u;
    requested.generation = 1u;
    Impl::Ui::WidgetRoot stored;
    stored.value = 37u;
    stored.generation = 9u;
    b8 active = false;
    arena.rejectAllocations();
    const b8 propagated = static_cast<b8>(ThrowsException<AllocationException>([&](){
        return Impl::Ui::TryBeginContextRoot(false, 1u, false, requested, stored, active, scopes);
    }));
    return propagated && stored.value == 37u && stored.generation == 9u && !active && scopes.empty();
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};
NWB_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

