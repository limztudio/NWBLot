// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#pragma once


#include <global/compile.h>
#include <global/assert.h>
#include <global/containers.h>
#include <global/filesystem.h>
#include <global/limit.h>
#include <global/math/type.h>
#include <global/simplemath.h>
#include <global/text_utils.h>
#include <global/type.h>

#include <core/alloc/general.h>

#include <ufbx.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#define NWB_FBX_TO_NWB_BEGIN NWB_BEGIN namespace FbxToNwb{
#define NWB_FBX_TO_NWB_END }; NWB_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_FBX_TO_NWB_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace UtilityDetail{

inline Core::Alloc::GlobalArena& Arena(){
    static Core::Alloc::GlobalArena s_Arena("NWB::FbxToNwb::UtilityArena");
    return s_Arena;
}

template<typename T>
class Allocator{
    template<typename U>
    friend class Allocator;

public:
    using value_type = T;
    using size_type = usize;
    using difference_type = isize;
    using pointer = T*;
    using const_pointer = const T*;
    using void_pointer = void*;
    using const_void_pointer = const void*;
    using reference = T&;
    using const_reference = const T&;
    using propagate_on_container_move_assignment = TrueType;
    using is_always_equal = FalseType;

    template<typename U>
    struct rebind{
        using other = Allocator<U>;
    };

public:
    Allocator()noexcept
        : m_arena(&Arena())
    {}
    explicit Allocator(Core::Alloc::GlobalArena& arena)noexcept
        : m_arena(&arena)
    {}
    template<typename U>
    Allocator(const Allocator<U>& other)noexcept
        : m_arena(other.m_arena)
    {}

    [[nodiscard]] pointer allocate(const size_type count){
        return m_arena->template allocate<T>(count);
    }
    void deallocate(pointer p, const size_type count)noexcept{
        m_arena->template deallocate<T>(p, count);
    }
    [[nodiscard]] Core::Alloc::GlobalArena& arena()const noexcept{ return *m_arena; }

private:
    Core::Alloc::GlobalArena* m_arena;
};

template<typename T, typename U>
inline bool operator==(const Allocator<T>& lhs, const Allocator<U>& rhs)noexcept{
    return &lhs.arena() == &rhs.arena();
}
template<typename T, typename U>
inline bool operator!=(const Allocator<T>& lhs, const Allocator<U>& rhs)noexcept{ return !(lhs == rhs); }

};

template<typename T>
using Vector = std::vector<T, UtilityDetail::Allocator<T>>;
using AString = std::basic_string<char, std::char_traits<char>, UtilityDetail::Allocator<char>>;
using TString = std::basic_string<tchar, std::char_traits<tchar>, UtilityDetail::Allocator<tchar>>;
using AStringStream = std::basic_stringstream<char, std::char_traits<char>, UtilityDetail::Allocator<char>>;

template<typename T, typename Hash = Hasher<T>, typename Equal = EqualTo<T>>
using HashSet = tsl::robin_set<T, Hash, Equal, UtilityDetail::Allocator<T>>;
template<typename K, typename V, typename Hash = Hasher<K>, typename Equal = EqualTo<K>>
using HashMap = tsl::robin_map<K, V, Hash, Equal, UtilityDetail::Allocator<Pair<K, V>>>;

template<typename T>
using UtilityVector = Vector<T>;


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_FBX_TO_NWB_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

