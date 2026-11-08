// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include <tests/common/capturing_logger.h>

#include <core/common/log.h>

#include <global/atomic.h>
#include <global/optional.h>
#include <global/thread.h>

#include <gtest/gtest.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace __hidden_logging_boundary_tests{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


using CapturingLogger = NWB::Tests::CapturingLogger;

inline constexpr TStringView s_ParallelLoggerMessage = NWB_TEXT("parallel logger message");


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


TEST(CommonLoggingBoundaries, LoggerMacrosBehaveAsSingleStatements){
    CapturingLogger logger;
    NWB::Core::Common::LoggerRegistrationGuard guard(logger);

    b8 elseBranchRan = false;
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
    constexpr u32 s_ExpectedErrorCount = s_ExpectedMessageCount / 2u;
    CapturingLogger logger;
    Latch startGate(s_ThreadCount + 2u);
    Atomic<u32> activeWriters{ s_ThreadCount };
    Atomic<u32> observedMessages{ 0u };
    Atomic<u32> observedErrors{ 0u };
    Atomic<b8> invalidObservation{ false };
    Thread reader([&]()noexcept{
        startGate.arrive_and_wait();
        // Wait for the first writer burst so reads overlap live writes.
        while(activeWriters.load(MemoryOrder::acquire) == s_ThreadCount)
            YieldThread();
        u32 lastMessageCount = 0u;
        u32 lastErrorCount = 0u;
        do{
            const u32 messageCount = logger.messageCount();
            const u32 errorCount = logger.errorCount();
            const NWB::Core::Common::LogType::Enum lastType = logger.lastType();
            // Fail only on impossible counts, not on momentary message/error skew.
            if(messageCount > s_ExpectedMessageCount || errorCount > s_ExpectedErrorCount || (lastType != NWB::Core::Common::LogType::Info && lastType != NWB::Core::Common::LogType::Error))
                invalidObservation.store(true, MemoryOrder::relaxed);
            if(messageCount > lastMessageCount){
                observedMessages.fetch_add(messageCount - lastMessageCount, MemoryOrder::relaxed);
                lastMessageCount = messageCount;
            }
            if(errorCount > lastErrorCount){
                observedErrors.fetch_add(errorCount - lastErrorCount, MemoryOrder::relaxed);
                lastErrorCount = errorCount;
            }
            YieldThread();
        }while(activeWriters.load(MemoryOrder::acquire) != 0u);
        // Drain the tail after the last writer exits the loop.
        const u32 messageCount = logger.messageCount();
        const u32 errorCount = logger.errorCount();
        if(messageCount > lastMessageCount)
            observedMessages.fetch_add(messageCount - lastMessageCount, MemoryOrder::relaxed);
        if(errorCount > lastErrorCount)
            observedErrors.fetch_add(errorCount - lastErrorCount, MemoryOrder::relaxed);
        // Partial reads already proved reader and writer overlap.
    });
    Array<Thread, s_ThreadCount> writers;
    for(u32 threadIndex = 0u; threadIndex < s_ThreadCount; ++threadIndex){
        writers[threadIndex] = Thread([&logger, &startGate, &activeWriters](){
            startGate.arrive_and_wait();
            for(u32 messageIndex = 0u; messageIndex < s_MessagesPerThread; ++messageIndex){
                const NWB::Core::Common::LogType::Enum type = (messageIndex & 1u) == 0u ? NWB::Core::Common::LogType::Info : NWB::Core::Common::LogType::Error;
                NWB::Core::Common::LoggerDetail::EnqueueMessage(logger, type, s_ParallelLoggerMessage);
            }
            activeWriters.fetch_sub(1u, MemoryOrder::release);
        });
    }

    startGate.arrive_and_wait();
    for(Thread& writer : writers)
        writer.join();
    reader.join();

    EXPECT_FALSE(invalidObservation.load(MemoryOrder::relaxed));
    // Reader polls live counts, so it always overlaps writes in progress.
    EXPECT_GT(observedMessages.load(MemoryOrder::relaxed), 0u);
    EXPECT_GT(observedErrors.load(MemoryOrder::relaxed), 0u);
    EXPECT_EQ(logger.messageCount(), s_ExpectedMessageCount);
    EXPECT_EQ(logger.errorCount(), s_ExpectedErrorCount);
    EXPECT_TRUE(logger.sawMessageContaining(s_ParallelLoggerMessage));
    EXPECT_TRUE(logger.sawErrorContaining(s_ParallelLoggerMessage));
}

