// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#pragma once


#include "capturing_logger.h"
#include "headless_graphics_scope.h"

#include <gtest/gtest.h>

#include <global/unique_ptr.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace Tests{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


struct HeadlessGraphicsTestConfig{
    static constexpr bool s_DeathTestThreadsafe = false;
    static constexpr bool s_TransferQueueEnabled = false;
    static constexpr bool s_SameClassMultiQueueEnabled = false;
    static constexpr bool s_SkipInSetUpTestSuite = true;
    static constexpr const char* s_SkipMessage = "no usable validation-enabled headless Vulkan device on this host";
    static constexpr const tchar* s_VulkanErrorMessage = NWB_TEXT("validation-enabled GPU tests emitted a Vulkan severity=error message");
};

template<typename Config = HeadlessGraphicsTestConfig>
class HeadlessGraphicsTest : public ::testing::Test{
protected:
    static void SetUpTestSuite(){
#if defined(NWB_DEBUG) || defined(NWB_OPTIMIZE)
        if constexpr(Config::s_DeathTestThreadsafe)
            GTEST_FLAG_SET(death_test_style, "threadsafe");
#endif

        s_logger.emplace();
        s_loggerGuard.emplace(*s_logger);
        s_scope = MakeUnique<HeadlessGraphicsScope>();
        bool initialized = true;
        if constexpr(Config::s_TransferQueueEnabled)
            initialized = initialized && s_scope->setTransferQueueEnabled(true);
        if constexpr(Config::s_SameClassMultiQueueEnabled)
            initialized = initialized && s_scope->setSameClassMultiQueueEnabled(true);
        initialized = initialized && s_scope->initialize();
        if(!initialized){
            if constexpr(Config::s_SkipInSetUpTestSuite)
                GTEST_SKIP() << Config::s_SkipMessage;
            return;
        }
        s_deviceInitialized = true;
    }

    static void TearDownTestSuite(){
        s_scope.reset();
        if(s_deviceInitialized && s_logger.has_value()){
            EXPECT_FALSE(s_logger->sawMessageContaining(NWB_TEXT("Vulkan debug: [severity=error")))
                << Config::s_VulkanErrorMessage;
        }
        s_loggerGuard.reset();
        s_logger.reset();
        s_deviceInitialized = false;
    }

    virtual void SetUp()override{
        if constexpr(!Config::s_SkipInSetUpTestSuite){
            if(!s_deviceInitialized)
                GTEST_SKIP() << Config::s_SkipMessage;
        }
    }

    [[nodiscard]] static auto& device(){
        return s_scope->graphics().getDevice();
    }

    [[nodiscard]] static Core::Alloc::GlobalArena& arena(){
        return s_scope->arena();
    }

    [[nodiscard]] static bool deviceInitialized(){
        return s_deviceInitialized;
    }

    [[nodiscard]] static HeadlessGraphicsScope& scope(){
        return *s_scope;
    }


protected:
    static bool s_deviceInitialized;
    static UniquePtr<HeadlessGraphicsScope> s_scope;
    static Optional<CapturingLogger> s_logger;
    static Optional<Core::Common::LoggerRegistrationGuard> s_loggerGuard;
};

template<typename Config>
bool HeadlessGraphicsTest<Config>::s_deviceInitialized = false;

template<typename Config>
UniquePtr<HeadlessGraphicsScope> HeadlessGraphicsTest<Config>::s_scope;

template<typename Config>
Optional<CapturingLogger> HeadlessGraphicsTest<Config>::s_logger;

template<typename Config>
Optional<Core::Common::LoggerRegistrationGuard> HeadlessGraphicsTest<Config>::s_loggerGuard;


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

