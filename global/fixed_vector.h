// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#pragma once


#include <array>

#include "generic.h"
#include "type.h"


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


template<typename T, u32 maxElements>
class FixedVector : private std::array<T, maxElements>{
public:
    using Base = std::array<T, maxElements>;

public:
    using typename Base::value_type;
    using typename Base::size_type;
    using typename Base::difference_type;
    using typename Base::reference;
    using typename Base::const_reference;
    using typename Base::pointer;
    using typename Base::const_pointer;
    using typename Base::iterator;
    using typename Base::const_iterator;


public:
    static constexpr u32 s_MaxElements = maxElements;


public:
    FixedVector()
        : Base()
        , m_currentSize(0)
    {}
    FixedVector(usize size)
        : Base()
        , m_currentSize(0)
    {
        resize(size);
    }
    FixedVector(InitializerList<T> il)
        : m_currentSize(0)
    {
        for(const T& i : il)
            push_back(i);
    }


public:
    constexpr reference operator[](size_type pos){
        return at(pos);
    }

    constexpr const_reference operator[](size_type pos)const{
        return at(pos);
    }

    constexpr reference at(size_type pos){
        NWB_ASSERT(pos < m_currentSize);
        if(pos >= m_currentSize)
            throw RuntimeException("FixedVector index out of range");

        return Base::operator[](pos);
    }

    constexpr const_reference at(size_type pos)const{
        NWB_ASSERT(pos < m_currentSize);
        if(pos >= m_currentSize)
            throw RuntimeException("FixedVector index out of range");

        return Base::operator[](pos);
    }

public:
    constexpr reference front(){ return at(0); }
    constexpr const_reference front()const{ return at(0); }

public:
    constexpr reference back(){
        NWB_ASSERT(m_currentSize > 0);
        if(m_currentSize == 0)
            throw RuntimeException("FixedVector back on empty vector");

        return Base::operator[](m_currentSize - 1);
    }
    constexpr const_reference back()const{
        NWB_ASSERT(m_currentSize > 0);
        if(m_currentSize == 0)
            throw RuntimeException("FixedVector back on empty vector");

        return Base::operator[](m_currentSize - 1);
    }

public:
    using Base::data;
    using Base::begin;
    using Base::cbegin;

public:
    constexpr iterator end()noexcept{ return iterator(begin()) + m_currentSize; }
    constexpr const_iterator end()const noexcept{ return cend(); }
    constexpr const_iterator cend()const noexcept{ return const_iterator(cbegin()) + m_currentSize; }

    constexpr bool empty()const noexcept{ return m_currentSize == 0; }
    constexpr size_type size()const noexcept{ return m_currentSize; }
    constexpr size_type max_size()const noexcept{ return s_MaxElements; }

    constexpr void clear()noexcept{
        m_currentSize = 0;
    }

    constexpr void fill(const T& value)noexcept{
        Base::fill(value);
        m_currentSize = s_MaxElements;
    }

    constexpr void swap(FixedVector& other)noexcept{
        Base::swap(other);
        Swap(m_currentSize, other.m_currentSize);
    }

    constexpr void push_back(const T& value){
        NWB_ASSERT(m_currentSize < s_MaxElements);
        if(m_currentSize >= s_MaxElements)
            throw RuntimeException("FixedVector capacity exceeded");

        *(data() + m_currentSize) = value;
        ++m_currentSize;
    }

    constexpr void push_back(T&& value){
        NWB_ASSERT(m_currentSize < s_MaxElements);
        if(m_currentSize >= s_MaxElements)
            throw RuntimeException("FixedVector capacity exceeded");

        *(data() + m_currentSize) = Move(value);
        ++m_currentSize;
    }

    constexpr void pop_back(){
        NWB_ASSERT(m_currentSize > 0);
        if(m_currentSize == 0)
            throw RuntimeException("FixedVector pop_back on empty vector");

        --m_currentSize;
    }

    constexpr void resize(size_type new_size){
        NWB_ASSERT(new_size <= s_MaxElements);
        if(new_size > s_MaxElements)
            throw RuntimeException("FixedVector size exceeds capacity");

        const size_type lo = m_currentSize < new_size ? m_currentSize : new_size;
        const size_type hi = m_currentSize < new_size ? new_size : m_currentSize;
        for(size_type i = lo; i < hi; ++i)
            *(data() + i) = T{};

        m_currentSize = new_size;
    }

    template<typename... Args>
    constexpr reference emplace_back(Args&&... args){
        NWB_ASSERT(m_currentSize < s_MaxElements);
        if(m_currentSize >= s_MaxElements)
            throw RuntimeException("FixedVector capacity exceeded");

        T* slot = data() + m_currentSize;
        *slot = T(Forward<Args>(args)...);
        ++m_currentSize;
        return *slot;
    }

private:
    size_type m_currentSize = 0;
};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

