// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "core.h"

#include "mimalloc.h"


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_ALLOC_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


void* coreAlloc(usize size, const char* log){
    (void)log;
    return mi_malloc(size);
}
void* coreRealloc(void* p, usize size, const char* log){
    (void)log;
    return mi_realloc(p, size);
}
void* coreAllocAligned(usize size, usize align, const char* log){
    (void)log;
    return mi_aligned_alloc(align, size);
}
void* coreReallocAligned(void* p, usize size, usize align, const char* log){
    (void)log;
    return mi_aligned_recalloc(p, 1, size, align);
}

void coreFree(void* ptr, const char* log)noexcept{
    (void)log;
    mi_free(ptr);
}
void coreFreeSize(void* ptr, usize size, const char* log)noexcept{
    (void)log;
    mi_free_size(ptr, size);
}
void coreFreeAligned(void* ptr, usize align, const char* log)noexcept{
    (void)log;
    mi_free_aligned(ptr, align);
}
void coreFreeSizeAligned(void* ptr, usize size, usize align, const char* log)noexcept{
    (void)log;
    mi_free_size_aligned(ptr, size, align);
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_ALLOC_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

