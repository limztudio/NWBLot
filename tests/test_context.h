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

inline Vector<u32> MakeTriangleIndices(){
    Vector<u32> indices;
    indices.push_back(0u);
    indices.push_back(1u);
    indices.push_back(2u);
    return indices;
}

inline Vector<u32> MakeQuadTriangleIndices(){
    Vector<u32> indices = MakeTriangleIndices();
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

