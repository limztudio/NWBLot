// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#pragma once


#include <engine_namespace.h>

#include <global/filesystem/operations_file_io.h>
#include <global/filesystem/path.h>
#include <global/filesystem/retention.h>
#include <global/filesystem/file_staging.h>
#include <global/filesystem/staged_file_writer.h>
#include <global/process_capture.h>
#include <global/shared_library.h>
#include <tests/common/gtest_sdk.h>
#include <tests/common/memory_sdk.h>

#if defined(FOUNDATION_PLATFORM_WINDOWS)
#include <windows.h>
#else
#include <unistd.h>
#endif


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_BEGIN
namespace Tests::GlobalCases{

using MeasuredPath = ::Path<Core::Alloc::GlobalArena>;
using PathComponent = typename MeasuredPath::Iterator;
inline void NextComponent(PathComponent& it){ ++it; }
[[nodiscard]] inline MeasuredPath OwnedComponent(const PathComponent& it){ return *it; }
[[nodiscard]] inline b8 SameComponent(const PathComponent& lhs, const PathComponent& rhs){ return lhs == rhs; }
inline void AppendPathText(MeasuredPath& path, TStringView suffix){ path += suffix; }
inline void AppendSubpath(MeasuredPath& path, TStringView child){ path /= child; }
[[nodiscard]] inline bool WithinPath(TStringView text, TStringView whole){
    return text.data() >= whole.data() && text.data() + text.size() <= whole.data() + whole.size();
}

[[noreturn]] inline void ThrowCaptureError(const char* reason){ throw RuntimeException(reason); }

template<typename P>
[[nodiscard]] auto CreateStagedFile(const P& path, Span<const u8> bytes, b8& created){ return StagedFileWriter(path).create(bytes.data(), bytes.size(), created); }
template<typename P>
[[nodiscard]] auto ReserveStagedPath(const P& path){ return StagedFileWriter(path).stageUnique(nullptr, 0u); }


struct OversizedBinarySource{
    inline static constexpr u8 s_Byte = 0u;
    [[nodiscard]] u64 size()const noexcept{ return static_cast<u64>(Limit<StreamSize>::s_Max) + 1u; }
    [[nodiscard]] b8 empty()const noexcept{ return false; }
    [[nodiscard]] const u8* data()const noexcept{ return &s_Byte; }
};

template<typename P>
[[nodiscard]] b8 CreateTestHardLink(const P& target, const P& linkPath){
#if defined(FOUNDATION_PLATFORM_WINDOWS)
    return CreateHardLink(linkPath.c_str(), target.c_str(), nullptr) != 0;
#else
    return ::link(target.c_str(), linkPath.c_str()) == 0;
#endif
}

template<typename P>
[[nodiscard]] i32 CreateTestSymlink(const char* target, const P& path){
#if !defined(FOUNDATION_PLATFORM_WINDOWS)
    return ::symlink(target, path.c_str());
#else
    (void)target; (void)path;
    return -1;
#endif
}

struct ChildPoll{
    i32 result = -1;
    b8 noChild = false;
};

[[nodiscard]] inline ChildPoll PollChild(i64 child){
#if defined(FOUNDATION_PLATFORM_LINUX) && !defined(FOUNDATION_PLATFORM_ANDROID)
    int status = 0;
    errno = 0;
    const pid_t result = ::waitpid(static_cast<pid_t>(child), &status, WNOHANG);
    return { static_cast<i32>(result), errno == ECHILD };
#else
    (void)child;
    return {};
#endif
}

inline void RetireTestChild(i64 child){
#if defined(FOUNDATION_PLATFORM_LINUX) && !defined(FOUNDATION_PLATFORM_ANDROID)
    ProcessExecutionDetail::KillAndReapProcess(static_cast<pid_t>(child));
#else
    (void)child;
#endif
}

[[nodiscard]] inline Expected<u64, ErrorCondition> ProcessMilliseconds(){
#if defined(FOUNDATION_PLATFORM_LINUX) && !defined(FOUNDATION_PLATFORM_ANDROID)
    return ProcessExecutionDetail::MonotonicMilliseconds();
#else
    return MakeUnexpected(ErrorCondition::operation_not_supported);
#endif
}

#if defined(FOUNDATION_PLATFORM_LINUX)
inline constexpr u64 s_MaxChildPid = static_cast<u64>(Limit<pid_t>::s_Max);
#else
inline constexpr u64 s_MaxChildPid = 0u;
#endif

#if defined(FOUNDATION_PLATFORM_WINDOWS)
using ProcessIdFunction = DWORD(WINAPI*)();
#else
using ProcessIdFunction = u32(*)();
#endif
[[nodiscard]] inline u32 CallProcessId(ProcessIdFunction fn){ return static_cast<u32>(fn()); }
[[nodiscard]] inline auto OpenTestLibrary(SharedLibrary& lib, HeapStorage& storage, TStringView name){ return lib.open(storage.arena, name); }
[[nodiscard]] inline auto ResolveProcessId(SharedLibrary& lib, HeapStorage& storage, AStringView name){ return lib.resolve<ProcessIdFunction>(storage.arena, name); }

};
NWB_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

