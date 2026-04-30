// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#pragma once


#include <core/alloc/core.h>
#include <core/alloc/custom.h>
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

#define NWB_TEST_CHECK(context, expression) (context).checkTrue((expression), #expression, __FILE__, __LINE__)

template<typename Tag>
struct CountingTestAllocator{
    inline static usize allocationCalls = 0u;

    static void resetAllocationCalls(){
        allocationCalls = 0u;
    }

    [[nodiscard]] static usize allocationCallCount(){
        return allocationCalls;
    }

    static void* allocate(usize size){
        ++allocationCalls;
        return Core::Alloc::CoreAlloc(size, "NWB::Tests::CountingTestAllocator::allocate");
    }

    static void free(void* ptr){
        Core::Alloc::CoreFree(ptr, "NWB::Tests::CountingTestAllocator::free");
    }

    static void* allocateAligned(usize size, usize align){
        ++allocationCalls;
        return Core::Alloc::CoreAllocAligned(size, align, "NWB::Tests::CountingTestAllocator::allocateAligned");
    }

    static void freeAligned(void* ptr){
        Core::Alloc::CoreFreeAligned(ptr, "NWB::Tests::CountingTestAllocator::freeAligned");
    }
};

template<typename AllocatorHooks>
struct TestArena{
    Core::Alloc::CustomArena arena;

    TestArena()
        : arena(
            &AllocatorHooks::allocate,
            &AllocatorHooks::free,
            &AllocatorHooks::allocateAligned,
            &AllocatorHooks::freeAligned
        )
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

};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

