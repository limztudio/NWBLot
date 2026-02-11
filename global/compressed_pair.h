// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#pragma once


#include "call_traits.h"


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace __hidden_compressed_pair{
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
	class CompressedPair;

	template<typename T1, typename T2, int version>
	class Implementation;
	template<typename T1, typename T2>
	class Implementation<T1, T2, 0>{
	public:
		typedef T1 first_type;
		typedef T2 second_type;
		typedef typename CallTraits<first_type>::param_type first_param_type;
		typedef typename CallTraits<second_type>::param_type second_param_type;
		typedef typename CallTraits<first_type>::reference first_reference;
		typedef typename CallTraits<second_type>::reference second_reference;
		typedef typename CallTraits<first_type>::const_reference first_const_reference;
		typedef typename CallTraits<second_type>::const_reference second_const_reference;


    public:
		Implementation(){}
		Implementation(first_param_type x, second_param_type y) : mFirst(x), mSecond(y){}
        Implementation(first_param_type x) : mFirst(x){}
        Implementation(second_param_type y) : mSecond(y){}


    public:
		first_reference first(){ return mFirst; }
		first_const_reference first()const{ return mFirst; }

		second_reference second(){ return mSecond; }
		second_const_reference second()const{ return mSecond; }

		void swap(CompressedPair<T1, T2>& y){
			internalSwap(mFirst, y.first());
			internalSwap(mSecond, y.second());
		}


	private:
		first_type mFirst;
		second_type mSecond;
	};
	template<typename T1, typename T2>
	class Implementation<T1, T2, 1> : private T1{
	public:
		typedef T1 first_type;
		typedef T2 second_type;
		typedef typename CallTraits<first_type>::param_type first_param_type;
		typedef typename CallTraits<second_type>::param_type second_param_type;
		typedef typename CallTraits<first_type>::reference first_reference;
		typedef typename CallTraits<second_type>::reference second_reference;
		typedef typename CallTraits<first_type>::const_reference first_const_reference;
		typedef typename CallTraits<second_type>::const_reference second_const_reference;


    public:
        Implementation(){}
		Implementation(first_param_type x, second_param_type y) : first_type(x), mSecond(y){}
        Implementation(first_param_type x) : first_type(x){}
        Implementation(second_param_type y) : mSecond(y){}


    public:
		first_reference first(){ return *this; }
		first_const_reference first()const{ return *this; }

		second_reference second(){ return mSecond; }
		second_const_reference second()const{ return mSecond; }

		void swap(CompressedPair<T1,T2>& y){
			internalSwap(mSecond, y.second());
		}


	private:
		second_type mSecond;
	};
	template<typename T1, typename T2>
	class Implementation<T1, T2, 2> : private T2{
	public:
		typedef T1 first_type;
		typedef T2 second_type;
		typedef typename CallTraits<first_type>::param_type first_param_type;
		typedef typename CallTraits<second_type>::param_type second_param_type;
		typedef typename CallTraits<first_type>::reference first_reference;
		typedef typename CallTraits<second_type>::reference second_reference;
		typedef typename CallTraits<first_type>::const_reference first_const_reference;
		typedef typename CallTraits<second_type>::const_reference second_const_reference;


    public:
        Implementation(){}
        Implementation(first_param_type x, second_param_type y) : second_type(y), mFirst(x){}
        Implementation(first_param_type x) : mFirst(x){}
        Implementation(second_param_type y) : second_type(y){}


    public:
		first_reference first(){ return mFirst; }
		first_const_reference first()const{ return mFirst; }

		second_reference second(){ return *this; }
		second_const_reference second()const{ return *this; }

		void swap(CompressedPair<T1,T2>& y){
			internalSwap(mFirst, y.first());
		}


	private:
		first_type mFirst;
	};
	template<typename T1, typename T2>
	class Implementation<T1, T2, 3> : private T1, private T2{
	public:
		typedef T1 first_type;
		typedef T2 second_type;
		typedef typename CallTraits<first_type>::param_type first_param_type;
		typedef typename CallTraits<second_type>::param_type second_param_type;
		typedef typename CallTraits<first_type>::reference first_reference;
		typedef typename CallTraits<second_type>::reference second_reference;
		typedef typename CallTraits<first_type>::const_reference first_const_reference;
		typedef typename CallTraits<second_type>::const_reference second_const_reference;


    public:
        Implementation(){}
        Implementation(first_param_type x, second_param_type y) : first_type(x), second_type(y){}
		Implementation(first_param_type x) : first_type(x){}
		Implementation(second_param_type y) : second_type(y){}


    public:
		first_reference first(){ return *this; }
		first_const_reference first()const{ return *this; }

		second_reference second(){ return *this; }
		second_const_reference second()const{ return *this; }

		void swap(CompressedPair<T1, T2>&){}
	};
	template<typename T1, typename T2>
	class Implementation<T1, T2, 4> : private T1{
	public:
		typedef T1 first_type;
		typedef T2 second_type;
		typedef typename CallTraits<first_type>::param_type first_param_type;
		typedef typename CallTraits<second_type>::param_type second_param_type;
		typedef typename CallTraits<first_type>::reference first_reference;
		typedef typename CallTraits<second_type>::reference second_reference;
		typedef typename CallTraits<first_type>::const_reference first_const_reference;
		typedef typename CallTraits<second_type>::const_reference second_const_reference;


	public:
		Implementation(){}
		Implementation(first_param_type x, second_param_type) : first_type(x){}
		Implementation(first_param_type x) : first_type(x){}


	public:
		first_reference first(){ return *this; }
		first_const_reference first()const{ return *this; }

		second_reference second(){ return *this; }
		second_const_reference second()const{ return *this; }

		void swap(CompressedPair<T1, T2>&){}
	};
	template<typename T1, typename T2>
	class Implementation<T1, T2, 5>{
	public:
		typedef T1 first_type;
		typedef T2 second_type;
		typedef typename CallTraits<first_type>::param_type first_param_type;
		typedef typename CallTraits<second_type>::param_type second_param_type;
		typedef typename CallTraits<first_type>::reference first_reference;
		typedef typename CallTraits<second_type>::reference second_reference;
		typedef typename CallTraits<first_type>::const_reference first_const_reference;
		typedef typename CallTraits<second_type>::const_reference second_const_reference;


	public:
		Implementation(){}
		Implementation(first_param_type x, second_param_type y) : mFirst(x), mSecond(y){}
		Implementation(first_param_type x) : mFirst(x), mSecond(x){}


	public:
		first_reference first(){ return mFirst; }
		first_const_reference first()const{ return mFirst; }

		second_reference second(){ return mSecond; }
		second_const_reference second()const{ return mSecond; }

		void swap(CompressedPair<T1, T2>& y){
			internalSwap(mFirst, y.first());
			internalSwap(mSecond, y.second());
		}


	private:
		first_type mFirst;
		second_type mSecond;
	};
};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


