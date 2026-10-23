// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include <core/frame/module.h>
#include <core/frame/frame_detail.h>
#include <core/common/log.h>

#include <global/terminal_entry.h>
#include <global/thread.h>
#include <global/termination.h>

#include <gtest/gtest.h>

#include <windows.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace __hidden_win32_lifecycle_tests{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


inline constexpr Name s_LoggerArena("tests/frame/clamp_observer");
inline constexpr TStringView s_ClampMessage = NWB_TEXT("Frame Win32 client size clamped");
inline constexpr TStringView s_FrameClassName = NWB_TEXT("NWB_FRAME");
inline constexpr int s_ClampObserverFailure = 53;
inline constexpr int s_UnexpectedFailure = 71;
inline constexpr u32 s_InitializationAttempts = 2u;
inline constexpr u32 s_InactiveLoopWatchdogMS = 1000u;
inline constexpr u32 s_WatchdogPollMS = 5u;


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

class LoopLogger final : public NWB::Core::Common::ILogger{
public:
    virtual NWB::Core::Common::LogArena& arena()noexcept override{ return m_arena; }
    virtual void enqueue(NWB::Core::Common::LogString&& text, NWB::Core::Common::LogType::Enum type)override{ record(text, type); }
    virtual void enqueue(const NWB::Core::Common::LogString& text, NWB::Core::Common::LogType::Enum type)override{ record(text, type); }


private:
    static void record(const NWB::Core::Common::LogString& text, const NWB::Core::Common::LogType::Enum type){
        if(type >= NWB::Core::Common::LogType::Warning)
            NWB_TCERR << text.data() << NWB_TEXT("\n");
    }


private:
    NWB::Core::Common::LogArena m_arena{ Name("tests/frame/inactive_loop_logger") };
};

class UnfocusedLoopRenderPass final : public NWB::Core::IRenderPass{
public:
    explicit UnfocusedLoopRenderPass(NWB::Core::GraphicsRuntime& graphics)noexcept
        : IRenderPass(graphics)
    {}


public:
    virtual bool shouldRenderUnfocused()noexcept override{ return true; }
};

struct LoopCallbackState{
    NWB::Core::Frame& frame;
    u32 callbacks = 0u;
};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


static void DrainQuitMessages()noexcept{
    MSG message{};
    while(PeekMessage(&message, nullptr, WM_QUIT, WM_QUIT, PM_REMOVE)){}
}

static bool RequestQuitOnUpdate(void* userData, f32 delta){
    static_cast<void>(delta);
    auto& state = *MakeNotNull(static_cast<LoopCallbackState*>(userData));
    ++state.callbacks;
    state.frame.requestQuit();
    return true;
}

