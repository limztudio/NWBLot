// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include <core/common/terminal_entry.h>
#include <global/scope_exit.h>

#include <gtest/gtest.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace __hidden_terminal_entry_tests{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


using namespace NWB::Core::Common;

struct TerminalTestError{
    int result;
};

TEST(TerminalEntry, SuccessfulAndExpectedFailureReturnsDoNotInvokeExceptionHandlers){
    u32 calls = 0u;
    u32 handled = 0u;
    const auto handle = [&](const TerminalTestError&){ ++handled; return -2; };
    const auto unexpected = [&](){ ++handled; return -3; };
    EXPECT_EQ(InvokeTerminalEntry<TerminalTestError>([&](){ ++calls; return 0; }, handle, unexpected), 0);
    EXPECT_EQ(InvokeTerminalEntry<TerminalTestError>([&](){ ++calls; return 1; }, handle, unexpected), 1);
    EXPECT_EQ(calls, 2u);
    EXPECT_EQ(handled, 0u);
}

TEST(TerminalEntry, TypedTerminalHandlerRunsAfterWorkUnwindsWhileReportingContextRemainsAlive){
    u32 cleanupCount = 0u;
    u32 handled = 0u;
    bool reportingContextAlive = true;
    int result = 0;
    {
        ScopeExit reportingContext([&]()noexcept{ reportingContextAlive = false; });

        result = InvokeTerminalEntry<TerminalTestError>([&]()->int{
            ScopeExit inner([&]()noexcept{ ++cleanupCount; });

            throw TerminalTestError{ 17 };
        }, [&](const TerminalTestError& error){
            EXPECT_EQ(cleanupCount, 1u);
            EXPECT_TRUE(reportingContextAlive);
            ++handled;
            return error.result;
        }, [](){ return -1; });
        EXPECT_TRUE(reportingContextAlive);
    }
    EXPECT_EQ(result, 17);
    EXPECT_EQ(handled, 1u);
    EXPECT_EQ(cleanupCount, 1u);
    EXPECT_FALSE(reportingContextAlive);
}

TEST(TerminalEntry, UnknownFailureUsesItsTerminalHandlerAfterReverseOrderCleanup){
    u32 order[3u] = {};
    u32 count = 0u;
    bool typedHandled = false;
    const int result = InvokeTerminalEntry<TerminalTestError>([&]()->int{
        ScopeExit first([&]()noexcept{ order[count] = 1u; ++count; });
        ScopeExit second([&]()noexcept{ order[count] = 2u; ++count; });

        throw 42;
    }, [&](const TerminalTestError&){ typedHandled = true; return 0; }, [&](){
        order[count] = 3u;
        ++count;
        return -1;
    });
    EXPECT_EQ(result, -1);
    EXPECT_FALSE(typedHandled);
    EXPECT_EQ(count, 3u);
    EXPECT_EQ(order[0u], 2u);
    EXPECT_EQ(order[1u], 1u);
    EXPECT_EQ(order[2u], 3u);
}

TEST(TerminalEntry, ApplicationFailurePolicyPreservesOutputAndNormalizesOnlyTypedTerminalExits){
    u32 messages = 0u;
    for(const int code : { 0, 109 }){
        const int result = InvokeTerminalEntry<TerminalTestError>([code]()->int{
            throw TerminalTestError{ code };
        }, [&](const TerminalTestError& error){
            ++messages;
            return error.result;
        }, [](){ return -7; }, TerminalErrorExitPolicy::ApplicationFailure);
        EXPECT_EQ(result, -1);
    }
    EXPECT_EQ(messages, 2u);
    EXPECT_EQ(InvokeTerminalEntry<TerminalTestError>([](){ return 23; }, [](const TerminalTestError&){
        return 0;
    }, [](){ return -7; }, TerminalErrorExitPolicy::ApplicationFailure), 23);
    EXPECT_EQ(InvokeTerminalEntry<TerminalTestError>([]()->int{ throw 42; }, [](const TerminalTestError&){
        return 0;
    }, [](){ return -7; }, TerminalErrorExitPolicy::ApplicationFailure), -7);
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

