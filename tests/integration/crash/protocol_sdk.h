// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#pragma once


#include <engine_namespace.h>

#include <core/crash/internal.h>
#include <core/crash/package_internal.h>
#include <global/filesystem/directory_iterator.h>
#include <tests/common/memory_sdk.h>
#include <gtest/gtest.h>

#if defined(FOUNDATION_PLATFORM_LINUX) && !defined(FOUNDATION_PLATFORM_ANDROID)
#include <sys/stat.h>
#endif


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_BEGIN
namespace Tests::CrashProtocolSdk{

[[nodiscard]] inline AStringView CurrentTestName()noexcept{
    return ::testing::UnitTest::GetInstance()->current_test_info()->name();
}

[[nodiscard]] constexpr u32 PlatformKind()noexcept{
    using namespace Core::Crash::CrashDetail::PlatformKind;
#if defined(FOUNDATION_PLATFORM_WINDOWS)
    return Windows;
#elif defined(FOUNDATION_PLATFORM_ANDROID)
    return Android;
#elif defined(FOUNDATION_PLATFORM_LINUX)
    return Linux;
#else
    return Unknown;
#endif
}

template<typename Observer>
class DiagnosticSubscription final : NoCopy{
public:
    explicit DiagnosticSubscription(Observer& observer)noexcept{
        ScopedLock lock(s_mutex);
        m_previousObserver = s_observer;
        s_observer = &observer;
        m_previousCallback = DiagnosticDetail::g_EventCallback.exchange(&Dispatch, MemoryOrder::acq_rel);
    }
    ~DiagnosticSubscription()noexcept{
        ScopedLock lock(s_mutex);
        SetDiagnosticEventCallback(m_previousCallback);
        s_observer = m_previousObserver;
    }

private:
    static void Dispatch(const DiagnosticEventRecord& record)noexcept{
        ScopedLock lock(s_mutex);
        if(s_observer)
            s_observer->observe(record);
    }

    inline static Futex s_mutex;
    inline static Observer* s_observer = nullptr;
    Observer* m_previousObserver = nullptr;
    DiagnosticEventCallback m_previousCallback = nullptr;
};

[[nodiscard]] inline AStringView ErrorCategory(const ErrorCode& error)noexcept{
    return error.category().name();
}

[[nodiscard]] inline b8 Flush(HeapStorage& storage, const Core::Crash::CrashDetail::CrashUploadSnapshot& snapshot){
    return Core::Crash::CrashDetail::FlushPendingCrashReportsImpl(storage.arena, snapshot);
}

template<typename Arena>
[[nodiscard]] auto Retain(ScratchStorage& storage, const ::Path<Arena>& path, const Core::Crash::CrashSpoolRetentionConfig& config, AStringView protectedName){
    return Core::Crash::CrashDetail::ApplyCrashSpoolRetention(storage.arena, path, config, protectedName);
}

template<typename Arena>
[[nodiscard]] b8 SetOwnerExecutable(const ::Path<Arena>& path){
#if defined(FOUNDATION_PLATFORM_LINUX) && !defined(FOUNDATION_PLATFORM_ANDROID)
    return ::chmod(path.c_str(), S_IRWXU) == 0;
#else
    (void)path;
    return true;
#endif
}

[[nodiscard]] inline Optional<usize> ProcessHandleCount(){
#if defined(FOUNDATION_PLATFORM_WINDOWS)
    DWORD count = 0;
    if(::GetProcessHandleCount(::GetCurrentProcess(), &count))
        return static_cast<usize>(count);
#elif defined(FOUNDATION_PLATFORM_LINUX) && !defined(FOUNDATION_PLATFORM_ANDROID)
    HeapStorage storage;
    auto entries = DirectoryIterator<Core::Alloc::GlobalArena>::Create(::Path<Core::Alloc::GlobalArena>(storage.arena, "/proc/self/fd"));
    if(entries)
        return static_cast<usize>(entries->end() - entries->begin());
#endif
    return {};
}

[[nodiscard]] inline Core::Crash::CrashDetail::CrashState& Runtime()noexcept{
    return Core::Crash::CrashDetail::g_State;
}

};
NWB_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

