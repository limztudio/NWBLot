// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#pragma once


#include "basic_string.h"
#include "expected.h"

#if defined(NWB_PLATFORM_WINDOWS)
#include <windows.h>
#else
#include <dlfcn.h>
#endif


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


class SharedLibrary{
public:
    SharedLibrary() = default;
    ~SharedLibrary(){ close(); }
    SharedLibrary(const SharedLibrary&) = delete;
    SharedLibrary& operator=(const SharedLibrary&) = delete;


public:
    template<typename ArenaT>
    [[nodiscard]] bool open(ArenaT& arena, const TStringView name){
        if(m_handle)
            return true;
        if(name.empty() || name.find(tchar{}) != TStringView::npos)
            return false;

        const TString<ArenaT> nativeName(name, arena);
#if defined(NWB_PLATFORM_WINDOWS)
        m_handle = ::LoadLibrary(nativeName.c_str());
#else
        m_handle = ::dlopen(nativeName.c_str(), RTLD_NOW | RTLD_LOCAL);
#endif
        return m_handle != nullptr;
    }

    [[nodiscard]] bool isOpen()const noexcept{ return m_handle != nullptr; }

    void close(){
        if(!m_handle)
            return;

#if defined(NWB_PLATFORM_WINDOWS)
        ::FreeLibrary(static_cast<HMODULE>(m_handle));
#else
        ::dlclose(m_handle);
#endif
        m_handle = nullptr;
    }

    template<typename Fn, typename ArenaT>
    [[nodiscard]] Expected<Fn> resolve(ArenaT& arena, const AStringView symbolName){
        const Fn function = reinterpret_cast<Fn>(resolveRaw(arena, symbolName));
        if(!function)
            return MakeUnexpected(Failure{});
        return function;
    }


private:
    template<typename ArenaT>
    [[nodiscard]] void* resolveRaw(ArenaT& arena, const AStringView symbolName)const{
        if(!m_handle || symbolName.empty() || symbolName.find(char{}) != AStringView::npos)
            return nullptr;

        const AString<ArenaT> nativeSymbolName(symbolName, arena);
#if defined(NWB_PLATFORM_WINDOWS)
        return reinterpret_cast<void*>(::GetProcAddress(static_cast<HMODULE>(m_handle), nativeSymbolName.c_str()));
#else
        return ::dlsym(m_handle, nativeSymbolName.c_str());
#endif
    }


private:
    void* m_handle = nullptr;
};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

