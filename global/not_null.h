// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#pragma once


#include "assert.h"
#include "optional.h"
#include "type_properties.h"

#include <functional>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


template<typename T>
class NotNull{
    static_assert(IsPointer_V<T>, "NotNull requires a pointer type.");


public:
    NotNull() = delete;
    NotNull(Nullptr) = delete;
    constexpr explicit NotNull(T ptr)noexcept
        : m_ptr(ptr)
    {
        FOUNDATION_ASSERT_MSG(m_ptr, "NotNull requires non-null pointer.");
    }
    template<typename U, typename = EnableIf_T<IsConvertible_V<U, T>>>
    constexpr explicit NotNull(U ptr)noexcept(noexcept(static_cast<T>(DeclVal<U&>())) && IsNothrowDestructible_V<U>)
        : m_ptr(static_cast<T>(ptr))
    {
        FOUNDATION_ASSERT_MSG(m_ptr, "NotNull requires non-null pointer.");
    }
    template<typename U, typename = EnableIf_T<IsConvertible_V<U, T>>>
    constexpr NotNull(const NotNull<U>& ptr)noexcept
        : m_ptr(ptr.get())
    {}
    constexpr NotNull(const NotNull&)noexcept = default;
    constexpr NotNull(NotNull&&)noexcept = default;


public:
    constexpr NotNull& operator=(const NotNull&)noexcept = default;

    constexpr NotNull& operator=(NotNull&&)noexcept = default;

    NotNull& operator=(Nullptr) = delete;

    constexpr NotNull& operator=(T ptr)noexcept{
        FOUNDATION_ASSERT_MSG(ptr, "NotNull requires non-null pointer.");
        m_ptr = ptr;
        return *this;
    }

    template<typename U, typename = EnableIf_T<IsConvertible_V<U, T>>>
    constexpr NotNull& operator=(U ptr)noexcept(noexcept(static_cast<T>(DeclVal<U&>())) && IsNothrowDestructible_V<U>){
        const T casted = static_cast<T>(ptr);
        FOUNDATION_ASSERT_MSG(casted, "NotNull requires non-null pointer.");
        m_ptr = casted;
        return *this;
    }

    constexpr b8 operator==(const NotNull&)const noexcept = default;

    [[nodiscard]] constexpr T operator->()const noexcept{ return m_ptr; }

    [[nodiscard]] constexpr decltype(auto) operator*()const noexcept{ return *m_ptr; }


public:
    [[nodiscard]] constexpr T get()const noexcept{ return m_ptr; }


private:
    T m_ptr = nullptr;
};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


template<typename T>
[[nodiscard]] constexpr NotNull<T> MakeNotNull(T ptr)noexcept{
    return NotNull<T>(ptr);
}


template<typename Collection>
[[nodiscard]] constexpr decltype(auto) BorrowPointerAt(const Collection& values, usize index)noexcept{
    return *values[index];
}

template<auto Callable, typename... Args>
[[nodiscard]] constexpr auto BorrowOptionalResult(Args&&... args)noexcept(noexcept(std::invoke(Callable, Forward<Args>(args)...))){
    auto* const result = std::invoke(Callable, Forward<Args>(args)...);
    using Borrow = decltype(MakeNotNull(result));
    return result ? Optional<Borrow>(MakeNotNull(result)) : Optional<Borrow>();
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace std{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


template<typename T>
struct hash<NotNull<T>>{
    size_t operator()(const NotNull<T> value)const noexcept(noexcept(hash<T>{}(value.get()))){ return hash<T>{}(value.get()); }
};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

