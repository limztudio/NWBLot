// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include <core/alloc/module.h>

#include <global/arena_object.h>

#include <gtest/gtest.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace __hidden_arena_object_construction_tests{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


using namespace NWB::Core;
using namespace NWB::Core::Alloc;

struct ConstructionFailure{};

struct ConstructionState{
    usize failAt = Limit<usize>::s_Max;
    usize constructors = 0u;
    usize completed = 0u;
    usize destructors = 0u;
    usize liveMembers = 0u;
    usize memberDestructors = 0u;
    usize destructionOrder[8u] = {};
    bool aligned = true;
};

template<typename Arena>
struct OwnedMember{
    ConstructionState& state;
    Vector<u8, Arena> bytes;

    OwnedMember(Arena& arena, ConstructionState& constructionState)
        : state(constructionState)
        , bytes(arena)
    {
        bytes.resize(73u, 29u);
        ++state.liveMembers;
    }
    ~OwnedMember(){
        --state.liveMembers;
        ++state.memberDestructors;
    }
};

template<typename Arena>
struct alignas(128u) ConstructionProbe{
    inline static thread_local Arena* s_Arena = nullptr;
    inline static thread_local ConstructionState* s_State = nullptr;

    OwnedMember<Arena> member;
    ConstructionState& state;
    usize index;

    ConstructionProbe()
        : ConstructionProbe(*s_Arena, *s_State)
    {}
    ConstructionProbe(Arena& arena, ConstructionState& constructionState)
        : member(arena, constructionState)
        , state(constructionState)
        , index(++state.constructors)
    {
        state.aligned = state.aligned && (reinterpret_cast<usize>(this) % alignof(ConstructionProbe) == 0u);
        if(index == state.failAt)
            throw ConstructionFailure{};
        ++state.completed;
    }
    ~ConstructionProbe(){
        state.destructionOrder[state.destructors++] = index;
    }
};

template<typename Arena>
class ActiveConstruction final : NoCopy{
public:
    ActiveConstruction(Arena& arena, ConstructionState& state)
        : m_previousArena(ConstructionProbe<Arena>::s_Arena)
        , m_previousState(ConstructionProbe<Arena>::s_State)
    {
        ConstructionProbe<Arena>::s_Arena = &arena;
        ConstructionProbe<Arena>::s_State = &state;
    }
    ~ActiveConstruction(){
        ConstructionProbe<Arena>::s_Arena = m_previousArena;
        ConstructionProbe<Arena>::s_State = m_previousState;
    }


private:
    Arena* m_previousArena;
    ConstructionState* m_previousState;
};

struct NestedConstruction{
    GlobalUniquePtr<ConstructionProbe<GlobalArena>> member;
    bool& destroyed;

    NestedConstruction(GlobalArena& arena, ConstructionState& state, bool& destructorCalled)
        : member(MakeGlobalUnique<ConstructionProbe<GlobalArena>>(arena, arena, state))
        , destroyed(destructorCalled)
    {
        throw ConstructionFailure{};
    }
    ~NestedConstruction(){ destroyed = true; }
};

struct RejectingArena{
    usize attempts = 0u;

    template<typename T>
    T* allocate(const usize count){
        static_cast<void>(count);
        ++attempts;
        return nullptr;
    }
    template<typename T>
    void deallocate(void* memory, const usize count){
        static_cast<void>(memory);
        static_cast<void>(count);
        ADD_FAILURE() << "A failed raw allocation must not be deallocated";
    }
};

