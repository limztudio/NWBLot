// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#pragma once


#include <stddef.h>

#include "../global.h"


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


// Struct with original free() and _msize() pointers
struct orig_ptrs{
    void (*free)(void*);
    size_t(*msize)(void*);
};

struct orig_aligned_ptrs{
    void (*aligned_free)(void*);
    size_t(*aligned_msize)(void*, size_t, size_t);
};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


extern "C"{
    NWB_DLL_API void __NWB_malloc_safer_free(void* ptr, void (*original_free)(void*));
    NWB_DLL_API void* __NWB_malloc_safer_realloc(void* ptr, usize, void*);
    NWB_DLL_API void* __NWB_malloc_safer_aligned_realloc(void* ptr, usize, usize, void*);
    NWB_DLL_API usize __NWB_malloc_safer_msize(void* ptr, usize(*orig_msize_crt80d)(void*));
    NWB_DLL_API usize __NWB_malloc_safer_aligned_msize(void* ptr, usize, usize, usize(*orig_msize_crt80d)(void*, usize, usize));

#if defined(NWB_PLATFORM_APPLE)
    NWB_DLL_API void __NWB_malloc_free_definite_size(void* object, usize size);
#endif
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

