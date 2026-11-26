// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#pragma once


#include <core/alloc/general.h>
#include <core/alloc/persistent.h>
#include <core/alloc/scratch.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_BEGIN
namespace Tests{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


struct RawAllocation{
    u8* bytes = nullptr;

    [[nodiscard]] bool operator==(const RawAllocation&)const noexcept = default;
    [[nodiscard]] usize address()const noexcept{ return reinterpret_cast<usize>(bytes); }
    [[nodiscard]] u8& operator[](const usize index)const noexcept{ return bytes[index]; }
    [[nodiscard]] u8 readVolatile(const usize index)const noexcept{ return static_cast<const volatile u8*>(bytes)[index]; }
    void writeVolatile(const usize index, const u8 value)const noexcept{ static_cast<volatile u8*>(bytes)[index] = value; }
};

[[nodiscard]] inline RawAllocation AllocateBytes(::ArenaBase& arena, const usize alignment, const usize count){
    return { static_cast<u8*>(arena.allocate(alignment, count)) };
}

[[nodiscard]] inline RawAllocation ResizeBytes(::ArenaBase& arena, const RawAllocation block, const usize alignment, const usize count){
    return { static_cast<u8*>(arena.reallocate(block.bytes, alignment, count)) };
}

[[nodiscard]] inline usize BackendSize(const RawAllocation block)noexcept{ return Core::Alloc::CoreMsize(block.bytes); }

inline void FreeBytes(::ArenaBase& arena, const RawAllocation block, const usize alignment, const usize count){
    arena.deallocate(block.bytes, alignment, count);
}

template<typename T>
class TestArrayData{
public:
    constexpr TestArrayData()noexcept = default;
    explicit constexpr TestArrayData(Span<const T> data)noexcept : m_data(data.data()){}
    constexpr operator const T*()const noexcept{ return m_data; }
    [[nodiscard]] constexpr Span<const T> declaredSpan(usize count)const noexcept{ return {m_data, count}; }

private:
    const T* m_data = nullptr;
};

template<typename Allocator, typename Tag = void>
class TestStorage{
public:
    TestStorage() : arena(Name("tests/common/test_arena")){}
    template<typename... Args>
    explicit TestStorage(Args&&... args) : arena(Forward<Args>(args)...){}
    template<typename T, typename... Args>
    [[nodiscard]] T makeValue(Args&&... args){ return T(arena, Forward<Args>(args)...); }

    [[nodiscard]] ArenaMemoryStats memoryStats()const noexcept{ return arena.memoryStats(); }

    [[nodiscard]] AllocationIdentity identity()const noexcept{ return AllocationIdentity(arena); }
    operator Allocator&()noexcept{ return arena; }
    operator const Allocator&()const noexcept{ return arena; }
    template<typename T>
    operator ContainerDetail::ArenaAllocator<T, Allocator>()noexcept{ return { arena }; }

    Allocator arena;
};

using HeapStorage = TestStorage<Core::Alloc::GlobalArena>;
template<typename T>
using HeapStorageVector = Vector<T, Core::Alloc::GlobalArena>;
using ScratchStorage = TestStorage<Core::Alloc::ScratchArena>;
template<typename T>
using ScratchStorageVector = Vector<T, Core::Alloc::ScratchArena>;
using PersistentStorage = TestStorage<Core::Alloc::PersistentArena>;


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


template<typename Tag = void>
using TestArena = TestStorage<Core::Alloc::GlobalArena, Tag>;


class NthFailArena final : public ArenaBase{
public:
    explicit NthFailArena(
        const Name name = Name("tests/common/nth_fail"),
        const Name backingName = Name("tests/common/nth_fail_backing"),
        const b8 throwOnFailure = false
    )
        : ArenaBase(name)
        , m_backing(backingName)
        , m_throwOnFailure(throwOnFailure)
    {}

    void arm(const usize failOnCall)noexcept{
        m_reject = false;
        m_armed = true;
        m_remaining = failOnCall;
        m_hit = false;
    }

    void rejectAllocations()noexcept{
        m_reject = true;
        m_armed = false;
        m_remaining = 0u;
        m_hit = false;
    }

    void disarm()noexcept{
        m_reject = false;
        m_armed = false;
        m_remaining = 0u;
        m_hit = false;
    }

    [[nodiscard]] b8 hit()const noexcept{ return m_hit; }

    void* allocate(usize align, usize size)override{
        if(shouldFail()){
            if(m_throwOnFailure)
                throw AllocationException{};
            return nullptr;
        }
        return m_backing.allocate(align, size);
    }

    void* reallocate(void* memory, usize align, usize size)override{
        if(shouldFail()){
            if(m_throwOnFailure)
                throw AllocationException{};
            return nullptr;
        }
        return m_backing.reallocate(memory, align, size);
    }

    void deallocate(void* memory, usize align, usize size)noexcept override{ m_backing.deallocate(memory, align, size); }


private:
    [[nodiscard]] b8 shouldFail()noexcept{
        if(m_reject){
            m_hit = true;
            return true;
        }
        if(!m_armed)
            return false;
        if(m_remaining <= 1u){
            m_hit = true;
            return true;
        }
        --m_remaining;
        return false;
    }


private:
    Core::Alloc::GlobalArena m_backing;
    usize m_remaining = 0u;
    b8 m_armed = false;
    b8 m_reject = false;
    b8 m_hit = false;
    b8 m_throwOnFailure = false;
};


namespace TestDetail{
    inline Core::Alloc::GlobalArena& Arena(){
        static Core::Alloc::GlobalArena s_Arena(Name("tests/common/default_test_arena"));
        return s_Arena;
    }
};

using TestAString = DefaultAString<Core::Alloc::GlobalArena, TestDetail::Arena>;


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};
NWB_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