TEST(CommonLoggingBoundaries, GuardTeardownInsideEnqueueWaitsForOtherReaderWithoutSelfDeadlock){
    class CallbackUnregisterLogger final : public NWB::Core::Common::ILogger{
    public:
        CallbackUnregisterLogger(
            NWB::Core::Common::LogArena& arena,
            Optional<NWB::Core::Common::LoggerRegistrationGuard>& guardSlot,
            Latch& bothEnteredGate,
            AtomicFlag& unregistrationStarted,
            Atomic<b8>& peerStillInCallback,
            Atomic<b8>& unregistrationReturnedWhilePeerActive,
            Atomic<u32>& callbackCount
        )noexcept
            : m_arena(arena)
            , m_guardSlot(guardSlot)
            , m_bothEnteredGate(bothEnteredGate)
            , m_unregistrationStarted(unregistrationStarted)
            , m_peerStillInCallback(peerStillInCallback)
            , m_unregistrationReturnedWhilePeerActive(unregistrationReturnedWhilePeerActive)
            , m_callbackCount(callbackCount)
        {}


    public:
        virtual NWB::Core::Common::LogArena& arena()noexcept override{ return m_arena; }

        virtual void enqueue(NWB::Core::Common::LogString&& str, const NWB::Core::Common::LogType::Enum type = NWB::Core::Common::LogType::Info)override{
            handle(TStringView(str.data(), str.size()), type);
        }

        virtual void enqueue(const NWB::Core::Common::LogString& str, const NWB::Core::Common::LogType::Enum type = NWB::Core::Common::LogType::Info)override{
            handle(TStringView(str.data(), str.size()), type);
        }


    private:
        void handle(const TStringView text, const NWB::Core::Common::LogType::Enum){
            m_callbackCount.fetch_add(1u, MemoryOrder::relaxed);
            m_bothEnteredGate.arrive_and_wait();
            if(text == NWB_TEXT("self-unregister")){
                m_unregistrationStarted.testAndSet(MemoryOrder::release);
                m_unregistrationStarted.notifyAll();
                m_guardSlot.reset();
                if(m_peerStillInCallback.load(MemoryOrder::acquire))
                    m_unregistrationReturnedWhilePeerActive.store(true, MemoryOrder::relaxed);
                NWB_LOGGER_ESSENTIAL_INFO(NWB_TEXT("routed-to-base-after-self-unregister"));
                return;
            }

            while(!m_unregistrationStarted.test(MemoryOrder::acquire))
                m_unregistrationStarted.wait(false, MemoryOrder::relaxed);
            for(u32 spin = 0u; spin < 64u; ++spin)
                YieldThread();
            m_peerStillInCallback.store(false, MemoryOrder::release);
        }


    private:
        NWB::Core::Common::LogArena& m_arena;
        Optional<NWB::Core::Common::LoggerRegistrationGuard>& m_guardSlot;
        Latch& m_bothEnteredGate;
        AtomicFlag& m_unregistrationStarted;
        Atomic<b8>& m_peerStillInCallback;
        Atomic<b8>& m_unregistrationReturnedWhilePeerActive;
        Atomic<u32>& m_callbackCount;
    };

    CapturingLogger baseLogger;
    NWB::Core::Common::LoggerRegistrationGuard baseGuard(baseLogger, NWB::Core::Common::LoggerBreakPolicy::ReportOnly);

    Optional<NWB::Core::Common::LoggerRegistrationGuard> nestedGuard;
    Latch bothEnteredGate(2u);
    AtomicFlag unregistrationStarted;
    Atomic<b8> peerStillInCallback{ true };
    Atomic<b8> unregistrationReturnedWhilePeerActive{ false };
    Atomic<u32> callbackCount{ 0u };

    CallbackUnregisterLogger callbackLogger(
        baseLogger.arena(),
        nestedGuard,
        bothEnteredGate,
        unregistrationStarted,
        peerStillInCallback,
        unregistrationReturnedWhilePeerActive,
        callbackCount
    );
    nestedGuard.emplace(callbackLogger, NWB::Core::Common::LoggerBreakPolicy::ReportOnly);

    Thread selfUnregisterThread([](){
        NWB_LOGGER_ERROR(NWB_TEXT("self-unregister"));
    });
    Thread peerReaderThread([](){
        NWB_LOGGER_ERROR(NWB_TEXT("peer-reader"));
    });

    selfUnregisterThread.join();
    peerReaderThread.join();

    EXPECT_FALSE(nestedGuard.has_value());
    EXPECT_FALSE(unregistrationReturnedWhilePeerActive.load(MemoryOrder::relaxed));
    EXPECT_EQ(callbackCount.load(MemoryOrder::relaxed), 2u);
    EXPECT_EQ(baseLogger.messageCount(), 1u);
    EXPECT_TRUE(baseLogger.sawMessageContaining(NWB_TEXT("routed-to-base-after-self-unregister")));
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

