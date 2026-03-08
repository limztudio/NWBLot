// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#pragma once


#include <core/global.h>
#include <core/alloc/custom.h>
#include <core/alloc/arena_object.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#define NWB_METASCRIPT_BEGIN NWB_CORE_BEGIN namespace Metascript{
#define NWB_METASCRIPT_END }; NWB_CORE_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_METASCRIPT_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


template<typename T>
using MAllocator = Alloc::CustomAllocator<T>;

using MChar = char;
using MString = BasicString<MChar, MAllocator<MChar>>;
using MStringView = BasicStringView<MChar>;

struct MStringHash{
    using is_transparent = void;

    [[nodiscard]] usize operator()(const MString& s)const noexcept{
        return static_cast<usize>(ComputeFnv64Bytes(s.data(), s.size()));
    }
    [[nodiscard]] usize operator()(MStringView sv)const noexcept{
        return static_cast<usize>(ComputeFnv64Bytes(sv.data(), sv.size()));
    }
};

struct MStringEqual{
    using is_transparent = void;

    [[nodiscard]] bool operator()(const MString& a, const MString& b)const noexcept{ return MStringView(a.data(), a.size()) == MStringView(b.data(), b.size()); }
    [[nodiscard]] bool operator()(const MString& a, MStringView b)const noexcept{ return MStringView(a.data(), a.size()) == b; }
    [[nodiscard]] bool operator()(MStringView a, const MString& b)const noexcept{ return a == MStringView(b.data(), b.size()); }
};

template<typename T>
using MVector = Vector<T, MAllocator<T>>;

template<typename V>
using MStringMap = HashMap<MString, V, MStringHash, MStringEqual, MAllocator<Pair<MString, V>>>;


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


class IMetaReader{
public:
    virtual ~IMetaReader() = default;

    [[nodiscard]] virtual isize read(MChar* buffer, usize maxBytes) = 0;
};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_METASCRIPT_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

