// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#pragma once


#include "assert.h"
#include "type_properties.h"


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


template<typename T>
class NotNull{
    static_assert(IsPointer_V<T>, "NotNull requires a pointer type.");


public:
    NotNull() = delete;
    NotNull(std::nullptr_t) = delete;

    constexpr explicit NotNull(T ptr)noexcept
        : m_ptr(ptr)
    {
        NWB_ASSERT_MSG(m_ptr, "NotNull requires non-null pointer.");
    }
    template<typename U, typename = EnableIf_T<IsConvertible_V<U, T>>>
    constexpr explicit NotNull(U ptr)noexcept
        : m_ptr(static_cast<T>(ptr))
    {
        NWB_ASSERT_MSG(m_ptr, "NotNull requires non-null pointer.");
    }

    constexpr NotNull(const NotNull&)noexcept = default;
    constexpr NotNull(NotNull&&)noexcept = default;
    constexpr NotNull& operator=(const NotNull&)noexcept = default;
    constexpr NotNull& operator=(NotNull&&)noexcept = default;
    NotNull& operator=(std::nullptr_t) = delete;


public:
    constexpr NotNull& operator=(T ptr)noexcept{
        NWB_ASSERT_MSG(ptr, "NotNull requires non-null pointer.");
        m_ptr = ptr;
        return *this;
    }
    template<typename U, typename = EnableIf_T<IsConvertible_V<U, T>>>
    constexpr NotNull& operator=(U ptr)noexcept{
        const T casted = static_cast<T>(ptr);
        NWB_ASSERT_MSG(casted, "NotNull requires non-null pointer.");
        m_ptr = casted;
        return *this;
    }

    constexpr bool operator==(const NotNull&)const noexcept = default;


public:
    [[nodiscard]] constexpr T get()const noexcept{ return m_ptr; }

    [[nodiscard]] constexpr T operator->()const noexcept{ return m_ptr; }
    [[nodiscard]] constexpr decltype(auto) operator*()const{ return *m_ptr; }


private:
    T m_ptr = nullptr;
};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


template<typename T>
[[nodiscard]] constexpr NotNull<T> MakeNotNull(T ptr)noexcept{
    return NotNull<T>(ptr);
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

