// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include <tests/common/simd_memory_guard_fixture.h>

#include <global/limit.h>
#include <global/simd/memory.h>

#if defined(NWB_PLATFORM_WINDOWS)
#include <Windows.h>
#elif defined(NWB_PLATFORM_LINUX)
#include <sys/mman.h>
#include <unistd.h>
#endif


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace Tests{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#if defined(NWB_PLATFORM_WINDOWS) || defined(NWB_PLATFORM_LINUX)
GuardedBytes::GuardedBytes()noexcept{
#if defined(NWB_PLATFORM_WINDOWS)
    SYSTEM_INFO systemInfo{};
    GetSystemInfo(&systemInfo);
    pageBytes = static_cast<usize>(systemInfo.dwPageSize);
#else
    const long systemPageBytes = sysconf(_SC_PAGESIZE);
    if(systemPageBytes <= 0)
        return;
    pageBytes = static_cast<usize>(systemPageBytes);
#endif
    if(pageBytes == 0u || pageBytes % s_SIMDMemoryCopyAlignment != 0u || pageBytes > Limit<usize>::s_Max / 6u)
        return;
    accessibleBytes = pageBytes * 4u;
    mappingBytes = pageBytes * 6u;
#if defined(NWB_PLATFORM_WINDOWS)
    mapping = VirtualAlloc(nullptr, mappingBytes, MEM_RESERVE, PAGE_NOACCESS);
    if(mapping == nullptr)
        return;
    void* const committed = VirtualAlloc(static_cast<u8*>(mapping) + pageBytes, accessibleBytes, MEM_COMMIT, PAGE_READWRITE);
    if(committed == nullptr)
        return;
#else
    void* const allocated = mmap(nullptr, mappingBytes, PROT_NONE, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
    if(allocated == MAP_FAILED)
        return;
    mapping = allocated;
    if(mprotect(static_cast<u8*>(mapping) + pageBytes, accessibleBytes, PROT_READ | PROT_WRITE) != 0)
        return;
#endif
    // The copy API writes existing byte objects, including at both ends of this accessible region.
    data = ::new(static_cast<u8*>(mapping) + pageBytes) u8[accessibleBytes];
    lowerGuard = static_cast<u8*>(mapping);
    upperGuard = data + accessibleBytes;
}

GuardedBytes::~GuardedBytes()noexcept{
    if(mapping == nullptr)
        return;
#if defined(NWB_PLATFORM_WINDOWS)
    if(VirtualFree(mapping, 0u, MEM_RELEASE) == 0)
        abort();
#else
    if(munmap(mapping, mappingBytes) != 0)
        abort();
#endif
}
#endif


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

