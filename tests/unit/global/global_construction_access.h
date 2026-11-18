// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#pragma once


#include <engine_namespace.h>

#include <tests/common/memory_sdk.h>
#include <tests/unit/global/global_construction_state.adhd.h>
#include <global/arena_object.h>
#include <core/alloc/module.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#define NWB_EXPECT_CONSTRUCTION_FAILURE(...) EXPECT_THROW(static_cast<void>(__VA_ARGS__), ::NWB::Tests::GlobalCases::ConstructionFailure)


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_BEGIN
namespace Tests::GlobalCases::Construction{

using namespace Core;
using namespace Core::Alloc;

template<typename Arena>
struct OwnedMember{
public:
    ConstructionState& state;
    ::Vector<u8, Arena> bytes;


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
public:
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
        , index(state.construct(reinterpret_cast<usize>(this) % alignof(ConstructionProbe) == 0u))
    {}
    ~ConstructionProbe()noexcept{ state.destroy(index); }
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
public:
    GlobalUniquePtr<ConstructionProbe<GlobalArena>> member;
    b8& destroyed;


public:
    NestedConstruction(GlobalArena& arena, ConstructionState& state, b8& destructorCalled)
        : member(MakeGlobalUnique<ConstructionProbe<GlobalArena>>(arena, arena, state))
        , destroyed(destructorCalled)
    {
        throw ConstructionFailure{};
    }
    ~NestedConstruction()noexcept{ destroyed = true; }
};

struct RejectingArena{
public:
    usize attempts = 0u;
    usize deallocations = 0u;


public:
    template<typename T>
    T* allocate([[maybe_unused]] const usize count)noexcept{
        ++attempts;
        return nullptr;
    }

