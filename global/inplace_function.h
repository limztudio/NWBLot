// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#pragma once


#include "assert.h"
#include "compile.h"
#include "generic.h"
#include "type.h"
#include "type_properties.h"


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


template<usize InlineStorageSize, typename Signature = void()>
class InplaceFunction;


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


template<usize InlineStorageSize, typename Ret, typename... Args>
class InplaceFunction<InlineStorageSize, Ret(Args...)>{
private:
    struct Storage{
        alignas(MaxAlign) u8 data[InlineStorageSize];
    };


private:
    using InvokeFn = Ret(*)(Storage&, Args...);
    using DestroyFn = void(*)(Storage&)noexcept;
    using MoveFn = void(*)(Storage&, Storage&)noexcept;
    using CopyFn = void(*)(Storage&, const Storage&);


public:
    inline InplaceFunction()noexcept = default;
    inline ~InplaceFunction()noexcept{ reset(); }
    inline InplaceFunction(const InplaceFunction& rhs){
        copyFrom(rhs);
    }
    inline InplaceFunction(InplaceFunction&& rhs)noexcept{
        moveFrom(Move(rhs));
    }
    template<
        typename Func,
        typename FuncType = Decay_T<Func>,
        typename = EnableIf_T<
            !IsSame_V<FuncType, InplaceFunction>
            && IsConstructible_V<FuncType, Func&&>
            && IsInvocableR_V<Ret, FuncType&, Args...>
            && IsNothrowMoveConstructible_V<FuncType>
            && IsNothrowDestructible_V<FuncType>
        >
    >
    inline explicit InplaceFunction(Func&& func)noexcept(noexcept(assign(Forward<Func>(func)))){
        assign(Forward<Func>(func));
    }


public:
    inline InplaceFunction& operator=(const InplaceFunction& rhs){
        if(this != &rhs){
            reset();
            copyFrom(rhs);
        }
        return *this;
    }

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
            && IsInvocableR_V<Ret, FuncType&, Args...>
            && IsNothrowMoveConstructible_V<FuncType>
            && IsNothrowDestructible_V<FuncType>
        >
    >
    inline InplaceFunction& operator=(Func&& func)noexcept(noexcept(assign(Forward<Func>(func)))){
        reset();
        assign(Forward<Func>(func));
        return *this;
    }

    inline explicit operator b8()const noexcept{ return m_invoke != nullptr; }

    inline Ret operator()(Args... args)const{
        FOUNDATION_ASSERT_MSG(m_invoke != nullptr, FOUNDATION_TEXT("InplaceFunction invoked without target"));
        return m_invoke(m_storage, Forward<Args>(args)...);
    }


public:
    inline void reset()noexcept{
        if(!m_destroy)
            return;

        m_destroy(m_storage);
        m_invoke = nullptr;
        m_destroy = nullptr;
        m_move = nullptr;
        m_copy = nullptr;
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

        m_invoke = [](Storage& storage, Args... args)noexcept(noexcept((*reinterpret_cast<FuncType*>(storage.data))(Forward<Args>(args)...))) -> Ret{
            FuncType& f = *reinterpret_cast<FuncType*>(storage.data);
            return f(Forward<Args>(args)...);
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
        if constexpr(IsCopyConstructible_V<FuncType>){
            m_copy = [](Storage& dst, const Storage& src){
                const FuncType& source = *reinterpret_cast<const FuncType*>(src.data);
                ::new(static_cast<void*>(dst.data)) FuncType(source);
            };
        }
        else{
            m_copy = nullptr;
        }
    }

    inline void copyFrom(const InplaceFunction& rhs){
        if(!rhs.m_invoke)
            return;

        FOUNDATION_ASSERT_MSG(rhs.m_copy != nullptr, FOUNDATION_TEXT("InplaceFunction target is not copy constructible"));
        rhs.m_copy(m_storage, rhs.m_storage);
        m_invoke = rhs.m_invoke;
        m_destroy = rhs.m_destroy;
        m_move = rhs.m_move;
        m_copy = rhs.m_copy;
    }

    inline void moveFrom(InplaceFunction&& rhs)noexcept{
        if(!rhs.m_invoke)
            return;

        rhs.m_move(m_storage, rhs.m_storage);
        m_invoke = rhs.m_invoke;
        m_destroy = rhs.m_destroy;
        m_move = rhs.m_move;
        m_copy = rhs.m_copy;

        rhs.m_invoke = nullptr;
        rhs.m_destroy = nullptr;
        rhs.m_move = nullptr;
        rhs.m_copy = nullptr;
    }


private:
    mutable Storage m_storage = {};
    InvokeFn m_invoke = nullptr;
    DestroyFn m_destroy = nullptr;
    MoveFn m_move = nullptr;
    CopyFn m_copy = nullptr;
};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


template<typename Ret, typename... Args>
class InplaceFunction<0u, Ret(Args...)>{
public:
    using RawFunction = Ret(*)(Args...);


public:
    constexpr InplaceFunction()noexcept = default;
    constexpr InplaceFunction(RawFunction func)noexcept
        : m_function(func)
    {}
    template<
        typename Func,
        typename FuncType = Decay_T<Func>,
        typename = EnableIf_T<!IsSame_V<FuncType, InplaceFunction> && IsConvertible_V<FuncType, RawFunction>>
    >
    constexpr InplaceFunction(Func&& func)noexcept
        : m_function(static_cast<RawFunction>(func))
    {}


public:
    constexpr InplaceFunction& operator=(RawFunction func)noexcept{
        m_function = func;
        return *this;
    }

    template<
        typename Func,
        typename FuncType = Decay_T<Func>,
        typename = EnableIf_T<!IsSame_V<FuncType, InplaceFunction> && IsConvertible_V<FuncType, RawFunction>>
    >
    constexpr InplaceFunction& operator=(Func&& func)noexcept{
        m_function = static_cast<RawFunction>(func);
        return *this;
    }

    constexpr explicit operator b8()const noexcept{ return m_function != nullptr; }

    constexpr operator RawFunction()const noexcept{ return m_function; }

    inline Ret operator()(Args... args)const{
        FOUNDATION_ASSERT_MSG(m_function != nullptr, FOUNDATION_TEXT("InplaceFunction<0> invoked without target"));
        return m_function(Forward<Args>(args)...);
    }


public:
    constexpr void reset()noexcept{ m_function = nullptr; }


private:
    RawFunction m_function = nullptr;
};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

