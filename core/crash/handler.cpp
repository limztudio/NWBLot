// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "internal.h"

#include <cstdlib>

#if defined(NWB_PLATFORM_WINDOWS)
#include <windows.h>
#elif defined(NWB_PLATFORM_LINUX)
#include <errno.h>
#include <unistd.h>
#endif


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_CRASH_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace __hidden_crash_handler{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


template<typename CharT>
[[nodiscard]] static bool __hidden_arg_equals(const CharT* lhs, const CharT* rhs)noexcept{
    if(!lhs || !rhs)
        return false;

    while(*lhs || *rhs){
        if(*lhs != *rhs)
            return false;
        ++lhs;
        ++rhs;
    }
    return true;
}

template<typename CharT>
[[nodiscard]] static u64 __hidden_parse_u64(const CharT* text)noexcept{
    u64 value = 0u;
    if(!text)
        return value;

    while(*text >= static_cast<CharT>('0') && *text <= static_cast<CharT>('9')){
        value = (value * 10u) + static_cast<u64>(*text - static_cast<CharT>('0'));
        ++text;
    }
    return value;
}

#if defined(NWB_PLATFORM_WINDOWS)
[[nodiscard]] static bool __hidden_read_all(const HANDLE handle, void* const data, const usize byteCount)noexcept{
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
#elif defined(NWB_PLATFORM_LINUX)
[[nodiscard]] static bool __hidden_read_all(const int fd, void* const data, const usize byteCount)noexcept{
    u8* cursor = static_cast<u8*>(data);
    usize remaining = byteCount;
    while(remaining > 0u){
        const ssize_t bytesRead = read(fd, cursor, remaining);
        if(bytesRead < 0){
            if(errno == EINTR)
                continue;
            return false;
        }
        if(bytesRead == 0)
            return false;

        cursor += static_cast<usize>(bytesRead);
        remaining -= static_cast<usize>(bytesRead);
    }
    return true;
}
#endif


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


int RunCrashHandlerProcess(const isize argc, tchar** argv){
    static_cast<void>(Detail::DumpArena());

#if defined(NWB_PLATFORM_WINDOWS)
    HANDLE requestReadHandle = INVALID_HANDLE_VALUE;
    HANDLE ackEvent = nullptr;

    for(isize i = 1; i + 1 < argc; ++i){
        if(__hidden_crash_handler::__hidden_arg_equals(argv[i], NWB_TEXT("--request-handle")))
            requestReadHandle = reinterpret_cast<HANDLE>(static_cast<uintptr_t>(__hidden_crash_handler::__hidden_parse_u64(argv[++i])));
        else if(__hidden_crash_handler::__hidden_arg_equals(argv[i], NWB_TEXT("--ack-event")))
            ackEvent = reinterpret_cast<HANDLE>(static_cast<uintptr_t>(__hidden_crash_handler::__hidden_parse_u64(argv[++i])));
    }

    if(requestReadHandle == INVALID_HANDLE_VALUE)
        return -1;

    for(;;){
        Detail::CrashRequest request;
        if(!__hidden_crash_handler::__hidden_read_all(requestReadHandle, &request, sizeof(request)))
            break;

        if(Detail::WriteCrashPackage(request))
            static_cast<void>(Detail::UploadCrashPackage(request));
        if(ackEvent)
            SetEvent(ackEvent);
    }

    return 0;
#elif defined(NWB_PLATFORM_LINUX)
    int requestReadFd = -1;
    for(isize i = 1; i + 1 < argc; ++i){
        if(__hidden_crash_handler::__hidden_arg_equals(argv[i], "--request-fd"))
            requestReadFd = static_cast<int>(__hidden_crash_handler::__hidden_parse_u64(argv[++i]));
    }

    if(requestReadFd < 0)
        return -1;

    for(;;){
        Detail::CrashRequest request;
        if(!__hidden_crash_handler::__hidden_read_all(requestReadFd, &request, sizeof(request)))
            break;

        if(Detail::WriteCrashPackage(request))
            static_cast<void>(Detail::UploadCrashPackage(request));
    }

    return 0;
#else
    static_cast<void>(argc);
    static_cast<void>(argv);
    return -1;
#endif
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_CRASH_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

