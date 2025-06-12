// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "core.h"

#include <tbb/tbbmalloc_proxy.h>

#if defined(NWB_PLATFORM_WINDOWS)
#include <malloc.h>
#else
#include <stdlib.h>
#endif


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_ALLOC_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


void* CoreAlloc(usize size, const char* log){
    (void)log;
    return malloc(size);
}
void* CoreRealloc(void* p, usize size, const char* log){
    (void)log;
    return realloc(p, size);
}
void* CoreAllocAligned(usize size, usize align, const char* log){
    (void)log;
#if defined(NWB_PLATFORM_WINDOWS)
    return _aligned_malloc(size, align);
#else
    void* cur = nullptr;
    if(posix_memalign(&cur, align, size) == 0)
        return cur;
    return nullptr;
#endif
}

void CoreFree(void* ptr, const char* log)noexcept{
    (void)log;
    free(ptr);
}
void CoreFreeSize(void* ptr, usize size, const char* log)noexcept{
    (void)size;
    (void)log;
    free(ptr);
}
void CoreFreeAligned(void* ptr, const char* log)noexcept{
    (void)log;
#if defined(NWB_PLATFORM_WINDOWS)
    _aligned_free(ptr);
#else
    free(ptr);
#endif
}
void CoreFreeSizeAligned(void* ptr, usize size, const char* log)noexcept{
    (void)size;
    (void)log;
#if defined(NWB_PLATFORM_WINDOWS)
    _aligned_free(ptr);
#else
    free(ptr);
#endif
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_ALLOC_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

