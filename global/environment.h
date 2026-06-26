// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#pragma once


#include "basic_string.h"
#include "compile.h"
#include "type.h"

#include <cstdlib>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


template<typename ArenaT>
[[nodiscard]] inline bool ReadEnvironmentVariable(const char* name, AString<ArenaT>& outValue){
    outValue.clear();
    if(!name || name[0] == 0)
        return false;

#if defined(_MSC_VER)
    char* value = nullptr;
    size_t valueSize = 0u;
    if(::_dupenv_s(&value, &valueSize, name) != 0 || !value)
        return false;

    const usize valueLength = valueSize > 0u ? static_cast<usize>(valueSize - 1u) : static_cast<usize>(NWB_STRLEN(value));
    outValue.assign(value, valueLength);
    ::free(value);
    return true;
#else
    const char* const value = ::std::getenv(name);
    if(!value)
        return false;

    outValue.assign(value);
    return true;
#endif
}


// Arena-free variant: reads an environment variable into a caller-provided fixed buffer (always NUL-terminated; the
// value is bounded-copied, truncated if longer than the buffer). Returns true when the variable is set AND non-empty.
// For one-shot diagnostic reads from a context without a handy arena (a static method, an init path). Uses the same
// MSVC `_dupenv_s` / POSIX `getenv` split as ReadEnvironmentVariable, so no deprecated-CRT warning is emitted.
[[nodiscard]] inline bool ReadEnvironmentVariableBuffer(const char* name, char* buffer, usize bufferSize){
    if(!buffer || bufferSize == 0u)
        return false;
    buffer[0] = 0;
    if(!name || name[0] == 0)
        return false;

#if defined(_MSC_VER)
    char* value = nullptr;
    size_t valueSize = 0u;
    if(::_dupenv_s(&value, &valueSize, name) != 0 || !value)
        return false;
#else
    const char* const value = ::std::getenv(name);
    if(!value)
        return false;
#endif

    usize i = 0u;
    for(; value[i] != 0 && (i + 1u) < bufferSize; ++i)
        buffer[i] = value[i];
    buffer[i] = 0;

#if defined(_MSC_VER)
    ::free(value);
#endif
    return buffer[0] != 0;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

