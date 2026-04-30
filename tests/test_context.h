// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#pragma once


#include <core/common/common.h>

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

};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

