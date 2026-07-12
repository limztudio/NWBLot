
#pragma once


#include "type.h"

#if defined(NWB_PLATFORM_WINDOWS)
#include <windows.h>
#else
#include <dlfcn.h>
#endif


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


// Minimal RAII loader for a platform shared library (.dll / .so). Opens by a tchar name (matching the
// platform's native string type), resolves C entry points by their narrow symbol name, and frees the library
// on destruction. For optional runtime dependencies that are loaded dynamically rather than linked, so a
// missing library is a recoverable condition instead of a load-time failure.
class SharedLibrary{
public:
    SharedLibrary() = default;
    ~SharedLibrary(){ close(); }
    SharedLibrary(const SharedLibrary&) = delete;
    SharedLibrary& operator=(const SharedLibrary&) = delete;

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

