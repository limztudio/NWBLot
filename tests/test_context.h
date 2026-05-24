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
using Allocator = ContainerDetail::DefaultArenaAllocatorFor_T<T, Core::Alloc::GlobalArena, Arena>;

};

template<typename T>
using TestVector = std::vector<T, TestDetail::Allocator<T>>;
using TestAString = std::basic_string<char, std::char_traits<char>, TestDetail::Allocator<char>>;

[[nodiscard]] inline bool NearlyEqual(const f32 lhs, const f32 rhs, const f32 epsilon = 0.00001f){
    return Abs(lhs - rhs) <= epsilon;
}

[[nodiscard]] inline bool NearlyEqual3(const SIMDVector value, const f32 x, const f32 y, const f32 z, const f32 epsilon = 0.00001f){
    return
        NearlyEqual(VectorGetX(value), x, epsilon)
        && NearlyEqual(VectorGetY(value), y, epsilon)
        && NearlyEqual(VectorGetZ(value), z, epsilon)
    ;
}

[[nodiscard]] inline bool NearlyEqual4(const SIMDVector value, const f32 x, const f32 y, const f32 z, const f32 w, const f32 epsilon = 0.00001f){
    return
        NearlyEqual(VectorGetX(value), x, epsilon)
        && NearlyEqual(VectorGetY(value), y, epsilon)
        && NearlyEqual(VectorGetZ(value), z, epsilon)
        && NearlyEqual(VectorGetW(value), w, epsilon)
    ;
}

[[nodiscard]] inline bool NearlyEqual3(const Float4& value, const f32 x, const f32 y, const f32 z, const f32 epsilon = 0.00001f){
    return
        NearlyEqual(value.x, x, epsilon)
        && NearlyEqual(value.y, y, epsilon)
        && NearlyEqual(value.z, z, epsilon)
    ;
}

[[nodiscard]] inline bool NearlyEqual4(const Float4U& value, const f32 x, const f32 y, const f32 z, const f32 w, const f32 epsilon = 0.00001f){
    return
        NearlyEqual(value.x, x, epsilon)
        && NearlyEqual(value.y, y, epsilon)
        && NearlyEqual(value.z, z, epsilon)
        && NearlyEqual(value.w, w, epsilon)
    ;
}

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

