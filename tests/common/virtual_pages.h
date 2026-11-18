// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#pragma once


#include <engine_namespace.h>

#include <global/limit.h>
#include <global/process.h>
#include <global/simd/memory.h>

#include <cstdlib>
#include <new>

#if defined(FOUNDATION_PLATFORM_LINUX)
#include <sys/mman.h>
#endif


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_BEGIN
namespace Tests{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


using ByteAddress = u8*;
using ReadAddress = const void*;

#if defined(FOUNDATION_PLATFORM_WINDOWS) || defined(FOUNDATION_PLATFORM_LINUX)
inline constexpr b8 s_HasVirtualPages = true;
#else
inline constexpr b8 s_HasVirtualPages = false;
#endif

template<typename Buffer>
inline ByteAddress WritableBytesStart(Buffer& buffer)noexcept{ return buffer.data(); }

inline u8 LoadByte(ReadAddress address, const usize offset)noexcept{ return static_cast<const u8*>(address)[offset]; }

inline void StoreByte(ByteAddress address, const usize offset, const u8 value)noexcept{ address[offset] = value; }

inline usize VirtualPageSize()noexcept{
#if defined(FOUNDATION_PLATFORM_WINDOWS)
    SYSTEM_INFO info{};
    GetSystemInfo(&info);
    return static_cast<usize>(info.dwPageSize);
#elif defined(FOUNDATION_PLATFORM_LINUX)
    const long size = sysconf(_SC_PAGESIZE);
    return size > 0 ? static_cast<usize>(size) : 0u;
#else
    return 0u;
#endif
}

inline ByteAddress ReserveVirtualPages(const usize bytes)noexcept{
#if defined(FOUNDATION_PLATFORM_WINDOWS)
    return static_cast<ByteAddress>(VirtualAlloc(nullptr, bytes, MEM_RESERVE, PAGE_NOACCESS));
#elif defined(FOUNDATION_PLATFORM_LINUX)
    void* const mapping = mmap(nullptr, bytes, PROT_NONE, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
    return mapping == MAP_FAILED ? nullptr : static_cast<ByteAddress>(mapping);
#else
    return nullptr;
#endif
}

inline b8 MakeVirtualPagesWritable(ByteAddress first, const usize bytes)noexcept{
#if defined(FOUNDATION_PLATFORM_WINDOWS)
    return VirtualAlloc(first, bytes, MEM_COMMIT, PAGE_READWRITE) != nullptr;
#elif defined(FOUNDATION_PLATFORM_LINUX)
    return mprotect(first, bytes, PROT_READ | PROT_WRITE) == 0;
#else
    return false;
#endif
}

inline b8 ReleaseVirtualPages(ByteAddress first, [[maybe_unused]] const usize bytes)noexcept{
#if defined(FOUNDATION_PLATFORM_WINDOWS)
    return VirtualFree(first, 0u, MEM_RELEASE) != 0;
#elif defined(FOUNDATION_PLATFORM_LINUX)
    return munmap(first, bytes) == 0;
#else
    return false;
#endif
}

inline b8 HasByteAddress(ReadAddress address)noexcept{
    return address != nullptr;
}

inline ByteAddress GuardAddress(ReadAddress base, const usize offset)noexcept{
    return reinterpret_cast<ByteAddress>(reinterpret_cast<usize>(base) + offset);
}

inline ByteAddress BeginByteLifetimes(ByteAddress first, const usize count)noexcept{
    return ::new(first) u8[count];
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};
NWB_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

