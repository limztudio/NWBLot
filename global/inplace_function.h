// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#pragma once


#include "assert.h"
#include "compile.h"
#include "generic.h"
#include "type.h"
#include "type_properties.h"


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


template<usize InlineStorageSize>
class InplaceFunction : NoCopy{
private:
    struct Storage{
        alignas(MaxAlign) u8 data[InlineStorageSize];
    };


private:
    using InvokeFn = void(*)(Storage&);
    using DestroyFn = void(*)(Storage&)noexcept;
    using MoveFn = void(*)(Storage&, Storage&)noexcept;


public:
    inline InplaceFunction()noexcept = default;
    inline ~InplaceFunction()noexcept{ reset(); }
    inline InplaceFunction(InplaceFunction&& rhs)noexcept{
        moveFrom(Move(rhs));
    }
    template<
        typename Func,
        typename FuncType = Decay_T<Func>,
        typename = EnableIf_T<
            !IsSame_V<FuncType, InplaceFunction>
            && IsConstructible_V<FuncType, Func&&>
            && IsInvocable_V<FuncType&>
            && IsNothrowMoveConstructible_V<FuncType>
            && IsNothrowDestructible_V<FuncType>
        >
    >
    inline explicit InplaceFunction(Func&& func)noexcept(noexcept(assign(Forward<Func>(func)))){
        assign(Forward<Func>(func));
    }


public:
    inline InplaceFunction& operator=(InplaceFunction&& rhs)noexcept{
        if(this != &rhs){
            reset();
            moveFrom(Move(rhs));
        }
        return *this;
    }

    template<
        typename Func,
        typename FuncType = Decay_T<Func>,
        typename = EnableIf_T<
            !IsSame_V<FuncType, InplaceFunction>
            && IsConstructible_V<FuncType, Func&&>
            && IsInvocable_V<FuncType&>
            && IsNothrowMoveConstructible_V<FuncType>
            && IsNothrowDestructible_V<FuncType>
        >
    >
    inline InplaceFunction& operator=(Func&& func)noexcept(noexcept(assign(Forward<Func>(func)))){
        reset();
        assign(Forward<Func>(func));
        return *this;
    }

    inline explicit operator bool()const noexcept{ return m_invoke != nullptr; }

    inline void operator()(){
        NWB_ASSERT_MSG(m_invoke != nullptr, NWB_TEXT("InplaceFunction invoked without target"));
        m_invoke(m_storage);
    }


public:
    inline void reset()noexcept{
        if(!m_destroy)
            return;

        m_destroy(m_storage);
        m_invoke = nullptr;
        m_destroy = nullptr;
        m_move = nullptr;
    }


private:
    template<typename Func>
    inline void assign(Func&& func)noexcept(noexcept(new(static_cast<void*>(m_storage.data)) Decay_T<Func>(Forward<Func>(func)))){
        using FuncType = Decay_T<Func>;

        static_assert(sizeof(FuncType) <= InlineStorageSize, "InplaceFunction capture size exceeds inline storage");
        static_assert(alignof(FuncType) <= alignof(Storage), "InplaceFunction capture alignment exceeds inline storage");
        static_assert(IsNothrowMoveConstructible_V<FuncType>, "InplaceFunction targets must be nothrow move constructible");
        static_assert(IsNothrowDestructible_V<FuncType>, "InplaceFunction targets must be nothrow destructible");

        new(static_cast<void*>(m_storage.data)) FuncType(Forward<Func>(func));

        m_invoke = [](Storage& storage)noexcept(noexcept((*reinterpret_cast<FuncType*>(storage.data))())){
            FuncType& f = *reinterpret_cast<FuncType*>(storage.data);
            f();
        };
        m_destroy = [](Storage& storage)noexcept{
            FuncType& f = *reinterpret_cast<FuncType*>(storage.data);
            f.~FuncType();
        };
        m_move = [](Storage& dst, Storage& src)noexcept{
            FuncType& source = *reinterpret_cast<FuncType*>(src.data);
            ::new(static_cast<void*>(dst.data)) FuncType(Move(source));
            source.~FuncType();
        };
    }

    inline void moveFrom(InplaceFunction&& rhs)noexcept{
        if(!rhs.m_invoke)
            return;

        rhs.m_move(m_storage, rhs.m_storage);
        m_invoke = rhs.m_invoke;
        m_destroy = rhs.m_destroy;
        m_move = rhs.m_move;

        rhs.m_invoke = nullptr;
        rhs.m_destroy = nullptr;
        rhs.m_move = nullptr;
    }


private:
    Storage m_storage = {};
    InvokeFn m_invoke = nullptr;
    DestroyFn m_destroy = nullptr;
    MoveFn m_move = nullptr;
};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

