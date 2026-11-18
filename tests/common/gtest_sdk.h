// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#pragma once

#include <core/alloc/general.h>
#include <engine_namespace.h>
#include <global/generic.h>
#include <gtest/gtest.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_BEGIN
namespace Tests{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


class ScopedTestTrace final : NoCopy{
public:
    template<typename T>
    explicit ScopedTestTrace(const T& message, const char* const file = __builtin_FILE(), const i32 line = __builtin_LINE())
        : m_trace(file, line, message)
    {}


private:
    ::testing::ScopedTrace m_trace;
};

template<typename Exception>
[[noreturn]] void ThrowTestException(const Exception& value){
    throw value;
}

template<typename Exception, typename Action>
[[nodiscard]] ::testing::AssertionResult ThrowsException(Action&& action){
    try{
        action();
    }
    catch(const Exception&){
        return ::testing::AssertionSuccess();
    }
    catch(...){
        return ::testing::AssertionFailure() << "threw a different exception";
    }
    return ::testing::AssertionFailure() << "did not throw";
}


template<typename Fixture>
class HostedTestFixture : public ::testing::Test{
protected:
    void SetUp()override{
        if constexpr(requires{ value.setup(); })
            value.setup();
    }
    void TearDown()override{
        if constexpr(requires{ value.teardown(); })
            value.teardown();
    }

public:
    [[nodiscard]] Fixture& fixture()noexcept{ return value; }

private:
    Core::Alloc::GlobalArena arena{Name("tests/fixture")};
    Fixture value = [&]() -> Fixture{
        if constexpr(std::is_constructible_v<Fixture, ::ArenaBase&>)
            return Fixture(arena);
        else
            return Fixture();
    }();
};

template<typename Fixture>
using GoogleTestHost = std::conditional_t<std::is_base_of_v<::testing::Test, Fixture>, Fixture, HostedTestFixture<Fixture>>;

template<typename Fixture>
[[nodiscard]] Fixture& TestFixtureValue(GoogleTestHost<Fixture>& host){
    if constexpr(std::is_base_of_v<::testing::Test, Fixture>)
        return host;
    else
        return host.fixture();
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


template<typename Arena>
void FinishStderrCapture(AString<Arena>& captured){
    const auto text = ::testing::internal::GetCapturedStderr();
    captured.assign(text.data(), text.size());
}

inline void RecordTestProperty(const AStringView key, const AStringView value){
    ::testing::Test::RecordProperty(AInteropString(key), AInteropString(value));
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};
NWB_END


#define NWB_GTEST_CASE(Suite, Case, Fn) TEST(Suite, Case){ Fn(); }

#if defined(NWB_FINAL)
#define NWB_GTEST_FINAL_CASE(Suite, Case, Fn) NWB_GTEST_CASE(Suite, Case, Fn)
#else
#define NWB_GTEST_FINAL_CASE(Suite, Case, Fn)
#endif

#define NWB_GTEST_FIXTURE_CASE(Suite, Case, Fn) \
    GTEST_TEST_(Suite, Case, ::NWB::Tests::GoogleTestHost<Suite>, ::testing::internal::GetTypeId<::NWB::Tests::GoogleTestHost<Suite>>()){ \
        Fn(::NWB::Tests::TestFixtureValue<Suite>(*this)); \
    }

#define NWB_GTEST_TYPED_PAIR(Suite, Fixture, First, Second) \
    template<typename Type> class Suite : public ::NWB::Tests::HostedTestFixture<Fixture<Type>>{}; \
    using Suite##Types = ::testing::Types<First, Second>; \
    TYPED_TEST_SUITE(Suite, Suite##Types);

#define NWB_GTEST_TYPED_CASE(Suite, Case, Fn) TYPED_TEST(Suite, Case){ Fn(this->fixture()); }

#define NWB_TEST_REQUIRE_NOTHROW(...) static_assert(noexcept(__VA_ARGS__))
#define NWB_TEST_REQUIRE_THROWING(...) static_assert(!noexcept(__VA_ARGS__))
#define NWB_TEST_EXPECT_ALLOCATION_SIZE_EXCEPTION(...) EXPECT_THROW((__VA_ARGS__), ::AllocationSizeException)
#define NWB_TEST_EXPECT_RUNTIME_EXCEPTION(...) EXPECT_THROW((__VA_ARGS__), ::RuntimeException)
#define NWB_TEST_EXPECT_UNSIGNED_EXCEPTION(...) EXPECT_THROW((__VA_ARGS__), u32)
#define NWB_TEST_EXPECT_SIGNED_EXCEPTION(...) EXPECT_THROW((__VA_ARGS__), i32)

#define NWB_GTEST_SKIP_REASON(reason) GTEST_SKIP() << (reason)


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

