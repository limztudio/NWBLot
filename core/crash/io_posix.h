// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#pragma once


#include "internal.h"

#if defined(NWB_PLATFORM_LINUX) || defined(NWB_PLATFORM_ANDROID)
#include <errno.h>
#include <sys/types.h>
#include <unistd.h>
#endif


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_CRASH_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace Detail{


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

[[nodiscard]] inline bool ReadAllFd(const int fd, void* const data, const usize byteCount)noexcept{
    return TransferAllPosix(
        static_cast<u8*>(data),
        byteCount,
        [fd](u8* const cursor, const usize remaining)noexcept{
            return read(fd, cursor, remaining);
        }
    );
}

[[nodiscard]] inline bool WriteAllFd(const int fd, const void* const data, const usize byteCount)noexcept{
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


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_CRASH_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

