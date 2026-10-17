// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include <core/common/log.h>

#include <global/atomic.h>
#include <global/thread.h>

#include <tests/common/capturing_logger.h>

#include <gtest/gtest.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace __hidden_logging_boundary_tests{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


using CapturingLogger = NWB::Tests::CapturingLogger;

inline constexpr u32 s_ExpectedDualCount = 2u;
inline constexpr TStringView s_PARALLEL_LOGGER_MESSAGE = NWB_TEXT("parallel logger message");


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


TEST(CommonLoggingBoundaries, LoggerMacrosBehaveAsSingleStatements){
    CapturingLogger logger;
    NWB::Core::Common::LoggerRegistrationGuard guard(logger);

    bool elseBranchRan = false;
    if(false)
        NWB_LOGGER_ESSENTIAL_INFO(NWB_TEXT("unreachable"));
    else
        elseBranchRan = true;

    EXPECT_TRUE(elseBranchRan);
    EXPECT_EQ(logger.messageCount(), 0u);
}

TEST(CommonLoggingBoundaries, CapturingLoggerSerializesConcurrentWritersAndReaders){
    constexpr u32 s_ThreadCount = 4u;
    constexpr u32 s_MessagesPerThread = 512u;
    constexpr u32 s_ExpectedMessageCount = s_ThreadCount * s_MessagesPerThread;
    constexpr u32 s_ExpectedErrorCount = s_ExpectedMessageCount / s_ExpectedDualCount;
    CapturingLogger logger;
    Latch startGate(s_ThreadCount + s_ExpectedDualCount);
    Atomic<u32> activeWriters{ s_ThreadCount };
    Atomic<bool> invalidObservation{ false };
    bool sawConcurrentMessage = false;
    bool sawConcurrentError = false;
    Thread reader([&](){
        startGate.arrive_and_wait();
        do{
            const u32 messageCount = logger.messageCount();
            const u32 errorCount = logger.errorCount();
            const NWB::Core::Common::LogType::Enum lastType = logger.lastType();
            if(messageCount > s_ExpectedMessageCount || errorCount > s_ExpectedErrorCount || (lastType != NWB::Core::Common::LogType::Info && lastType != NWB::Core::Common::LogType::Error))
                invalidObservation.store(true, MemoryOrder::relaxed);
            if(logger.sawMessageContaining(s_PARALLEL_LOGGER_MESSAGE))
                sawConcurrentMessage = true;
            if(logger.sawErrorContaining(s_PARALLEL_LOGGER_MESSAGE))
                sawConcurrentError = true;
            YieldThread();
        }while(activeWriters.load(MemoryOrder::acquire) != 0u);
    }
    );
    Thread writers[s_ThreadCount];
    for(u32 threadIndex = 0u; threadIndex < s_ThreadCount; ++threadIndex){
        writers[threadIndex] = Thread([&logger, &startGate, &activeWriters](){
            startGate.arrive_and_wait();
            for(u32 messageIndex = 0u; messageIndex < s_MessagesPerThread; ++messageIndex){
                const NWB::Core::Common::LogType::Enum type = (messageIndex & 1u) == 0u ? NWB::Core::Common::LogType::Info : NWB::Core::Common::LogType::Error;
                NWB::Core::Common::LoggerDetail::EnqueueMessage(logger, type, s_PARALLEL_LOGGER_MESSAGE);
            }
            activeWriters.fetch_sub(1u, MemoryOrder::release);
        }
        );
    }

    startGate.arrive_and_wait();
    for(Thread& writer : writers)
        writer.join();
    reader.join();

    EXPECT_FALSE(invalidObservation.load(MemoryOrder::relaxed));
    EXPECT_TRUE(sawConcurrentMessage);
    EXPECT_TRUE(sawConcurrentError);
    EXPECT_EQ(logger.messageCount(), s_ExpectedMessageCount);
    EXPECT_EQ(logger.errorCount(), s_ExpectedErrorCount);
    EXPECT_TRUE(logger.sawMessageContaining(s_PARALLEL_LOGGER_MESSAGE));
    EXPECT_TRUE(logger.sawErrorContaining(s_PARALLEL_LOGGER_MESSAGE));
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

