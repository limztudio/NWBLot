// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include <array>

#include "generic.h"
#include "type.h"


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


template <typename T, uint32_t _max_elements>
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
    FixedVector(size_t size)
        : base()
        , current_size(size)
    {
        NWB_ASSERT(size <= max_elements);
    }
    FixedVector(std::initializer_list<T> il)
        : current_size(0)
    {
        for(auto i : il)
            push_back(i);
    }


public:
    using base::at;

public:
    reference operator[](size_type pos){
        NWB_ASSERT(pos < current_size);
        return base::operator[](pos);
    }

    const_reference operator[](size_type pos)const{
        NWB_ASSERT(pos < current_size);
        return base::operator[](pos);
    }

public:
    using base::front;

public:
    reference back()noexcept{ auto tmp =  end(); --tmp; return *tmp; }
    const_reference back()const noexcept{ auto tmp = cend(); --tmp; return *tmp; }

public:
    using base::data;
    using base::begin;
    using base::cbegin;

public:
    iterator end()noexcept{ return iterator(begin()) + current_size; }
    const_iterator end()const noexcept{ return cend(); }
    const_iterator cend()const noexcept{ return const_iterator(cbegin()) + current_size; }

    bool empty()const noexcept{ return current_size == 0; }
    size_type size()const noexcept{ return current_size; }
    constexpr size_type max_size()const noexcept{ return max_elements; }

    void fill(const T& value)noexcept{
        base::fill(value);
        current_size = max_elements;
    }

    void swap(static_vector& other)noexcept{
        base::swap(*this);
        std::swap(current_size, other.current_size);
    }

    void push_back(const T& value)noexcept{
        NWB_ASSERT(current_size < max_elements);
        *(data() + current_size) = value;
        current_size++;
    }

    void push_back(T&& value)noexcept{
        NWB_ASSERT(current_size < max_elements);
        *(data() + current_size) = Move(value);
        current_size++;
    }

    void pop_back()noexcept{
        NWB_ASSERT(current_size > 0);
        current_size--;
    }

    void resize(size_type new_size)noexcept{
        NWB_ASSERT(new_size <= max_elements);

        if(current_size > new_size){
            for(size_type i = new_size; i < current_size; i++)
                *(data() + i) = T{};
        }
        else{
            for(size_type i = current_size; i < new_size; i++)
                *(data() + i) = T{};
        }

        current_size = new_size;
    }

    reference emplace_back()noexcept{
        NWB_ASSERT(current_size < max_elements);
        ++current_size;
        back() = T{};
        return back();
    }

private:
    size_type current_size = 0;
};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

