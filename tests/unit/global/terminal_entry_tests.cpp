// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include <global/terminal_entry.h>
#include <global/scope_exit.h>
#include <global/type.h>

#include <gtest/gtest.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace __hidden_terminal_entry_tests{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


struct TerminalTestError{
    int result;
};

TEST(TerminalEntry, TypedTerminalHandlerRunsAfterWorkUnwindsWhileReportingContextRemainsAlive){
    u32 cleanupCount = 0u;
    u32 handled = 0u;
    b8 reportingContextAlive = true;
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
        }, []()noexcept{ return -1; }
        );
        EXPECT_TRUE(reportingContextAlive);
    }
    EXPECT_EQ(result, 17);
    EXPECT_EQ(handled, 1u);
    EXPECT_EQ(cleanupCount, 1u);
    EXPECT_FALSE(reportingContextAlive);
}

TEST(TerminalEntry, UnknownFailureUsesItsTerminalHandlerAfterReverseOrderCleanup){
    Array<u32, 3u> order = {};
    u32 count = 0u;
    b8 typedHandled = false;
    const int result = InvokeTerminalEntry<TerminalTestError>([&]()->int{
        ScopeExit first([&]()noexcept{ order[count] = 1u; ++count; });
        ScopeExit second([&]()noexcept{ order[count] = 2u; ++count; });

        throw 42;
    }, [&](const TerminalTestError&)noexcept{ typedHandled = true; return 0; }, [&]()noexcept{
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


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