static void CheckInactiveLoop(const bool renderUnfocused){
    DrainQuitMessages();
    ScopeExit drainQuit([]()noexcept{ DrainQuitMessages(); });
    const i32 screenWidth = GetSystemMetrics(SM_CXSCREEN);
    const i32 screenHeight = GetSystemMetrics(SM_CYSCREEN);
    if(screenWidth <= 0 || screenHeight <= 0)
        GTEST_SKIP() << "Native desktop unavailable; inactive-window loop remains unqualified";
    if(!AreDpiAwarenessContextsEqual(GetThreadDpiAwarenessContext(), DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2) && !SetProcessDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2))
        GTEST_SKIP() << "Native DPI admission unavailable; inactive-window loop remains unqualified; native_code=" << GetLastError();

    const HINSTANCE instance = GetModuleHandle(nullptr);
    ASSERT_NE(instance, nullptr);
    LoopLogger logger;
    NWB::Core::Common::LoggerRegistrationGuard loggerGuard(logger, NWB::Core::Common::LoggerBreakPolicy::ReportOnly);
    NWB::Core::CpuTaskSchedulerConfig cpuTasks;
    cpuTasks.workerCount = 1u;
    NWB::Core::Frame frame(instance, 64u, 64u, cpuTasks);
    // This test has no shaders; supply a valid generic heap layout before native startup.
    NWB::Core::GpuDescriptorHeapAbi heapAbi;
    heapAbi.resourceSetIndex = 0u;
    heapAbi.samplerSetIndex = 1u;
    heapAbi.accelStructSetIndex = 2u;
    heapAbi.sampledImageBinding = 0u;
    heapAbi.storageImageBinding = 1u;
    heapAbi.sampledBufferBinding = 2u;
    heapAbi.storageBufferBinding = 3u;
    heapAbi.uniformBufferBinding = 4u;
    heapAbi.sampledImage2DArrayBinding = 5u;
    heapAbi.sampledImage3DBinding = 6u;
    heapAbi.sampledImage2DArrayUintBinding = 7u;
    heapAbi.sampledImageCubeBinding = 8u;
    heapAbi.samplerBinding = 0u;
    heapAbi.accelStructBinding = 0u;
    ASSERT_TRUE(frame.graphics().setBindlessHeapAbi(heapAbi));
    if(!frame.init())
        GTEST_SKIP() << "Hidden Win32 frame or selected graphics startup unsupported; inactive-window loop remains unqualified; native_code=" << GetLastError();
    auto& native = frame.data<NWB::Core::Common::WinFrame>();
    ASSERT_NE(native.hwnd(), nullptr);
    ASSERT_FALSE(IsWindowVisible(native.hwnd()));
    native.setActive(false);
    UnfocusedLoopRenderPass pass(frame.graphics());
    if(renderUnfocused)
        frame.graphics().addRenderPassToBack(pass);
    ScopeExit removePass([&]()noexcept{
        if(renderUnfocused)
            frame.graphics().removeRenderPass(pass);
    }
    );
    ASSERT_EQ(frame.graphics().shouldRenderUnfocused(), renderUnfocused);
    LoopCallbackState callback{ frame };
    frame.setProjectUpdateCallback(&RequestQuitOnUpdate, &callback);
    const DWORD mainThread = GetCurrentThreadId();
    Atomic<bool> watchdogFired{ false };
    Atomic<DWORD> watchdogPostError{ ERROR_SUCCESS };
    // The bounded quit keeps the old blocking loop a normal assertion failure.
    JoiningThread watchdog([&](const StopToken& stop)noexcept{
        const ULONGLONG deadline = GetTickCount64() + s_InactiveLoopWatchdogMS;
        while(!stop.stop_requested() && GetTickCount64() < deadline)
            SleepMS(s_WatchdogPollMS);
        if(stop.stop_requested())
            return;
        watchdogFired.store(true, MemoryOrder::relaxed);
        if(!PostThreadMessage(mainThread, WM_QUIT, 0u, 0)){
            watchdogPostError.store(GetLastError(), MemoryOrder::relaxed);
            if(!PostMessage(native.hwnd(), WM_CLOSE, 0u, 0))
                TerminateInvariant();
        }
    }
    );
    const bool loopSucceeded = frame.mainLoop();
    watchdog.request_stop();
    watchdog.join();
    EXPECT_TRUE(loopSucceeded);
    EXPECT_EQ(watchdogPostError.load(MemoryOrder::relaxed), static_cast<DWORD>(ERROR_SUCCESS));
    EXPECT_EQ(callback.callbacks, renderUnfocused ? 1u : 0u);
    EXPECT_EQ(watchdogFired.load(MemoryOrder::relaxed), !renderUnfocused);
    EXPECT_EQ(frame.graphics().getFrameIndex(), 0u);
}


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

TEST(FrameWin32Lifecycle, InactiveWindowRunsAnUpdateForAnUnfocusedRenderPass){
    CheckInactiveLoop(true);
}

TEST(FrameWin32Lifecycle, InactiveWindowWithoutUnfocusedRenderPassWaitsForQuit){
    CheckInactiveLoop(false);
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

