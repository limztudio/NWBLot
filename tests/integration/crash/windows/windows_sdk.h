// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#pragma once


#include <engine_namespace.h>

#include <core/crash/internal.h>
#include <global/termination.h>
#include <dbghelp.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_BEGIN
namespace Tests::WindowsCrashSdk{
using namespace Core::Crash::CrashDetail;

using ThreadHandle = HANDLE;
[[nodiscard]] inline ThreadHandle HandlerThread()noexcept{ return g_State.handlerProcessInfo.hThread; }
[[nodiscard]] inline b8 IsValid(ThreadHandle handle)noexcept{ return handle != nullptr; }
[[nodiscard]] inline DWORD WaitForHandled(DWORD timeout)noexcept{ return ::WaitForSingleObject(g_State.crashHandledEvent, timeout); }

class SuspendedThread final : NoCopy{
public:
    explicit SuspendedThread(HANDLE thread)noexcept : m_thread(thread), m_previousSuspendCount(::SuspendThread(thread)), m_active(m_previousSuspendCount != DWORD(-1)){}
    ~SuspendedThread()noexcept{
        if(m_active && ::ResumeThread(m_thread) == DWORD(-1))
            TerminateInvariant();
    }
    [[nodiscard]] DWORD previousSuspendCount()const noexcept{ return m_previousSuspendCount; }
    [[nodiscard]] DWORD resume()noexcept{
        const DWORD result = ::ResumeThread(m_thread);
        if(result != DWORD(-1))
            m_active = false;
        return result;
    }
private:
    HANDLE m_thread;
    DWORD m_previousSuspendCount;
    b8 m_active;
};

class FileHandle final : NoCopy{
public:
    template<typename Arena>
    FileHandle(const ::Path<Arena>& path, DWORD access, DWORD share, b8 inherit, DWORD creation, DWORD flags)noexcept{
        SECURITY_ATTRIBUTES security = {sizeof(security), nullptr, inherit};
        m_handle = ::CreateFileW(path.c_str(), access, share, &security, creation, flags, nullptr);
    }
    ~FileHandle()noexcept{ if(valid()) ::CloseHandle(m_handle); }
    [[nodiscard]] b8 valid()const noexcept{ return m_handle != INVALID_HANDLE_VALUE; }
    [[nodiscard]] b8 close()noexcept{
        if(!::CloseHandle(m_handle)) return false;
        m_handle = INVALID_HANDLE_VALUE;
        return true;
    }
private:
    HANDLE m_handle = INVALID_HANDLE_VALUE;
};

template<typename Arena>
[[nodiscard]] b8 Delete(const ::Path<Arena>& path)noexcept{ return ::DeleteFileW(path.c_str()) != FALSE; }

[[nodiscard]] inline CONTEXT& SourceContext(ManualDumpContextStorage& storage)noexcept{ return storage.context; }
[[nodiscard]] inline EXCEPTION_RECORD& SourceException(ManualDumpContextStorage& storage)noexcept{ return storage.exceptionRecord; }
[[nodiscard]] inline EXCEPTION_POINTERS& SourcePointers(ManualDumpContextStorage& storage)noexcept{ return storage.exceptionPointers; }
[[nodiscard]] inline CONTEXT& RequestContext(CrashRequest& request)noexcept{ return request.manualContext; }
[[nodiscard]] inline EXCEPTION_RECORD& RequestException(CrashRequest& request)noexcept{ return request.manualExceptionRecord; }

inline void Link(EXCEPTION_RECORD& record, EXCEPTION_RECORD& next)noexcept{ record.ExceptionRecord = &next; }
inline void ClearLink(EXCEPTION_RECORD& record)noexcept{ record.ExceptionRecord = nullptr; }
template<typename T>
[[nodiscard]] usize PointerAddress(T* ptr)noexcept{ return reinterpret_cast<usize>(ptr); }
[[nodiscard]] inline u64 ProgramCounter(const CONTEXT& context)noexcept{
#if defined(_M_X64) || defined(__x86_64__)
    return context.Rip;
#elif defined(_M_IX86) || defined(__i386__)
    return context.Eip;
#elif defined(_M_ARM64) || defined(__aarch64__)
    return context.Pc;
#else
#error Unsupported Windows context architecture
#endif
}

struct DumpStream{
    b8 found = false;
    usize address = 0;
    u32 size = 0;
};

[[nodiscard]] inline DumpStream ReadStream(Span<u8> bytes, u32 kind)noexcept{
    MINIDUMP_DIRECTORY* directory = nullptr;
    void* stream = nullptr;
    ULONG size = 0;
    const BOOL found = ::MiniDumpReadDumpStream(bytes.data(), kind, &directory, &stream, &size);
    return {found != FALSE, reinterpret_cast<usize>(stream), size};
}

};
NWB_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

