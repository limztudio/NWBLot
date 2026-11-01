// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#pragma once


#include "atomic.h"
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

[[nodiscard]] inline u64 AllocateServiceIdentity()noexcept{
    static Atomic<u64> s_NextIdentity{ 1u };
    return NextNonWrappingIdentity(s_NextIdentity);
}

inline void AdvanceNonWrappingRevision(u64& revision)noexcept{
    if(revision == Limit<u64>::s_Max)
        TerminateInvariant();
    ++revision;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

