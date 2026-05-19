// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#pragma once


#include <global/assert.h>
#include <global/limit.h>
#include <global/type.h>
#include <global/type_properties.h>

#include <new>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace ContainerDetail{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace AdaptorDetail{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


template<typename T>
struct ArenaAllocatorTraits{
    static_assert(!IsConst_V<T>, "NWB arena allocators forbid containers of const elements because allocator<const T> is ill-formed.");
    static_assert(!IsFunction_V<T>, "NWB arena allocators forbid allocators for function elements because of [allocator.requirements].");
    static_assert(!IsReference_V<T>, "NWB arena allocators forbid allocators for reference elements because of [allocator.requirements].");

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
};

usize CachelineSize();
[[nodiscard]] usize CacheAlignedAlignment(usize valueAlignment)noexcept;

template<typename T>
[[nodiscard]] inline usize SizeOf(usize count){
    static_assert(sizeof(T) > 0, "value_type must be complete before calling allocate.");
    if(count > Limit<usize>::s_Max / sizeof(T))
        throw std::bad_array_new_length{};
    return count * sizeof(T);
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


template<typename T, typename ArenaT>
class ArenaAllocator : public AdaptorDetail::ArenaAllocatorTraits<T>{
    template<typename U, typename A>
    friend class ArenaAllocator;


public:
    using _From_primary = ArenaAllocator<T, ArenaT>;
    using Base = AdaptorDetail::ArenaAllocatorTraits<T>;
    using value_type = typename Base::value_type;
    using size_type = typename Base::size_type;
    using pointer = typename Base::pointer;


public:
    template<typename U>
    struct rebind{
        using other = ArenaAllocator<U, ArenaT>;
    };


public:
    ArenaAllocator() = delete;
    constexpr ArenaAllocator(ArenaT& arena)noexcept
        : m_arena(arena)
    {}
    constexpr ArenaAllocator(const ArenaAllocator&)noexcept = default;
    template<typename U>
    constexpr ArenaAllocator(const ArenaAllocator<U, ArenaT>& other)noexcept
        : m_arena(other.m_arena)
    {}

    constexpr ~ArenaAllocator() = default;
    constexpr ArenaAllocator& operator=(const ArenaAllocator&)noexcept{ return *this; }


public:
    constexpr void deallocate(pointer const buffer, const size_type count)noexcept{
        NWB_ASSERT_MSG((buffer != nullptr || count == 0), NWB_TEXT("null pointer cannot point to a block of non-zero size"));
        if(buffer == nullptr)
            return;

        const size_type bytes = sizeof(value_type) * count;
        m_arena.deallocate(buffer, static_cast<size_type>(alignof(value_type)), bytes);
    }

    [[nodiscard]] constexpr pointer allocate(const size_type count){
        const size_type bytes = AdaptorDetail::SizeOf<value_type>(count);
        if(bytes == 0u)
            return nullptr;

        pointer const output = static_cast<pointer>(m_arena.allocate(static_cast<size_type>(alignof(value_type)), bytes));
        NWB_ASSERT(output != nullptr);
        if(output == nullptr)
            throw std::bad_alloc{};

        return output;
    }
#if _HAS_CXX23
    [[nodiscard]] constexpr AllocationResult<pointer> allocate_at_least(const size_type count){ return { allocate(count), count }; }
#endif

    [[nodiscard]] ArenaT& arena()const noexcept{ return m_arena; }


private:
    ArenaT& m_arena;
};


template<typename T, typename U, typename ArenaT>
inline bool operator==(const ArenaAllocator<T, ArenaT>& lhs, const ArenaAllocator<U, ArenaT>& rhs)noexcept{
    return &lhs.arena() == &rhs.arena();
}

template<typename T, typename U, typename ArenaT>
inline bool operator!=(const ArenaAllocator<T, ArenaT>& lhs, const ArenaAllocator<U, ArenaT>& rhs)noexcept{ return !(lhs == rhs); }


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


template<typename T, typename ArenaT>
class ArenaCacheAlignedAllocator : public AdaptorDetail::ArenaAllocatorTraits<T>{
    template<typename U, typename A>
    friend class ArenaCacheAlignedAllocator;


public:
    using _From_primary = ArenaCacheAlignedAllocator<T, ArenaT>;
    using Base = AdaptorDetail::ArenaAllocatorTraits<T>;
    using value_type = typename Base::value_type;
    using size_type = typename Base::size_type;
    using pointer = typename Base::pointer;


public:
    template<typename U>
    struct rebind{
        using other = ArenaCacheAlignedAllocator<U, ArenaT>;
    };


public:
    [[nodiscard]] static size_type alignment()noexcept{
        return AdaptorDetail::CacheAlignedAlignment(static_cast<size_type>(alignof(value_type)));
    }


public:
    ArenaCacheAlignedAllocator() = delete;
    constexpr ArenaCacheAlignedAllocator(ArenaT& arena)noexcept
        : m_arena(arena)
    {}
    constexpr ArenaCacheAlignedAllocator(const ArenaCacheAlignedAllocator&)noexcept = default;
    template<typename U>
    constexpr ArenaCacheAlignedAllocator(const ArenaCacheAlignedAllocator<U, ArenaT>& other)noexcept
        : m_arena(other.m_arena)
    {}

    constexpr ~ArenaCacheAlignedAllocator() = default;
    constexpr ArenaCacheAlignedAllocator& operator=(const ArenaCacheAlignedAllocator&)noexcept{ return *this; }


public:
    [[nodiscard]] size_type max_size()const noexcept{
        return (Limit<size_type>::s_Max - alignment()) / sizeof(value_type);
    }

    constexpr void deallocate(pointer const buffer, const size_type count)noexcept{
        NWB_ASSERT_MSG((buffer != nullptr || count == 0), NWB_TEXT("null pointer cannot point to a block of non-zero size"));
        if(buffer == nullptr)
            return;

        const size_type bytes = sizeof(value_type) * count;
        m_arena.deallocate(buffer, alignment(), bytes);
    }

    [[nodiscard]] constexpr pointer allocate(const size_type count){
        const size_type bytes = AdaptorDetail::SizeOf<value_type>(count);
        if(bytes == 0u)
            return nullptr;

        pointer const output = static_cast<pointer>(m_arena.allocate(alignment(), bytes));
        NWB_ASSERT(output != nullptr);
        if(output == nullptr)
            throw std::bad_alloc{};

        return output;
    }
#if _HAS_CXX23
    [[nodiscard]] constexpr AllocationResult<pointer> allocate_at_least(const size_type count){ return { allocate(count), count }; }
#endif

    [[nodiscard]] ArenaT& arena()const noexcept{ return m_arena; }


private:
    ArenaT& m_arena;
};


template<typename T, typename U, typename ArenaT>
inline bool operator==(const ArenaCacheAlignedAllocator<T, ArenaT>& lhs, const ArenaCacheAlignedAllocator<U, ArenaT>& rhs)noexcept{
    return &lhs.arena() == &rhs.arena();
}

template<typename T, typename U, typename ArenaT>
inline bool operator!=(const ArenaCacheAlignedAllocator<T, ArenaT>& lhs, const ArenaCacheAlignedAllocator<U, ArenaT>& rhs)noexcept{ return !(lhs == rhs); }


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


template<typename T>
concept ArenaResourceLike = requires(T& arena, void* pointer, usize align, usize size){
    arena.allocate(align, size);
    arena.deallocate(pointer, align, size);
};

template<typename T, typename ArenaT>
using ArenaAllocatorFor_T = ArenaAllocator<T, ArenaT>;

template<typename T, typename ArenaT>
using ArenaCacheAlignedAllocatorFor_T = ArenaCacheAlignedAllocator<T, ArenaT>;


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

