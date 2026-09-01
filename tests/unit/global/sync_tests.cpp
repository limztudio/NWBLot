// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include <tests/common/test_context.h>
#include <gtest/gtest.h>

#include <global/filesystem/operations.h>
#include <global/filesystem/path.h>
#include <global/sync.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace __hidden_sync_tests{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


using AString = NWB::Tests::TestAString;
using TestPath = ::Path<NWB::Core::Alloc::GlobalArena>;

struct SyncTestArenaTag{};
using TestArena = NWB::Tests::TestArena<SyncTestArenaTag>;


static TestPath RepoRoot(TestArena& testArena){
    return TestPath(testArena.arena, __FILE__).parent_path().parent_path().parent_path().parent_path().lexically_normal();
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


static_assert(noexcept(MachinePause(1)));


TEST(GlobalSync, WindowsArm64UsesProcessorYieldIntrinsic){
    TestArena testArena;
    AString source;
    ASSERT_TRUE(ReadTextFile(RepoRoot(testArena) / "global" / "sync.h", source));

    const AStringView sourceView(source.data(), source.size());
    const usize branchBegin = sourceView.find("#if defined(NWB_PLATFORM_WINDOWS) && defined(_M_ARM64)");
    const usize branchEnd = sourceView.find("#elif defined(__ARM_ARCH_7A__) || defined(__aarch64__)", branchBegin);
    ASSERT_NE(branchBegin, AStringView::npos);
    ASSERT_NE(branchEnd, AStringView::npos);
    ASSERT_LT(branchBegin, branchEnd);

    const AStringView windowsArm64Branch = sourceView.substr(branchBegin, branchEnd - branchBegin);
    EXPECT_NE(windowsArm64Branch.find("__yield();"), AStringView::npos);
    EXPECT_NE(windowsArm64Branch.find("--delay;"), AStringView::npos);
    EXPECT_EQ(windowsArm64Branch.find("YieldThread();"), AStringView::npos);

    MachinePause(1);
    AtomicBackOff backoff;
    EXPECT_TRUE(backoff.boundedPause());
    EXPECT_TRUE(backoff.boundedPause());
    EXPECT_TRUE(backoff.boundedPause());
    EXPECT_TRUE(backoff.boundedPause());
    EXPECT_FALSE(backoff.boundedPause());
}

TEST(GlobalSync, FutexPreservesEveryIncrementUnderContention){
    constexpr u32 s_ThreadCount = 4u;
    constexpr u32 s_IncrementsPerThread = 8192u;
    Futex mutex;
    Latch startGate(s_ThreadCount + 1u);
    u32 value = 0u;
    Thread workers[s_ThreadCount];
    for(u32 threadIndex = 0u; threadIndex < s_ThreadCount; ++threadIndex){
        workers[threadIndex] = Thread([&mutex, &startGate, &value](){
            startGate.arrive_and_wait();
            for(u32 incrementIndex = 0u; incrementIndex < s_IncrementsPerThread; ++incrementIndex){
                ScopedLock lock(mutex);
                ++value;
            }
        });
    }

    startGate.arrive_and_wait();
    for(Thread& worker : workers)
        worker.join();

    EXPECT_EQ(value, s_ThreadCount * s_IncrementsPerThread);
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

