// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


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
    (void)log;
    void* cur = scalable_malloc(size);
    return cur;
}
void* CoreRealloc(void* p, usize size, const char* log){
    (void)log;
    void* cur = scalable_realloc(p, size);
    return cur;
}
void* CoreAllocAligned(usize size, usize align, const char* log){
    (void)log;
    void* cur = scalable_aligned_malloc(size, align);
    return cur;
}

void CoreFree(void* ptr, const char* log)noexcept{
    (void)log;
    scalable_free(ptr);
}
void CoreFreeSize(void* ptr, usize size, const char* log)noexcept{
    (void)size;
    (void)log;
    scalable_free(ptr);
}
void CoreFreeAligned(void* ptr, const char* log)noexcept{
    (void)log;
    scalable_aligned_free(ptr);
}
void CoreFreeSizeAligned(void* ptr, usize size, const char* log)noexcept{
    (void)size;
    (void)log;
    scalable_aligned_free(ptr);
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_ALLOC_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

