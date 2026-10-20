// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#pragma once


#include "module.h"
#include "arena_names.h"
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
inline constexpr bool s_TerminalFailureNoThrow = noexcept(!NameSymbols::WriteDefaultFile());
#else
inline constexpr bool s_TerminalFailureNoThrow = true;
#endif


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


// RAII detach of Name callbacks on exit; WriteDefaultFile still works after uninstall.
class ScopedNameSymbolRegistry final{
public:
    ScopedNameSymbolRegistry(){ NameSymbols::InstallRuntimeRegistry(); }
    ~ScopedNameSymbolRegistry()noexcept{ NameSymbols::UninstallRuntimeRegistry(); }
    ScopedNameSymbolRegistry(const ScopedNameSymbolRegistry&) = delete;


public:
    ScopedNameSymbolRegistry& operator=(const ScopedNameSymbolRegistry&) = delete;
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

        utf8Args.emplace_back(BasicStringDetail::WideToUtf8(arena, WStringView(argv[i])));
        utf8Argv.push_back(utf8Args.back().data());
    }

    utf8Argv.push_back(nullptr);
    return Forward<Run>(run)(static_cast<int>(argCount), utf8Argv.data());
}
#endif


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
    WindowsCommandLineArgs()noexcept{
        m_argv = CommandLineToArgvW(GetCommandLineW(), &m_argc);
    }
    ~WindowsCommandLineArgs()noexcept{
        if(m_argv)
            LocalFree(m_argv);
    }
    WindowsCommandLineArgs(const WindowsCommandLineArgs&) = delete;


public:
    WindowsCommandLineArgs& operator=(const WindowsCommandLineArgs&) = delete;


public:
    [[nodiscard]] bool valid()const noexcept{ return m_argv != nullptr; }
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
    }
    );
}

template<typename EntryPoint>
[[nodiscard]] inline int InvokeWindowsUnicodeConsoleEntryPoint(EntryPoint entryPoint)noexcept(s_TerminalFailureNoThrow && IsNothrowConstructible_V<EntryPoint, EntryPoint&> && IsNothrowDestructible_V<EntryPoint>){
    return InvokeWithInitializedCommon([entryPoint](){
        WindowsCommandLineArgs args;
        if(args.valid())
            return entryPoint(args.argc(), args.argv(), GetModuleHandleW(nullptr));

        return s_ApplicationEntryFailure;
    }
    );
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

