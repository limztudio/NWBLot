// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#pragma once


#include "common.h"


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_COMMON_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace ApplicationEntryDetail{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


using UnicodeEntryPointFn = int(*)(isize, wchar**, void*);
using AnsiEntryPointFn = int(*)(isize, char**, void*);

template<typename EntryPoint, typename CharT>
[[nodiscard]] inline int InvokeEntryPoint(EntryPoint entryPoint, const isize argc, CharT** argv, void* instance){
    InitializerGuard commonInitializerGuard;
    if(!commonInitializerGuard.initialize())
        return -1;

    return entryPoint(argc, argv, instance);
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_COMMON_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#if defined(NWB_PLATFORM_WINDOWS)
#include <stdlib.h>
#include <windows.h>
#if defined(NWB_UNICODE)
#include <shellapi.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_COMMON_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace ApplicationEntryDetail{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


class WindowsCommandLineArgs final{
public:
    WindowsCommandLineArgs(){
        m_argv = CommandLineToArgvW(GetCommandLineW(), &m_argc);
    }
    ~WindowsCommandLineArgs(){
        if(m_argv)
            LocalFree(m_argv);
    }

    WindowsCommandLineArgs(const WindowsCommandLineArgs&) = delete;
    WindowsCommandLineArgs& operator=(const WindowsCommandLineArgs&) = delete;

    [[nodiscard]] bool valid()const{ return m_argv != nullptr; }
    [[nodiscard]] isize argc()const{ return static_cast<isize>(m_argc); }
    [[nodiscard]] wchar** argv()const{ return m_argv; }

private:
    int m_argc = 0;
    LPWSTR* m_argv = nullptr;
};

template<typename EntryPoint>
[[nodiscard]] inline int InvokeWindowsUnicodeEntryPoint(EntryPoint entryPoint, HINSTANCE hInstance){
    InitializerGuard commonInitializerGuard;
    if(!commonInitializerGuard.initialize())
        return -1;

    WindowsCommandLineArgs args;
    if(args.valid())
        return entryPoint(args.argc(), args.argv(), hInstance);

    return -1;
}

template<typename EntryPoint>
[[nodiscard]] inline int InvokeWindowsUnicodeConsoleEntryPoint(EntryPoint entryPoint){
    InitializerGuard commonInitializerGuard;
    if(!commonInitializerGuard.initialize())
        return -1;

    WindowsCommandLineArgs args;
    if(args.valid())
        return entryPoint(args.argc(), args.argv(), GetModuleHandleW(nullptr));

    return -1;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_COMMON_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#define NWB_DEFINE_APPLICATION_ENTRY_POINT(entryPoint) \
    int WINAPI wWinMain(_In_ HINSTANCE hInstance, _In_opt_ HINSTANCE hPrevInstance, _In_ LPWSTR lpCmdLine, _In_ int nCmdShow){ \
        static_cast<void>(hPrevInstance); \
        static_cast<void>(lpCmdLine); \
        static_cast<void>(nCmdShow); \
        return ::NWB::Core::Common::ApplicationEntryDetail::InvokeWindowsUnicodeEntryPoint(static_cast<::NWB::Core::Common::ApplicationEntryDetail::UnicodeEntryPointFn>(entryPoint), hInstance); \
    } \
    int main(int, char**){ \
        return ::NWB::Core::Common::ApplicationEntryDetail::InvokeWindowsUnicodeConsoleEntryPoint(static_cast<::NWB::Core::Common::ApplicationEntryDetail::UnicodeEntryPointFn>(entryPoint)); \
    }
#else
#define NWB_DEFINE_APPLICATION_ENTRY_POINT(entryPoint) \
    int WINAPI WinMain(_In_ HINSTANCE hInstance, _In_opt_ HINSTANCE hPrevInstance, _In_ LPSTR lpCmdLine, _In_ int nCmdShow){ \
        static_cast<void>(hPrevInstance); \
        static_cast<void>(lpCmdLine); \
        static_cast<void>(nCmdShow); \
        return ::NWB::Core::Common::ApplicationEntryDetail::InvokeEntryPoint(static_cast<::NWB::Core::Common::ApplicationEntryDetail::AnsiEntryPointFn>(entryPoint), static_cast<isize>(__argc), __argv, hInstance); \
    } \
    int main(int argc, char** argv){ \
        return ::NWB::Core::Common::ApplicationEntryDetail::InvokeEntryPoint(static_cast<::NWB::Core::Common::ApplicationEntryDetail::AnsiEntryPointFn>(entryPoint), static_cast<isize>(argc), argv, GetModuleHandleA(nullptr)); \
    }
#endif
#elif defined(NWB_PLATFORM_LINUX)
#define NWB_DEFINE_APPLICATION_ENTRY_POINT(entryPoint) \
    int main(int argc, char** argv){ \
        return ::NWB::Core::Common::ApplicationEntryDetail::InvokeEntryPoint(static_cast<::NWB::Core::Common::ApplicationEntryDetail::AnsiEntryPointFn>(entryPoint), static_cast<isize>(argc), argv, nullptr); \
    }
#endif

#ifndef NWB_DEFINE_APPLICATION_ENTRY_POINT
#define NWB_DEFINE_APPLICATION_ENTRY_POINT(entryPoint)
#endif


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

