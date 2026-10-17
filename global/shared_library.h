// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#pragma once


#include "basic_string.h"
#include "expected.h"
#include "error_code.h"
#include "diagnostics.h"
#include "termination.h"

#if defined(NWB_PLATFORM_WINDOWS)
#include <windows.h>
#else
#include <dlfcn.h>
#endif


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


struct SharedLibraryFailure{
    ErrorCode code;
    DiagnosticEventText detail{};
    bool detailTruncated = false;


public:
    [[nodiscard]] static SharedLibraryFailure FromNativeText(const char* const nativeError)noexcept{
        SharedLibraryFailure failure{ .code = MakeErrorCode(ErrorCondition::io_error), };
        if(nativeError){
            usize index = 0u;
            while(nativeError[index] != 0 && index + 1u < LengthOf(failure.detail.value)){
                failure.detail.value[index] = nativeError[index];
                ++index;
            }
            failure.detail.value[index] = 0;
            failure.detailTruncated = nativeError[index] != 0;
        }
        return failure;
    }
};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


class SharedLibrary{
public:
    SharedLibrary()noexcept = default;
    ~SharedLibrary()noexcept{ close(); }
    SharedLibrary(const SharedLibrary&) = delete;


public:
    SharedLibrary& operator=(const SharedLibrary&) = delete;


public:
    template<typename ArenaT>
    [[nodiscard]] Expected<void, SharedLibraryFailure> open(ArenaT& arena, const TStringView name){
        if(m_handle)
            return {};
        if(name.empty() || name.find(tchar{}) != TStringView::npos)
            return MakeUnexpected(SharedLibraryFailure{ .code = MakeErrorCode(ErrorCondition::invalid_argument), });

        const TString<ArenaT> nativeName(name, arena);
#if defined(NWB_PLATFORM_WINDOWS)
        m_handle = ::LoadLibrary(nativeName.c_str());
        if(!m_handle){
            const DWORD error = GetLastError();
            return MakeUnexpected(SharedLibraryFailure{ .code = ErrorCode(static_cast<i32>(error), std::system_category()), });
        }
#else
        m_handle = ::dlopen(nativeName.c_str(), RTLD_NOW | RTLD_LOCAL);
        if(!m_handle){
            const char* const nativeError = ::dlerror();
            return MakeUnexpected(SharedLibraryFailure::FromNativeText(nativeError));
        }
#endif
        return {};
    }

    [[nodiscard]] bool isOpen()const noexcept{ return m_handle != nullptr; }

    void close(){
        if(!m_handle)
            return;

#if defined(NWB_PLATFORM_WINDOWS)
        const bool released = ::FreeLibrary(static_cast<HMODULE>(m_handle)) != 0;
#else
        const bool released = ::dlclose(m_handle) == 0;
#endif
        if(!released)
            TerminateInvariant();
        m_handle = nullptr;
    }

    template<typename Fn, typename ArenaT>
    [[nodiscard]] Expected<Fn, SharedLibraryFailure> resolve(ArenaT& arena, const AStringView symbolName){
        const auto symbol = resolveRaw(arena, symbolName);
        if(!symbol)
            return MakeUnexpected(symbol.error());
        return reinterpret_cast<Fn>(*symbol);
    }


private:
    template<typename ArenaT>
    [[nodiscard]] Expected<void*, SharedLibraryFailure> resolveRaw(ArenaT& arena, const AStringView symbolName)const{
        if(!m_handle)
            return MakeUnexpected(SharedLibraryFailure{ .code = MakeErrorCode(ErrorCondition::bad_file_descriptor), });
        if(symbolName.empty() || symbolName.find(char{}) != AStringView::npos)
            return MakeUnexpected(SharedLibraryFailure{ .code = MakeErrorCode(ErrorCondition::invalid_argument), });

        const AString<ArenaT> nativeSymbolName(symbolName, arena);
#if defined(NWB_PLATFORM_WINDOWS)
        void* const symbol = reinterpret_cast<void*>(::GetProcAddress(static_cast<HMODULE>(m_handle), nativeSymbolName.c_str()));
        if(!symbol){
            const DWORD error = GetLastError();
            return MakeUnexpected(SharedLibraryFailure{ .code = ErrorCode(static_cast<i32>(error), std::system_category()), });
        }
#else
        // dlsym requires clearing old loader state to distinguish a null symbol value from an actual lookup failure.
        ::dlerror();
        void* const symbol = ::dlsym(m_handle, nativeSymbolName.c_str());
        const char* const nativeError = ::dlerror();
        if(nativeError)
            return MakeUnexpected(SharedLibraryFailure::FromNativeText(nativeError));
        if(!symbol)
            return MakeUnexpected(SharedLibraryFailure{ .code = MakeErrorCode(ErrorCondition::bad_address), });
#endif
        return symbol;
    }


private:
    void* m_handle = nullptr;
};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

