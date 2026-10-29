// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#pragma once


#include "type.h"


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace CallTraitsDetail{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


template<typename T, b8 Small>
struct Implementation1{ typedef const T& param_type; };
template<typename T>
struct Implementation1<T, true>{ typedef const T param_type; };

template<typename T, b8 IsPointer, b8 IsArithmetic>
struct Implementation0{ typedef const T& param_type; };
template<typename T, b8 IsPointer>
struct Implementation0<T, IsPointer, true>{ typedef typename Implementation1<T, sizeof(T) <= sizeof(void*)>::param_type param_type; };
template<typename T, b8 IsArithmetic>
struct Implementation0<T, true, IsArithmetic>{ typedef T const param_type; };


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


template<typename T>
struct CallTraits{
public:
    typedef T value_type;
    typedef T& reference;
    typedef const T& const_reference;
    typedef typename CallTraitsDetail::Implementation0<T, IsPointer<T>::value, IsArithmetic<T>::value>::param_type param_type;
};
template<typename T>
struct CallTraits<T&>{
    typedef T& value_type;
    typedef T& reference;
    typedef const T& const_reference;
    typedef T& param_type;
};
template<typename T, size_t N>
struct CallTraits<T[N]>{
private:
    typedef T array_type[N];


public:
    typedef const T* value_type;
    typedef array_type& reference;
    typedef const array_type& const_reference;
    typedef const T* const param_type;
};
template<typename T, size_t N>
struct CallTraits<const T[N]>{
private:
    typedef const T array_type[N];


public:
    typedef const T* value_type;
    typedef array_type& reference;
    typedef const array_type& const_reference;
    typedef const T* const param_type;
};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