template<typename Arena, typename Factory>
void VerifyConstructionFailure(Arena& arena, const usize failAt, Factory factory){
    ConstructionState state{ .failAt = failAt };
    ActiveConstruction<Arena> active(arena, state);
    const ArenaMemoryStats before = arena.memoryStats();
    EXPECT_THROW(
        {
            auto object = factory(state);
            EXPECT_TRUE(object);
        },
        ConstructionFailure
    );
    const ArenaMemoryStats after = arena.memoryStats();
    EXPECT_EQ(state.constructors, failAt);
    EXPECT_EQ(state.completed, failAt - 1u);
    EXPECT_EQ(state.destructors, failAt - 1u);
    EXPECT_EQ(state.liveMembers, 0u);
    EXPECT_EQ(state.memberDestructors, failAt);
    EXPECT_TRUE(state.aligned);
    for(usize index = 0u; index < state.destructors; ++index)
        EXPECT_EQ(state.destructionOrder[index], failAt - 1u - index);
    EXPECT_EQ(after.usedBytes, before.usedBytes);
    EXPECT_EQ(after.reservedBytes, before.reservedBytes);
    EXPECT_GT(after.allocationCount, before.allocationCount);
    EXPECT_EQ(after.allocationCount - before.allocationCount, after.deallocationCount - before.deallocationCount);
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


TEST(ArenaObjectConstruction, RawObjectFailureReleasesAlignedStorageAndConstructedMembers){
    GlobalArena arena(Name("tests/arena_object/raw_constructor_failure"));
    using Probe = ConstructionProbe<GlobalArena>;
    VerifyConstructionFailure(arena, 1u, [&](ConstructionState& state){
        return ArenaUniquePtr<Probe, GlobalArena>(NewArenaObject<Probe>(arena, arena, state), ArenaDeleter<Probe, GlobalArena>(arena));
    });
}

TEST(ArenaObjectConstruction, GenericUniqueObjectFailureReleasesAlignedStorageAndConstructedMembers){
    GlobalArena arena(Name("tests/arena_object/generic_constructor_failure"));
    VerifyConstructionFailure(arena, 1u, [&](ConstructionState& state){
        return MakeArenaUnique<ConstructionProbe<GlobalArena>>(arena, arena, state);
    });
}

TEST(ArenaObjectConstruction, GlobalUniqueObjectFailureReleasesAlignedStorageAndConstructedMembers){
    GlobalArena arena(Name("tests/arena_object/global_constructor_failure"));
    VerifyConstructionFailure(arena, 1u, [&](ConstructionState& state){
        return MakeGlobalUnique<ConstructionProbe<GlobalArena>>(arena, arena, state);
    });
}

TEST(ArenaObjectConstruction, PersistentUniqueObjectFailureReleasesAlignedStorageAndConstructedMembers){
    PersistentArena arena(Name("tests/arena_object/persistent_constructor_failure"), PersistentArena::StructureAlignedSize(16384u));
    VerifyConstructionFailure(arena, 1u, [&](ConstructionState& state){
        return MakePersistentUnique<ConstructionProbe<PersistentArena>>(arena, arena, state);
    });
}

TEST(ArenaObjectConstruction, GenericArrayFailureDestroysOnlyCompletedElementsAndReleasesStorage){
    GlobalArena arena(Name("tests/arena_object/generic_array_failure"));
    VerifyConstructionFailure(arena, 3u, [&](ConstructionState&){
        return MakeArenaUnique<ConstructionProbe<GlobalArena>[]>(arena, 4u);
    });
}

TEST(ArenaObjectConstruction, GlobalArrayFailureDestroysOnlyCompletedElementsAndReleasesStorage){
    GlobalArena arena(Name("tests/arena_object/global_array_failure"));
    VerifyConstructionFailure(arena, 3u, [&](ConstructionState&){
        return MakeGlobalUnique<ConstructionProbe<GlobalArena>[]>(arena, 4u);
    });
}

TEST(ArenaObjectConstruction, PersistentArrayFailureDestroysOnlyCompletedElementsAndReleasesStorage){
    PersistentArena arena(Name("tests/arena_object/persistent_array_failure"), PersistentArena::StructureAlignedSize(16384u));
    VerifyConstructionFailure(arena, 3u, [&](ConstructionState&){
        return MakePersistentUnique<ConstructionProbe<PersistentArena>[]>(arena, 4u);
    });
}

TEST(ArenaObjectConstruction, NestedFactorySuccessIsDestroyedWhenOuterConstructionThrows){
    GlobalArena arena(Name("tests/arena_object/nested_constructor_failure"));
    ConstructionState state;
    bool outerDestroyed = false;
    const ArenaMemoryStats before = arena.memoryStats();
    EXPECT_THROW(
        {
            auto object = MakeGlobalUnique<NestedConstruction>(arena, arena, state, outerDestroyed);
            EXPECT_TRUE(object);
        },
        ConstructionFailure
    );
    const ArenaMemoryStats after = arena.memoryStats();
    EXPECT_FALSE(outerDestroyed);
    EXPECT_EQ(state.constructors, 1u);
    EXPECT_EQ(state.completed, 1u);
    EXPECT_EQ(state.destructors, 1u);
    EXPECT_EQ(state.memberDestructors, 1u);
    EXPECT_EQ(state.liveMembers, 0u);
    EXPECT_EQ(after.usedBytes, before.usedBytes);
    EXPECT_EQ(after.allocationCount - before.allocationCount, after.deallocationCount - before.deallocationCount);
}

TEST(ArenaObjectConstruction, SuccessfulObjectsAndArraysTransferStorageToTheirOwners){
    GlobalArena arena(Name("tests/arena_object/successful_construction"));
    ConstructionState state;
    ActiveConstruction<GlobalArena> active(arena, state);
    const ArenaMemoryStats before = arena.memoryStats();
    {
        auto* const raw = NewArenaObject<ConstructionProbe<GlobalArena>>(arena, arena, state);
        ASSERT_NE(raw, nullptr);
        DestroyArenaObject(arena, raw);
        auto object = MakeGlobalUnique<ConstructionProbe<GlobalArena>>(arena, arena, state);
        auto array = MakeGlobalUnique<ConstructionProbe<GlobalArena>[]>(arena, 4u);
        ASSERT_TRUE(object);
        ASSERT_TRUE(array);
        EXPECT_EQ(state.completed, 6u);
        EXPECT_EQ(state.destructors, 1u);
        EXPECT_EQ(state.liveMembers, 5u);
        EXPECT_TRUE(state.aligned);
        EXPECT_GT(arena.memoryStats().usedBytes, before.usedBytes);
    }
    const ArenaMemoryStats after = arena.memoryStats();
    EXPECT_EQ(state.destructors, 6u);
    EXPECT_EQ(state.memberDestructors, 6u);
    EXPECT_EQ(state.liveMembers, 0u);
    EXPECT_EQ(after.usedBytes, before.usedBytes);
    EXPECT_EQ(after.allocationCount - before.allocationCount, after.deallocationCount - before.deallocationCount);
}

TEST(ArenaObjectConstruction, FailedRawAllocationsNeverRunConstructorsOrDestructors){
    GlobalArena memberArena(Name("tests/arena_object/rejected_construction"));
    RejectingArena arena;
    ConstructionState state;
    ActiveConstruction<GlobalArena> active(memberArena, state);
    using Probe = ConstructionProbe<GlobalArena>;
    EXPECT_EQ(NewArenaObject<Probe>(arena, memberArena, state), nullptr);
    EXPECT_FALSE(MakeArenaUnique<Probe>(arena, memberArena, state));
    EXPECT_FALSE(MakeArenaUnique<Probe[]>(arena, 4u));
    EXPECT_EQ(arena.attempts, 3u);
    EXPECT_EQ(state.constructors, 0u);
    EXPECT_EQ(state.destructors, 0u);
    EXPECT_EQ(state.memberDestructors, 0u);
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