template<typename T1, typename T2>
class CompressedPair : private __hidden_compressed_pair::Implementation<T1, T2,
	__hidden_compressed_pair::Switch<
	T1,
	T2,
	IsSame<typename RemoveCV<T1>::type, typename RemoveCV<T2>::type>::value,
	IsEmpty<T1>::value,
	IsEmpty<T2>::value>::value>
{
private:
	typedef __hidden_compressed_pair::Implementation<T1, T2,
		__hidden_compressed_pair::Switch<
		T1,
		T2,
		IsSame<typename RemoveCV<T1>::type, typename RemoveCV<T2>::type>::value,
		IsEmpty<T1>::value,
		IsEmpty<T2>::value>::value> base;


public:
	typedef T1 first_type;
	typedef T2 second_type;
	typedef typename CallTraits<first_type>::param_type first_param_type;
	typedef typename CallTraits<second_type>::param_type second_param_type;
	typedef typename CallTraits<first_type>::reference first_reference;
	typedef typename CallTraits<second_type>::reference second_reference;
	typedef typename CallTraits<first_type>::const_reference first_const_reference;
	typedef typename CallTraits<second_type>::const_reference second_const_reference;


public:
	CompressedPair() : base(){}
	CompressedPair(first_param_type x, second_param_type y) : base(x, y){}
	explicit CompressedPair(first_param_type x) : base(x){}
	explicit CompressedPair(second_param_type y) : base(y){}


public:
	first_reference first(){ return base::first(); }
	first_const_reference first()const{ return base::first(); }

	second_reference second(){ return base::second(); }
	second_const_reference second()const{ return base::second(); }

	void swap(CompressedPair& y){ base::swap(y); }
};
template<typename T>
class CompressedPair<T, T>
	: private __hidden_compressed_pair::Implementation<T, T,
	__hidden_compressed_pair::Switch<
	T,
	T,
	IsSame<typename RemoveCV<T>::type, typename RemoveCV<T>::type>::value,
	IsEmpty<T>::value,
	IsEmpty<T>::value>::value>
{
private:
	typedef __hidden_compressed_pair::Implementation<T, T,
		__hidden_compressed_pair::Switch<
		T,
		T,
		IsSame<typename RemoveCV<T>::type, typename RemoveCV<T>::type>::value,
		IsEmpty<T>::value,
		IsEmpty<T>::value>::value> base;


public:
	typedef T first_type;
	typedef T second_type;
	typedef typename CallTraits<first_type>::param_type first_param_type;
	typedef typename CallTraits<second_type>::param_type second_param_type;
	typedef typename CallTraits<first_type>::reference first_reference;
	typedef typename CallTraits<second_type>::reference second_reference;
	typedef typename CallTraits<first_type>::const_reference first_const_reference;
	typedef typename CallTraits<second_type>::const_reference second_const_reference;


public:
	CompressedPair() : base(){}
	CompressedPair(first_param_type x, second_param_type y) : base(x, y){}
	explicit CompressedPair(first_param_type x) : base(x){}


public:
	first_reference first(){ return base::first(); }
	first_const_reference first()const{ return base::first(); }

	second_reference second(){ return base::second(); }
	second_const_reference second()const{ return base::second(); }

	void swap(CompressedPair<T, T>& y){ base::swap(y); }
};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace std{
    template<typename T1, typename T2>
    inline void swap(CompressedPair<T1, T2>& x, CompressedPair<T1, T2>& y){ x.swap(y); }
};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

