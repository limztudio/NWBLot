// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#pragma once


#include "type.h"

#if defined(NWB_PLATFORM_WINDOWS)
#include <windows.h>
#else
#include <dlfcn.h>
#endif


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


// Minimal RAII loader for platform shared libraries (.dll / .so) for optional runtime dependencies.
class SharedLibrary{
public:
    SharedLibrary() = default;
    ~SharedLibrary(){ close(); }
    SharedLibrary(const SharedLibrary&) = delete;
    SharedLibrary& operator=(const SharedLibrary&) = delete;


public:
    [[nodiscard]] bool open(const tchar* name){
        if(m_handle)
            return true;
        if(!name)
            return false;

#if defined(NWB_PLATFORM_WINDOWS)
        m_handle = ::LoadLibrary(name);
#else
        m_handle = ::dlopen(name, RTLD_NOW | RTLD_LOCAL);
#endif
        return m_handle != nullptr;
    }

    [[nodiscard]] bool isOpen()const{ return m_handle != nullptr; }

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

    template<typename Fn>
    [[nodiscard]] bool resolve(const char* symbolName, Fn& outFn){
        outFn = reinterpret_cast<Fn>(resolveRaw(symbolName));
        return outFn != nullptr;
    }


private:
    [[nodiscard]] void* resolveRaw(const char* symbolName)const{
        if(!m_handle || !symbolName)
            return nullptr;

#if defined(NWB_PLATFORM_WINDOWS)
        return reinterpret_cast<void*>(::GetProcAddress(static_cast<HMODULE>(m_handle), symbolName));
#else
        return ::dlsym(m_handle, symbolName);
#endif
    }


private:
    void* m_handle = nullptr;
};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

