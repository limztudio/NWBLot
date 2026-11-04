// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#pragma once


#include "atomic.h"
#include "expected.h"
#include "limit.h"
#include "termination.h"


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


// The caller owns a counter initialized to one; exhausted identity domains terminate rather than reusing an identity.
[[nodiscard]] inline u64 NextNonWrappingIdentity(Atomic<u64>& nextIdentity)noexcept{
    const u64 identity = nextIdentity.fetch_add(1u, MemoryOrder::relaxed);
    if(identity == 0u || identity == Limit<u64>::s_Max)
        TerminateInvariant();
    return identity;
}

// The counter is unchanged when its next value is zero or the maximum.
[[nodiscard]] inline u64 TryNextAtomicIdentity(Atomic<u64>& nextIdentity)noexcept{
    u64 identity = nextIdentity.load(MemoryOrder::relaxed);
    while(identity != 0u && identity != Limit<u64>::s_Max){
        if(nextIdentity.compare_exchange_weak(identity, identity + 1u, MemoryOrder::relaxed, MemoryOrder::relaxed))
            return identity;
    }
    return 0u;
}

[[nodiscard]] inline Expected<u64> TryNextAtomicIdentityAllowZero(Atomic<u64>& nextIdentity)noexcept{
    u64 identity = nextIdentity.load(MemoryOrder::relaxed);
    while(identity != Limit<u64>::s_Max){
        if(nextIdentity.compare_exchange_weak(identity, identity + 1u, MemoryOrder::relaxed, MemoryOrder::relaxed))
            return identity;
    }
    return MakeUnexpected(Failure{});
}

template<typename ResultT, typename CounterT>
    requires(IsIntegral_V<ResultT> && !IsSigned_V<ResultT> && !IsSame_V<ResultT, b8> && IsIntegral_V<CounterT> && !IsSigned_V<CounterT> && !IsSame_V<CounterT, b8> && sizeof(ResultT) < sizeof(CounterT))
[[nodiscard]] inline Expected<ResultT> TryNextAtomicBoundedIdentity(Atomic<CounterT>& nextIdentity)noexcept{
    CounterT identity = nextIdentity.load(MemoryOrder::relaxed);
    while(identity != 0u && identity <= static_cast<CounterT>(Limit<ResultT>::s_Max)){
        if(nextIdentity.compare_exchange_weak(identity, identity + 1u, MemoryOrder::relaxed, MemoryOrder::relaxed))
            return static_cast<ResultT>(identity);
    }
    return MakeUnexpected(Failure{});
}

[[nodiscard]] inline u64 AllocateServiceIdentity()noexcept{
    static Atomic<u64> s_NextIdentity{ 1u };
    return NextNonWrappingIdentity(s_NextIdentity);
}

[[nodiscard]] inline u64 NextAtomicWrappingNonzero(Atomic<u64>& counter)noexcept{
    u64 value = counter.fetch_add(1u, MemoryOrder::relaxed) + 1u;
    while(value == 0u)
        value = counter.fetch_add(1u, MemoryOrder::relaxed) + 1u;
    return value;
}

template<typename T>
    requires(IsIntegral_V<T> && !IsSigned_V<T> && !IsSame_V<T, b8>)
inline void AdvanceWrappingNonzero(T& value)noexcept{
    ++value;
    if(value == 0u)
        ++value;
}

inline void AdvanceNonWrappingRevision(u64& revision)noexcept{
    if(revision == Limit<u64>::s_Max)
        TerminateInvariant();
    ++revision;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

