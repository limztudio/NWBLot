// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#pragma once


#include <engine_namespace.h>

#include <core/alloc/adhd_arena.h>


#include <core/alloc/general.h>
#include <global/binary.h>
#include <global/optional.h>
#include <global/type_properties.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_BEGIN
namespace Tests{

template<typename T>
[[nodiscard]] constexpr usize TestReflectedMethodCount()noexcept{ return T::reflectedMethodCount(); }


template<typename T>
[[nodiscard]] T& TestProcessValue(){
    static T value{};
    return value;
}

template<typename T>
class TestObserved final : NoCopy{
public:
    explicit TestObserved(::ArenaBase& arena) : m_value(::AdhdDefaultConstructInArena<T>(arena)){}
    TestObserved(::ArenaBase& arena, const T& initial) : m_value(::AdhdRebindConstruct<T>(arena, initial)){}
    [[nodiscard]] T& value()const noexcept{ return m_value; }

private:
    mutable T m_value;
};

template<typename T>
class TestValueSlot final : NoCopy{
public:
    [[nodiscard]] T& value(){ return m_value.value(); }
    [[nodiscard]] const T& value()const{ return m_value.value(); }

    template<typename Storage, typename... Args>
    [[nodiscard]] T& constructIn(Storage& storage, Args&&... args){ return m_value.emplace(storage.arena, Forward<Args>(args)...); }

    template<typename Storage>
    [[nodiscard]] T& takeIn(Storage& storage, T& source){ return m_value.emplace(storage.arena, Move(source)); }

    [[nodiscard]] T& take(T& source){ return m_value.emplace(Move(source)); }
    [[nodiscard]] T& copy(const T& source){ return m_value.emplace(source); }

private:
    Optional<T> m_value;
};

template<typename T, typename Error>
[[nodiscard]] T& CheckedTestValue(Expected<T, Error>& result)noexcept{
    if(!result.has_value())
        TerminateInvariant();
    return *result;
}

[[nodiscard]] inline AStringView BytePrefixText(Span<const u8> bytes, usize count)noexcept{
    const auto prefix = bytes.first(count);
    return { reinterpret_cast<const char*>(prefix.data()), prefix.size() };
}

template<typename T>
[[nodiscard]] constexpr b8 TestNothrowDestructible()noexcept{ return IsNothrowDestructible_V<T>; }

template<typename T>
[[nodiscard]] usize TestValueAddress(const T& value)noexcept{ return reinterpret_cast<usize>(AddressOf(value)); }

template<typename T>
[[nodiscard]] usize TestDataAddress(const T& values)noexcept(noexcept(values.data())){ return reinterpret_cast<usize>(values.data()); }

[[nodiscard]] inline BinaryByteView TestByteView(const Span<const u8> source, const usize reportedSize, const b8 present)noexcept{
    return {present ? source.data() : nullptr, reportedSize};
}

template<typename T>
void TestMoveAssign(T& destination, T& source)noexcept(noexcept(destination = Move(source))){ destination = Move(source); }

template<typename Sequence, typename Storage>
[[nodiscard]] b8 TestUsesStorage(const Sequence& sequence, const Storage& storage)noexcept(noexcept(sequence.get_allocator().arena())){
    return &sequence.get_allocator().arena() == &storage.arena;
}

};
NWB_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

