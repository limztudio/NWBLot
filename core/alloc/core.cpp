// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "core.h"

#include <tbb/tbbmalloc_proxy.h>

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
    (void)log;
    void* cur = malloc(size);
    return cur;
}
void* CoreRealloc(void* p, usize size, const char* log){
    (void)log;
    void* cur = realloc(p, size);
    return cur;
}
void* CoreAllocAligned(usize size, usize align, const char* log){
    (void)log;
#if defined(NWB_PLATFORM_WINDOWS)
    void* cur = _aligned_malloc(size, align);
#else
    void* cur = nullptr;
    if(posix_memalign(&cur, align, size) != 0)
        cur = nullptr;
#endif
    return cur;
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

