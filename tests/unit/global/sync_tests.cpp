// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include <gtest/gtest.h>

#include <global/sync.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace __hidden_sync_tests{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


TEST(GlobalSync, BoundedBackoffStopsAtItsPauseBudget){
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

