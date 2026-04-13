// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#pragma once


#include "platform.h"
#include "type.h"
#include "basic_string.h"
#include "containers.h"


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#if defined(NWB_PLATFORM_WINDOWS)
#include <stdlib.h>
#include <windows.h>
#if defined(NWB_UNICODE)
#include <shellapi.h>
#define NWB_DEFINE_APPLICATION_ENTRY_POINT(entryPoint) \
    int WINAPI wWinMain(_In_ HINSTANCE hInstance, _In_opt_ HINSTANCE hPrevInstance, _In_ LPWSTR lpCmdLine, _In_ int nCmdShow){ \
        (void)hPrevInstance; \
        (void)lpCmdLine; \
        (void)nCmdShow; \
        int nwbArgc = 0; \
        LPWSTR* nwbArgv = CommandLineToArgvW(GetCommandLineW(), &nwbArgc); \
        if(nwbArgv){ \
            const int nwbReturnCode = entryPoint(static_cast<isize>(nwbArgc), nwbArgv, hInstance); \
            LocalFree(nwbArgv); \
            return nwbReturnCode; \
        } \
        wchar nwbEmptyProgramName[] = L""; \
        wchar* nwbEmptyArgv[] = { nwbEmptyProgramName }; \
        return entryPoint(static_cast<isize>(1), nwbEmptyArgv, hInstance); \
    } \
    int main(int argc, char** argv){ \
        const usize nwbArgCount = argc > 0 ? static_cast<usize>(argc) : 0; \
        Vector<WString> nwbWideArgs; \
        Vector<wchar*> nwbWideArgv; \
        nwbWideArgs.reserve(nwbArgCount > 0 ? nwbArgCount : 1); \
        nwbWideArgv.reserve(nwbArgCount > 0 ? nwbArgCount : 1); \
        if(nwbArgCount == 0 || argv == nullptr){ \
            nwbWideArgs.push_back(WString()); \
            nwbWideArgv.push_back(nwbWideArgs.back().data()); \
        } \
        else{ \
            for(usize nwbArgIndex = 0; nwbArgIndex < nwbArgCount; ++nwbArgIndex){ \
                if(argv[nwbArgIndex] == nullptr){ \
                    nwbWideArgv.push_back(nullptr); \
                    continue; \
                } \
                nwbWideArgs.push_back(StringConvert(AStringView(argv[nwbArgIndex]))); \
                nwbWideArgv.push_back(nwbWideArgs.back().data()); \
            } \
        } \
        return entryPoint(static_cast<isize>(nwbWideArgv.size()), nwbWideArgv.data(), GetModuleHandleW(nullptr)); \
    }
#else
#define NWB_DEFINE_APPLICATION_ENTRY_POINT(entryPoint) \
    int WINAPI WinMain(_In_ HINSTANCE hInstance, _In_opt_ HINSTANCE hPrevInstance, _In_ LPSTR lpCmdLine, _In_ int nCmdShow){ \
        (void)hPrevInstance; \
        (void)lpCmdLine; \
        (void)nCmdShow; \
        return entryPoint(__argc, __argv, hInstance); \
    } \
    int main(int argc, char** argv){ \
        return entryPoint(static_cast<isize>(argc), argv, GetModuleHandleA(nullptr)); \
    }
#endif
#elif defined(NWB_PLATFORM_LINUX)
#define NWB_DEFINE_APPLICATION_ENTRY_POINT(entryPoint) \
    int main(int argc, char** argv){ \
        return entryPoint(static_cast<isize>(argc), argv, nullptr); \
    }
#endif

#ifndef NWB_DEFINE_APPLICATION_ENTRY_POINT
#define NWB_DEFINE_APPLICATION_ENTRY_POINT(entryPoint)
#endif


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

