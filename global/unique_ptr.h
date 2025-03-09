// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#pragma once


#include "compressed_pair.h"
#include "smart_ptr.h"


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


template <typename T, typename Deleter = DefaultDeleter<T>>
class UniquePtr{
    static_assert(!IsRValueReference<Deleter>::value, "The supplied Deleter cannot be a r-value reference.");
public:
    typedef Deleter deleter_type;
    typedef T element_type;
    typedef UniquePtr<element_type, deleter_type> this_type;
    typedef typename __hidden_smart_ptr::UniquePointerType<element_type, deleter_type>::type pointer;


public:
    constexpr UniquePtr()noexcept : mPair(pointer()){
        static_assert(!IsPointer<deleter_type>::value, "UniquePtr deleter default-constructed with null pointer. Use a different constructor or change your deleter to a class.");
    }
	constexpr UniquePtr(std::nullptr_t)noexcept : mPair(pointer()){
        static_assert(!IsPointer<deleter_type>::value, "UniquePtr deleter default-constructed with null pointer. Use a different constructor or change your deleter to a class.");
    }
	explicit UniquePtr(pointer pValue)noexcept : mPair(pValue){
		static_assert(!IsPointer<deleter_type>::value, "UniquePtr deleter default-constructed with null pointer. Use a different constructor or change your deleter to a class.");
	}
	UniquePtr(pointer pValue, typename Conditional<IsLValueReference<deleter_type>::value, deleter_type, typename AddLValueReference<const deleter_type>::type>::type deleter)noexcept : mPair(pValue, deleter){}
	UniquePtr(pointer pValue, typename RemoveReference<deleter_type>::type&& deleter)noexcept : mPair(pValue, std::move(deleter)){
		static_assert(!IsLValueReference<deleter_type>::value, "deleter_type reference refers to an rvalue deleter. The reference will probably become invalid before used. Change the deleter_type to not be a reference or construct with permanent deleter.");
	}
	UniquePtr(this_type&& x)noexcept : mPair(x.release(), std::forward<deleter_type>(x.get_deleter())){}
	template <typename U, typename E>
	UniquePtr(UniquePtr<U, E>&& u, typename EnableIf<!IsArray<U>::value&& IsConvertible<typename UniquePtr<U, E>::pointer, pointer>::value&& IsConvertible<E, deleter_type>::value && (IsSame<deleter_type, E>::value || !IsLValueReference<deleter_type>::value)>::type* = 0)noexcept : mPair(u.release(), std::forward<E>(u.get_deleter())){}
	UniquePtr(const this_type&) = delete;
	UniquePtr& operator=(const this_type&) = delete;
	UniquePtr& operator=(pointer pValue) = delete;

    ~UniquePtr()noexcept{ reset(); }


public:
	this_type& operator=(this_type&& x)noexcept{
		reset(x.release());
		mPair.second() = std::move(std::forward<deleter_type>(x.get_deleter()));
		return *this;
	}
	template <typename U, typename E>
	typename EnableIf<!IsArray<U>::value&& IsConvertible<typename UniquePtr<U, E>::pointer, pointer>::value&& IsAssignable<deleter_type&, E&&>::value, this_type&>::type operator=(UniquePtr<U, E>&& u)noexcept{
		reset(u.release());
		mPair.second() = std::move(std::forward<E>(u.get_deleter()));
		return *this;
	}
	this_type& operator=(std::nullptr_t)noexcept{
		reset();
		return *this;
	}

	typename AddLValueReference<T>::type operator*()const{ return *mPair.first(); }
	pointer operator->()const noexcept{ return mPair.first(); }

	explicit operator bool()const noexcept{ return (mPair.first() != pointer()); }


public:
	void reset(pointer pValue = pointer())noexcept{
		if(pValue != mPair.first()){
			if(auto first = std::exchange(mPair.first(), pValue))
				get_deleter()(first);
		}
	}

	pointer release()noexcept{
		pointer const pTemp = mPair.first();
		mPair.first() = pointer();
		return pTemp;
	}
	pointer detach()noexcept{ return release(); }

	void swap(this_type& x)noexcept{ mPair.swap(x.mPair); }

	pointer get()const noexcept{ return mPair.first(); }

