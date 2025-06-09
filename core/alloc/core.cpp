// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "core.h"

#include <tbb/tbbmalloc_proxy.h>
#include <tbb/scalable_allocator.h>
#include <tbb/cache_aligned_allocator.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_ALLOC_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


void* coreAlloc(usize size, const char* log){
    (void)log;
    return scalable_malloc(size);
}
void* coreRealloc(void* p, usize size, const char* log){
    (void)log;
    return scalable_realloc(p, size);
}
void* coreAllocAligned(usize size, usize align, const char* log){
    (void)log;
    return scalable_aligned_malloc(size, align);
}
void* coreReallocAligned(void* p, usize size, usize align, const char* log){
    (void)log;
    return scalable_aligned_realloc(p, size, align);
}

void coreFree(void* ptr, const char* log)noexcept{
    (void)log;
    scalable_free(ptr);
}
void coreFreeSize(void* ptr, usize size, const char* log)noexcept{
    (void)size;
    (void)log;
    scalable_free(ptr);
}
void coreFreeAligned(void* ptr, usize align, const char* log)noexcept{
    (void)align;
    (void)log;
    scalable_aligned_free(ptr);
}
void coreFreeSizeAligned(void* ptr, usize size, usize align, const char* log)noexcept{
    (void)size;
    (void)align;
    (void)log;
    scalable_aligned_free(ptr);
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_ALLOC_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

