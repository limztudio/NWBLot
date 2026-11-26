// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#pragma once


#include "assert.h"
#include "type.h"


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


class NoAssign{
public:
    NoAssign(const NoAssign&)noexcept = default;
    NoAssign()noexcept = default;


public:
    void operator=(const NoAssign&) = delete;
};

class NoCopy : NoAssign{
public:
    NoCopy(const NoCopy&) = delete;
    NoCopy(NoCopy&&)noexcept = default;
    NoCopy()noexcept = default;


public:
    NoCopy& operator=(NoCopy&&) = delete;
};

class MoveOnly{
public:
    MoveOnly(const MoveOnly&) = delete;
    MoveOnly(MoveOnly&&)noexcept = default;
    MoveOnly()noexcept = default;


public:
    MoveOnly& operator=(const MoveOnly&) = delete;
    MoveOnly& operator=(MoveOnly&&)noexcept = default;
};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


template<typename T, typename U>
inline T checked_cast(U u)noexcept(IsPointer_V<T> && IsPointer_V<U>){
    static_assert(!IsSame<T, U>::value, "Unnecessary checked_cast");
    if(!u)
        return nullptr;
    T t = static_cast<T>(u);
    FOUNDATION_ASSERT(t);
    return t;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