	deleter_type& get_deleter()noexcept{ return mPair.second(); }
	const deleter_type& get_deleter()const noexcept{ return mPair.second(); }


protected:
    CompressedPair<pointer, deleter_type> mPair;
};
template <typename T, typename Deleter>
class UniquePtr<T[], Deleter>{
public:
	typedef Deleter deleter_type;
	typedef T element_type;
	typedef UniquePtr<element_type[], deleter_type> this_type;
	typedef typename __hidden_smart_ptr::UniquePointerType<element_type, deleter_type>::type pointer;


public:
	constexpr UniquePtr()noexcept : mPair(pointer()){
		static_assert(!IsPointer<deleter_type>::value, "UniquePtr deleter default-constructed with null pointer. Use a different constructor or change your deleter to a class.");
	}
	constexpr UniquePtr(std::nullptr_t)noexcept : mPair(pointer()){
		static_assert(!IsPointer<deleter_type>::value, "UniquePtr deleter default-constructed with null pointer. Use a different constructor or change your deleter to a class.");
	}
	template <typename P, typename = EnableIf_T<__hidden_smart_ptr::IsArrayCvConvertible<P, pointer>::value>>
	explicit UniquePtr(P pArray)noexcept : mPair(pArray){
		static_assert(!IsPointer<deleter_type>::value, "UniquePtr deleter default-constructed with null pointer. Use a different constructor or change your deleter to a class.");
	}
	template <typename P>
	UniquePtr(P pArray, typename Conditional<IsLValueReference<deleter_type>::value, deleter_type, typename AddLValueReference<const deleter_type>::type>::type deleter, typename EnableIf<__hidden_smart_ptr::IsArrayCvConvertible<P, pointer>::value>::type* = 0)noexcept : mPair(pArray, deleter){}
	template <typename P>
	UniquePtr(P pArray, typename RemoveReference<deleter_type>::type&& deleter, EnableIf_T<__hidden_smart_ptr::IsArrayCvConvertible<P, pointer>::value>* = 0)noexcept : mPair(pArray, std::move(deleter)){
		static_assert(!IsLValueReference<deleter_type>::value, "deleter_type reference refers to an rvalue deleter. The reference will probably become invalid before used. Change the deleter_type to not be a reference or construct with permanent deleter.");
	}
	UniquePtr(this_type&& x)noexcept : mPair(x.release(), std::forward<deleter_type>(x.get_deleter())) {}
	template <typename U, typename E>
	UniquePtr(UniquePtr<U, E>&& u, typename EnableIf<__hidden_smart_ptr::IsSafeArrayConversion<T, pointer, U, typename UniquePtr<U, E>::pointer>::value && IsConvertible<E, deleter_type>::value && (!IsLValueReference<deleter_type>::value || IsSame<E, deleter_type>::value)>::type* = 0)noexcept : mPair(u.release(), std::forward<E>(u.get_deleter())){}
	UniquePtr(const this_type&) = delete;
	UniquePtr& operator=(const this_type&) = delete;
	UniquePtr& operator=(pointer pArray) = delete;

	~UniquePtr()noexcept{ reset(); }


public:
	this_type& operator=(this_type&& x)noexcept{
		reset(x.release());
		mPair.second() = std::move(std::forward<deleter_type>(x.get_deleter()));
		return *this;
	}
	template <typename U, typename E>
	typename EnableIf<__hidden_smart_ptr::IsSafeArrayConversion<T, pointer, U, typename UniquePtr<U, E>::pointer>::value&& IsAssignable<deleter_type&, E&&>::value, this_type&>::type operator=(UniquePtr<U, E>&& u)noexcept{
		reset(u.release());
		mPair.second() = std::move(std::forward<E>(u.get_deleter()));
		return *this;
	}
	this_type& operator=(std::nullptr_t)noexcept{
		reset();
		return *this;
	}

	explicit operator bool()const noexcept{ return (mPair.first() != pointer()); }

	typename AddLValueReference<T>::type operator[](ptrdiff_t i)const{ return mPair.first()[i]; }


public:
	void reset(pointer pArray = pointer())noexcept{
		if(pArray != mPair.first()){
			if(auto first = std::exchange(mPair.first(), pArray))
				get_deleter()(first);
		}
	}

	pointer release()noexcept{
		pointer const pTemp = mPair.first();
		mPair.first() = pointer();
		return pTemp;
	}
	pointer detach()noexcept{ return release(); }

	void swap(this_type& x)noexcept{ mPair.swap(x.mPair); }

	pointer get()const noexcept{ return mPair.first(); }

	deleter_type& get_deleter()noexcept{ return mPair.second(); }
	const deleter_type& get_deleter()const noexcept{ return mPair.second(); }


protected:
	CompressedPair<pointer, deleter_type> mPair;
};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


template <typename T>
using ScratchUniquePtr = UniquePtr<T, EmptyDeleter<T>>;


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


template <typename T, typename... Args>
inline typename EnableIf<!IsArray<T>::value, UniquePtr<T>>::type makeUnique(Args&&... args){
	return UniquePtr<T>(new T(std::forward<Args>(args)...));
}
template <typename T>
inline typename EnableIf<IsUnboundedArray<T>::value, UniquePtr<T>>::type makeUnique(size_t n){
	typedef typename RemoveExtent<T>::type TBase;
	return UniquePtr<T>(new TBase[n]);
}
template <typename T, typename... Args>
typename EnableIf<IsBoundedArray<T>::value>::type
makeUnique(Args&&...) = delete;

