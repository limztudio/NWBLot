#include "core.h"

#include <tbb/scalable_allocator.h>

#if defined(NWB_PLATFORM_WINDOWS)
#include <malloc.h>
#else
#include <stdlib.h>
#endif

#include "general.h"


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_ALLOC_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


void* CoreAlloc(usize size, const char* log){
    static_cast<void>(log);
    void* cur = scalable_malloc(size);
    return cur;
}
void* CoreRealloc(void* p, usize size, const char* log){
    static_cast<void>(log);
    void* cur = scalable_realloc(p, size);
    return cur;
}
void* CoreReallocAligned(void* p, usize size, usize align, const char* log){
    static_cast<void>(log);
    void* cur = scalable_aligned_realloc(p, size, align);
    return cur;
}
void* CoreAllocAligned(usize size, usize align, const char* log){
    static_cast<void>(log);
    void* cur = scalable_aligned_malloc(size, align);
    return cur;
}

usize CoreMsize(void* ptr)noexcept{
    return static_cast<usize>(scalable_msize(ptr));
}

void CoreFree(void* ptr, const char* log)noexcept{
    static_cast<void>(log);
    scalable_free(ptr);
}
void CoreFreeSize(void* ptr, usize size, const char* log)noexcept{
    static_cast<void>(size);
    static_cast<void>(log);
    scalable_free(ptr);
}
void CoreFreeAligned(void* ptr, const char* log)noexcept{
    static_cast<void>(log);
    scalable_aligned_free(ptr);
}
void CoreFreeSizeAligned(void* ptr, usize size, const char* log)noexcept{
    static_cast<void>(size);
    static_cast<void>(log);
    scalable_aligned_free(ptr);
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_ALLOC_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

