// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#pragma once


#include "../id.h"


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_UI_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


// The host supplies value lifetimes; the router retains no model or data-source address.
struct ControlToken{
    u64 instanceGeneration = 0u;
    u64 contentGeneration = 0u;
    u64 contentRevision = 0u;

    [[nodiscard]] bool valid()const{
        return instanceGeneration != 0u && contentGeneration != 0u && contentRevision != 0u;
    }
    [[nodiscard]] bool empty()const{
        return instanceGeneration == 0u && contentGeneration == 0u && contentRevision == 0u;
    }
};

[[nodiscard]] inline bool operator==(const ControlToken& lhs, const ControlToken& rhs){
    return
        lhs.instanceGeneration == rhs.instanceGeneration && lhs.contentGeneration == rhs.contentGeneration
        && lhs.contentRevision == rhs.contentRevision
    ;
}

namespace ControlActionKind{
    enum Enum : u8{ Wheel, Up, Down, PageUp, PageDown, Home, End, Submit, Activate, Left, Right };
};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_UI_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