template <typename T, typename Arena, typename... Args>
inline typename EnableIf<!IsArray<T>::value, ScratchUniquePtr<T>>::type scratchUnique(Arena& arena, Args&&... args){
    auto* output = IsConstantEvaluated() ? reinterpret_cast<T*>(arena.allocate(1, sizeof(T))) : reinterpret_cast<T*>(arena.allocate(alignof(T), sizeof(T)));
	return ScratchUniquePtr<T>(new(output) T(std::forward<Args>(args)...));
}
template <typename T, typename Arena>
inline typename EnableIf<IsUnboundedArray<T>::value, ScratchUniquePtr<T>>::type scratchUnique(Arena& arena, size_t n){
	typedef typename RemoveExtent<T>::type TBase;
	auto* output = IsConstantEvaluated() ? reinterpret_cast<TBase*>(arena.allocate(1, sizeof(TBase) * n)) : reinterpret_cast<TBase*>(arena.allocate(alignof(TBase), sizeof(TBase) * n));
	return ScratchUniquePtr<T>(new(output) TBase[n], EmptyDeleter<T>(n));
}
template <typename T, typename Arena, typename... Args>
typename EnableIf<IsBoundedArray<T>::value>::type
scratchUnique(Args&&...) = delete;


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace std{
	template <typename T, typename D>
	struct hash<UniquePtr<T, D>>{
		size_t operator()(const UniquePtr<T, D>& x) const noexcept{ return std::hash<typename UniquePtr<T, D>::pointer>()(x.get()); }
	};

	template <typename T, typename D>
	inline void swap(UniquePtr<T, D>& a, UniquePtr<T, D>& b)noexcept{ a.swap(b); }
};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


template <typename T1, typename D1, typename T2, typename D2>
inline bool operator==(const UniquePtr<T1, D1>& a, const UniquePtr<T2, D2>& b){ return (a.get() == b.get()); }

template <typename T1, typename D1, typename T2, typename D2>
requires ThreeWayComparableWith<typename UniquePtr<T1, D1>::pointer, typename UniquePtr<T2, D2>::pointer>
inline CompareThreeWayResult_T<typename UniquePtr<T1, D1>::pointer, typename UniquePtr<T2, D2>::pointer> operator<=>(const UniquePtr<T1, D1>& a, const UniquePtr<T2, D2>& b){ return a.get() <=> b.get(); }

template <typename T1, typename D1, typename T2, typename D2>
inline bool operator<(const UniquePtr<T1, D1>& a, const UniquePtr<T2, D2>& b){
	typedef typename UniquePtr<T1, D1>::pointer P1;
	typedef typename UniquePtr<T2, D2>::pointer P2;
	typedef typename CommonType<P1, P2>::type PCommon;
	PCommon pT1 = a.get();
	PCommon pT2 = b.get();
	return std::less<PCommon>()(pT1, pT2);
}
template <typename T1, typename D1, typename T2, typename D2>
inline bool operator>(const UniquePtr<T1, D1>& a, const UniquePtr<T2, D2>& b){ return (b < a); }
template <typename T1, typename D1, typename T2, typename D2>
inline bool operator<=(const UniquePtr<T1, D1>& a, const UniquePtr<T2, D2>& b){ return !(b < a); }
template <typename T1, typename D1, typename T2, typename D2>
inline bool operator>=(const UniquePtr<T1, D1>& a, const UniquePtr<T2, D2>& b){ return !(a < b); }

template <typename T, typename D>
inline bool operator==(const UniquePtr<T, D>& a, std::nullptr_t)noexcept{ return !a; }
template <typename T, typename D>
requires ThreeWayComparableWith<typename UniquePtr<T, D>::pointer, std::nullptr_t>
inline CompareThreeWayResult_T<typename UniquePtr<T, D>::pointer, std::nullptr_t> operator<=>(const UniquePtr<T, D>& a, std::nullptr_t){return a.get() <=> nullptr; }

template <typename T, typename D>
inline bool operator<(const UniquePtr<T, D>& a, std::nullptr_t){
	typedef typename UniquePtr<T, D>::pointer pointer;
	return std::less<pointer>()(a.get(), nullptr);
}
template <typename T, typename D>
inline bool operator<(std::nullptr_t, const UniquePtr<T, D>& b){
	typedef typename UniquePtr<T, D>::pointer pointer;
	pointer pT = b.get();
	return std::less<pointer>()(nullptr, pT);
}
template <typename T, typename D>
inline bool operator>(const UniquePtr<T, D>& a, std::nullptr_t){ return (nullptr < a); }
template <typename T, typename D>
inline bool operator>(std::nullptr_t, const UniquePtr<T, D>& b){ return (b < nullptr); }
template <typename T, typename D>
inline bool operator<=(const UniquePtr<T, D>& a, std::nullptr_t){ return !(nullptr < a); }
template <typename T, typename D>
inline bool operator<=(std::nullptr_t, const UniquePtr<T, D>& b){ return !(b < nullptr); }
template <typename T, typename D>
inline bool operator>=(const UniquePtr<T, D>& a, std::nullptr_t){ return !(a < nullptr); }
template <typename T, typename D>
inline bool operator>=(std::nullptr_t, const UniquePtr<T, D>& b){ return !(nullptr < b); }


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

