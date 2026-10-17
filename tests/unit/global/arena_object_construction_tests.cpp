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


public:
    OwnedMember(Arena& arena, ConstructionState& constructionState)
        : state(constructionState)
        , bytes(arena)
    {
        bytes.resize(73u, 29u);
        ++state.liveMembers;
    }
    ~OwnedMember()noexcept{
        --state.liveMembers;
        ++state.memberDestructors;
    }
};

template<typename Arena>
struct alignas(128u) ConstructionProbe{
    inline static thread_local Arena* s_Arena = nullptr;
    inline static thread_local ConstructionState* s_State = nullptr;


public:
    OwnedMember<Arena> member;
    ConstructionState& state;
    usize index;


public:
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
    ~ConstructionProbe()noexcept{
        state.destructionOrder[state.destructors++] = index;
    }
};

template<typename Arena>
class ActiveConstruction final : NoCopy{
public:
    ActiveConstruction(Arena& arena, ConstructionState& state)noexcept
        : m_previousArena(ConstructionProbe<Arena>::s_Arena)
        , m_previousState(ConstructionProbe<Arena>::s_State)
    {
        ConstructionProbe<Arena>::s_Arena = &arena;
        ConstructionProbe<Arena>::s_State = &state;
    }
    ~ActiveConstruction()noexcept{
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


public:
    NestedConstruction(GlobalArena& arena, ConstructionState& state, bool& destructorCalled)
        : member(MakeGlobalUnique<ConstructionProbe<GlobalArena>>(arena, arena, state))
        , destroyed(destructorCalled)
    {
        throw ConstructionFailure{};
    }
    ~NestedConstruction()noexcept{ destroyed = true; }
};

struct RejectingArena{
    usize attempts = 0u;


public:
    template<typename T>
    T* allocate(const usize count)noexcept{
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

struct ArenaPlacementProbe{
    inline static usize s_PlacementCalls = 0u;
    inline static usize s_Constructors = 0u;
    inline static usize s_Destructors = 0u;


public:
    static void* operator new(usize, void*)noexcept{
        ++s_PlacementCalls;
        return nullptr;
    }
    static void* operator new[](usize, void*)noexcept{
        ++s_PlacementCalls;
        return nullptr;
    }


public:
    ArenaPlacementProbe()noexcept{ ++s_Constructors; }
    ~ArenaPlacementProbe()noexcept{ ++s_Destructors; }
};

template<typename Arena>
struct ThrowingConstructionArgument{
    Arena& arena;
    ConstructionState& state;


public:
    ~ThrowingConstructionArgument()noexcept(false){ throw ConstructionFailure{}; }
};

template<typename Arena>
struct ConstructionArgumentSource{
    Arena& arena;
    ConstructionState& state;


public:
    operator ThrowingConstructionArgument<Arena>()const noexcept{ return { arena, state }; }
};

template<typename Arena>
struct ArgumentCleanupProbe{
    OwnedMember<Arena> member;


public:
    explicit ArgumentCleanupProbe(const ThrowingConstructionArgument<Arena>& argument)
        : member(argument.arena, argument.state)
    {}
    ~ArgumentCleanupProbe()noexcept = default;
};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


template<typename Arena>
struct PrivateDestructorProbe{
    friend struct ::ArenaDeleter<PrivateDestructorProbe<Arena>, Arena>;


public:
    OwnedMember<Arena> member;
    bool* destroyed;


public:
    explicit PrivateDestructorProbe(const ThrowingConstructionArgument<Arena>& argument)
        : member(argument.arena, argument.state)
        , destroyed(nullptr)
    {}
    PrivateDestructorProbe(Arena& arena, ConstructionState& state, bool& wasDestroyed)
        : member(arena, state)
        , destroyed(&wasDestroyed)
    {}
private:
    ~PrivateDestructorProbe()noexcept{
        if(destroyed)
            *destroyed = true;
    }
};

template<typename Arena>
struct OperatorPrivateDestructorProbe{
    friend void ::ArenaDeleter<OperatorPrivateDestructorProbe<Arena>, Arena>::operator()(OperatorPrivateDestructorProbe<Arena>*)const noexcept;


public:
    OwnedMember<Arena> member;


public:
    explicit OperatorPrivateDestructorProbe(const ThrowingConstructionArgument<Arena>& argument)
        : member(argument.arena, argument.state)
    {}
    OperatorPrivateDestructorProbe(Arena& arena, ConstructionState& state)
        : member(arena, state)
    {}
private:
    ~OperatorPrivateDestructorProbe()noexcept = default;
};

template<typename Arena>
struct PrivateConstructorProbe{
    friend PrivateConstructorProbe<Arena>* ::NewArenaObject<PrivateConstructorProbe<Arena>, Arena, bool&>(Arena&, bool&);


public:
    bool& alive;


private:
    explicit PrivateConstructorProbe(bool& wasConstructed)noexcept
        : alive(wasConstructed)
    { alive = true; }
public:
    ~PrivateConstructorProbe()noexcept{ alive = false; }
};

template<typename Arena>
struct RawPrivateDestructorProbe{
    friend void ::DestroyArenaObject<RawPrivateDestructorProbe<Arena>, Arena>(Arena&, RawPrivateDestructorProbe<Arena>*);


public:
    bool& destroyed;


public:
    explicit RawPrivateDestructorProbe(bool& wasDestroyed)noexcept
        : destroyed(wasDestroyed)
    {}
private:
    ~RawPrivateDestructorProbe()noexcept{ destroyed = true; }
};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


template<typename Arena, typename Factory>
void VerifyConstructionFailure(Arena& arena, const usize failAt, Factory factory){
    ConstructionState state{ .failAt = failAt };
    ActiveConstruction<Arena> active(arena, state);
    const ArenaMemoryStats before = arena.memoryStats();
    EXPECT_THROW(EXPECT_TRUE(factory(state)), ConstructionFailure);
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
    }
    );
}

TEST(ArenaObjectConstruction, GenericUniqueObjectFailureReleasesAlignedStorageAndConstructedMembers){
    GlobalArena arena(Name("tests/arena_object/generic_constructor_failure"));
    VerifyConstructionFailure(arena, 1u, [&](ConstructionState& state){
        return MakeArenaUnique<ConstructionProbe<GlobalArena>>(arena, arena, state);
    }
    );
}

TEST(ArenaObjectConstruction, GlobalUniqueObjectFailureReleasesAlignedStorageAndConstructedMembers){
    GlobalArena arena(Name("tests/arena_object/global_constructor_failure"));
    VerifyConstructionFailure(arena, 1u, [&](ConstructionState& state){
        return MakeGlobalUnique<ConstructionProbe<GlobalArena>>(arena, arena, state);
    }
    );
}

TEST(ArenaObjectConstruction, PersistentUniqueObjectFailureReleasesAlignedStorageAndConstructedMembers){
    PersistentArena arena(Name("tests/arena_object/persistent_constructor_failure"), PersistentArena::StructureAlignedSize(16384u));
    VerifyConstructionFailure(arena, 1u, [&](ConstructionState& state){
        return MakePersistentUnique<ConstructionProbe<PersistentArena>>(arena, arena, state);
    }
    );
}

TEST(ArenaObjectConstruction, GenericArrayFailureDestroysOnlyCompletedElementsAndReleasesStorage){
    GlobalArena arena(Name("tests/arena_object/generic_array_failure"));
    VerifyConstructionFailure(arena, 3u, [&](ConstructionState&){
        return MakeArenaUnique<ConstructionProbe<GlobalArena>[]>(arena, 4u);
    }
    );
}

TEST(ArenaObjectConstruction, GlobalArrayFailureDestroysOnlyCompletedElementsAndReleasesStorage){
    GlobalArena arena(Name("tests/arena_object/global_array_failure"));
    VerifyConstructionFailure(arena, 3u, [&](ConstructionState&){
        return MakeGlobalUnique<ConstructionProbe<GlobalArena>[]>(arena, 4u);
    }
    );
}

TEST(ArenaObjectConstruction, PersistentArrayFailureDestroysOnlyCompletedElementsAndReleasesStorage){
    PersistentArena arena(Name("tests/arena_object/persistent_array_failure"), PersistentArena::StructureAlignedSize(16384u));
    VerifyConstructionFailure(arena, 3u, [&](ConstructionState&){
        return MakePersistentUnique<ConstructionProbe<PersistentArena>[]>(arena, 4u);
    }
    );
}

TEST(ArenaObjectConstruction, NestedFactorySuccessIsDestroyedWhenOuterConstructionThrows){
    GlobalArena arena(Name("tests/arena_object/nested_constructor_failure"));
    ConstructionState state;
    bool outerDestroyed = false;
    const ArenaMemoryStats before = arena.memoryStats();
    EXPECT_THROW(EXPECT_TRUE(MakeGlobalUnique<NestedConstruction>(arena, arena, state, outerDestroyed)), ConstructionFailure);
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

TEST(ArenaObjectConstruction, ArenaFactoriesRetainStorageIdentityAndBypassClassAllocationHooks){
    GlobalArena global(Name("tests/arena_object/canonical_global_placement"));
    PersistentArena persistent(Name("tests/arena_object/canonical_persistent_placement"), PersistentArena::StructureAlignedSize(4096u));
    ArenaPlacementProbe::s_PlacementCalls = 0u;
    ArenaPlacementProbe::s_Constructors = 0u;
    ArenaPlacementProbe::s_Destructors = 0u;
    const ArenaMemoryStats globalBefore = global.memoryStats();
    const ArenaMemoryStats persistentBefore = persistent.memoryStats();
    {
        auto globalObject = MakeGlobalUnique<ArenaPlacementProbe>(global);
        auto persistentObject = MakePersistentUnique<ArenaPlacementProbe>(persistent);
        auto globalArray = MakeGlobalUnique<ArenaPlacementProbe[]>(global, 3u);
        auto persistentArray = MakePersistentUnique<ArenaPlacementProbe[]>(persistent, 3u);
        EXPECT_TRUE(globalObject);
        EXPECT_TRUE(persistentObject);
        EXPECT_TRUE(globalArray);
        EXPECT_TRUE(persistentArray);
        ArenaPlacementProbe* const globalRaw = NewArenaObject<ArenaPlacementProbe>(global);
        ArenaPlacementProbe* const persistentRaw = NewArenaObject<ArenaPlacementProbe>(persistent);
        EXPECT_NE(globalRaw, nullptr);
        EXPECT_NE(persistentRaw, nullptr);
        DestroyArenaObject(global, globalRaw);
        DestroyArenaObject(persistent, persistentRaw);
    }
    EXPECT_EQ(ArenaPlacementProbe::s_PlacementCalls, 0u);
    EXPECT_EQ(ArenaPlacementProbe::s_Constructors, 10u);
    EXPECT_EQ(ArenaPlacementProbe::s_Destructors, 10u);
    const ArenaMemoryStats globalAfter = global.memoryStats();
    const ArenaMemoryStats persistentAfter = persistent.memoryStats();
    EXPECT_EQ(globalAfter.usedBytes, globalBefore.usedBytes);
    EXPECT_EQ(globalAfter.reservedBytes, globalBefore.reservedBytes);
    EXPECT_EQ(persistentAfter.usedBytes, persistentBefore.usedBytes);
    EXPECT_EQ(persistentAfter.reservedBytes, persistentBefore.reservedBytes);
    EXPECT_EQ(globalAfter.allocationCount - globalBefore.allocationCount, globalAfter.deallocationCount - globalBefore.deallocationCount);
    EXPECT_EQ(persistentAfter.allocationCount - persistentBefore.allocationCount, persistentAfter.deallocationCount - persistentBefore.deallocationCount);
}

TEST(ArenaObjectConstruction, ArgumentTemporaryFailureDestroysCompletedMembersBeforeReleasingArenaStorage){
    GlobalArena global(Name("tests/arena_object/global_argument_cleanup_failure"));
    PersistentArena persistent(Name("tests/arena_object/persistent_argument_cleanup_failure"), PersistentArena::StructureAlignedSize(4096u));
    ConstructionState globalState;
    ConstructionState persistentState;
    const ConstructionArgumentSource<GlobalArena> globalArgument{ global, globalState };
    const ConstructionArgumentSource<PersistentArena> persistentArgument{ persistent, persistentState };
    const ArenaMemoryStats globalBefore = global.memoryStats();
    const ArenaMemoryStats persistentBefore = persistent.memoryStats();
    EXPECT_THROW(EXPECT_TRUE(MakeGlobalUnique<ArgumentCleanupProbe<GlobalArena>>(global, globalArgument)), ConstructionFailure);
    EXPECT_THROW(EXPECT_TRUE(MakePersistentUnique<ArgumentCleanupProbe<PersistentArena>>(persistent, persistentArgument)), ConstructionFailure);
    EXPECT_EQ(globalState.liveMembers, 0u);
    EXPECT_EQ(globalState.memberDestructors, 1u);
    EXPECT_EQ(persistentState.liveMembers, 0u);
    EXPECT_EQ(persistentState.memberDestructors, 1u);
    const ArenaMemoryStats globalAfter = global.memoryStats();
    const ArenaMemoryStats persistentAfter = persistent.memoryStats();
    EXPECT_EQ(globalAfter.usedBytes, globalBefore.usedBytes);
    EXPECT_EQ(globalAfter.reservedBytes, globalBefore.reservedBytes);
    EXPECT_EQ(persistentAfter.usedBytes, persistentBefore.usedBytes);
    EXPECT_EQ(persistentAfter.reservedBytes, persistentBefore.reservedBytes);
    EXPECT_EQ(globalAfter.allocationCount - globalBefore.allocationCount, globalAfter.deallocationCount - globalBefore.deallocationCount);
    EXPECT_EQ(persistentAfter.allocationCount - persistentBefore.allocationCount, persistentAfter.deallocationCount - persistentBefore.deallocationCount);
}

TEST(ArenaObjectConstruction, CanonicalPrivateDestructorsRetainOwningRollbackAndRawConstructionAccess){
    GlobalArena global(Name("tests/arena_object/private_global_destructor"));
    PersistentArena persistent(Name("tests/arena_object/private_persistent_destructor"), PersistentArena::StructureAlignedSize(4096u));
    ConstructionState globalState;
    ConstructionState persistentState;
    const ConstructionArgumentSource<GlobalArena> globalArgument{ global, globalState };
    const ConstructionArgumentSource<PersistentArena> persistentArgument{ persistent, persistentState };
    const ArenaMemoryStats globalBefore = global.memoryStats();
    const ArenaMemoryStats persistentBefore = persistent.memoryStats();
    EXPECT_THROW(EXPECT_TRUE(MakeGlobalUnique<PrivateDestructorProbe<GlobalArena>>(global, globalArgument)), ConstructionFailure);
    EXPECT_THROW(EXPECT_TRUE(MakePersistentUnique<PrivateDestructorProbe<PersistentArena>>(persistent, persistentArgument)), ConstructionFailure);
    EXPECT_EQ(globalState.liveMembers, 0u);
    EXPECT_EQ(globalState.memberDestructors, 1u);
    EXPECT_EQ(persistentState.liveMembers, 0u);
    EXPECT_EQ(persistentState.memberDestructors, 1u);
    bool globalDestroyed = false;
    bool persistentDestroyed = false;
    {
        auto globalOwner = MakeGlobalUnique<PrivateDestructorProbe<GlobalArena>>(global, global, globalState, globalDestroyed);
        auto persistentOwner = MakePersistentUnique<PrivateDestructorProbe<PersistentArena>>(persistent, persistent, persistentState, persistentDestroyed);
        EXPECT_TRUE(globalOwner);
        EXPECT_TRUE(persistentOwner);
    }
    EXPECT_TRUE(globalDestroyed);
    EXPECT_TRUE(persistentDestroyed);
    bool rawDestroyed = false;
    auto* const raw = NewArenaObject<RawPrivateDestructorProbe<GlobalArena>>(global, rawDestroyed);
    ASSERT_NE(raw, nullptr);
    DestroyArenaObject(global, raw);
    EXPECT_TRUE(rawDestroyed);
    const ArenaMemoryStats globalAfter = global.memoryStats();
    const ArenaMemoryStats persistentAfter = persistent.memoryStats();
    EXPECT_EQ(globalAfter.usedBytes, globalBefore.usedBytes);
    EXPECT_EQ(globalAfter.reservedBytes, globalBefore.reservedBytes);
    EXPECT_EQ(persistentAfter.usedBytes, persistentBefore.usedBytes);
    EXPECT_EQ(persistentAfter.reservedBytes, persistentBefore.reservedBytes);
    EXPECT_EQ(globalAfter.allocationCount - globalBefore.allocationCount, globalAfter.deallocationCount - globalBefore.deallocationCount);
    EXPECT_EQ(persistentAfter.allocationCount - persistentBefore.allocationCount, persistentAfter.deallocationCount - persistentBefore.deallocationCount);
}

TEST(ArenaObjectConstruction, IndividualDeleterFriendRetainsCompletedRollbackAndNormalOwningDestruction){
    GlobalArena global(Name("tests/arena_object/operator_friend_global_destructor"));
    PersistentArena persistent(Name("tests/arena_object/operator_friend_persistent_destructor"), PersistentArena::StructureAlignedSize(4096u));
    ConstructionState globalState;
    ConstructionState persistentState;
    const ConstructionArgumentSource<GlobalArena> globalArgument{ global, globalState };
    const ConstructionArgumentSource<PersistentArena> persistentArgument{ persistent, persistentState };
    const ArenaMemoryStats globalBefore = global.memoryStats();
    const ArenaMemoryStats persistentBefore = persistent.memoryStats();
    EXPECT_THROW(EXPECT_TRUE(MakeGlobalUnique<OperatorPrivateDestructorProbe<GlobalArena>>(global, globalArgument)), ConstructionFailure);
    EXPECT_THROW(EXPECT_TRUE(MakePersistentUnique<OperatorPrivateDestructorProbe<PersistentArena>>(persistent, persistentArgument)), ConstructionFailure);
    EXPECT_EQ(globalState.liveMembers, 0u);
    EXPECT_EQ(globalState.memberDestructors, 1u);
    EXPECT_EQ(persistentState.liveMembers, 0u);
    EXPECT_EQ(persistentState.memberDestructors, 1u);
    {
        auto globalOwner = MakeGlobalUnique<OperatorPrivateDestructorProbe<GlobalArena>>(global, global, globalState);
        auto persistentOwner = MakePersistentUnique<OperatorPrivateDestructorProbe<PersistentArena>>(persistent, persistent, persistentState);
        EXPECT_TRUE(globalOwner);
        EXPECT_TRUE(persistentOwner);
    }
    EXPECT_EQ(globalState.liveMembers, 0u);
    EXPECT_EQ(globalState.memberDestructors, 2u);
    EXPECT_EQ(persistentState.liveMembers, 0u);
    EXPECT_EQ(persistentState.memberDestructors, 2u);
    bool globalPrivateAlive = false;
    bool persistentPrivateAlive = false;
    {
        auto globalPrivate = MakeGlobalUnique<PrivateConstructorProbe<GlobalArena>>(global, globalPrivateAlive);
        auto persistentPrivate = MakePersistentUnique<PrivateConstructorProbe<PersistentArena>>(persistent, persistentPrivateAlive);
        EXPECT_TRUE(globalPrivate);
        EXPECT_TRUE(persistentPrivate);
        EXPECT_TRUE(globalPrivateAlive);
        EXPECT_TRUE(persistentPrivateAlive);
    }
    EXPECT_FALSE(globalPrivateAlive);
    EXPECT_FALSE(persistentPrivateAlive);
    const ArenaMemoryStats globalAfter = global.memoryStats();
    const ArenaMemoryStats persistentAfter = persistent.memoryStats();
    EXPECT_EQ(globalAfter.usedBytes, globalBefore.usedBytes);
    EXPECT_EQ(globalAfter.reservedBytes, globalBefore.reservedBytes);
    EXPECT_EQ(persistentAfter.usedBytes, persistentBefore.usedBytes);
    EXPECT_EQ(persistentAfter.reservedBytes, persistentBefore.reservedBytes);
    EXPECT_EQ(globalAfter.allocationCount - globalBefore.allocationCount, globalAfter.deallocationCount - globalBefore.deallocationCount);
    EXPECT_EQ(persistentAfter.allocationCount - persistentBefore.allocationCount, persistentAfter.deallocationCount - persistentBefore.deallocationCount);
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

