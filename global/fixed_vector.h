// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#pragma once


#include <array>

#include "generic.h"
#include "type.h"


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


template<typename T, u32 _max_elements>
class FixedVector : private std::array<T, _max_elements>{
public:
    typedef std::array<T, _max_elements> base;
    enum { max_elements = _max_elements };

public:
    using typename base::value_type;
    using typename base::size_type;
    using typename base::difference_type;
    using typename base::reference;
    using typename base::const_reference;
    using typename base::pointer;
    using typename base::const_pointer;
    using typename base::iterator;
    using typename base::const_iterator;


public:
    FixedVector()
        : base()
        , current_size(0)
    {}
    FixedVector(usize size)
        : base()
        , current_size(0)
    {
        resize(size);
    }
    FixedVector(std::initializer_list<T> il)
        : current_size(0)
    {
        for(auto i : il)
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
        NWB_ASSERT(pos < current_size);
        if(pos >= current_size)
            throw RuntimeException("FixedVector index out of range");

        return base::operator[](pos);
    }

    constexpr const_reference at(size_type pos)const{
        NWB_ASSERT(pos < current_size);
        if(pos >= current_size)
            throw RuntimeException("FixedVector index out of range");

        return base::operator[](pos);
    }

public:
    constexpr reference front(){ return at(0); }
    constexpr const_reference front()const{ return at(0); }

public:
    constexpr reference back(){
        NWB_ASSERT(current_size > 0);
        if(current_size == 0)
            throw RuntimeException("FixedVector back on empty vector");

        return base::operator[](current_size - 1);
    }
    constexpr const_reference back()const{
        NWB_ASSERT(current_size > 0);
        if(current_size == 0)
            throw RuntimeException("FixedVector back on empty vector");

        return base::operator[](current_size - 1);
    }

public:
    using base::data;
    using base::begin;
    using base::cbegin;

public:
    constexpr iterator end()noexcept{ return iterator(begin()) + current_size; }
    constexpr const_iterator end()const noexcept{ return cend(); }
    constexpr const_iterator cend()const noexcept{ return const_iterator(cbegin()) + current_size; }

    constexpr bool empty()const noexcept{ return current_size == 0; }
    constexpr size_type size()const noexcept{ return current_size; }
    constexpr size_type max_size()const noexcept{ return max_elements; }

    constexpr void fill(const T& value)noexcept{
        base::fill(value);
        current_size = max_elements;
    }

    constexpr void swap(FixedVector& other)noexcept{
        base::swap(other);
        std::swap(current_size, other.current_size);
    }

    constexpr void push_back(const T& value){
        NWB_ASSERT(current_size < max_elements);
        if(current_size >= max_elements)
            throw RuntimeException("FixedVector capacity exceeded");

        *(data() + current_size) = value;
        ++current_size;
    }

    constexpr void push_back(T&& value){
        NWB_ASSERT(current_size < max_elements);
        if(current_size >= max_elements)
            throw RuntimeException("FixedVector capacity exceeded");

        *(data() + current_size) = Move(value);
        ++current_size;
    }

    constexpr void pop_back(){
        NWB_ASSERT(current_size > 0);
        if(current_size == 0)
            throw RuntimeException("FixedVector pop_back on empty vector");

        --current_size;
    }

    constexpr void resize(size_type new_size){
        NWB_ASSERT(new_size <= max_elements);
        if(new_size > max_elements)
            throw RuntimeException("FixedVector size exceeds capacity");

        const size_type lo = current_size < new_size ? current_size : new_size;
        const size_type hi = current_size < new_size ? new_size : current_size;
        for(size_type i = lo; i < hi; ++i)
            *(data() + i) = T{};

        current_size = new_size;
    }

    template<typename... Args>
    constexpr reference emplace_back(Args&&... args){
        NWB_ASSERT(current_size < max_elements);
        if(current_size >= max_elements)
            throw RuntimeException("FixedVector capacity exceeded");

        T* slot = data() + current_size;
        *slot = T(Forward<Args>(args)...);
        ++current_size;
        return *slot;
    }

private:
    size_type current_size = 0;
};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