    template<typename T>
    void deallocate([[maybe_unused]] void* memory, [[maybe_unused]] const usize count){
        ++deallocations;
    }
};

struct ArenaPlacementProbe{
public:
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
public:
    Arena& arena;
    ConstructionState& state;


public:
    ~ThrowingConstructionArgument()noexcept(false){ throw ConstructionFailure{}; }
};

template<typename Arena>
struct ConstructionArgumentSource{
public:
    Arena& arena;
    ConstructionState& state;


public:
    operator ThrowingConstructionArgument<Arena>()const noexcept{ return { arena, state }; }
};

template<typename Arena>
struct ArgumentCleanupProbe{
public:
    OwnedMember<Arena> member;


public:
    explicit ArgumentCleanupProbe(const ThrowingConstructionArgument<Arena>& argument)
        : member(argument.arena, argument.state)
    {}
    ~ArgumentCleanupProbe()noexcept = default;
};

template<typename Arena>
struct PrivateDestructorProbe{
    friend struct ::ArenaDeleter<PrivateDestructorProbe<Arena>, Arena>;


public:
    OwnedMember<Arena> member;
    b8* destroyed;


public:
    explicit PrivateDestructorProbe(const ThrowingConstructionArgument<Arena>& argument)
        : member(argument.arena, argument.state)
        , destroyed(nullptr)
    {}
    PrivateDestructorProbe(Arena& arena, ConstructionState& state, b8& wasDestroyed)
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
    friend PrivateConstructorProbe<Arena>* ::NewArenaObject<PrivateConstructorProbe<Arena>, Arena, b8&>(Arena&, b8&);


public:
    b8& alive;


private:
    explicit PrivateConstructorProbe(b8& wasConstructed)noexcept
        : alive(wasConstructed)
    { alive = true; }


public:
    ~PrivateConstructorProbe()noexcept{ alive = false; }
};

template<typename Arena>
class RawPrivateDestructorProbe{
    template<typename Concrete, typename OwnerArena>
    friend void ::DestroyArenaObject(OwnerArena& arena, Concrete* p)noexcept(noexcept(p->~Concrete()) && noexcept(arena.template deallocate<Concrete>(p, 1)));


public:
    explicit RawPrivateDestructorProbe(b8& wasDestroyed)noexcept
        : destroyed(wasDestroyed)
    {}


private:
    ~RawPrivateDestructorProbe()noexcept{ destroyed = true; }


public:
    b8& destroyed;
};

struct OwnerReentryCallback{
public:
    NotNull<void (*)(NotNull<void*>)noexcept> function;
    NotNull<void*> context;
};

template<typename Owner>
struct ReentryOwners{
public:
    Owner& slot;
    Owner& replacement;
    Owner& sink;
};

template<typename Owner>
void RebindOwner(NotNull<void*> value)noexcept(noexcept(DeclVal<Owner&>() = Move(DeclVal<Owner&>()))){
    auto& owners = *static_cast<ReentryOwners<Owner>*>(value.get());
    owners.slot = Move(owners.replacement);
    owners.sink = Move(owners.slot);
}

struct PolymorphicAllocationState{
public:
    usize paddingDestructors = 0u;
    usize baseDestructors = 0u;
    usize concreteDestructors = 0u;
    const OwnerReentryCallback* reentry = nullptr;
};

struct PolymorphicAllocationPadding{
public:
    PolymorphicAllocationState& state;
    Array<usize, 3u> padding = {};


public:
    explicit PolymorphicAllocationPadding(PolymorphicAllocationState& value)noexcept
        : state(value)
    {}
    virtual ~PolymorphicAllocationPadding()noexcept{ ++state.paddingDestructors; }
};

struct PolymorphicAllocationBase{
public:
    PolymorphicAllocationState& state;


public:
    explicit PolymorphicAllocationBase(PolymorphicAllocationState& value)noexcept
        : state(value)
    {}
    virtual ~PolymorphicAllocationBase()noexcept{ ++state.baseDestructors; }
};

struct PolymorphicAllocationIntermediate : virtual PolymorphicAllocationBase{
public:
    explicit PolymorphicAllocationIntermediate(PolymorphicAllocationState& value)noexcept
        : PolymorphicAllocationBase(value)
    {}
    virtual ~PolymorphicAllocationIntermediate()noexcept override = default;
};

struct alignas(128u) PolymorphicAllocationProbe final : PolymorphicAllocationPadding, PolymorphicAllocationIntermediate{
public:
    explicit PolymorphicAllocationProbe(PolymorphicAllocationState& value)noexcept
        : PolymorphicAllocationBase(value)
        , PolymorphicAllocationPadding(value)
        , PolymorphicAllocationIntermediate(value)
    {}
    virtual ~PolymorphicAllocationProbe()noexcept override{
        auto& state = PolymorphicAllocationPadding::state;
        ++state.concreteDestructors;
        if(state.reentry)
            state.reentry->function.get()(state.reentry->context);
    }
};

struct ArrayReentryProbe{
public:
    usize& destroyed;
    const OwnerReentryCallback* reentry;


public:
    explicit ArrayReentryProbe(usize& value, const OwnerReentryCallback* callback = nullptr)noexcept
        : destroyed(value)
        , reentry(callback)
    {}
    ~ArrayReentryProbe()noexcept{
        ++destroyed;
        if(reentry)
            reentry->function.get()(reentry->context);
    }
};

template<typename S>
using Underlying = RemoveReference_T<decltype(DeclVal<S&>().arena)>;
template<typename A, typename Tag>
A& ProbeArgument(NWB::Tests::TestStorage<A, Tag>& storage)noexcept{ return storage.arena; }
template<typename T>
T&& ProbeArgument(T&& argument)noexcept{ return Forward<T>(argument); }

struct ScalarKind{ template<typename A> using Type = ConstructionProbe<A>; };
struct ProbeArrayKind{ template<typename A> using Type = ConstructionProbe<A>[]; };
struct NestedKind{ template<typename A> using Type = NestedConstruction; };
struct PlacementKind{ template<typename A> using Type = ArenaPlacementProbe; };
struct PlacementArrayKind{ template<typename A> using Type = ArenaPlacementProbe[]; };
struct ArgumentKind{ template<typename A> using Type = ArgumentCleanupProbe<A>; };
struct PrivateDtorKind{ template<typename A> using Type = PrivateDestructorProbe<A>; };
struct OperatorDtorKind{ template<typename A> using Type = OperatorPrivateDestructorProbe<A>; };
struct PrivateCtorKind{ template<typename A> using Type = PrivateConstructorProbe<A>; };
struct RawPrivateKind{ template<typename A> using Type = RawPrivateDestructorProbe<A>; };
struct PolyBaseKind{ template<typename A> using Type = PolymorphicAllocationBase; };
struct PolyConcreteKind{ template<typename A> using Type = PolymorphicAllocationProbe; };
struct PolyMiddleKind{ template<typename A> using Type = PolymorphicAllocationIntermediate; };
struct ConstPolyBaseKind{ template<typename A> using Type = const PolymorphicAllocationBase; };

template<typename Kind, typename S>
using ProbeType = typename Kind::template Type<Underlying<S>>;
template<typename Kind, typename S>
using ProbeOwner = ArenaUniquePtr<ProbeType<Kind, S>, Underlying<S>>;

template<typename S>
using ActiveProbe = ActiveConstruction<Underlying<S>>;

template<typename Kind, typename S, typename... Args>
[[nodiscard]] auto GenericObject(S& storage, Args&&... args){ return MakeArenaUnique<ProbeType<Kind, S>>(storage.arena, ProbeArgument(Forward<Args>(args))...); }

template<typename Kind, typename... Args>
[[nodiscard]] auto GlobalObject(NWB::Tests::HeapStorage& storage, Args&&... args){ return MakeGlobalUnique<ProbeType<Kind, NWB::Tests::HeapStorage>>(storage.arena, ProbeArgument(Forward<Args>(args))...); }

template<typename Kind, typename... Args>
[[nodiscard]] auto PersistentObject(NWB::Tests::PersistentStorage& storage, Args&&... args){ return MakePersistentUnique<ProbeType<Kind, NWB::Tests::PersistentStorage>>(storage.arena, ProbeArgument(Forward<Args>(args))...); }

template<typename Kind, typename S, typename... Args>
[[nodiscard]] auto RawObject(S& storage, Args&&... args){ return NewArenaObject<ProbeType<Kind, S>>(storage.arena, ProbeArgument(Forward<Args>(args))...); }

template<typename S, typename T>
void DeleteRawObject(S& storage, T* raw){ DestroyArenaObject(storage.arena, raw); }

template<typename S>
[[nodiscard]] auto OwnRawScalar(S& storage, ConstructionState& state){
    using T = ConstructionProbe<Underlying<S>>;
    return ArenaUniquePtr<T, Underlying<S>>(NewArenaObject<T>(storage.arena, storage.arena, state), ArenaDeleter<T, Underlying<S>>(storage.arena));
}

template<typename S>
[[nodiscard]] auto ConvertingArgument(S& storage, ConstructionState& state){ return ConstructionArgumentSource<Underlying<S>>{ storage.arena, state }; }
template<typename T>
[[nodiscard]] b8 Present(const T& value)noexcept{ return static_cast<bool>(value); }
template<typename S>
[[nodiscard]] bool RejectedRaw(RejectingArena& rejecting, S& storage, ConstructionState& state){ return NewArenaObject<ConstructionProbe<Underlying<S>>>(rejecting, storage.arena, state) != nullptr; }
template<typename S>
[[nodiscard]] auto RejectedScalar(RejectingArena& rejecting, S& storage, ConstructionState& state){ return MakeArenaUnique<ConstructionProbe<Underlying<S>>>(rejecting, storage.arena, state); }
template<typename S>
[[nodiscard]] auto RejectedArray(RejectingArena& rejecting, S&, usize count){ return MakeArenaUnique<ConstructionProbe<Underlying<S>>[]>(rejecting, count); }

inline usize& PlacementCalls(){ return ArenaPlacementProbe::s_PlacementCalls; }
inline usize& ProbeConstructors(){ return ArenaPlacementProbe::s_Constructors; }
inline usize& ProbeDestructors(){ return ArenaPlacementProbe::s_Destructors; }

template<typename Owner>
struct OwnerRebinding{
    ReentryOwners<Owner> owners;
    OwnerReentryCallback callback;
    OwnerRebinding(Owner& slot, Owner& replacement, Owner& sink)
        : owners{ slot, replacement, sink }
        , callback{ MakeNotNull(&RebindOwner<Owner>), MakeNotNull(static_cast<void*>(&owners)) }
    {}
};

template<typename S>
using ScalarRebinding = OwnerRebinding<ProbeOwner<PolyBaseKind, S>>;
template<typename S>
[[nodiscard]] bool AdjustedBase(const S& owner){
    return static_cast<const void*>(owner.get()) != static_cast<const void*>(static_cast<const PolymorphicAllocationBase*>(owner.get()));
}
template<typename Owner>
void SetReentry(PolymorphicAllocationState& state, OwnerRebinding<Owner>& rebind){ state.reentry = &rebind.callback; }
template<typename To, typename S, typename From>
[[nodiscard]] auto MoveProbeOwner(From& source){ return ProbeOwner<To, S>(Move(source)); }

template<typename S, bool Empty>
using ArrayCleanup = Conditional_T<Empty, EmptyDeleter<ArrayReentryProbe[]>, ArenaDeleter<ArrayReentryProbe[], Underlying<S>>>;
template<typename S, bool Empty>
using ArrayProbeOwner = UniquePtr<ArrayReentryProbe[], ArrayCleanup<S, Empty>>;
template<typename S, bool Empty>
using ArrayRebinding = OwnerRebinding<ArrayProbeOwner<S, Empty>>;

template<typename S, bool Empty>
class ReentryArrayStorage final : NoCopy{
    using Arena = Underlying<S>;
    using Owner = ArrayProbeOwner<S, Empty>;
    using Deleter = ArrayCleanup<S, Empty>;
public:
    ReentryArrayStorage(S& storage, usize count)
        : m_arena(storage.arena), m_count(count), m_memory(m_arena.template allocate<ArrayReentryProbe>(count))
    {
        if(m_memory)
            m_unowned.emplace(m_arena, count, MakeNotNull(m_memory), m_elements);
    }
    [[nodiscard]] bool available()const noexcept{ return m_memory != nullptr; }
    [[nodiscard]] Owner constructOne(usize& destroyed){
        m_elements = ::new(static_cast<void*>(m_memory)) ArrayReentryProbe[1u]{ ArrayReentryProbe(destroyed) };
        return take();
    }
    [[nodiscard]] Owner constructPair(usize& destroyed, OwnerRebinding<Owner>& binding){
        m_elements = ::new(static_cast<void*>(m_memory)) ArrayReentryProbe[2u]{ ArrayReentryProbe(destroyed, &binding.callback), ArrayReentryProbe(destroyed) };
        return take();
    }

private:
    [[nodiscard]] Owner take(){
        if constexpr(Empty)
            return Owner(m_elements, Deleter(m_count));
        else{
            Owner owner(m_elements, Deleter(m_arena, m_count));
            m_unowned->release();
            return owner;
        }
    }
    Arena& m_arena;
    usize m_count;
    ArrayReentryProbe* m_memory;
    ArrayReentryProbe* m_elements = nullptr;
    Optional<ArenaObjectDetail::ConstructionStorage<ArrayReentryProbe, Arena, false>> m_unowned;
};

template<typename To, typename From>
void TransferProbe(To& target, From& source){ target = Move(source); }

template<typename Owner>
[[nodiscard]] auto ReleaseWithDeleter(Owner& owner){
    const auto deleter = owner.getDeleter();
    return Owner(owner.release(), deleter);
}

template<typename S>
[[nodiscard]] ArenaMemoryStats ConstructionStats(const S& storage){ return storage.memoryStats(); }

};
NWB_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

