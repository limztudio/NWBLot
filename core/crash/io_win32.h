// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#pragma once


#include "internal.h"


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_CRASH_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace Detail{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#if defined(NWB_PLATFORM_WINDOWS)
[[nodiscard]] inline bool ReadAllWin32(const HANDLE handle, void* const data, const usize byteCount)noexcept{
    u8* cursor = static_cast<u8*>(data);
    usize remaining = byteCount;
    while(remaining > 0u){
        DWORD bytesRead = 0u;
        const DWORD requestSize = remaining > static_cast<usize>(Limit<DWORD>::s_Max)
            ? Limit<DWORD>::s_Max
            : static_cast<DWORD>(remaining)
        ;
        if(!ReadFile(handle, cursor, requestSize, &bytesRead, nullptr) || bytesRead == 0u)
            return false;

        cursor += bytesRead;
        remaining -= bytesRead;
    }
    return true;
}

[[nodiscard]] inline bool WriteAllWin32(const HANDLE handle, const void* const data, const usize byteCount)noexcept{
    const u8* cursor = static_cast<const u8*>(data);
    usize remaining = byteCount;
    while(remaining > 0u){
        DWORD bytesWritten = 0u;
        const DWORD requestSize = remaining > static_cast<usize>(Limit<DWORD>::s_Max)
            ? Limit<DWORD>::s_Max
            : static_cast<DWORD>(remaining)
        ;
        if(!WriteFile(handle, cursor, requestSize, &bytesWritten, nullptr) || bytesWritten == 0u)
            return false;

        cursor += bytesWritten;
        remaining -= bytesWritten;
    }
    return true;
}
#endif


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_CRASH_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
