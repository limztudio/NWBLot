// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#pragma once


#include <engine_namespace.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include <core/ecs/world.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_BEGIN
namespace Tests::EcsProbes{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


inline thread_local b8 s_CallerThread = false;
inline void SetCallerThreadFlag(const b8 value)noexcept{ s_CallerThread = value; }
[[nodiscard]] inline b8 IsCallerThreadFlagSet()noexcept{ return s_CallerThread; }

template<typename T>
[[nodiscard]] usize TryComponentAddress(Core::ECS::World& world, const Core::ECS::EntityID id)noexcept(noexcept(world.tryGetComponent<T>(id))){
    return reinterpret_cast<usize>(world.tryGetComponent<T>(id));
}

template<typename T>
[[nodiscard]] usize ConstTryComponentAddress(const Core::ECS::World& world, const Core::ECS::EntityID id)noexcept(noexcept(world.tryGetComponent<T>(id))){
    return reinterpret_cast<usize>(world.tryGetComponent<T>(id));
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


struct AddressTrapComponent{
public:
    mutable u32 addressCalls = 0u;


public:
    [[nodiscard]] AddressTrapComponent* operator&(){ ++addressCalls; throw 37u; }

    [[nodiscard]] const AddressTrapComponent* operator&()const{ ++addressCalls; throw 41u; }
};

struct ThrowingMoveAssignmentComponent{
    b8& rejectMove;
    u32 value;


public:
    ThrowingMoveAssignmentComponent(b8& rejectMoveArg, u32 valueArg)noexcept
        : rejectMove(rejectMoveArg)
        , value(valueArg)
    {}
    ThrowingMoveAssignmentComponent(const ThrowingMoveAssignmentComponent&) = delete;
    ThrowingMoveAssignmentComponent(ThrowingMoveAssignmentComponent&&)noexcept = default;


public:
    ThrowingMoveAssignmentComponent& operator=(const ThrowingMoveAssignmentComponent&) = delete;

    ThrowingMoveAssignmentComponent& operator=(ThrowingMoveAssignmentComponent&& rhs){
        if(rejectMove)
            throw 67u;
        value = rhs.value;
        return *this;
    }
};

struct ThrowingMoveMessage{
    b8& rejectMove;
    u32& moveAttempts;
    u32& destructions;


public:
    ThrowingMoveMessage(b8& rejectMoveArg, u32& moveAttemptsArg, u32& destructionsArg)noexcept
        : rejectMove(rejectMoveArg)
        , moveAttempts(moveAttemptsArg)
        , destructions(destructionsArg)
    {}
    ThrowingMoveMessage(const ThrowingMoveMessage&) = delete;
    ThrowingMoveMessage(ThrowingMoveMessage&& rhs)
        : rejectMove(rhs.rejectMove)
        , moveAttempts(rhs.moveAttempts)
        , destructions(rhs.destructions)
    {
        ++moveAttempts;
        if(rejectMove)
            throw 73u;
    }
    ~ThrowingMoveMessage()noexcept{ ++destructions; }


public:
    ThrowingMoveMessage& operator=(const ThrowingMoveMessage&) = delete;

    ThrowingMoveMessage& operator=(ThrowingMoveMessage&&) = delete;
};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};
NWB_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

