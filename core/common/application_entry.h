// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#pragma once


#include "log.h"
#include "module.h"
#include "name_symbols.h"

#include <core/alloc/scratch.h>

#include <global/terminal_entry.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_COMMON_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace ApplicationEntryDetail{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


using UnicodeEntryPointFn = int(*)(isize, wchar**, void*);
using AnsiEntryPointFn = int(*)(isize, char**, void*);

inline constexpr int s_ApplicationEntryFailure = -1;
inline constexpr int s_EmptyArgCount = 0;
#if defined(NWB_BUILDMODE)
// Build-mode failure exits also write the allocating name-symbol file.
inline constexpr b8 s_TerminalFailureNoThrow = noexcept(!NameSymbols::WriteDefaultFile());
#else
inline constexpr b8 s_TerminalFailureNoThrow = true;
#endif


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


// RAII detach of Name callbacks on exit; WriteDefaultFile still works after uninstall.
class ScopedNameSymbolRegistry final : NoCopy{
public:
    ScopedNameSymbolRegistry(){ NameSymbols::InstallRuntimeRegistry(); }
    ~ScopedNameSymbolRegistry()noexcept{ NameSymbols::UninstallRuntimeRegistry(); }
};

template<typename Invoke>
[[nodiscard]] inline int InvokeWithInitializedCommon(Invoke&& invoke)noexcept(s_TerminalFailureNoThrow){
    const auto terminalFailure = []()noexcept(s_TerminalFailureNoThrow){
#if defined(NWB_BUILDMODE)
        if(!NameSymbols::WriteDefaultFile())
            return s_ApplicationEntryFailure;
#endif
        return s_ApplicationEntryFailure;
    };
    return InvokeTerminalEntry<GeneralException>([&](){
        const ScopedNameSymbolRegistry nameSymbolRegistry;

        InitializerGuard commonInitializerGuard;
        if(!commonInitializerGuard.initialize())
            return terminalFailure();

        const int result = Forward<Invoke>(invoke)();
#if defined(NWB_BUILDMODE)
        if(!NameSymbols::WriteDefaultFile())
            return s_ApplicationEntryFailure;
#endif
        return result;
    }, [&](const GeneralException&)noexcept(noexcept(terminalFailure())){ return terminalFailure(); }, terminalFailure
    );
}

template<typename EntryPoint, typename CharT>
[[nodiscard]] inline int InvokeEntryPoint(EntryPoint entryPoint, const isize argc, CharT** argv, void* instance)noexcept(s_TerminalFailureNoThrow && IsNothrowConstructible_V<EntryPoint, EntryPoint&> && IsNothrowDestructible_V<EntryPoint>){
    return InvokeWithInitializedCommon([entryPoint, argc, argv, instance]()noexcept(
        noexcept(DeclVal<const RemoveReference_T<EntryPoint>&>()(DeclVal<const isize&>(), DeclVal<CharT** const&>(), DeclVal<void* const&>()))
        && (
            !IsReference_V<decltype(DeclVal<const RemoveReference_T<EntryPoint>&>()(DeclVal<const isize&>(), DeclVal<CharT** const&>(), DeclVal<void* const&>()))>
            || IsNothrowConvertible_V<
                decltype(DeclVal<const RemoveReference_T<EntryPoint>&>()(DeclVal<const isize&>(), DeclVal<CharT** const&>(), DeclVal<void* const&>())),
                Decay_T<decltype(DeclVal<const RemoveReference_T<EntryPoint>&>()(DeclVal<const isize&>(), DeclVal<CharT** const&>(), DeclVal<void* const&>()))>
            >
        )
    ){ return entryPoint(argc, argv, instance); }
    );
}

#if defined(NWB_UNICODE)
template<typename Run>
[[nodiscard]] inline int InvokeWithUtf8Args(const isize argc, wchar** argv, Run&& run){
    const usize argCount = argc > 0 ? static_cast<usize>(argc) : 0;
    Alloc::ScratchArena arena(CommonArenaScope::s_ApplicationEntryArgsArena);
    Vector<AString<Alloc::ScratchArena>, Alloc::ScratchArena> utf8Args{arena};
    Vector<char*, Alloc::ScratchArena> utf8Argv{arena};
    utf8Args.reserve(argCount);
    utf8Argv.reserve(argCount + 1u);

    for(usize i = 0u; i < argCount; ++i){
        if(argv == nullptr || argv[i] == nullptr){
            utf8Args.emplace_back(arena);
            utf8Argv.push_back(nullptr);
            continue;
        }

        utf8Args.emplace_back(WideToUtf8(arena, WStringView(argv[i])));
        utf8Argv.push_back(utf8Args.back().data());
    }

    utf8Argv.push_back(nullptr);
    return Forward<Run>(run)(static_cast<int>(argCount), utf8Argv.data());
}
#endif


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


