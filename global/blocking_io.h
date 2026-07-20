#pragma once


#include "limit.h"
#include "platform.h"
#include "type.h"

#if defined(NWB_PLATFORM_WINDOWS)
#include <windows.h>
#elif defined(NWB_PLATFORM_LINUX) || defined(NWB_PLATFORM_ANDROID)
#include <errno.h>
#include <sys/types.h>
#include <unistd.h>
#endif


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#if defined(NWB_PLATFORM_LINUX) || defined(NWB_PLATFORM_ANDROID)
template<typename PointerT, typename OperationT>
[[nodiscard]] inline bool TransferAllPosix(PointerT cursor, const usize byteCount, OperationT operation)noexcept{
    usize remaining = byteCount;
    while(remaining > 0u){
        const ssize_t bytesTransferred = operation(cursor, remaining);
        if(bytesTransferred < 0){
            if(errno == EINTR)
                continue;
            return false;
        }
        if(bytesTransferred == 0)
            return false;

        cursor += static_cast<usize>(bytesTransferred);
        remaining -= static_cast<usize>(bytesTransferred);
    }
    return true;
}

[[nodiscard]] inline bool ReadAllFileDescriptor(const int fd, void* const data, const usize byteCount)noexcept{
    return TransferAllPosix(
        static_cast<u8*>(data),
        byteCount,
        [fd](u8* const cursor, const usize remaining)noexcept{
            return read(fd, cursor, remaining);
        }
    );
}

[[nodiscard]] inline bool WriteAllFileDescriptor(const int fd, const void* const data, const usize byteCount)noexcept{
    return TransferAllPosix(
        static_cast<const u8*>(data),
        byteCount,
        [fd](const u8* const cursor, const usize remaining)noexcept{
            return write(fd, cursor, remaining);
        }
    );
}
#endif


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#if defined(NWB_PLATFORM_WINDOWS)
[[nodiscard]] inline bool ReadAllWin32Handle(const HANDLE handle, void* const data, const usize byteCount)noexcept{
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

[[nodiscard]] inline bool WriteAllWin32Handle(const HANDLE handle, const void* const data, const usize byteCount)noexcept{
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

