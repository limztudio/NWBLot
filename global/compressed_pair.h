// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#pragma once


#include "call_traits.h"


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


template<typename T1, typename T2>
class CompressedPair;


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace CompressedPairDetail{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


template<typename T>
inline void internalSwap(T& t1, T& t2){
	T tTemp = t1;
	t1 = t2;
	t2 = tTemp;
}

template<typename T1, typename T2, bool isSame, bool firstEmpty, bool secondEmpty>
struct Switch;

template<typename T1, typename T2>
struct Switch<T1, T2, false, false, false>{ static const int value = 0; };
template<typename T1, typename T2>
struct Switch<T1, T2, false, true, false>{ static const int value = 1; };
template<typename T1, typename T2>
struct Switch<T1, T2, false, false, true>{ static const int value = 2; };
template<typename T1, typename T2>
struct Switch<T1, T2, false, true, true>{ static const int value = 3; };
template<typename T1, typename T2>
struct Switch<T1, T2, true, true, true>{ static const int value = 4; };
template<typename T1, typename T2>
struct Switch<T1, T2, true, false, false>{ static const int value = 5; };

template<typename T1, typename T2>
struct ImplementationTypes{
	typedef T1 first_type;
	typedef T2 second_type;
	typedef typename CallTraits<first_type>::param_type first_param_type;
	typedef typename CallTraits<second_type>::param_type second_param_type;
	typedef typename CallTraits<first_type>::reference first_reference;
	typedef typename CallTraits<second_type>::reference second_reference;
	typedef typename CallTraits<first_type>::const_reference first_const_reference;
	typedef typename CallTraits<second_type>::const_reference second_const_reference;
};

struct FirstValueTag{};
struct SecondValueTag{};
struct BothValuesTag{};

template<typename T1, typename T2>
class ValueStorage : public ImplementationTypes<T1, T2>{
private:
	typedef ImplementationTypes<T1, T2> types;


public:
	ValueStorage(){}
	ValueStorage(BothValuesTag, typename types::first_param_type x, typename types::second_param_type y) : mFirst(x), mSecond(y){}
	ValueStorage(FirstValueTag, typename types::first_param_type x) : mFirst(x){}
	ValueStorage(SecondValueTag, typename types::second_param_type y) : mSecond(y){}


public:
	typename types::first_reference first(){ return mFirst; }
	typename types::first_const_reference first()const{ return mFirst; }

	typename types::second_reference second(){ return mSecond; }
	typename types::second_const_reference second()const{ return mSecond; }

	void swap(::CompressedPair<T1, T2>& y){
		internalSwap(mFirst, y.first());
		internalSwap(mSecond, y.second());
	}


private:
	typename types::first_type mFirst;
	typename types::second_type mSecond;
};

template<typename T1, typename T2, int version>
class Implementation;

template<typename T1, typename T2>
using SelectedImplementation = Implementation<T1, T2,
	Switch<
	T1,
	T2,
	IsSame<typename RemoveCV<T1>::type, typename RemoveCV<T2>::type>::value,
	IsEmpty<T1>::value,
	IsEmpty<T2>::value>::value>;

template<typename T1, typename T2>
class Implementation<T1, T2, 0> : public ValueStorage<T1, T2>{
private:
	typedef ValueStorage<T1, T2> base;


public:
	Implementation(){}
	Implementation(typename base::first_param_type x, typename base::second_param_type y) : base(BothValuesTag{}, x, y){}
	Implementation(typename base::first_param_type x) : base(FirstValueTag{}, x){}
	Implementation(typename base::second_param_type y) : base(SecondValueTag{}, y){}
};
template<typename T1, typename T2>
class Implementation<T1, T2, 1> : public ImplementationTypes<T1, T2>, private T1{
private:
	typedef ImplementationTypes<T1, T2> types;


public:
	Implementation(){}
	Implementation(typename types::first_param_type x, typename types::second_param_type y) : T1(x), mSecond(y){}
	Implementation(typename types::first_param_type x) : T1(x){}
	Implementation(typename types::second_param_type y) : mSecond(y){}


public:
	typename types::first_reference first(){ return *this; }
	typename types::first_const_reference first()const{ return *this; }

	typename types::second_reference second(){ return mSecond; }
	typename types::second_const_reference second()const{ return mSecond; }

	void swap(::CompressedPair<T1,T2>& y){
		internalSwap(mSecond, y.second());
	}


private:
	typename types::second_type mSecond;
};
template<typename T1, typename T2>
class Implementation<T1, T2, 2> : public ImplementationTypes<T1, T2>, private T2{
private:
	typedef ImplementationTypes<T1, T2> types;


public:
	Implementation(){}
	Implementation(typename types::first_param_type x, typename types::second_param_type y) : T2(y), mFirst(x){}
	Implementation(typename types::first_param_type x) : mFirst(x){}
	Implementation(typename types::second_param_type y) : T2(y){}


public:
	typename types::first_reference first(){ return mFirst; }
	typename types::first_const_reference first()const{ return mFirst; }

	typename types::second_reference second(){ return *this; }
	typename types::second_const_reference second()const{ return *this; }

	void swap(::CompressedPair<T1,T2>& y){
		internalSwap(mFirst, y.first());
	}


private:
	typename types::first_type mFirst;
};
template<typename T1, typename T2>
class Implementation<T1, T2, 3> : public ImplementationTypes<T1, T2>, private T1, private T2{
private:
	typedef ImplementationTypes<T1, T2> types;


public:
	Implementation(){}
	Implementation(typename types::first_param_type x, typename types::second_param_type y) : T1(x), T2(y){}
	Implementation(typename types::first_param_type x) : T1(x){}
	Implementation(typename types::second_param_type y) : T2(y){}


public:
	typename types::first_reference first(){ return *this; }
	typename types::first_const_reference first()const{ return *this; }

	typename types::second_reference second(){ return *this; }
	typename types::second_const_reference second()const{ return *this; }

	void swap(::CompressedPair<T1, T2>&){}
};
template<typename T1, typename T2>
class Implementation<T1, T2, 4> : public ImplementationTypes<T1, T2>, private T1{
private:
	typedef ImplementationTypes<T1, T2> types;


public:
	Implementation(){}
	Implementation(typename types::first_param_type x, typename types::second_param_type) : T1(x){}
	Implementation(typename types::first_param_type x) : T1(x){}


public:
	typename types::first_reference first(){ return *this; }
	typename types::first_const_reference first()const{ return *this; }

	typename types::second_reference second(){ return *this; }
	typename types::second_const_reference second()const{ return *this; }

	void swap(::CompressedPair<T1, T2>&){}
};
template<typename T1, typename T2>
class Implementation<T1, T2, 5> : public ValueStorage<T1, T2>{
private:
	typedef ValueStorage<T1, T2> base;


public:
	Implementation(){}
	Implementation(typename base::first_param_type x, typename base::second_param_type y) : base(BothValuesTag{}, x, y){}
	Implementation(typename base::first_param_type x) : base(BothValuesTag{}, x, x){}
};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


template<typename T1, typename T2>
class CompressedPair : private CompressedPairDetail::SelectedImplementation<T1, T2>
{
private:
	typedef CompressedPairDetail::SelectedImplementation<T1, T2> base;


public:
	typedef typename base::first_type first_type;
	typedef typename base::second_type second_type;
	typedef typename base::first_param_type first_param_type;
	typedef typename base::second_param_type second_param_type;
	typedef typename base::first_reference first_reference;
	typedef typename base::second_reference second_reference;
	typedef typename base::first_const_reference first_const_reference;
	typedef typename base::second_const_reference second_const_reference;


public:
	CompressedPair() : base(){}
	CompressedPair(first_param_type x, second_param_type y) : base(x, y){}
	explicit CompressedPair(first_param_type x) : base(x){}


public:
	first_reference first(){ return base::first(); }
	first_const_reference first()const{ return base::first(); }

	second_reference second(){ return base::second(); }
	second_const_reference second()const{ return base::second(); }

	void swap(CompressedPair& y){ base::swap(y); }
};
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace std{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


template<typename T1, typename T2>
inline void swap(CompressedPair<T1, T2>& x, CompressedPair<T1, T2>& y){ x.swap(y); }


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

