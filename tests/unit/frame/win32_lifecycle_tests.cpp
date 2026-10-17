// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include <core/frame/module.h>
#include <core/frame/frame_detail.h>
#include <core/common/log.h>

#include <global/terminal_entry.h>

#include <gtest/gtest.h>

#include <windows.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace __hidden_frame_lifecycle_tests{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


inline constexpr Name s_LoggerArena("tests/frame/clamp_observer");
inline constexpr TStringView s_ClampMessage = NWB_TEXT("Frame Win32 client size clamped");
inline constexpr TStringView s_FrameClassName = NWB_TEXT("NWB_FRAME");
inline constexpr int s_ClampObserverFailure = 53;
inline constexpr int s_UnexpectedFailure = 71;
inline constexpr u32 s_InitializationAttempts = 2u;


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


struct ClampObserverFailure final{};

class ThrowingClampLogger final : public NWB::Core::Common::ILogger{
private:
    using LogArena = NWB::Core::Common::LogArena;
    using LogString = NWB::Core::Common::LogString;


public:
    ThrowingClampLogger()
        : m_arena(s_LoggerArena)
    {}


public:
    virtual LogArena& arena()noexcept override{ return m_arena; }
    virtual void enqueue(LogString&& text, NWB::Core::Common::LogType::Enum)override{ record(text); }
    virtual void enqueue(const LogString& text, NWB::Core::Common::LogType::Enum)override{ record(text); }
    [[nodiscard]] u32 clampCount()const noexcept{ return m_clampCount; }


private:
    void record(const TStringView text){
        if(text.find(s_ClampMessage) == TStringView::npos)
            return;
        ++m_clampCount;
        throw ClampObserverFailure{};
    }


private:
    LogArena m_arena;
    u32 m_clampCount = 0u;
};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


TEST(FrameWindowDimensions, LogicalDimensionsRemainPositiveAtLargestNativeScale){
    using NWB::Core::FrameDetail::LogicalDimensionForPixels;
    EXPECT_EQ(LogicalDimensionForPixels(2u, s_MaxI32), 1);
    EXPECT_EQ(LogicalDimensionForPixels(static_cast<u16>(s_MaxU16), s_MaxI32), 1);
    EXPECT_EQ(LogicalDimensionForPixels(static_cast<u16>(s_MaxU16), 1), static_cast<i32>(s_MaxU16));
    EXPECT_EQ(LogicalDimensionForPixels(5u, 2), 3);
    EXPECT_EQ(LogicalDimensionForPixels(0u, s_MaxI32), 1);
    EXPECT_EQ(LogicalDimensionForPixels(5u, 0), 5);
}

TEST(FrameWin32Lifecycle, ThrowingClampObserverReleasesNativeClassBeforeLaterInitialization){
    const i32 screenWidth = GetSystemMetrics(SM_CXSCREEN);
    const i32 screenHeight = GetSystemMetrics(SM_CYSCREEN);
    if(screenWidth <= 0 || screenHeight <= 0 || screenWidth >= static_cast<i32>(s_MaxU16) || screenHeight >= static_cast<i32>(s_MaxU16))
        GTEST_SKIP() << "Native desktop cannot provide the bounded oversize client clamp trigger; class unwind remains unqualified: " << screenWidth << "x" << screenHeight;
    if(!AreDpiAwarenessContextsEqual(GetThreadDpiAwarenessContext(), DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2) && !SetProcessDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2)){
        const DWORD nativeError = GetLastError();
        GTEST_SKIP() << "Native desktop DPI admission unavailable; class unwind remains unqualified; native_code=" << nativeError;
    }

    const HINSTANCE instance = GetModuleHandle(nullptr);
    ASSERT_NE(instance, nullptr);
    ThrowingClampLogger logger;
    NWB::Core::Common::LoggerRegistrationGuard loggerGuard(logger);
    NWB::Core::CpuTaskSchedulerConfig cpuTasks;
    cpuTasks.workerCount = 1u;
    for(u32 attempt = 0u; attempt < s_InitializationAttempts; ++attempt){
        WNDCLASSEX before{};
        before.cbSize = sizeof(before);
        const bool existed = GetClassInfoEx(instance, s_FrameClassName.data(), &before) != FALSE;
        const DWORD beforeError = existed ? ERROR_SUCCESS : GetLastError();
        ASSERT_FALSE(existed);
        ASSERT_EQ(beforeError, static_cast<DWORD>(ERROR_CLASS_DOES_NOT_EXIST));

        const int result = InvokeTerminalEntry<ClampObserverFailure>([&]()->int{
            NWB::Core::Frame frame(instance, static_cast<u16>(s_MaxU16), static_cast<u16>(s_MaxU16), cpuTasks);
            return frame.init() ? 0 : 1;
        }, [](const ClampObserverFailure&)noexcept{ return s_ClampObserverFailure; }, []()noexcept{ return s_UnexpectedFailure; }
        );
        ASSERT_EQ(result, s_ClampObserverFailure);
        ASSERT_EQ(logger.clampCount(), attempt + 1u);

        WNDCLASSEX after{};
        after.cbSize = sizeof(after);
        const bool retained = GetClassInfoEx(instance, s_FrameClassName.data(), &after) != FALSE;
        const DWORD afterError = retained ? ERROR_SUCCESS : GetLastError();
        EXPECT_FALSE(retained);
        EXPECT_EQ(afterError, static_cast<DWORD>(ERROR_CLASS_DOES_NOT_EXIST));
    }
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