template<typename LoggerT, typename Run>
[[nodiscard]] inline int RunConsoleCommandLineTool(const TStringView appName, const int initFailureExitCode, const int argc, char** argv, Run&& run){
    LoggerT logger;
    if(!logger.init(appName)){
        NWB_TCERR << NWB_TEXT("[") << appName << NWB_TEXT("] logger.init() failed\n");
        return initFailureExitCode;
    }
    const LoggerRegistrationGuard loggerRegistrationGuard(logger, LoggerBreakPolicy::BreakOnFatal);
    return Forward<Run>(run)(argc, argv);
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_COMMON_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#if defined(NWB_PLATFORM_WINDOWS)
#include <windows.h>
#if defined(NWB_UNICODE)
#include <shellapi.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_COMMON_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace ApplicationEntryDetail{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


class WindowsCommandLineArgs final : NoCopy{
public:
    WindowsCommandLineArgs()noexcept{
        m_argv = CommandLineToArgvW(GetCommandLineW(), &m_argc);
    }
    ~WindowsCommandLineArgs()noexcept{
        if(m_argv)
            LocalFree(m_argv);
    }


public:
    [[nodiscard]] b8 valid()const noexcept{ return m_argv != nullptr; }

    [[nodiscard]] isize argc()const noexcept{ return static_cast<isize>(m_argc); }

    [[nodiscard]] wchar** argv()const noexcept{ return m_argv; }


private:
    int m_argc = s_EmptyArgCount;
    LPWSTR* m_argv = nullptr;
};

template<typename EntryPoint>
[[nodiscard]] inline int InvokeWindowsUnicodeEntryPoint(EntryPoint entryPoint, HINSTANCE hInstance)noexcept(s_TerminalFailureNoThrow && IsNothrowConstructible_V<EntryPoint, EntryPoint&> && IsNothrowDestructible_V<EntryPoint>){
    return InvokeWithInitializedCommon([entryPoint, hInstance](){
        WindowsCommandLineArgs args;
        if(args.valid())
            return entryPoint(args.argc(), args.argv(), hInstance);

        return s_ApplicationEntryFailure;
    });
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_COMMON_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#define NWB_DEFINE_APPLICATION_ENTRY_POINT(entryPoint) \
    int WINAPI wWinMain(_In_ HINSTANCE hInstance, [[maybe_unused]] _In_opt_ HINSTANCE hPrevInstance, [[maybe_unused]] _In_ LPWSTR lpCmdLine, [[maybe_unused]] _In_ int nCmdShow){ \
        return ::NWB::Core::Common::ApplicationEntryDetail::InvokeWindowsUnicodeEntryPoint(static_cast<::NWB::Core::Common::ApplicationEntryDetail::UnicodeEntryPointFn>(entryPoint), hInstance); \
    } \
    int main(int, char**)noexcept(::NWB::Core::Common::ApplicationEntryDetail::s_TerminalFailureNoThrow){ \
        return ::NWB::Core::Common::ApplicationEntryDetail::InvokeWindowsUnicodeEntryPoint(static_cast<::NWB::Core::Common::ApplicationEntryDetail::UnicodeEntryPointFn>(entryPoint), GetModuleHandleW(nullptr)); \
    }
#else
#define NWB_DEFINE_APPLICATION_ENTRY_POINT(entryPoint) \
    int WINAPI WinMain(_In_ HINSTANCE hInstance, [[maybe_unused]] _In_opt_ HINSTANCE hPrevInstance, [[maybe_unused]] _In_ LPSTR lpCmdLine, [[maybe_unused]] _In_ int nCmdShow){ \
        return ::NWB::Core::Common::ApplicationEntryDetail::InvokeEntryPoint(static_cast<::NWB::Core::Common::ApplicationEntryDetail::AnsiEntryPointFn>(entryPoint), static_cast<isize>(__argc), __argv, hInstance); \
    } \
    int main(int argc, char** argv)noexcept(::NWB::Core::Common::ApplicationEntryDetail::s_TerminalFailureNoThrow){ \
        return ::NWB::Core::Common::ApplicationEntryDetail::InvokeEntryPoint(static_cast<::NWB::Core::Common::ApplicationEntryDetail::AnsiEntryPointFn>(entryPoint), static_cast<isize>(argc), argv, GetModuleHandleA(nullptr)); \
    }
#endif
#elif defined(NWB_PLATFORM_LINUX)
#define NWB_DEFINE_APPLICATION_ENTRY_POINT(entryPoint) \
    int main(int argc, char** argv)noexcept(::NWB::Core::Common::ApplicationEntryDetail::s_TerminalFailureNoThrow){ \
        return ::NWB::Core::Common::ApplicationEntryDetail::InvokeEntryPoint(static_cast<::NWB::Core::Common::ApplicationEntryDetail::AnsiEntryPointFn>(entryPoint), static_cast<isize>(argc), argv, nullptr); \
    }
#endif

#ifndef NWB_DEFINE_APPLICATION_ENTRY_POINT
#define NWB_DEFINE_APPLICATION_ENTRY_POINT(entryPoint)
#endif

#if defined(NWB_PLATFORM_WINDOWS) && defined(NWB_UNICODE)
#define NWB_DEFINE_UTF8_APPLICATION_ENTRY_POINT(runFunction) \
    static int EntryPoint(const isize argc, wchar** argv, void*){ \
        return ::NWB::Core::Common::ApplicationEntryDetail::InvokeWithUtf8Args(argc, argv, runFunction); \
    } \
    NWB_DEFINE_APPLICATION_ENTRY_POINT(EntryPoint)
#else
#define NWB_DEFINE_UTF8_APPLICATION_ENTRY_POINT(runFunction) \
    static int EntryPoint(const isize argc, char** argv, void*){ \
        return (runFunction)(static_cast<int>(argc), argv); \
    } \
    NWB_DEFINE_APPLICATION_ENTRY_POINT(EntryPoint)
#endif

#define NWB_DEFINE_CONSOLE_TOOL_ENTRY_POINT(loggerType, appName, initFailureExitCode, runFunction) \
    static int RunConsoleToolEntryPoint(const int argc, char** argv){ \
        return ::NWB::Core::Common::RunConsoleCommandLineTool<loggerType>(appName, initFailureExitCode, argc, argv, runFunction); \
    } \
    NWB_DEFINE_UTF8_APPLICATION_ENTRY_POINT(RunConsoleToolEntryPoint)


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

