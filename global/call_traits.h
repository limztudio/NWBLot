// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#pragma once


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace CallTraitsDetail{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


template<typename T, bool small>
struct Implementation1{ typedef const T& param_type; };
template<typename T>
struct Implementation1<T, true>{ typedef const T param_type; };

template<typename T, bool isPointer, bool isArithmetic>
struct Implementation0{ typedef const T& param_type; };
template<typename T, bool isPointer>
struct Implementation0<T, isPointer, true>{ typedef typename Implementation1<T, sizeof(T) <= sizeof(void*)>::param_type param_type; };
template<typename T, bool isArithmetic>
struct Implementation0<T, true, isArithmetic>{ typedef T const param_type; };


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

