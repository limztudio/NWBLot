// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#pragma once


#include <core/alloc/core.h>
#include <core/alloc/general.h>
#include <core/common/application_entry.h>

#include <global/global.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace Tests{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


struct TestContext{
    u32 passed = 0;
    u32 failed = 0;

    void checkTrue(const bool condition, const AStringView expression, const AStringView file, const i32 line){
        if(condition){
            ++passed;
            return;
        }

        ++failed;
        NWB_CERR << file << '(' << line << "): check failed: " << expression << '\n';
    }
};

#define NWB_TEST_CHECK(context, expression) (context).checkTrue((expression), #expression, __FILE__, __LINE__)

template<typename Tag = void>
struct TestArena{
    Core::Alloc::GlobalArena arena;

    TestArena()
        : arena("NWB::Tests::TestArena")
    {}
};

namespace TestDetail{

inline Core::Alloc::GlobalArena& Arena(){
    static Core::Alloc::GlobalArena s_Arena("NWB::Tests::DefaultTestArena");
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
using TestVector = std::vector<T, TestDetail::Allocator<T>>;
using TestAString = std::basic_string<char, std::char_traits<char>, TestDetail::Allocator<char>>;

inline TestVector<u32> MakeTriangleIndices(){
    TestVector<u32> indices;
    indices.push_back(0u);
    indices.push_back(1u);
    indices.push_back(2u);
    return indices;
}

inline TestVector<u32> MakeQuadTriangleIndices(){
    TestVector<u32> indices = MakeTriangleIndices();
    indices.push_back(0u);
    indices.push_back(2u);
    indices.push_back(3u);
    return indices;
}

template<typename RunTests>
[[nodiscard]] inline int RunTestSuite(const char* suiteName, RunTests&& runTests){
    Core::Common::InitializerGuard commonInitializerGuard;
    if(!commonInitializerGuard.initialize()){
        NWB_CERR << suiteName << " tests failed: common initialization failed\n";
        return -1;
    }

    TestContext context;
    Forward<RunTests>(runTests)(context);

    if(context.failed != 0u){
        NWB_CERR << suiteName << " tests failed: " << context.failed << " of " << (context.passed + context.failed) << '\n';
        return -1;
    }

    NWB_COUT << suiteName << " tests passed: " << context.passed << '\n';
    return 0;
}

#define NWB_DEFINE_TEST_ENTRY_POINT(suiteName, ...) \
    static int EntryPoint(const isize argc, tchar** argv, void*){ \
        static_cast<void>(argc); \
        static_cast<void>(argv); \
        return ::NWB::Tests::RunTestSuite(suiteName, __VA_ARGS__); \
    } \
    NWB_DEFINE_APPLICATION_ENTRY_POINT(EntryPoint)

};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

