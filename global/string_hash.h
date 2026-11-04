// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#pragma once


#include "hash_utils.h"


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


template<typename StringT>
struct TransparentStringHash{
    using is_transparent = void;


public:
    [[nodiscard]] usize operator()(const StringT& s)const noexcept(noexcept(ComputeFnv64Bytes(s.data(), s.size()))){ return static_cast<usize>(ComputeFnv64Bytes(s.data(), s.size())); }

    [[nodiscard]] usize operator()(AStringView sv)const noexcept{ return static_cast<usize>(ComputeFnv64Bytes(sv.data(), sv.size())); }
};

template<typename StringT>
struct TransparentStringEqual{
    using is_transparent = void;


public:
    [[nodiscard]] b8 operator()(const StringT& a, const StringT& b)const noexcept(noexcept(AStringView(a) == AStringView(b))){ return AStringView(a) == AStringView(b); }

    [[nodiscard]] b8 operator()(const StringT& a, AStringView b)const noexcept(noexcept(AStringView(a) == b)){ return AStringView(a) == b; }

    [[nodiscard]] b8 operator()(AStringView a, const StringT& b)const noexcept(noexcept(a == AStringView(b))){ return a == AStringView(b); }
};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

